/**
 * Adds (or removes) the EchoEar hooks and statusline wrapper in ~/.claude/settings.json.
 *
 *   node dist/install-hooks.js install     back up settings, add hooks, wrap statusLine
 *   node dist/install-hooks.js uninstall   remove hooks, restore the original statusLine
 *
 * Idempotent: entries are recognised by the absolute path of this package's dist/ files,
 * and existing hooks from other tools are left untouched.
 */
import { copyFile, mkdir, readFile, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { CONFIG_PATH, ECHOEAR_DIR, type EchoearConfig } from "./ipc/paths.js";

const SETTINGS_PATH = join(homedir(), ".claude", "settings.json");
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

const ECHOEAR_HOOKS: Record<string, HookMatcher> = {
  PermissionRequest: { hooks: [hookCommand("permission", PERMISSION_HOOK_TIMEOUT_S)] },
  UserPromptSubmit: { hooks: [hookCommand("prompt", EVENT_HOOK_TIMEOUT_S)] },
  Stop: { hooks: [hookCommand("stop", EVENT_HOOK_TIMEOUT_S)] },
  StopFailure: { hooks: [hookCommand("stop-failure", EVENT_HOOK_TIMEOUT_S)] },
  Notification: { matcher: "idle_prompt", hooks: [hookCommand("notification", EVENT_HOOK_TIMEOUT_S)] },
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

async function readConfig(): Promise<EchoearConfig> {
  return readFile(CONFIG_PATH, "utf8")
    .then((text) => JSON.parse(text) as EchoearConfig)
    .catch(() => ({}));
}

async function install(settings: Settings): Promise<void> {
  removeOurHooks(settings);
  settings.hooks ??= {};
  for (const [event, matcher] of Object.entries(ECHOEAR_HOOKS)) {
    (settings.hooks[event] ??= []).push(matcher);
  }

  if (settings.statusLine?.command !== STATUSLINE_COMMAND) {
    const config: EchoearConfig = { ...(await readConfig()), statuslineCommand: settings.statusLine?.command };
    await mkdir(ECHOEAR_DIR, { recursive: true, mode: 0o700 });
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

const action = process.argv[2];
if (action !== "install" && action !== "uninstall") {
  console.error("usage: install-hooks.js <install|uninstall>");
  process.exit(1);
}

const settings = JSON.parse(await readFile(SETTINGS_PATH, "utf8")) as Settings;
const backupPath = `${SETTINGS_PATH}.echoear-backup-${Date.now()}`;
await copyFile(SETTINGS_PATH, backupPath);

await (action === "install" ? install(settings) : uninstall(settings));
await writeFile(SETTINGS_PATH, JSON.stringify(settings, null, 2) + "\n");

console.log(`${action === "install" ? "Installed" : "Removed"} EchoEar hooks in ${SETTINGS_PATH}`);
console.log(`Backup: ${backupPath}`);
