/**
 * Bridge daemon: connects Claude Code hooks/statusline (IPC socket) and Codex session
 * files to the EchoEar device over USB.
 *
 *   ECHOEAR_PORT=/dev/cu.usbmodemXXXX  skip auto-detection
 *   ECHOEAR_CONFIRM_TIMEOUT_S=120      how long a request waits on the device
 */
import { clockMessage } from "./clock/clock-message.js";
import { startIpcServer } from "./ipc/ipc-server.js";
import { SOCKET_PATH } from "./ipc/paths.js";
import { PermissionBroker } from "./permission/permission-broker.js";
import { SerialTransport } from "./transport/serial-transport.js";
import { startCodexSessionWatcher } from "./usage/codex-session-watcher.js";
import { UsageStore } from "./usage/usage-store.js";

const CONFIRM_TIMEOUT_MS = Number(process.env.ECHOEAR_CONFIRM_TIMEOUT_S ?? 120) * 1000;
/** Coalesces bursts of statusline/Codex updates into one usage frame. */
const USAGE_THROTTLE_MS = 1000;
const MAX_EVENT_TEXT_CHARS = 200;
/** The device keeps time itself; resyncing corrects drift and picks up DST changes. */
const CLOCK_RESYNC_MS = 10 * 60 * 1000;

const log = (...args: unknown[]) => console.log(new Date().toLocaleTimeString(), ...args);

const transport = new SerialTransport(process.env.ECHOEAR_PORT);
const usage = new UsageStore();
const broker = new PermissionBroker((msg) => transport.send(msg), CONFIRM_TIMEOUT_MS);

let usageTimer: NodeJS.Timeout | null = null;
usage.on("change", () => {
  usageTimer ??= setTimeout(() => {
    usageTimer = null;
    transport.send(usage.snapshot());
  }, USAGE_THROTTLE_MS);
});

transport.on("connected", (path) => log(`device port open: ${path}`));
transport.on("disconnected", () => {
  log("device disconnected");
  broker.cancelAll();
});
transport.on("log", (line) => log("[device]", line));
transport.on("message", (msg) => {
  switch (msg.t) {
    case "hello":
      log(`device ready (fw ${msg.fw})`);
      transport.send(clockMessage());
      transport.send(usage.snapshot());
      broker.resendActive();
      break;
    case "confirm_res": {
      const accepted = broker.onDeviceDecision(msg.id, msg.action, msg.src);
      log(`device ${msg.action} #${msg.id} via ${msg.src}${accepted ? "" : " (ignored)"}`);
      break;
    }
  }
});

await startIpcServer({
  permission: (input, signal) => {
    // No live device: answer immediately so Claude Code shows its own prompt.
    if (!transport.alive) return Promise.resolve(null);
    log(`permission request: ${input.tool_name}`);
    return broker.request(input, signal);
  },
  statusline: (input) => usage.updateFromStatusline(input),
  event: (event) => transport.send({ t: "event", ...event }),
});

await startCodexSessionWatcher({
  onRateLimits: (limits) => usage.updateCodex(limits),
  onTaskComplete: (message) =>
    transport.send({
      t: "event",
      src: "codex",
      kind: "done",
      text: message.replace(/\s+/g, " ").trim().slice(0, MAX_EVENT_TEXT_CHARS) || "Task finished",
    }),
});

setInterval(() => transport.send(clockMessage()), CLOCK_RESYNC_MS).unref();

transport.start();
log(`bridge listening on ${SOCKET_PATH}`);

for (const signal of ["SIGINT", "SIGTERM"] as const) {
  process.on(signal, () => {
    broker.cancelAll();
    transport.close();
    process.exit(0);
  });
}
