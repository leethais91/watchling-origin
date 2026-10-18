/**
 * Claude Code statusLine wrapper: forwards the session JSON (rate limits, cost) to the
 * bridge, then runs the user's original statusline command with the same stdin so the
 * terminal status bar is unchanged.
 */
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { postToBridge, readStdin } from "./ipc/ipc-client.js";
import { CONFIG_PATH, type WatchlingConfig } from "./ipc/paths.js";

/** Claude Code cancels slow statuslines; never let the bridge delay it noticeably. */
const FORWARD_TIMEOUT_MS = 300;

const input = await readStdin();

let parsed: unknown = null;
try {
  parsed = JSON.parse(input);
} catch {
  // Not JSON: nothing to forward, still render the original statusline.
}

const config = await readFile(CONFIG_PATH, "utf8")
  .then((text) => JSON.parse(text) as WatchlingConfig)
  .catch((): WatchlingConfig => ({}));

const forwarded = parsed ? postToBridge("/statusline", parsed, FORWARD_TIMEOUT_MS) : Promise.resolve(null);

if (config.statuslineCommand) {
  const child = spawn(config.statuslineCommand, { shell: true, stdio: ["pipe", "inherit", "inherit"] });
  child.stdin.end(input);
  const code = await new Promise<number>((resolve) => child.on("close", (c) => resolve(c ?? 0)));
  await forwarded;
  process.exitCode = code;
} else {
  await forwarded;
}
