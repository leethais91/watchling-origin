/** JSON messages carried in FrameType.Json frames. Keep in sync with docs/device-protocol.md. */

export type Risk = "low" | "medium" | "high";
export type Decision = "approve" | "deny";
export type DecisionSource = "touch" | "voice" | "tap";
export type AgentSource = "claude" | "codex";
export type EventKind = "done" | "attention" | "error";

/** Percentages are integers 0..100, null when unknown. */
export interface UsageMessage {
  t: "usage";
  claude: { h5: number | null; d7: number | null; costToday: number | null };
  codex: { h5: number | null; d7: number | null };
}

/** Unix seconds (UTC) and the host's offset from UTC in minutes (Asia/Saigon = 420). */
export interface ClockMessage {
  t: "time";
  epoch: number;
  tz: number;
}

/** Speech clip header; the PCM (16 kHz mono s16le) follows in FrameType.Audio frames. */
export interface SpeechBeginMessage {
  t: "say";
  id: number;
  bytes: number;
}

export interface ConfirmRequestMessage {
  t: "confirm_req";
  id: number;
  src: AgentSource;
  tool: string;
  summary: string;
  risk: Risk;
  project: string;
}

export interface AgentEventMessage {
  t: "event";
  src: AgentSource;
  kind: EventKind;
  text: string;
}

/** An agent started (busy) or stopped working without a turn-ending event. */
export interface AgentActivityMessage {
  t: "activity";
  src: AgentSource;
  busy: boolean;
}

/** Bridge → device */
export type HostMessage =
  | { t: "hello" }
  | { t: "ping" }
  | UsageMessage
  | ClockMessage
  | SpeechBeginMessage
  | { t: "say_end"; id: number }
  | AgentEventMessage
  | AgentActivityMessage
  | ConfirmRequestMessage
  | { t: "confirm_cancel"; id: number };

/** Device → bridge */
export type DeviceMessage =
  | { t: "hello"; fw: string }
  | { t: "pong" }
  | { t: "confirm_res"; id: number; action: Decision; src: DecisionSource };
