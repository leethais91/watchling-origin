/**
 * Idle chatter: after 10 minutes with no agent activity the robot says one short line, then another
 * every 10–20 minutes up to four, then stays quiet until an agent does something again. Never while
 * an agent works and never at night.
 *
 *   WATCHLING_CHATTER=off            no chatter (also off whenever speech is off)
 *   WATCHLING_CHATTER_DELAY_S=30     fixed wait instead of 10–20 minutes, for trying it out
 */
import type { AgentSource, UsageMessage } from "../protocol/messages.js";
import { type ChatterLine, LineSelector } from "./chatter-lines.js";

const MINUTE_MS = 60 * 1000;
export const FIRST_QUIET_MS = { min: 10 * MINUTE_MS, max: 10 * MINUTE_MS };
export const NEXT_QUIET_MS = { min: 10 * MINUTE_MS, max: 20 * MINUTE_MS };
export const MAX_LINES_PER_SPELL = 4;
/** Local hours [start, end) wrapping midnight with no chatter. */
export const QUIET_HOURS = { start: 22, end: 8 };
/** Claude Code fires no hook when a turn is interrupted, so a busy flag also runs out (as on the device). */
export const BUSY_TIMEOUT_MS = 15 * 60 * 1000;

export interface ChatterOptions {
  /** Plays `line`; returns false when it could not be sent (voice off, device not on USB). */
  play: (line: ChatterLine) => boolean;
  usage: () => UsageMessage;
  /** Ids of the lines that have a clip. */
  available: ReadonlySet<string>;
  now?: () => Date;
  /** [0, 1), like Math.random. */
  random?: () => number;
  /** Fixed wait before every turn instead of FIRST/NEXT_QUIET_MS, for trying it out. */
  quietMs?: { min: number; max: number };
}

export class Chatter {
  private timer: NodeJS.Timeout | null = null;
  /** Lines played and turns taken (played, silent or skipped) in this quiet spell. */
  private spoken = 0;
  private turns = 0;
  private readonly busySince = new Map<AgentSource, number>();
  private readonly selector: LineSelector;
  private readonly now: () => Date;
  private readonly random: () => number;

  constructor(private readonly opts: ChatterOptions) {
    this.selector = new LineSelector(opts.available);
    this.now = opts.now ?? (() => new Date());
    this.random = opts.random ?? Math.random;
  }

  start(): void {
    this.schedule();
  }

  stop(): void {
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
  }

  /** An agent started or stopped working, asked for permission or finished: a new quiet spell. */
  onAgent(src: AgentSource, busy: boolean): void {
    if (busy) this.busySince.set(src, this.now().getTime());
    else this.busySince.delete(src);
    this.spoken = 0;
    this.turns = 0;
    this.schedule();
  }

  private schedule(): void {
    this.stop();
    if (this.spoken >= MAX_LINES_PER_SPELL) return;
    const { min, max } = this.opts.quietMs ?? (this.turns === 0 ? FIRST_QUIET_MS : NEXT_QUIET_MS);
    const delay = min + Math.floor(this.random() * (max - min));
    this.timer = setTimeout(() => this.tick(), delay);
    this.timer.unref();
  }

  private tick(): void {
    this.timer = null;
    // Checked when the timer fires, not when it was set: an agent may have started meanwhile.
    if (this.anyBusy()) {
      this.schedule();
      return;
    }
    this.turns++;
    if (!this.quietHours()) this.speak();
    this.schedule();
  }

  private speak(): void {
    const now = this.now();
    const usage = this.opts.usage();
    const line = this.selector.pick(
      { now, claudeH5: usage.claude.h5, codexH5: usage.codex.h5, costToday: usage.claude.costToday },
      this.random,
    );
    if (line && this.opts.play(line)) {
      this.selector.played(line, now);
      this.spoken++;
    }
  }

  private quietHours(): boolean {
    const hour = this.now().getHours();
    return hour >= QUIET_HOURS.start || hour < QUIET_HOURS.end;
  }

  private anyBusy(): boolean {
    const now = this.now().getTime();
    for (const [src, since] of this.busySince) {
      if (now - since > BUSY_TIMEOUT_MS) this.busySince.delete(src);
    }
    return this.busySince.size > 0;
  }
}
