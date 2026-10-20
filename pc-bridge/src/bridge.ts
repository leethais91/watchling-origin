/**
 * Bridge daemon: connects Claude Code hooks/statusline (IPC socket) and Codex session
 * files to the Watchling device over USB, or BLE when the cable is out. Started by the
 * SessionStart hook through launchd (see ipc/bridge-process.ts) or by hand (`npm start`).
 *
 *   WATCHLING_PORT=/dev/cu.usbmodemXXXX  skip auto-detection
 *   WATCHLING_BLE=off                    USB only (no Bluetooth permission needed)
 *   WATCHLING_CONFIRM_TIMEOUT_S=120      how long a request waits on the device
 *   WATCHLING_TTS / WATCHLING_TTS_VOICE    spoken announcements, see tts/tts-engine.ts
 *   WATCHLING_CHATTER / WATCHLING_CHATTER_DELAY_S  idle chatter, see chatter/chatter.ts
 */
import { Chatter } from "./chatter/chatter.js";
import { ChatterClips } from "./chatter/chatter-clips.js";
import { CHATTER_LINES } from "./chatter/chatter-lines.js";
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
import { createTtsEngine } from "./tts/tts-engine.js";
import { startCodexSessionWatcher } from "./usage/codex-session-watcher.js";
import { UsageStore } from "./usage/usage-store.js";

const CONFIRM_TIMEOUT_MS = Number(process.env.WATCHLING_CONFIRM_TIMEOUT_S ?? 120) * 1000;
/** How long a lost link may take to come back before waiting requests go to the terminal. */
const LINK_LOSS_GRACE_MS = 10_000;
/** Coalesces bursts of statusline/Codex updates into one usage frame. */
const USAGE_THROTTLE_MS = 1000;
const MAX_EVENT_TEXT_CHARS = 200;
/** The device keeps time itself; resyncing corrects drift and picks up DST changes. */
const CLOCK_RESYNC_MS = 10 * 60 * 1000;

const log = (...args: unknown[]) => console.log(new Date().toLocaleTimeString(), ...args);

const usb = new SerialTransport(process.env.WATCHLING_PORT);
const ble = process.env.WATCHLING_BLE !== "off" ? new BleTransport() : null;
const links: { name: string; link: DeviceLink }[] = [{ name: "USB", link: usb }];
if (ble) links.push({ name: "BLE", link: ble });
const transport = new TransportHub(links);
const usage = new UsageStore(USAGE_STATE_PATH);
await usage.restore();
const tts = createTtsEngine();
/** Voice switch on the device's settings screen; synthesis is skipped while it is off. */
let deviceVoice = true;
/** Bluetooth switch on the device's settings screen; null until firmware reports it. */
let deviceBle: boolean | null = null;
let deviceFw: string | null = null;
/** Speech clips are raw PCM: USB only. Over BLE the device still chimes. */
const speechLink = {
  get alive() {
    return deviceVoice && transport.active === usb && usb.alive;
  },
  send: (msg: HostMessage) => transport.send(msg),
  sendAudio: (payload: Uint8Array) => transport.sendAudio(payload),
};
const speaker = tts && new Speaker(tts, speechLink, (err) => log(`speech failed: ${err.message}`));
const chatterDelayMs = Number(process.env.WATCHLING_CHATTER_DELAY_S) * 1000;
const chatterClips = speaker && process.env.WATCHLING_CHATTER !== "off" ? await ChatterClips.load() : null;
for (const problem of chatterClips?.problems ?? []) log(`chatter clip skipped: ${problem}`);
const chatter =
  speaker && chatterClips
    ? new Chatter({
        play: (line) => {
          if (!speechLink.alive) {
            log(`chatter: ${line.id} (not played: voice off or device not on USB)`);
            return false;
          }
          const clip = chatterClips.build(line, Math.random);
          log(`chatter: ${line.id} [${clip.face.mood}${clip.sfx ? ` +${clip.sfx}` : ""}] ${line.text}`);
          void speaker.playClip(clip.pcm, clip.face);
          return true;
        },
        usage: () => usage.snapshot(),
        available: chatterClips.available,
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

transport.on("connected", (name) => {
  log(`talking to the device over ${name}`);
  if (broker.keepAll()) log("link back in time; keeping permission requests");
});
transport.on("disconnected", () => {
  log(`device unreachable; releasing permission requests in ${LINK_LOSS_GRACE_MS / 1000} s`);
  broker.cancelAllAfter(LINK_LOSS_GRACE_MS);
});
transport.on("log", (line) => log("[device]", line));
transport.on("message", (msg) => {
  switch (msg.t) {
    case "hello":
      deviceVoice = msg.voice ?? true;
      deviceBle = msg.ble ?? null;
      deviceFw = msg.fw;
      log(`device ready (fw ${msg.fw})${deviceVoice ? "" : " (voice off)"}${deviceBle === false ? " (bluetooth off)" : ""}`);
      transport.send(clockMessage());
      transport.send(usage.snapshot());
      broker.resendActive();
      break;
    case "settings":
      // One message carries every switch; log only the ones that changed.
      if (msg.voice !== deviceVoice) {
        deviceVoice = msg.voice;
        log(`device voice ${deviceVoice ? "on" : "off"}`);
      }
      if (msg.ble !== undefined && msg.ble !== deviceBle) {
        deviceBle = msg.ble;
        log(`device bluetooth ${deviceBle ? "on" : "off"}`);
      }
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
  status: () => ({
    link: transport.activeName,
    usb: usb.alive,
    ble: ble ? ble.alive : "disabled",
    deviceBle,
    fw: deviceFw,
    pendingPermissions: broker.pendingCount,
  }),
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
if (chatterClips) log(`chatter: ${chatterClips.available.size}/${CHATTER_LINES.length} clips`);
log(`bridge listening on ${SOCKET_PATH}${tts ? "" : " (speech off)"}${chatter ? "" : " (chatter off)"}`);

function shutdown(): never {
  chatter?.stop();
  broker.cancelAll();
  transport.close();
  process.exit(0);
}

for (const signal of ["SIGINT", "SIGTERM"] as const) process.on(signal, shutdown);
