/**
 * Idle chatter: after 5–10 minutes with no agent activity the robot says something
 * silly, at most a few times per quiet spell so it doesn't talk to an empty room all
 * night. Any agent activity starts a new spell; nothing is said while an agent works.
 *
 *   ECHOEAR_CHATTER=off            no chatter (also off whenever speech is off)
 *   ECHOEAR_CHATTER_VOICE=Linh     macOS voice for the Vietnamese lines
 */
import type { AgentSource, UsageMessage } from "../protocol/messages.js";
import { chatterLine } from "./chatter-lines.js";

export const MIN_QUIET_MS = 5 * 60 * 1000;
export const MAX_QUIET_MS = 10 * 60 * 1000;
export const MAX_LINES_PER_SPELL = 3;
/** Claude Code fires no hook when a turn is interrupted, so a busy flag also runs out (as on the device). */
export const BUSY_TIMEOUT_MS = 15 * 60 * 1000;

export interface ChatterOptions {
  say: (text: string) => void;
  usage: () => UsageMessage;
  now?: () => Date;
  /** [0, 1), like Math.random. */
  random?: () => number;
}

export class Chatter {
  private timer: NodeJS.Timeout | null = null;
  private spoken = 0;
  private previous: string | undefined;
  private readonly busySince = new Map<AgentSource, number>();
  private readonly now: () => Date;
  private readonly random: () => number;

  constructor(private readonly opts: ChatterOptions) {
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
    this.schedule();
  }

  private schedule(): void {
    this.stop();
    if (this.spoken >= MAX_LINES_PER_SPELL) return;
    const delay = MIN_QUIET_MS + Math.floor(this.random() * (MAX_QUIET_MS - MIN_QUIET_MS));
    this.timer = setTimeout(() => this.tick(), delay);
    this.timer.unref();
  }

  private tick(): void {
    this.timer = null;
    if (this.anyBusy()) {
      this.schedule();
      return;
    }
    const usage = this.opts.usage();
    const line = chatterLine(
      { hour: this.now().getHours(), claudeH5: usage.claude.h5, codexH5: usage.codex.h5, costToday: usage.claude.costToday },
      this.random,
      this.previous,
    );
    this.previous = line;
    this.spoken++;
    this.opts.say(line);
    this.schedule();
  }

  private anyBusy(): boolean {
    const now = this.now().getTime();
    for (const [src, since] of this.busySince) {
      if (now - since > BUSY_TIMEOUT_MS) this.busySince.delete(src);
    }
    return this.busySince.size > 0;
  }
}
