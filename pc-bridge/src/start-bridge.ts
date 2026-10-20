/**
 * `npm run bridge:start`: starts the installed launchd bridge without opening an agent
 * session, e.g. after `npm run bridge:stop` and a flash. Waits until it accepts connections.
 */
import { access } from "node:fs/promises";
import { setTimeout as sleep } from "node:timers/promises";
import { LAUNCH_AGENT_PATH, isBridgeRunning, startBridge } from "./ipc/bridge-process.js";

const START_TIMEOUT_MS = 5000;
const POLL_MS = 100;

if (await isBridgeRunning()) {
  console.log("bridge is already running");
  process.exit(0);
}

try {
  await access(LAUNCH_AGENT_PATH);
} catch {
  console.error("bridge launch agent is not installed; run `npm run hooks:install` (or hooks:install:codex) first");
  process.exit(1);
}

await startBridge();
for (let waited = 0; waited < START_TIMEOUT_MS; waited += POLL_MS) {
  if (await isBridgeRunning()) {
    console.log("bridge started");
    process.exit(0);
  }
  await sleep(POLL_MS);
}
console.error("bridge did not start; check ~/.watchling/bridge.log");
process.exit(1);
