/**
 * Adds (or removes) the Watchling hooks and statusline wrapper in ~/.claude/settings.json,
 * or with --codex the session-start and permission hooks in ~/.codex/hooks.json (same
 * hooks format). Install also writes the on-demand launchd agent that the SessionStart hooks
 * start, pinned to the Node running this installer; uninstall removes it once neither integration needs it.
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
interface HookEntry {
  type: string;
  command?: string;
  prompt?: string;
}
interface HookMatcher {
  matcher?: string;
  hooks: HookEntry[];
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

// Only exact script tokens from this checkout establish ownership. Similar paths
// in other repositories must remain untouched, including after a checkout moves.
const isWatchlingScript = (command: string, name: string): boolean => {
  const path = join(DIST_DIR, `${name}.js`);
  const escaped = path.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const unquoted = /[\s"']/.test(path) ? "(?!)" : escaped;
  return new RegExp(`(?:^|\\s)(?:"${escaped}"|'${escaped}'|${unquoted})(?=\\s|$)`).test(command);
};
const isOurs = (hook: HookEntry) => hook.type === "command" && typeof hook.command === "string" && isWatchlingScript(hook.command, "hook");
const isOurStatusline = (settings: Settings) =>
  typeof settings.statusLine?.command === "string" && isWatchlingScript(settings.statusLine.command, "statusline");

/** Reject malformed hook documents before modifying them or deciding to stop a shared service. */
function parseSettings(text: string): Settings {
  const value: unknown = JSON.parse(text);
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Expected a settings object");
  const settings = value as Settings;
  if (settings.statusLine !== undefined) {
    const statusLine = settings.statusLine;
    if (!statusLine || typeof statusLine !== "object" || Array.isArray(statusLine) ||
      statusLine.type !== "command" || typeof statusLine.command !== "string") {
      throw new Error("Invalid statusLine command");
    }
  }
  if (settings.hooks !== undefined) {
    if (!settings.hooks || typeof settings.hooks !== "object" || Array.isArray(settings.hooks)) throw new Error("Invalid hooks object");
    for (const matchers of Object.values(settings.hooks)) {
      if (!Array.isArray(matchers) || matchers.some((m) =>
        !m || !Array.isArray(m.hooks) || m.hooks.some((h: HookEntry) =>
          !h || typeof h !== "object" || Array.isArray(h) || typeof h.type !== "string" ||
          (h.type === "command" && typeof h.command !== "string") ||
          ((h.type === "prompt" || h.type === "agent") && typeof h.prompt !== "string")))) {
        throw new Error("Invalid hook matchers");
      }
    }
  }
  return settings;
}

async function otherIntegrationNeedsAgent(path: string): Promise<boolean> {
  try {
    const other = parseSettings(await readFile(path, "utf8"));
    return Object.values(other.hooks ?? {}).some((matchers) => matchers.some((m) => m.hooks.some(isOurs))) || isOurStatusline(other);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return false;
    // Unknown state must never stop a potentially active integration.
    console.warn(`Could not inspect ${path}; keeping the shared bridge launch agent.`);
    return true;
  }
}

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

  if (!isOurStatusline(settings)) {
    const config: WatchlingConfig = { ...(await readConfig()), statuslineCommand: settings.statusLine?.command };
    await mkdir(WATCHLING_DIR, { recursive: true, mode: 0o700 });
    await writeFile(CONFIG_PATH, JSON.stringify(config, null, 2) + "\n");
  }
  settings.statusLine = { ...settings.statusLine, type: "command", command: STATUSLINE_COMMAND };
}

async function uninstall(settings: Settings): Promise<void> {
  removeOurHooks(settings);
  if (isOurStatusline(settings)) {
    const original = (await readConfig()).statuslineCommand;
    if (original) settings.statusLine!.command = original;
    else delete settings.statusLine;
  }
}

if (process.platform !== "darwin") {
  console.error("Watchling hook installation requires macOS (launchd). No configuration was changed.");
  process.exit(1);
}

const [action, flag, ...extra] = process.argv.slice(2);
if ((action !== "install" && action !== "uninstall") || (flag !== undefined && flag !== "--codex") || extra.length > 0) {
  console.error("usage: install-hooks.js <install|uninstall> [--codex]");
  process.exit(1);
}
const codex = flag === "--codex";
const path = codex ? CODEX_HOOKS_PATH : SETTINGS_PATH;

// Both integrations may be absent on a new machine or already removed.
const existing = await readFile(path, "utf8").catch((err: NodeJS.ErrnoException) => {
  if (err.code === "ENOENT") return null;
  throw err;
});
const settings = existing ? parseSettings(existing) : {};
const backupPath = existing ? `${path}.watchling-backup-${Date.now()}` : null;
if (backupPath) await copyFile(path, backupPath);

if (codex) {
  if (action === "install") addOurHooks(settings, CODEX_HOOKS);
  else removeOurHooks(settings);
} else {
  await (action === "install" ? install(settings) : uninstall(settings));
}
// Persist removal before stopping the service. Missing uninstall targets stay absent.
if (existing !== null || action === "install") {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, JSON.stringify(settings, null, 2) + "\n");
}
if (action === "install") await installLaunchAgent();
else if (!(await otherIntegrationNeedsAgent(codex ? SETTINGS_PATH : CODEX_HOOKS_PATH))) await uninstallLaunchAgent();

console.log(`${action === "install" ? "Installed" : "Removed"} Watchling hooks in ${path}`);
if (backupPath) console.log(`Backup: ${backupPath}`);
