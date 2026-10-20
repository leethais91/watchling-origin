/**
 * USB-Serial-JTAG link to the device: auto-detects the Espressif port, reconnects
 * after unplug or when an open port stays silent, and tracks liveness with ping/pong.
 */
import { EventEmitter } from "node:events";
import { setTimeout as sleep } from "node:timers/promises";
import { SerialPort } from "serialport";
import { FrameType, encodeFrame } from "../protocol/frame-codec.js";
import type { HostMessage } from "../protocol/messages.js";
import {
  ALIVE_WINDOW_MS,
  PING_INTERVAL_MS,
  createFrameReader,
  encodeJsonFrame,
  isLinkStale,
  type DeviceLink,
  type LinkEvents,
} from "./device-link.js";

/** USB vendor ID assigned to Espressif (ESP32-S3 USB-Serial-JTAG enumerates with it). */
export const ESPRESSIF_VENDOR_ID = "303a";

const RECONNECT_INTERVAL_MS = 2000;

export async function findDevicePath(): Promise<string | null> {
  const ports = await SerialPort.list();
  const device = ports.find((p) => p.vendorId?.toLowerCase() === ESPRESSIF_VENDOR_ID);
  if (!device) return null;
  // On macOS the callout device (cu.*) opens without waiting for carrier detect.
  return process.platform === "darwin" ? device.path.replace("/dev/tty.", "/dev/cu.") : device.path;
}

export class SerialTransport extends EventEmitter<LinkEvents> implements DeviceLink {
  private port: SerialPort | null = null;
  private lastSeenMs = 0;
  private openedAtMs = 0;
  private stopped = false;

  constructor(private readonly fixedPath?: string) {
    super();
  }

  start(): void {
    void this.connectLoop();
    setInterval(() => {
      // A port can stay open while the device never answers (e.g. it re-enumerated under
      // another path); only a close makes connectLoop look for the device again.
      const port = this.port;
      if (port?.isOpen && isLinkStale(this.openedAtMs, this.lastSeenMs)) {
        this.emit("log", `${port.path} silent; reopening`);
        // A failed close emits no "close" event, so drop the port here or it is never retried.
        port.close((err) => {
          if (!err) return;
          this.emit("log", `close ${port.path} failed: ${err.message}`);
          this.dropPort(port);
        });
        return;
      }
      this.send({ t: "ping" });
    }, PING_INTERVAL_MS).unref();
  }

  get alive(): boolean {
    return this.port !== null && Date.now() - this.lastSeenMs < ALIVE_WINDOW_MS;
  }

  send(msg: HostMessage): boolean {
    if (!this.port?.isOpen) return false;
    this.port.write(encodeJsonFrame(msg));
    return true;
  }

  sendAudio(payload: Uint8Array): boolean {
    if (!this.port?.isOpen) return false;
    this.port.write(encodeFrame(FrameType.Audio, payload));
    return true;
  }

  close(): void {
    this.stopped = true;
    this.port?.close();
  }

  private async connectLoop(): Promise<void> {
    while (!this.stopped && !this.port) {
      const path = this.fixedPath ?? (await findDevicePath());
      if (path) {
        try {
          await this.open(path);
          return;
        } catch (err) {
          this.emit("log", `open ${path} failed: ${(err as Error).message}`);
        }
      }
      await sleep(RECONNECT_INTERVAL_MS);
    }
  }

  private open(path: string): Promise<void> {
    const port = new SerialPort({ path, baudRate: 115200, autoOpen: false });
    const read = createFrameReader(
      (msg) => {
        this.lastSeenMs = Date.now();
        this.emit("message", msg);
      },
      (line) => this.emit("log", line),
    );
    port.on("data", read);
    // Without a listener, an error from a write or an unplug would crash the bridge.
    port.on("error", (err) => this.emit("log", `${path}: ${err.message}`));
    port.on("close", () => this.dropPort(port));

    return new Promise((resolve, reject) => {
      port.open((err) => {
        if (err) return reject(err);
        this.port = port;
        this.openedAtMs = Date.now();
        this.emit("connected", path);
        // A ping, not a hello: the hub says hello once this link is picked, so one switch
        // makes one re-sync.
        this.send({ t: "ping" });
        resolve();
      });
    });
  }

  /** Forgets a closed (or unclosable) port and starts looking for the device again; runs once per port. */
  private dropPort(port: SerialPort): void {
    if (this.port !== port) return;
    this.port = null;
    this.lastSeenMs = 0;
    this.emit("disconnected");
    if (!this.stopped) void this.connectLoop();
  }
}
