/**
 * Turns a Claude Code PermissionRequest hook payload into the short text and risk
 * level shown on the device. Risk drives the approval policy: voice may approve
 * only "low", IMU tap may only deny.
 */
import { basename, relative } from "node:path";
import type { Risk } from "../protocol/messages.js";

export interface PermissionInput {
  session_id?: string;
  cwd?: string;
  tool_name: string;
  tool_input?: Record<string, unknown>;
}

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
  } else {
    text = JSON.stringify(input.tool_input ?? {});
  }
  return truncate(text || tool);
}

export function projectName(input: PermissionInput): string {
  return input.cwd ? basename(input.cwd) : "";
}

function stringField(input: PermissionInput, key: string): string {
  const value = input.tool_input?.[key];
  return typeof value === "string" ? value : "";
}

/** ASCII ellipsis: the device's built-in fonts only cover ASCII. */
function truncate(text: string): string {
  return text.length > MAX_SUMMARY_CHARS ? `${text.slice(0, MAX_SUMMARY_CHARS - 3)}...` : text;
}
