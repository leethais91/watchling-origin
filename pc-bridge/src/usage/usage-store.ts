/**
 * Latest subscription usage for Claude Code (from the statusline payload) and Codex
 * (from session rollout files), plus today's estimated Claude Code spend.
 */
import { EventEmitter } from "node:events";
import type { UsageMessage } from "../protocol/messages.js";

interface UsageWindow {
  pct: number;
  /** Unix epoch seconds; after this the window has reset to 0%. */
  resetsAt: number | null;
}

interface AgentWindows {
  h5: UsageWindow | null;
  d7: UsageWindow | null;
}

/** Subset of the Claude Code statusline stdin JSON that the bridge reads. */
export interface StatuslineInput {
  session_id?: string;
  cost?: { total_cost_usd?: number };
  rate_limits?: {
    five_hour?: { used_percentage?: number; resets_at?: number };
    seven_day?: { used_percentage?: number; resets_at?: number };
  };
}

/** Subset of a Codex `token_count` event's rate_limits object. */
export interface CodexRateLimits {
  primary?: { used_percent?: number; resets_at?: number } | null;
  secondary?: { used_percent?: number; resets_at?: number } | null;
}

export class UsageStore extends EventEmitter<{ change: [] }> {
  private readonly claude: AgentWindows = { h5: null, d7: null };
  private readonly codex: AgentWindows = { h5: null, d7: null };
  /** Latest cumulative cost per Claude Code session seen today. */
  private readonly sessionCosts = new Map<string, number>();
  private costDay = localDayKey();

  updateFromStatusline(input: StatuslineInput): void {
    const limits = input.rate_limits;
    // Windows are absent before the first response and after they reset; keep the last value.
    this.claude.h5 = toWindow(limits?.five_hour?.used_percentage, limits?.five_hour?.resets_at) ?? this.claude.h5;
    this.claude.d7 = toWindow(limits?.seven_day?.used_percentage, limits?.seven_day?.resets_at) ?? this.claude.d7;

    const cost = input.cost?.total_cost_usd;
    if (input.session_id && typeof cost === "number") {
      this.rollDay();
      this.sessionCosts.set(input.session_id, cost);
    }
    this.emit("change");
  }

  updateCodex(limits: CodexRateLimits): void {
    this.codex.h5 = toWindow(limits.primary?.used_percent, limits.primary?.resets_at) ?? this.codex.h5;
    this.codex.d7 = toWindow(limits.secondary?.used_percent, limits.secondary?.resets_at) ?? this.codex.d7;
    this.emit("change");
  }

  snapshot(): UsageMessage {
    this.rollDay();
    const costs = [...this.sessionCosts.values()];
    return {
      t: "usage",
      claude: {
        h5: currentPct(this.claude.h5),
        d7: currentPct(this.claude.d7),
        costToday: costs.length ? Math.round(costs.reduce((a, b) => a + b, 0) * 100) / 100 : null,
      },
      codex: { h5: currentPct(this.codex.h5), d7: currentPct(this.codex.d7) },
    };
  }

  private rollDay(): void {
    const today = localDayKey();
    if (today !== this.costDay) {
      this.costDay = today;
      this.sessionCosts.clear();
    }
  }
}

function toWindow(pct: number | undefined, resetsAt: number | undefined): UsageWindow | null {
  return typeof pct === "number" ? { pct, resetsAt: typeof resetsAt === "number" ? resetsAt : null } : null;
}

function currentPct(window: UsageWindow | null): number | null {
  if (!window) return null;
  if (window.resetsAt !== null && Date.now() / 1000 >= window.resetsAt) return 0;
  return Math.max(0, Math.min(100, Math.round(window.pct)));
}

function localDayKey(): string {
  return new Date().toLocaleDateString("en-CA");
}
