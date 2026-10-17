/** What the device says out loud: short, plain English that reads well through TTS. */
import type { AgentSource, ConfirmRequestMessage } from "../protocol/messages.js";

const MAX_SPOKEN_CHARS = 140;
const MAX_COMMAND_WORDS = 6;
const FILE_TOOLS = new Set(["Edit", "Write", "MultiEdit", "NotebookEdit"]);

const agentName = (src: AgentSource) => (src === "codex" ? "Codex" : "Claude");

export function permissionSpeech(req: Pick<ConfirmRequestMessage, "tool" | "summary">): string {
  const { tool, summary } = req;
  if (tool === "Bash") return `Claude wants to run ${shortCommand(summary)}`;
  if (FILE_TOOLS.has(tool)) return `Claude wants to edit ${summary.split("/").pop() || "a file"}`;
  if (tool === "WebFetch") return `Claude wants to fetch ${hostOf(summary)}`;
  const mcp = /^mcp__(.+?)__(.+)$/.exec(tool);
  if (mcp) return `Claude wants to use ${words(mcp[2])} from ${words(mcp[1])}`;
  return `Claude wants to use ${words(tool)}`;
}

/** "Claude finished." plus the first sentence of the agent's final message. */
export function doneSpeech(src: AgentSource, text: string): string {
  const sentence = firstSentence(plain(text));
  return sentence ? `${agentName(src)} finished. ${sentence}` : `${agentName(src)} finished.`;
}

/** First segment of the first line, cut before pipes/chains and long argument lists. */
function shortCommand(command: string): string {
  const head = command.split("\n")[0].split(/\s*(?:&&|\|\||;|\|)\s*/)[0];
  const parts = head.trim().split(/\s+/);
  return parts.slice(0, MAX_COMMAND_WORDS).join(" ") + (parts.length > MAX_COMMAND_WORDS ? " and more" : "");
}

function hostOf(url: string): string {
  try {
    return new URL(url).hostname;
  } catch {
    return "a web page";
  }
}

const words = (identifier: string) => identifier.replace(/[_-]+/g, " ").trim();

/** Drops markdown and code formatting that TTS would read out as symbols. */
function plain(text: string): string {
  return text
    .replace(/```[\s\S]*?```/g, " ")
    .replace(/https?:\/\/\S+/g, "a link")
    .replace(/[*_#>`~|]/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

function firstSentence(text: string): string {
  const sentence = /^(.+?[.!?])(?:\s|$)/.exec(text)?.[1] ?? text;
  if (sentence.length <= MAX_SPOKEN_CHARS) return sentence;
  const cut = sentence.slice(0, MAX_SPOKEN_CHARS);
  return cut.slice(0, cut.lastIndexOf(" ") > 0 ? cut.lastIndexOf(" ") : cut.length);
}
