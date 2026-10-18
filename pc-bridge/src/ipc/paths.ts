import { homedir } from "node:os";
import { join } from "node:path";

/** Per-user state directory (mode 0700): IPC socket and installer config. */
export const WATCHLING_DIR = join(homedir(), ".watchling");
/** Unix socket the hooks and statusline use to reach the bridge daemon. */
export const SOCKET_PATH = join(WATCHLING_DIR, "bridge.sock");
/** Installer state, e.g. the statusline command that was wrapped. */
export const CONFIG_PATH = join(WATCHLING_DIR, "config.json");
/** Last Claude Code usage and today's spend, kept across bridge restarts. */
export const USAGE_STATE_PATH = join(WATCHLING_DIR, "usage.json");
/** Output of the bridge when launchd runs it (see bridge-process.ts). */
export const BRIDGE_LOG_PATH = join(WATCHLING_DIR, "bridge.log");

export interface WatchlingConfig {
  /** Original Claude Code statusLine command, run after forwarding usage to the bridge. */
  statuslineCommand?: string;
}
