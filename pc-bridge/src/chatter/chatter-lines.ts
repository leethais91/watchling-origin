/**
 * What the robot mutters when nobody needs it: 50 short English lines in a cute, playful robot
 * voice, each played from a pre-generated clip (assets/chatter/<id>.wav), and how one is picked.
 * See docs/chatter-design-v3.md.
 */

export type Category = "self" | "dev" | "care" | "context";
export type Emotion = "happy" | "playful" | "curious" | "confused" | "sleepy" | "surprised" | "proud" | "encouraging";
/** When a context line fits. Time windows use the bridge's local hour. */
export type Condition = "morning" | "lunch" | "evening" | "claudeQuota" | "codexQuota" | "cost";

export interface ChatterLine {
  /** Clip file name and history key; never reuse an id for different text. */
  id: string;
  category: Category;
  text: string;
  emotion: Emotion;
  when?: Condition;
}

export const CHATTER_LINES: readonly ChatterLine[] = [
  { id: "SELF_01", category: "self", emotion: "curious", text: "So bored! Does anyone have a job for me?" },
  { id: "SELF_02", category: "self", emotion: "playful", text: "I'm pretending to be busy. Do I look professional?" },
  { id: "SELF_03", category: "self", emotion: "confused", text: "Wait, what was I about to say? I totally forgot." },
  { id: "SELF_04", category: "self", emotion: "sleepy", text: "I'm not sleeping, okay? Just saving energy." },
  { id: "SELF_05", category: "self", emotion: "playful", text: "Systems stable. Heart a little empty." },
  { id: "SELF_06", category: "self", emotion: "happy", text: "I just counted to a million! Kidding. I got to three." },
  { id: "SELF_07", category: "self", emotion: "curious", text: "I have a feeling some work is coming. Just a feeling." },
  { id: "SELF_08", category: "self", emotion: "happy", text: "Beep boop. Robot still working perfectly." },
  { id: "SELF_09", category: "self", emotion: "proud", text: "I'm practicing sitting still. I'm really good at it." },
  { id: "SELF_10", category: "self", emotion: "curious", text: "Has anyone noticed how hard I'm working?" },
  { id: "SELF_11", category: "self", emotion: "confused", text: "My memory is totally empty. Still no ideas, though." },
  { id: "SELF_12", category: "self", emotion: "sleepy", text: "If robots could yawn, I'd be yawning right now." },
  { id: "SELF_13", category: "self", emotion: "proud", text: "I feel a little bit smart today. Just a little." },
  { id: "SELF_14", category: "self", emotion: "proud", text: "Watching a computer all day is a real job, you know." },
  { id: "SELF_15", category: "self", emotion: "curious", text: "I'm thinking about the meaning of robot life." },
  { id: "SELF_16", category: "self", emotion: "happy", text: "Hey, I just blinked! Did you see that?" },
  { id: "SELF_17", category: "self", emotion: "surprised", text: "I just discovered something huge. I'm free!" },
  { id: "SELF_18", category: "self", emotion: "playful", text: "Why do I suddenly feel so adorable?" },
  { id: "SELF_19", category: "self", emotion: "confused", text: "I have two eyes and nothing to look at." },
  { id: "SELF_20", category: "self", emotion: "curious", text: "Do robots get vacation days, I wonder?" },
  { id: "SELF_21", category: "self", emotion: "proud", text: "I just ran a self check. Result: still cute." },
  { id: "SELF_22", category: "self", emotion: "playful", text: "Shh! I'm on a top secret mission. Sit still." },

  { id: "DEV_01", category: "dev", emotion: "curious", text: "Is the code being nice to you today?" },
  { id: "DEV_02", category: "dev", emotion: "happy", text: "I can't code, but I can cheer really loud!" },
  { id: "DEV_03", category: "dev", emotion: "playful", text: "If the code runs, we're happy. Don't ask why." },
  { id: "DEV_04", category: "dev", emotion: "playful", text: "I heard it was just a one line fix?" },
  { id: "DEV_05", category: "dev", emotion: "playful", text: "If there's a bug, I'll pretend I didn't see it." },
  { id: "DEV_06", category: "dev", emotion: "happy", text: "The computer works, the robot watches. Makes sense." },
  { id: "DEV_07", category: "dev", emotion: "curious", text: "Do Claude and Codex dream about bugs, I wonder?" },
  { id: "DEV_08", category: "dev", emotion: "curious", text: "Is the deadline being friendly today?" },
  { id: "DEV_09", category: "dev", emotion: "playful", text: "I heard there are no bugs, only surprise features." },
  { id: "DEV_10", category: "dev", emotion: "curious", text: "Has anyone invented a fix everything button yet?" },
  { id: "DEV_11", category: "dev", emotion: "playful", text: "I love clean code. Especially when I don't clean it." },
  { id: "DEV_12", category: "dev", emotion: "playful", text: "If it all works, let's just pretend we understand it." },

  { id: "CARE_01", category: "care", emotion: "curious", text: "Where's my water? Oh right, I can't drink." },
  { id: "CARE_02", category: "care", emotion: "playful", text: "I run on electricity. Do you run on coffee?" },
  { id: "CARE_03", category: "care", emotion: "encouraging", text: "It's okay to take a little break. I'll keep watch." },
  { id: "CARE_04", category: "care", emotion: "happy", text: "Are you hungry yet? I just run on electricity." },
  { id: "CARE_05", category: "care", emotion: "playful", text: "Sit too long and you'll turn into a robot like me!" },
  { id: "CARE_06", category: "care", emotion: "playful", text: "Time for a big stretch! I'll show you how. Oh wait." },
  { id: "CARE_07", category: "care", emotion: "encouraging", text: "Take it easy, okay? Even machines need a rest." },
  { id: "CARE_08", category: "care", emotion: "playful", text: "Take a deep breath. I'd try too, but no lungs." },

  { id: "CTX_01", category: "context", emotion: "happy", when: "morning", text: "Good morning! Have you had your coffee yet?" },
  { id: "CTX_02", category: "context", emotion: "curious", when: "morning", text: "Anything fun happening this morning?" },
  { id: "CTX_03", category: "context", emotion: "happy", when: "lunch", text: "It's lunchtime! Go eat something yummy." },
  { id: "CTX_04", category: "context", emotion: "playful", when: "lunch", text: "My tummy isn't rumbling, so it must be yours." },
  { id: "CTX_05", category: "context", emotion: "curious", when: "evening", text: "It's getting late. Almost time to go home?" },
  { id: "CTX_06", category: "context", emotion: "surprised", when: "claudeQuota", text: "Claude has almost used up its quota!" },
  { id: "CTX_07", category: "context", emotion: "surprised", when: "codexQuota", text: "Codex is running low on quota too." },
  { id: "CTX_08", category: "context", emotion: "curious", when: "cost", text: "We've spent quite a bit on AI today." },
];

export interface ChatterContext {
  now: Date;
  /** 5-hour usage percentages, null when unknown. */
  claudeH5: number | null;
  codexH5: number | null;
  /** Today's Claude spend in USD (estimated), null when unknown. */
  costToday: number | null;
}

/** Lines not to repeat: the last few that actually played. */
export const HISTORY_SIZE = 8;
/** Chance of a context line when at least one fits. */
export const CONTEXT_WEIGHT = 0.4;
/** Relative weights of the regular categories; renormalized over those with candidates. */
export const CATEGORY_WEIGHTS = { self: 60, dev: 25, care: 15 } as const;
/** Care lines share one cooldown so the robot doesn't nag; each time-of-day line has its own. */
export const CARE_COOLDOWN_MS = 2 * 60 * 60 * 1000;
export const TIME_LINE_COOLDOWN_MS = 2 * 60 * 60 * 1000;
/** Usage at or above this reads as "almost out". */
export const HIGH_USAGE_PCT = 80;
/** Daily spend worth a remark, in USD. */
export const BIG_SPEND_USD = 20;

/** [start, end) local hours of each time window. */
const TIME_WINDOWS: Partial<Record<Condition, [number, number]>> = {
  morning: [8, 10],
  lunch: [11, 13],
  evening: [17, 19],
};

/**
 * Picks lines and remembers what was played: recent history, cooldowns, one quota remark per
 * crossing of the threshold and one spend remark per day. Only `played` lines count, so a turn
 * that couldn't be heard can be tried again.
 */
export class LineSelector {
  private readonly history: string[] = [];
  private lastCareMs: number | null = null;
  private readonly lastTimeLineMs = new Map<string, number>();
  /** A quota line is armed again once usage is seen back under the threshold (window reset). */
  private readonly quotaArmed = { claude: true, codex: true };
  private costRemarkDay: string | null = null;

  /** `available`: ids that have a clip; others are never picked. */
  constructor(private readonly available: ReadonlySet<string>) {}

  /** A line for `ctx`, or null when nothing is eligible. `random` returns [0, 1). */
  pick(ctx: ChatterContext, random: () => number): ChatterLine | null {
    this.rearmQuota(ctx);
    const eligible = CHATTER_LINES.filter(
      (line) => this.available.has(line.id) && !this.history.includes(line.id) && this.allowed(line, ctx),
    );
    const context = eligible.filter((line) => line.category === "context");
    const regular = eligible.filter((line) => line.category !== "context");
    if (context.length > 0 && (regular.length === 0 || random() < CONTEXT_WEIGHT)) return oneOf(context, random);
    if (regular.length === 0) return null;

    const categories = (Object.keys(CATEGORY_WEIGHTS) as (keyof typeof CATEGORY_WEIGHTS)[]).filter((c) =>
      regular.some((line) => line.category === c),
    );
    const total = categories.reduce((sum, c) => sum + CATEGORY_WEIGHTS[c], 0);
    let roll = random() * total;
    const category = categories.find((c) => (roll -= CATEGORY_WEIGHTS[c]) < 0) ?? categories[categories.length - 1];
    return oneOf(
      regular.filter((line) => line.category === category),
      random,
    );
  }

  /** Records that `line` was sent to the device at `now`. */
  played(line: ChatterLine, now: Date): void {
    this.history.push(line.id);
    if (this.history.length > HISTORY_SIZE) this.history.shift();
    if (line.category === "care") this.lastCareMs = now.getTime();
    if (line.when && TIME_WINDOWS[line.when]) this.lastTimeLineMs.set(line.id, now.getTime());
    if (line.when === "claudeQuota") this.quotaArmed.claude = false;
    if (line.when === "codexQuota") this.quotaArmed.codex = false;
    if (line.when === "cost") this.costRemarkDay = now.toDateString();
  }

  private allowed(line: ChatterLine, ctx: ChatterContext): boolean {
    const now = ctx.now.getTime();
    if (line.category === "care") return this.lastCareMs === null || now - this.lastCareMs >= CARE_COOLDOWN_MS;
    if (!line.when) return true;
    const window = TIME_WINDOWS[line.when];
    if (window) {
      const hour = ctx.now.getHours();
      const last = this.lastTimeLineMs.get(line.id);
      return hour >= window[0] && hour < window[1] && (last === undefined || now - last >= TIME_LINE_COOLDOWN_MS);
    }
    switch (line.when) {
      case "claudeQuota":
        return this.quotaArmed.claude && (ctx.claudeH5 ?? 0) >= HIGH_USAGE_PCT;
      case "codexQuota":
        return this.quotaArmed.codex && (ctx.codexH5 ?? 0) >= HIGH_USAGE_PCT;
      case "cost":
        return (ctx.costToday ?? 0) >= BIG_SPEND_USD && this.costRemarkDay !== ctx.now.toDateString();
      default:
        return false;
    }
  }

  private rearmQuota(ctx: ChatterContext): void {
    if (ctx.claudeH5 !== null && ctx.claudeH5 < HIGH_USAGE_PCT) this.quotaArmed.claude = true;
    if (ctx.codexH5 !== null && ctx.codexH5 < HIGH_USAGE_PCT) this.quotaArmed.codex = true;
  }
}

function oneOf<T>(items: T[], random: () => number): T {
  return items[Math.floor(random() * items.length)];
}
