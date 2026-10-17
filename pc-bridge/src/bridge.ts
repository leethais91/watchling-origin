/**
 * Bridge daemon: connects Claude Code hooks/statusline (IPC socket) and Codex session
 * files to the EchoEar device over USB.
 *
 *   ECHOEAR_PORT=/dev/cu.usbmodemXXXX  skip auto-detection
 *   ECHOEAR_CONFIRM_TIMEOUT_S=120      how long a request waits on the device
 *   ECHOEAR_TTS / ECHOEAR_TTS_VOICE    spoken announcements, see tts/tts-engine.ts
 */
import { clockMessage } from "./clock/clock-message.js";
import { startIpcServer } from "./ipc/ipc-server.js";
import { SOCKET_PATH } from "./ipc/paths.js";
import { PermissionBroker } from "./permission/permission-broker.js";
import type { AgentEventMessage } from "./protocol/messages.js";
import { Speaker } from "./speech/speaker.js";
import { doneSpeech, permissionSpeech } from "./speech/speech-text.js";
import { SerialTransport } from "./transport/serial-transport.js";
import { createTtsEngine } from "./tts/tts-engine.js";
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
const tts = createTtsEngine();
const speaker = tts && new Speaker(tts, transport, (err) => log(`speech failed: ${err.message}`));
const broker = new PermissionBroker((msg) => {
  const sent = transport.send(msg);
  if (sent && msg.t === "confirm_req") void speaker?.say(permissionSpeech(msg));
  return sent;
}, CONFIRM_TIMEOUT_MS);

/** Shows the event on the device (which chimes) and reads finished tasks aloud. */
function sendEvent(event: Omit<AgentEventMessage, "t">): void {
  transport.send({ t: "event", ...event });
  if (event.kind === "done") void speaker?.say(doneSpeech(event.src, event.text));
}

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
  event: sendEvent,
  activity: (activity) => transport.send({ t: "activity", ...activity }),
});

await startCodexSessionWatcher({
  onRateLimits: (limits) => usage.updateCodex(limits),
  onTaskComplete: (message) =>
    sendEvent({
      src: "codex",
      kind: "done",
      text: message.replace(/\s+/g, " ").trim().slice(0, MAX_EVENT_TEXT_CHARS) || "Task finished",
    }),
  onActivity: (busy) => transport.send({ t: "activity", src: "codex", busy }),
});

setInterval(() => transport.send(clockMessage()), CLOCK_RESYNC_MS).unref();

transport.start();
log(`bridge listening on ${SOCKET_PATH}${tts ? "" : " (speech off)"}`);

for (const signal of ["SIGINT", "SIGTERM"] as const) {
  process.on(signal, () => {
    broker.cancelAll();
    transport.close();
    process.exit(0);
  });
}
