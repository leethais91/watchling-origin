/**
 * Claude Code hook entry: `node dist/hook.js <permission|stop|stop-failure|notification>`
 * with the hook JSON on stdin. Every path exits 0; printing nothing leaves Claude Code
 * on its normal flow, so a stopped bridge or unplugged device never blocks work.
 */
import type { AgentEventMessage } from "./protocol/messages.js";
import type { PermissionResponse } from "./ipc/ipc-server.js";
import { postToBridge, readStdin } from "./ipc/ipc-client.js";

const EVENT_TIMEOUT_MS = 1000;
const MAX_EVENT_TEXT_CHARS = 200;

/** Tools whose dialog collects an answer rather than a yes/no; leave them to the terminal. */
const TERMINAL_ONLY_TOOLS = new Set(["AskUserQuestion", "ExitPlanMode"]);

interface HookInput {
  tool_name?: string;
  notification_type?: string;
  message?: string;
  last_assistant_message?: string;
  error?: string;
}

const command = process.argv[2];
const input = JSON.parse(await readStdin()) as HookInput;

switch (command) {
  case "permission":
    await handlePermission(input);
    break;
  case "stop":
    await sendEvent("done", input.last_assistant_message || "Task finished");
    break;
  case "stop-failure":
    await sendEvent("error", input.last_assistant_message || input.error || "API error");
    break;
  case "notification":
    // Permission prompts arrive through PermissionRequest; only surface idle waits here.
    if (input.notification_type === "idle_prompt") await sendEvent("attention", input.message || "Waiting for input");
    break;
}

async function handlePermission(hookInput: HookInput): Promise<void> {
  if (!hookInput.tool_name || TERMINAL_ONLY_TOOLS.has(hookInput.tool_name)) return;
  const response = await postToBridge<PermissionResponse>("/permission", hookInput);
  if (!response?.decision) return;

  const decision =
    response.decision === "approve"
      ? { behavior: "allow" }
      : { behavior: "deny", message: "Denied from the EchoEar desk display." };
  process.stdout.write(
    JSON.stringify({ hookSpecificOutput: { hookEventName: "PermissionRequest", decision } }),
  );
}

async function sendEvent(kind: AgentEventMessage["kind"], text: string): Promise<void> {
  const event: Omit<AgentEventMessage, "t"> = {
    src: "claude",
    kind,
    text: text.replace(/\s+/g, " ").trim().slice(0, MAX_EVENT_TEXT_CHARS),
  };
  await postToBridge("/event", event, EVENT_TIMEOUT_MS);
}
