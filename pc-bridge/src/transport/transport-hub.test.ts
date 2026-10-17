import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { test } from "node:test";
import type { DeviceMessage, HostMessage } from "../protocol/messages.js";
import type { DeviceLink, LinkEvents } from "./device-link.js";
import { TransportHub } from "./transport-hub.js";

class FakeLink extends EventEmitter<LinkEvents> implements DeviceLink {
  alive = false;
  sent: HostMessage[] = [];
  start(): void {}
  close(): void {}
  send(msg: HostMessage): boolean {
    this.sent.push(msg);
    return true;
  }
  sendAudio(): boolean {
    return true;
  }
  receive(msg: DeviceMessage): void {
    this.alive = true;
    this.emit("message", msg);
  }
}

function setup() {
  const usb = new FakeLink();
  const ble = new FakeLink();
  const hub = new TransportHub([
    { name: "USB", link: usb },
    { name: "BLE", link: ble },
  ]);
  const received: DeviceMessage[] = [];
  const events: string[] = [];
  hub.on("message", (msg) => received.push(msg));
  hub.on("connected", (name) => events.push(`connected ${name}`));
  hub.on("disconnected", () => events.push("disconnected"));
  return { usb, ble, hub, received, events };
}

test("prefers USB and drops the standby link's duplicates", () => {
  const { usb, ble, hub, received, events } = setup();
  ble.receive({ t: "pong" });
  usb.receive({ t: "pong" });
  assert.equal(hub.active, usb);
  assert.deepEqual(events, ["connected BLE", "connected USB"]);
  assert.deepEqual(usb.sent, [{ t: "hello" }], "a switch says hello so the bridge re-syncs");

  ble.receive({ t: "confirm_res", id: 1, action: "approve", src: "touch" });
  usb.receive({ t: "confirm_res", id: 1, action: "approve", src: "touch" });
  assert.deepEqual(
    received.map((m) => m.t),
    ["pong", "pong", "confirm_res"],
    "first BLE pong while BLE was the only link, then USB only",
  );

  hub.send({ t: "ping" });
  assert.deepEqual(usb.sent.at(-1), { t: "ping" });
  assert.equal(ble.sent.length, 1, "only the hello from when BLE was active");
});

test("falls back to BLE when USB goes quiet and reports no link at the end", () => {
  const { usb, ble, hub, events } = setup();
  usb.receive({ t: "pong" });
  ble.receive({ t: "pong" });
  usb.alive = false;
  hub.evaluate();
  assert.equal(hub.active, ble);
  ble.alive = false;
  usb.emit("disconnected");
  assert.equal(hub.active, null);
  assert.equal(hub.alive, false);
  assert.deepEqual(events, ["connected USB", "connected BLE", "disconnected"]);
  assert.equal(hub.send({ t: "ping" }), false);
});
