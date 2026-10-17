/**
 * Queues permission requests and shows them on the device one at a time.
 *
 * Every request resolves exactly once: with the device's decision, or with null
 * (timeout, hook process gone, device unplugged) so Claude Code falls back to
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

  constructor(
    private readonly send: (msg: HostMessage) => boolean,
    private readonly timeoutMs: number,
  ) {}

  request(input: PermissionInput, signal: AbortSignal): Promise<Decision | null> {
    return new Promise((resolve) => {
      const id = this.nextId++;
      const message: ConfirmRequestMessage = {
        t: "confirm_req",
        id,
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

  /** Releases every waiting hook to the terminal prompt. */
  cancelAll(): void {
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
