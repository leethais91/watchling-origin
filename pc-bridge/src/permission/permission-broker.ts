/**
 * Queues permission requests and shows them on the device one at a time.
 *
 * Every request resolves exactly once: with the device's decision, or with null
 * (timeout, hook process gone, device unreachable for a while) so Claude Code falls back to
 * its own terminal prompt.
 */
import type {
  ConfirmRequestMessage,
  Decision,
  DecisionSource,
  HostMessage,
} from "../protocol/messages.js";
import { classifyRisk, projectName, summarizePermission, type PermissionInput } from "./permission-summary.js";

interface PendingRequest {
  message: ConfirmRequestMessage;
  resolve: (decision: Decision | null) => void;
  timer: NodeJS.Timeout;
}

/** Approval policy enforced on the bridge side regardless of what the firmware allows. */
export function isDecisionAllowed(message: ConfirmRequestMessage, action: Decision, src: DecisionSource): boolean {
  if (src === "touch") return true;
  if (src === "tap") return action === "deny";
  return action === "deny" || message.risk === "low";
}

export class PermissionBroker {
  private readonly queue: PendingRequest[] = [];
  private nextId = 1;
  private releaseTimer: NodeJS.Timeout | null = null;

  constructor(
    private readonly send: (msg: HostMessage) => boolean,
    private readonly timeoutMs: number,
  ) {}

  /** Requests waiting for a decision, the one on screen included. */
  get pendingCount(): number {
    return this.queue.length;
  }

  request(input: PermissionInput, signal: AbortSignal): Promise<Decision | null> {
    return new Promise((resolve) => {
      const id = this.nextId++;
      const message: ConfirmRequestMessage = {
        t: "confirm_req",
        id,
        src: input.src ?? "claude",
        tool: input.tool_name,
        summary: summarizePermission(input),
        risk: classifyRisk(input),
        project: projectName(input),
      };
      const timer = setTimeout(() => this.finish(id, null), this.timeoutMs);
      this.queue.push({ message, resolve, timer });
      signal.addEventListener("abort", () => this.finish(id, null), { once: true });
      if (this.queue.length === 1) this.send(message);
    });
  }

  /** Applies a device answer; only the request currently on screen can be answered. */
  onDeviceDecision(id: number, action: Decision, src: DecisionSource): boolean {
    const active = this.queue[0];
    if (!active || active.message.id !== id || !isDecisionAllowed(active.message, action, src)) return false;
    this.finish(id, action);
    return true;
  }

  /** Re-shows the active request, e.g. after the device rebooted. */
  resendActive(): void {
    const active = this.queue[0];
    if (active) this.send(active.message);
  }

  /**
   * The device link dropped: release every waiting hook after `graceMs` unless `keepAll()`
   * comes first. A BLE link often reconnects within seconds; the device's `hello` then
   * brings back the active request through `resendActive()`.
   */
  cancelAllAfter(graceMs: number): void {
    this.releaseTimer ??= setTimeout(() => this.cancelAll(), graceMs);
  }

  /** The link is back in time; returns whether a release was pending. */
  keepAll(): boolean {
    if (!this.releaseTimer) return false;
    clearTimeout(this.releaseTimer);
    this.releaseTimer = null;
    return true;
  }

  /** Releases every waiting hook to the terminal prompt (now, so a pending grace is moot). */
  cancelAll(): void {
    if (this.releaseTimer) clearTimeout(this.releaseTimer);
    this.releaseTimer = null;
    for (const pending of [...this.queue]) this.finish(pending.message.id, null);
  }

  private finish(id: number, decision: Decision | null): void {
    const index = this.queue.findIndex((p) => p.message.id === id);
    if (index === -1) return;
    const [pending] = this.queue.splice(index, 1);
    clearTimeout(pending.timer);
    if (index === 0) {
      if (decision === null) this.send({ t: "confirm_cancel", id });
      const next = this.queue[0];
      if (next) this.send(next.message);
    }
    pending.resolve(decision);
  }
}
