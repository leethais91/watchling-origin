/**
 * Turns a PermissionRequest hook payload (Claude Code or Codex, same shape) into the
 * short text and risk level shown on the device. Risk drives the approval policy:
 * voice may approve only "low", IMU tap may only deny.
 */
import { basename, isAbsolute, relative } from "node:path";
import type { AgentSource, Risk } from "../protocol/messages.js";

export interface PermissionInput {
  session_id?: string;
  cwd?: string;
  tool_name: string;
  tool_input?: Record<string, unknown>;
  /** Added by our hook; absent means Claude Code. */
  src?: AgentSource;
}

/** Codex edits files with apply_patch; tool_input.command holds the patch text. */
const CODEX_PATCH_TOOL = "apply_patch";
const PATCH_FILE_LINE = /^\*\*\* (Add|Update|Delete) File: (.+)$/gm;

const MAX_SUMMARY_CHARS = 240;

/** Shell commands that delete, publish, escalate or rewrite history. */
const HIGH_RISK_COMMAND =
  /\b(rm|rmdir|sudo|su|dd|mkfs|chmod|chown|kill|pkill|killall|shutdown|reboot|launchctl)\b|\bgit\s+(push|reset|clean|rebase|checkout\s+--|branch\s+-D)|\bnpm\s+publish\b|\b(curl|wget)\b[^|]*\|\s*(ba|z)?sh\b|\bdrop\s+(table|database)\b|>\s*\/(etc|usr|bin)\//i;

/** Tools that only read or fetch. */
const LOW_RISK_TOOLS = new Set(["Read", "Glob", "Grep", "WebFetch", "WebSearch"]);

const FILE_TOOLS = new Set(["Edit", "Write", "MultiEdit", "NotebookEdit"]);

export function classifyRisk(input: PermissionInput): Risk {
  if (input.tool_name === "Bash") {
    return HIGH_RISK_COMMAND.test(stringField(input, "command")) ? "high" : "medium";
  }
  if (input.tool_name === CODEX_PATCH_TOOL) {
    return patchFiles(input).some((f) => f.action === "Delete") ? "high" : "medium";
  }
  return LOW_RISK_TOOLS.has(input.tool_name) ? "low" : "medium";
}

export function summarizePermission(input: PermissionInput): string {
  const tool = input.tool_name;
  let text: string;
  if (tool === "Bash") {
    text = stringField(input, "command");
  } else if (FILE_TOOLS.has(tool)) {
    const path = stringField(input, "file_path") || stringField(input, "notebook_path");
    text = input.cwd && path ? relative(input.cwd, path) : path;
  } else if (tool === "WebFetch") {
    text = stringField(input, "url");
  } else if (tool === CODEX_PATCH_TOOL) {
    // One line per file, e.g. "update src/app.ts".
    text = patchFiles(input)
      .map((f) => `${f.action.toLowerCase()} ${f.path}`)
      .join("\n");
  } else {
    text = JSON.stringify(input.tool_input ?? {});
  }
  return truncate(text || tool);
}

export function projectName(input: PermissionInput): string {
  return input.cwd ? basename(input.cwd) : "";
}

function patchFiles(input: PermissionInput): { action: string; path: string }[] {
  return [...stringField(input, "command").matchAll(PATCH_FILE_LINE)].map(([, action, path]) => ({
    action,
    path: input.cwd && isAbsolute(path) ? relative(input.cwd, path) : path.trim(),
  }));
}

function stringField(input: PermissionInput, key: string): string {
  const value = input.tool_input?.[key];
  return typeof value === "string" ? value : "";
}

/** ASCII ellipsis keeps the cut obvious in any font. */
function truncate(text: string): string {
  return text.length > MAX_SUMMARY_CHARS ? `${text.slice(0, MAX_SUMMARY_CHARS - 3)}...` : text;
}
