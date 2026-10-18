/** What the robot mutters when nobody needs it: short Vietnamese lines for the Linh voice. */

export interface ChatterContext {
  /** Local hour 0..23. */
  hour: number;
  /** 5-hour usage percentages, null when unknown. */
  claudeH5: number | null;
  codexH5: number | null;
  /** Today's Claude spend in USD, null when unknown. */
  costToday: number | null;
}

const ANYTIME = [
  "Rảnh quá, có ai giao việc cho tui không?",
  "Tui đang canh Claude giùm bạn nè.",
  "Bíp bíp. Hệ thống vẫn ổn định.",
  "Uống miếng nước đi bạn ơi.",
  "Đứng dậy vươn vai chút đi.",
  "Hôm nay code vui không?",
  "Tui là robot, nhưng tui cũng biết chán đó nha.",
  "Không ai chơi với tui hết trơn.",
  "Commit chưa đó? Đừng để mất code nha.",
];

const LATE_NIGHT = ["Khuya rồi, đi ngủ đi bạn ơi.", "Giờ này còn thức là cú đêm rồi đó.", "Mai code tiếp cũng được mà."];
const MORNING = ["Chào buổi sáng! Cà phê chưa?", "Sáng nay làm gì đây ta?"];
const LUNCH = ["Tới giờ ăn trưa rồi đó.", "Đói bụng chưa? Tui thì chạy bằng điện."];
const EVENING = ["Chiều rồi, sắp tan làm chưa?"];

/** Usage at or above this reads as "almost out". */
const HIGH_USAGE_PCT = 80;
/** Daily spend worth teasing about, in USD. */
const BIG_SPEND_USD = 20;

/** Lines that fit the moment; they are weighted above the anytime ones. */
function contextLines(ctx: ChatterContext): string[] {
  const lines: string[] = [];
  const { hour } = ctx;
  if (hour >= 23 || hour < 4) lines.push(...LATE_NIGHT);
  else if (hour >= 6 && hour < 10) lines.push(...MORNING);
  else if (hour >= 11 && hour < 13) lines.push(...LUNCH);
  else if (hour >= 17 && hour < 19) lines.push(...EVENING);
  if ((ctx.claudeH5 ?? 0) >= HIGH_USAGE_PCT) lines.push("Claude xài gần hết quota rồi đó nha.");
  if ((ctx.codexH5 ?? 0) >= HIGH_USAGE_PCT) lines.push("Codex cũng sắp hết quota rồi.");
  if ((ctx.costToday ?? 0) >= BIG_SPEND_USD) lines.push(`Hôm nay đốt ${Math.round(ctx.costToday ?? 0)} đô rồi đó.`);
  return lines;
}

/** Picks a line for `ctx`, never `previous` twice in a row. `random` returns [0, 1). */
export function chatterLine(ctx: ChatterContext, random: () => number, previous?: string): string {
  const fitting = contextLines(ctx);
  const pool = [...ANYTIME, ...fitting, ...fitting].filter((line) => line !== previous);
  return pool[Math.floor(random() * pool.length)];
}
