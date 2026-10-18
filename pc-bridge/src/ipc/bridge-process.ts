/**
 * Runs the bridge as an on-demand launchd agent, so SessionStart hooks can start it.
 * Node built-ins only, like ipc-client.ts, so hooks stay fast under whatever Node the
 * agent finds.
 *
 * Why launchd and not a detached child: macOS charges Bluetooth access to the app that
 * started the process chain. Started from a hook, that is Claude Code, which declares no
 * Bluetooth usage, so macOS kills the bridge the moment it touches BLE. A launchd job is
 * its own responsible process. launchd also runs at most one instance of the job.
 */
import { execFile } from "node:child_process";
import { homedir } from "node:os";
import { join } from "node:path";
import { connect } from "node:net";
import { promisify } from "node:util";
import { SOCKET_PATH } from "./paths.js";

const run = promisify(execFile);

export const LAUNCH_AGENT_LABEL = "com.echoear.bridge";
export const LAUNCH_AGENT_PATH = join(homedir(), "Library", "LaunchAgents", `${LAUNCH_AGENT_LABEL}.plist`);
const SERVICE = `gui/${process.getuid?.() ?? 0}`;
const SERVICE_TARGET = `${SERVICE}/${LAUNCH_AGENT_LABEL}`;

/** True when a bridge accepts connections on the IPC socket. */
export function isBridgeRunning(socketPath = SOCKET_PATH): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = connect(socketPath);
    socket.once("connect", () => {
      socket.destroy();
      resolve(true);
    });
    socket.once("error", () => resolve(false));
  });
}

export interface LaunchAgentOptions {
  /** Node that runs the bridge; serialport's native module is built for one Node version. */
  nodePath: string;
  script: string;
  cwd: string;
  /** stdout and stderr are appended here. */
  logPath: string;
}

const xmlEscape = (text: string) =>
  text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

/**
 * The job only runs when kickstarted: not at login, and not restarted after it exits,
 * so `npm run bridge:stop` keeps it stopped until the next agent session.
 */
export function launchAgentPlist({ nodePath, script, cwd, logPath }: LaunchAgentOptions): string {
  const string = (value: string) => `<string>${xmlEscape(value)}</string>`;
  return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>${string(LAUNCH_AGENT_LABEL)}
  <key>ProgramArguments</key>
  <array>${string(nodePath)}${string(script)}</array>
  <key>WorkingDirectory</key>${string(cwd)}
  <key>StandardOutPath</key>${string(logPath)}
  <key>StandardErrorPath</key>${string(logPath)}
  <key>RunAtLoad</key><false/>
  <key>KeepAlive</key><false/>
  <key>ProcessType</key>${string("Interactive")}
</dict>
</plist>
`;
}

/** Loads the agent's plist into launchd, replacing a previously loaded version. */
export async function loadLaunchAgent(): Promise<void> {
  await unloadLaunchAgent();
  await run("launchctl", ["bootstrap", SERVICE, LAUNCH_AGENT_PATH]);
}

/** Removes the agent from launchd, stopping a running bridge. No-op when not loaded. */
export async function unloadLaunchAgent(): Promise<void> {
  await run("launchctl", ["bootout", SERVICE_TARGET]).catch(() => {});
}

/**
 * Starts the bridge; a no-op when it already runs. The agent loads at login from
 * ~/Library/LaunchAgents, but is bootstrapped here too in case it was booted out.
 */
export async function startBridge(): Promise<void> {
  try {
    await run("launchctl", ["kickstart", SERVICE_TARGET]);
  } catch {
    await run("launchctl", ["bootstrap", SERVICE, LAUNCH_AGENT_PATH]).catch(() => {});
    await run("launchctl", ["kickstart", SERVICE_TARGET]).catch(() => {});
  }
}
