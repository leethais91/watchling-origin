/**
 * `npm run bridge:status`: whether the bridge runs, which link it talks on, and what the
 * device reported.
 */
import { isBridgeRunning } from "./ipc/bridge-process.js";
import { formatBridgeStatus, type BridgeStatus } from "./ipc/bridge-status.js";
import { postToBridge } from "./ipc/ipc-client.js";

const STATUS_TIMEOUT_MS = 2000;

if (!(await isBridgeRunning())) {
  console.log("bridge is not running; it starts with the next agent session or `npm run bridge:start`");
  process.exit(0);
}

const status = await postToBridge<BridgeStatus>("/status", {}, STATUS_TIMEOUT_MS);
if (!status) {
  console.error("bridge did not answer; check ~/.watchling/bridge.log");
  process.exit(1);
}
console.log(formatBridgeStatus(status));
