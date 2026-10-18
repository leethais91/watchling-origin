/**
 * Bridge daemon: connects Claude Code hooks/statusline (IPC socket) and Codex session
 * files to the EchoEar device over USB, or BLE when the cable is out. Started by the
 * SessionStart hook through launchd (see ipc/bridge-process.ts) or by hand (`npm start`).
 *
 *   ECHOEAR_PORT=/dev/cu.usbmodemXXXX  skip auto-detection
 *   ECHOEAR_BLE=off                    USB only (no Bluetooth permission needed)
 *   ECHOEAR_CONFIRM_TIMEOUT_S=120      how long a request waits on the device
 *   ECHOEAR_TTS / ECHOEAR_TTS_VOICE    spoken announcements, see tts/tts-engine.ts
 *   ECHOEAR_CHATTER / ECHOEAR_CHATTER_VOICE  idle chatter, see chatter/chatter.ts
 */
import { Chatter } from "./chatter/chatter.js";
import { clockMessage } from "./clock/clock-message.js";
import { startIpcServer } from "./ipc/ipc-server.js";
import { SOCKET_PATH, USAGE_STATE_PATH } from "./ipc/paths.js";
import { PermissionBroker } from "./permission/permission-broker.js";
import type { AgentEventMessage, AgentSource, HostMessage } from "./protocol/messages.js";
import { Speaker } from "./speech/speaker.js";
import { doneSpeech, permissionSpeech } from "./speech/speech-text.js";
import { BleTransport } from "./transport/ble-transport.js";
import type { DeviceLink } from "./transport/device-link.js";
import { SerialTransport } from "./transport/serial-transport.js";
import { TransportHub } from "./transport/transport-hub.js";
import { SayEngine } from "./tts/say-engine.js";
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

const usb = new SerialTransport(process.env.ECHOEAR_PORT);
const links: { name: string; link: DeviceLink }[] = [{ name: "USB", link: usb }];
if (process.env.ECHOEAR_BLE !== "off") links.push({ name: "BLE", link: new BleTransport() });
const transport = new TransportHub(links);
const usage = new UsageStore(USAGE_STATE_PATH);
await usage.restore();
const tts = createTtsEngine();
/** Voice switch on the device's settings screen; synthesis is skipped while it is off. */
let deviceVoice = true;
/** Speech clips are raw PCM: USB only. Over BLE the device still chimes. */
const speechLink = {
  get alive() {
    return deviceVoice && transport.active === usb && usb.alive;
  },
  send: (msg: HostMessage) => transport.send(msg),
  sendAudio: (payload: Uint8Array) => transport.sendAudio(payload),
};
const speaker = tts && new Speaker(tts, speechLink, (err) => log(`speech failed: ${err.message}`));
const chatterVoice = new SayEngine(process.env.ECHOEAR_CHATTER_VOICE ?? "Linh");
const chatterDelayMs = Number(process.env.ECHOEAR_CHATTER_DELAY_S) * 1000;
const chatter =
  speaker && process.env.ECHOEAR_CHATTER !== "off"
    ? new Chatter({
        say: (text) => {
          log(`chatter: ${text}${speechLink.alive ? "" : " (not spoken: voice off or device not on USB)"}`);
          void speaker.say(text, chatterVoice);
        },
        usage: () => usage.snapshot(),
        quietMs: chatterDelayMs > 0 ? { min: chatterDelayMs, max: chatterDelayMs } : undefined,
      })
    : null;
const broker = new PermissionBroker((msg) => {
  const sent = transport.send(msg);
  if (sent && msg.t === "confirm_req") {
    void speaker?.say(permissionSpeech(msg));
    chatter?.onAgent(msg.src, true);
  }
  return sent;
}, CONFIRM_TIMEOUT_MS);

/** Shows the event on the device (which chimes) and reads finished tasks aloud. */
function sendEvent(event: Omit<AgentEventMessage, "t">): void {
  transport.send({ t: "event", ...event });
  chatter?.onAgent(event.src, false);
  if (event.kind === "done") void speaker?.say(doneSpeech(event.src, event.text));
}

function sendActivity(src: AgentSource, busy: boolean): void {
  transport.send({ t: "activity", src, busy });
  chatter?.onAgent(src, busy);
}

let usageTimer: NodeJS.Timeout | null = null;
usage.on("change", () => {
  usageTimer ??= setTimeout(() => {
    usageTimer = null;
    transport.send(usage.snapshot());
  }, USAGE_THROTTLE_MS);
});

transport.on("connected", (name) => log(`talking to the device over ${name}`));
transport.on("disconnected", () => {
  log("device unreachable");
  broker.cancelAll();
});
transport.on("log", (line) => log("[device]", line));
transport.on("message", (msg) => {
  switch (msg.t) {
    case "hello":
      deviceVoice = msg.voice ?? true;
      log(`device ready (fw ${msg.fw})${deviceVoice ? "" : " (voice off)"}`);
      transport.send(clockMessage());
      transport.send(usage.snapshot());
      broker.resendActive();
      break;
    case "settings":
      deviceVoice = msg.voice;
      log(`device voice ${deviceVoice ? "on" : "off"}`);
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
  activity: (activity) => sendActivity(activity.src, activity.busy),
  shutdown: () => {
    log("stop requested");
    shutdown();
  },
});

await startCodexSessionWatcher({
  onRateLimits: (limits) => usage.updateCodex(limits),
  onTaskComplete: (message) =>
    sendEvent({
      src: "codex",
      kind: "done",
      text: message.replace(/\s+/g, " ").trim().slice(0, MAX_EVENT_TEXT_CHARS) || "Task finished",
    }),
  onActivity: (busy) => sendActivity("codex", busy),
});

setInterval(() => transport.send(clockMessage()), CLOCK_RESYNC_MS).unref();

transport.start();
chatter?.start();
log(`bridge listening on ${SOCKET_PATH}${tts ? "" : " (speech off)"}${chatter ? "" : " (chatter off)"}`);

function shutdown(): never {
  chatter?.stop();
  broker.cancelAll();
  transport.close();
  process.exit(0);
}

for (const signal of ["SIGINT", "SIGTERM"] as const) process.on(signal, shutdown);
