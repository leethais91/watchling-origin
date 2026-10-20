import assert from "node:assert/strict";
import { test } from "node:test";
import { formatBridgeStatus } from "./bridge-status.js";

test("status shows the active link, both links and the device's Bluetooth switch", () => {
  const text = formatBridgeStatus({
    link: "USB",
    usb: true,
    ble: false,
    deviceBle: true,
    fw: "v0.1.0",
    pendingPermissions: 1,
  });
  assert.equal(
    text,
    [
      "bridge  running",
      "link    USB",
      "usb     answering",
      "ble     not answering",
      "device  fw v0.1.0, bluetooth on",
      "waiting 1 permission request",
    ].join("\n"),
  );
});

test("status explains an unreachable device, disabled BLE and older firmware", () => {
  const text = formatBridgeStatus({
    link: null,
    usb: false,
    ble: "disabled",
    deviceBle: null,
    fw: null,
    pendingPermissions: 0,
  });
  assert.match(text, /^link {4}none \(device unreachable\)$/m);
  assert.match(text, /^ble {5}disabled \(WATCHLING_BLE=off\)$/m);
  assert.match(text, /^device {2}fw unknown, bluetooth unknown$/m);
  assert.match(text, /^waiting 0 permission requests$/m);
});
