/**
 * USB-Serial-JTAG link to the device: auto-detects the Espressif port, reconnects
 * after unplug, and tracks liveness with ping/pong.
 */
import { EventEmitter } from "node:events";
import { setTimeout as sleep } from "node:timers/promises";
import { SerialPort } from "serialport";
import { FrameDecoder, FrameType, encodeFrame } from "../protocol/frame-codec.js";
import type { DeviceMessage, HostMessage } from "../protocol/messages.js";

/** USB vendor ID assigned to Espressif (ESP32-S3 USB-Serial-JTAG enumerates with it). */
export const ESPRESSIF_VENDOR_ID = "303a";

const RECONNECT_INTERVAL_MS = 2000;
const PING_INTERVAL_MS = 2000;
/** Device counts as alive if it answered within this window. */
const ALIVE_WINDOW_MS = 5000;

export async function findDevicePath(): Promise<string | null> {
  const ports = await SerialPort.list();
  const device = ports.find((p) => p.vendorId?.toLowerCase() === ESPRESSIF_VENDOR_ID);
  if (!device) return null;
  // On macOS the callout device (cu.*) opens without waiting for carrier detect.
  return process.platform === "darwin" ? device.path.replace("/dev/tty.", "/dev/cu.") : device.path;
}

interface TransportEvents {
  message: [DeviceMessage];
  log: [string];
  connected: [string];
  disconnected: [];
}

export class SerialTransport extends EventEmitter<TransportEvents> {
  private port: SerialPort | null = null;
  private lastSeenMs = 0;
  private stopped = false;

  constructor(private readonly fixedPath?: string) {
    super();
  }

  start(): void {
    void this.connectLoop();
    setInterval(() => this.send({ t: "ping" }), PING_INTERVAL_MS).unref();
  }

  get alive(): boolean {
    return this.port !== null && Date.now() - this.lastSeenMs < ALIVE_WINDOW_MS;
  }

  send(msg: HostMessage): boolean {
    if (!this.port?.isOpen) return false;
    this.port.write(encodeFrame(FrameType.Json, Buffer.from(JSON.stringify(msg), "utf8")));
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
    const decoder = new FrameDecoder();

    port.on("data", (data: Buffer) => {
      for (const chunk of decoder.push(data)) {
        if (chunk.kind === "text") {
          for (const line of chunk.text.split(/\r?\n/)) if (line.trim()) this.emit("log", line);
          continue;
        }
        if (chunk.type !== FrameType.Json) continue;
        try {
          const msg = JSON.parse(Buffer.from(chunk.payload).toString("utf8")) as DeviceMessage;
          this.lastSeenMs = Date.now();
          this.emit("message", msg);
        } catch {
          this.emit("log", "dropped malformed JSON frame");
        }
      }
    });

    port.on("close", () => {
      this.port = null;
      this.lastSeenMs = 0;
      this.emit("disconnected");
      if (!this.stopped) void this.connectLoop();
    });

    return new Promise((resolve, reject) => {
      port.open((err) => {
        if (err) return reject(err);
        this.port = port;
        this.emit("connected", path);
        this.send({ t: "hello" });
        resolve();
      });
    });
  }
}
