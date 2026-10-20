/**
 * Picks which device link the bridge talks on: the first alive link in preference order
 * (USB, then BLE). Standby links keep pinging so a switch is instant, but only the active
 * link's messages reach the bridge; the device answers on every link, so this also drops
 * the duplicates. A switch sends `hello`, and the device's reply makes the bridge re-sync.
 */
import { EventEmitter } from "node:events";
import type { HostMessage } from "../protocol/messages.js";
import type { DeviceLink, LinkEvents } from "./device-link.js";

const EVALUATE_INTERVAL_MS = 1000;

export class TransportHub extends EventEmitter<LinkEvents> implements DeviceLink {
  private activeLink: DeviceLink | null = null;
  private timer: NodeJS.Timeout | null = null;

  /** `links` in order of preference, each with a short name for logs. */
  constructor(private readonly links: { name: string; link: DeviceLink }[]) {
    super();
    for (const { name, link } of links) {
      link.on("message", (msg) => {
        this.evaluate();
        if (link === this.activeLink) this.emit("message", msg);
      });
      link.on("log", (line) => this.emit("log", line));
      link.on("connected", (label) => this.emit("log", `${name} link open: ${label}`));
      link.on("disconnected", () => this.evaluate());
    }
  }

  get active(): DeviceLink | null {
    return this.activeLink;
  }

  /** Name of the active link ("USB", "BLE"), or null when none answers. */
  get activeName(): string | null {
    return this.links.find(({ link }) => link === this.activeLink)?.name ?? null;
  }

  get alive(): boolean {
    return this.activeLink?.alive ?? false;
  }

  start(): void {
    for (const { link } of this.links) link.start();
    this.timer = setInterval(() => this.evaluate(), EVALUATE_INTERVAL_MS);
    this.timer.unref();
  }

  send(msg: HostMessage): boolean {
    return this.activeLink?.send(msg) ?? false;
  }

  sendAudio(payload: Uint8Array): boolean {
    return this.activeLink?.sendAudio(payload) ?? false;
  }

  close(): void {
    if (this.timer) clearInterval(this.timer);
    for (const { link } of this.links) link.close();
  }

  /** Re-picks the active link; public for tests. */
  evaluate(): void {
    const next = this.links.find(({ link }) => link.alive) ?? null;
    if ((next?.link ?? null) === this.activeLink) return;
    this.activeLink = next?.link ?? null;
    if (next) {
      this.emit("connected", next.name);
      next.link.send({ t: "hello" });
    } else {
      this.emit("disconnected");
    }
  }
}
