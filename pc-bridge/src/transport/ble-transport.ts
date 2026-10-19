/**
 * BLE link to the device: Nordic UART Service carrying the same framed stream as USB.
 * Scans by service UUID, reconnects after drops, and tracks liveness with ping/pong.
 *
 * Pairing is driven by the device: the first write needs an authenticated link, so macOS
 * shows its passkey prompt and the device displays the code. Until the device answers,
 * the ping is a `hello`, so a pairing that eats the first messages still syncs.
 *
 * Speech clips are not sent over BLE (too slow for raw PCM); chimes still play.
 */
import { EventEmitter } from "node:events";
import { setTimeout as sleep } from "node:timers/promises";
import type { Characteristic, Peripheral } from "@stoprocent/noble";
import type { HostMessage } from "../protocol/messages.js";
import {
  ALIVE_WINDOW_MS,
  PING_INTERVAL_MS,
  createFrameReader,
  encodeJsonFrame,
  type DeviceLink,
  type LinkEvents,
} from "./device-link.js";

const NUS_SERVICE = "6e400001b5a3f393e0a9e50e24dcca9e";
const NUS_RX = "6e400002b5a3f393e0a9e50e24dcca9e"; // we write
const NUS_TX = "6e400003b5a3f393e0a9e50e24dcca9e"; // device notifies
/** Fits one ATT write at the default macOS MTU (185) without a long write. */
const WRITE_CHUNK_BYTES = 180;
const RECONNECT_INTERVAL_MS = 3000;
/** How long to wait for the adapter before logging its state and trying again. */
const POWER_ON_TIMEOUT_MS = 15000;

export class BleTransport extends EventEmitter<LinkEvents> implements DeviceLink {
  private peripheral: Peripheral | null = null;
  private rx: Characteristic | null = null;
  private writes: Promise<void> = Promise.resolve();
  private pendingWrites = 0;
  private lastSeenMs = 0;
  private stopped = false;

  start(): void {
    this.runConnectLoop();
    setInterval(() => this.send(this.lastSeenMs === 0 ? { t: "hello" } : { t: "ping" }), PING_INTERVAL_MS).unref();
  }

  get alive(): boolean {
    return this.rx !== null && Date.now() - this.lastSeenMs < ALIVE_WINDOW_MS;
  }

  /**
   * Queues the frame as write-with-response chunks; order is kept across messages. Pings
   * are skipped while writes are pending: during pairing macOS holds the first write until
   * the passkey is entered, and a backlog of hellos would make the bridge re-sync each time.
   */
  send(msg: HostMessage): boolean {
    const rx = this.rx;
    if (!rx) return false;
    if ((msg.t === "ping" || msg.t === "hello") && this.pendingWrites > 0) return true;
    const frame = encodeJsonFrame(msg);
    this.pendingWrites++;
    this.writes = this.writes.then(async () => {
      try {
        for (let off = 0; off < frame.length; off += WRITE_CHUNK_BYTES) {
          await rx.writeAsync(Buffer.from(frame.subarray(off, off + WRITE_CHUNK_BYTES)), false);
        }
      } catch (err) {
        // Typically "insufficient authentication" while pairing; the next ping retries.
        this.emit("log", `BLE write failed: ${(err as Error).message}`);
      } finally {
        this.pendingWrites--;
      }
    });
    return true;
  }

  sendAudio(): boolean {
    return false;
  }

  close(): void {
    this.stopped = true;
    void this.peripheral?.disconnectAsync().catch(() => {});
  }

  /** BLE problems must never take the bridge (and its USB link) down with them. */
  private runConnectLoop(): void {
    this.connectLoop().catch((err: Error) => {
      this.emit("log", `BLE stopped: ${err.message}`);
    });
  }

  private async connectLoop(): Promise<void> {
    // Loaded lazily: importing noble starts CoreBluetooth (and the macOS permission prompt).
    const noble = (await import("@stoprocent/noble")).default;
    while (!this.stopped) {
      try {
        await noble.waitForPoweredOnAsync(POWER_ON_TIMEOUT_MS);
        break;
      } catch {
        // "unauthorized": allow Bluetooth for this terminal app in System Settings > Privacy & Security.
        this.emit("log", `Bluetooth not ready (state: ${noble.state}); retrying`);
      }
    }
    while (!this.stopped && !this.peripheral) {
      let found: Peripheral | undefined;
      try {
        await noble.startScanningAsync([NUS_SERVICE], false);
        for await (const peripheral of noble.discoverAsync()) {
          found = peripheral;
          break;
        }
        await noble.stopScanningAsync();
        if (found) await this.open(found);
      } catch (err) {
        // noble can reject with a bare string or code instead of an Error.
        this.emit("log", `BLE connect failed: ${err instanceof Error ? err.message : String(err)}`);
        await found?.disconnectAsync().catch(() => {});
        this.peripheral = null;
        await sleep(RECONNECT_INTERVAL_MS);
      }
    }
  }

  private async open(peripheral: Peripheral): Promise<void> {
    this.peripheral = peripheral;
    await peripheral.connectAsync();
    const { characteristics } = await peripheral.discoverSomeServicesAndCharacteristicsAsync(
      [NUS_SERVICE],
      [NUS_RX, NUS_TX],
    );
    const rx = characteristics.find((c) => c.uuid === NUS_RX);
    const tx = characteristics.find((c) => c.uuid === NUS_TX);
    if (!rx || !tx) throw new Error("device has no UART characteristics");

    const read = createFrameReader(
      (msg) => {
        this.lastSeenMs = Date.now();
        this.emit("message", msg);
      },
      (line) => this.emit("log", line),
    );
    tx.on("data", read);
    await tx.subscribeAsync();

    peripheral.once("disconnect", () => {
      this.peripheral = null;
      this.rx = null;
      this.lastSeenMs = 0;
      this.emit("disconnected");
      if (!this.stopped) void sleep(RECONNECT_INTERVAL_MS).then(() => this.runConnectLoop());
    });
    this.rx = rx;
    this.emit("connected", `BLE ${peripheral.advertisement.localName ?? peripheral.id}`);
    this.send({ t: "hello" });
  }
}
