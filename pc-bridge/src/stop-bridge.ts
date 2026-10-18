/**
 * `npm run bridge:stop`: asks the running bridge to exit and waits until it is gone, so
 * the serial port is free for `idf.py flash`. The next agent session starts it again.
 */
import { setTimeout as sleep } from "node:timers/promises";
import { isBridgeRunning } from "./ipc/bridge-process.js";
import { postToBridge } from "./ipc/ipc-client.js";

const STOP_TIMEOUT_MS = 5000;
const POLL_MS = 100;

if (!(await isBridgeRunning())) {
  console.log("bridge is not running");
  process.exit(0);
}

await postToBridge("/shutdown", {}, STOP_TIMEOUT_MS);
for (let waited = 0; waited < STOP_TIMEOUT_MS; waited += POLL_MS) {
  if (!(await isBridgeRunning())) {
    console.log("bridge stopped");
    process.exit(0);
  }
  await sleep(POLL_MS);
}
console.error("bridge did not stop; check ~/.watchling/bridge.log");
process.exit(1);
