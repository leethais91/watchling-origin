/**
 * Adds (or removes) the Watchling hooks and statusline wrapper in ~/.claude/settings.json,
 * or with --codex the session-start and permission hooks in ~/.codex/hooks.json (same
 * hooks format). Install also writes the on-demand launchd agent that the SessionStart hooks
 * start, pinned to the Node running this installer; uninstall (Claude) removes it.
 *
 *   node dist/install-hooks.js install [--codex]     back up the file, add hooks (Claude: wrap statusLine)
 *   node dist/install-hooks.js uninstall [--codex]   remove hooks (Claude: restore the original statusLine)
 *
 * Idempotent: entries are recognised by the absolute path of this package's dist/ files,
 * and existing hooks from other tools are left untouched.
 */
import { copyFile, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  LAUNCH_AGENT_PATH,
  launchAgentPlist,
  loadLaunchAgent,
  unloadLaunchAgent,
} from "./ipc/bridge-process.js";
import { BRIDGE_LOG_PATH, CONFIG_PATH, WATCHLING_DIR, type WatchlingConfig } from "./ipc/paths.js";

const SETTINGS_PATH = join(homedir(), ".claude", "settings.json");
const CODEX_HOOKS_PATH = join(homedir(), ".codex", "hooks.json");
const DIST_DIR = dirname(fileURLToPath(import.meta.url));
const HOOK_SCRIPT = join(DIST_DIR, "hook.js");
const STATUSLINE_SCRIPT = join(DIST_DIR, "statusline.js");
const STATUSLINE_COMMAND = `node "${STATUSLINE_SCRIPT}"`;

/** Longer than the bridge's own confirm timeout so the bridge always answers first. */
const PERMISSION_HOOK_TIMEOUT_S = 150;
const EVENT_HOOK_TIMEOUT_S = 5;

interface HookCommand {
  type: "command";
  command: string;
  timeout?: number;
}
interface HookMatcher {
  matcher?: string;
  hooks: HookCommand[];
}
interface Settings {
  hooks?: Record<string, HookMatcher[]>;
  statusLine?: { type: string; command: string; [key: string]: unknown };
  [key: string]: unknown;
}

const hookCommand = (arg: string, timeout: number): HookCommand => ({
  type: "command",
  command: `node "${HOOK_SCRIPT}" ${arg}`,
  timeout,
});

const WATCHLING_HOOKS: Record<string, HookMatcher> = {
  SessionStart: { hooks: [hookCommand("session-start", EVENT_HOOK_TIMEOUT_S)] },
  PermissionRequest: { hooks: [hookCommand("permission", PERMISSION_HOOK_TIMEOUT_S)] },
  UserPromptSubmit: { hooks: [hookCommand("prompt", EVENT_HOOK_TIMEOUT_S)] },
  Stop: { hooks: [hookCommand("stop", EVENT_HOOK_TIMEOUT_S)] },
  StopFailure: { hooks: [hookCommand("stop-failure", EVENT_HOOK_TIMEOUT_S)] },
  Notification: { matcher: "idle_prompt", hooks: [hookCommand("notification", EVENT_HOOK_TIMEOUT_S)] },
};

/** Codex already reports busy/done through its session files; it only needs approvals. */
const CODEX_HOOKS: Record<string, HookMatcher> = {
  SessionStart: { hooks: [hookCommand("session-start codex", EVENT_HOOK_TIMEOUT_S)] },
  PermissionRequest: { hooks: [hookCommand("permission codex", PERMISSION_HOOK_TIMEOUT_S)] },
};

const isOurs = (hook: HookCommand) => hook.command.includes(HOOK_SCRIPT);

function removeOurHooks(settings: Settings): void {
  for (const [event, matchers] of Object.entries(settings.hooks ?? {})) {
    const kept = matchers
      .map((m) => ({ ...m, hooks: m.hooks.filter((h) => !isOurs(h)) }))
      .filter((m) => m.hooks.length > 0);
    if (kept.length > 0) settings.hooks![event] = kept;
    else delete settings.hooks![event];
  }
}

async function readConfig(): Promise<WatchlingConfig> {
  return readFile(CONFIG_PATH, "utf8")
    .then((text) => JSON.parse(text) as WatchlingConfig)
    .catch(() => ({}));
}

async function installLaunchAgent(): Promise<void> {
  const plist = launchAgentPlist({
    nodePath: process.execPath,
    script: join(DIST_DIR, "bridge.js"),
    cwd: dirname(DIST_DIR),
    logPath: BRIDGE_LOG_PATH,
  });
  await mkdir(WATCHLING_DIR, { recursive: true, mode: 0o700 });
  await mkdir(dirname(LAUNCH_AGENT_PATH), { recursive: true });
  await writeFile(LAUNCH_AGENT_PATH, plist);
  await loadLaunchAgent();
  console.log(`Bridge launch agent: ${LAUNCH_AGENT_PATH} (Node ${process.version})`);
}

async function uninstallLaunchAgent(): Promise<void> {
  await unloadLaunchAgent();
  await rm(LAUNCH_AGENT_PATH, { force: true });
}

function addOurHooks(settings: Settings, hooks: Record<string, HookMatcher>): void {
  removeOurHooks(settings);
  settings.hooks ??= {};
  for (const [event, matcher] of Object.entries(hooks)) {
    (settings.hooks[event] ??= []).push(matcher);
  }
}

async function install(settings: Settings): Promise<void> {
  addOurHooks(settings, WATCHLING_HOOKS);

  if (settings.statusLine?.command !== STATUSLINE_COMMAND) {
    const config: WatchlingConfig = { ...(await readConfig()), statuslineCommand: settings.statusLine?.command };
    await mkdir(WATCHLING_DIR, { recursive: true, mode: 0o700 });
    await writeFile(CONFIG_PATH, JSON.stringify(config, null, 2) + "\n");
    settings.statusLine = { ...settings.statusLine, type: "command", command: STATUSLINE_COMMAND };
  }
}

async function uninstall(settings: Settings): Promise<void> {
  removeOurHooks(settings);
  if (settings.statusLine?.command === STATUSLINE_COMMAND) {
    const original = (await readConfig()).statuslineCommand;
    if (original) settings.statusLine.command = original;
    else delete settings.statusLine;
  }
}

const [action, flag] = process.argv.slice(2);
if ((action !== "install" && action !== "uninstall") || (flag !== undefined && flag !== "--codex")) {
  console.error("usage: install-hooks.js <install|uninstall> [--codex]");
  process.exit(1);
}
const codex = flag === "--codex";
const path = codex ? CODEX_HOOKS_PATH : SETTINGS_PATH;

// Codex has no hooks.json until something adds one; Claude Code always has settings.json.
const existing = await readFile(path, "utf8").catch((err: NodeJS.ErrnoException) => {
  if (codex && err.code === "ENOENT") return null;
  throw err;
});
const settings = (existing ? JSON.parse(existing) : {}) as Settings;
const backupPath = existing ? `${path}.watchling-backup-${Date.now()}` : null;
if (backupPath) await copyFile(path, backupPath);

if (codex) {
  if (action === "install") addOurHooks(settings, CODEX_HOOKS);
  else removeOurHooks(settings);
} else {
  await (action === "install" ? install(settings) : uninstall(settings));
}
// Codex uninstall keeps the agent: it does nothing unless a remaining hook starts it.
if (action === "install") await installLaunchAgent();
else if (!codex) await uninstallLaunchAgent();
await mkdir(dirname(path), { recursive: true });
await writeFile(path, JSON.stringify(settings, null, 2) + "\n");

console.log(`${action === "install" ? "Installed" : "Removed"} Watchling hooks in ${path}`);
if (backupPath) console.log(`Backup: ${backupPath}`);
