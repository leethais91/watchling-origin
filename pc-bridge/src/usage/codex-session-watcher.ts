/**
 * Follows Codex CLI rollout files (~/.codex/sessions/YYYY/MM/DD/*.jsonl) for
 * `token_count` events (rate limits), `task_started` / `turn_aborted` (working or not) and
 * `task_complete` events (turn finished).
 * The format is undocumented, so every field is read defensively.
 */
import { open, readdir, stat } from "node:fs/promises";
import { watch } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import type { CodexRateLimits } from "./usage-store.js";

export const CODEX_SESSIONS_DIR = join(homedir(), ".codex", "sessions");

export interface CodexWatcherHandlers {
  onRateLimits: (limits: CodexRateLimits) => void;
  onTaskComplete: (lastMessage: string) => void;
  onActivity: (busy: boolean) => void;
}

interface FileCursor {
  offset: number;
  /** Trailing partial line from the previous read. */
  remainder: string;
}

export async function startCodexSessionWatcher(handlers: CodexWatcherHandlers): Promise<void> {
  const cursors = new Map<string, FileCursor>();

  // Seed usage from the newest file without replaying its old turn events.
  const newest = await findNewestRollout(CODEX_SESSIONS_DIR);
  if (newest) {
    const cursor: FileCursor = { offset: 0, remainder: "" };
    await readAppended(newest, cursor, { ...handlers, onTaskComplete: () => {}, onActivity: () => {} });
    cursors.set(newest, cursor);
  }

  let reading = Promise.resolve();
  try {
    watch(CODEX_SESSIONS_DIR, { recursive: true }, (_event, filename) => {
      if (!filename?.endsWith(".jsonl")) return;
      const path = join(CODEX_SESSIONS_DIR, filename);
      // Serialize reads so appended chunks are processed in order.
      reading = reading.then(async () => {
        let cursor = cursors.get(path);
        if (!cursor) {
          cursor = { offset: 0, remainder: "" };
          cursors.set(path, cursor);
        }
        await readAppended(path, cursor, handlers).catch(() => {});
      });
    });
  } catch {
    // No Codex sessions directory: Codex usage stays unknown.
  }
}

async function readAppended(path: string, cursor: FileCursor, handlers: CodexWatcherHandlers): Promise<void> {
  const file = await open(path, "r");
  try {
    const { size } = await file.stat();
    if (size < cursor.offset) cursor.offset = 0; // truncated or replaced
    if (size === cursor.offset) return;
    const buffer = Buffer.alloc(size - cursor.offset);
    await file.read(buffer, 0, buffer.length, cursor.offset);
    cursor.offset = size;

    const lines = (cursor.remainder + buffer.toString("utf8")).split("\n");
    cursor.remainder = lines.pop() ?? "";
    for (const line of lines) handleLine(line, handlers);
  } finally {
    await file.close();
  }
}

function handleLine(line: string, handlers: CodexWatcherHandlers): void {
  let record: { type?: string; payload?: Record<string, unknown> };
  try {
    record = JSON.parse(line);
  } catch {
    return;
  }
  if (record.type !== "event_msg" || !record.payload) return;
  const payload = record.payload;
  if (payload.type === "token_count" && payload.rate_limits && typeof payload.rate_limits === "object") {
    handlers.onRateLimits(payload.rate_limits as CodexRateLimits);
  } else if (payload.type === "task_complete") {
    const message = typeof payload.last_agent_message === "string" ? payload.last_agent_message : "";
    handlers.onTaskComplete(message);
  } else if (payload.type === "task_started") {
    handlers.onActivity(true);
  } else if (payload.type === "turn_aborted") {
    handlers.onActivity(false);
  }
}

/** Walks the YYYY/MM/DD tree newest-first and returns the most recently modified rollout. */
async function findNewestRollout(root: string): Promise<string | null> {
  const newestChild = async (dir: string): Promise<string[]> =>
    (await readdir(dir).catch(() => [] as string[])).sort().reverse().map((name) => join(dir, name));

  for (const year of await newestChild(root)) {
    for (const month of await newestChild(year)) {
      for (const day of await newestChild(month)) {
        const files = (await newestChild(day)).filter((f) => f.endsWith(".jsonl"));
        if (files.length === 0) continue;
        const stats = await Promise.all(files.map(async (f) => ({ f, mtime: (await stat(f)).mtimeMs })));
        return stats.sort((a, b) => b.mtime - a.mtime)[0].f;
      }
    }
  }
  return null;
}
