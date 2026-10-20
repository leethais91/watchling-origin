/**
 * What `npm run bridge:status` reports, sent by the running bridge on POST /status.
 * Node built-ins only, like ipc-client.ts.
 */

export interface BridgeStatus {
  /** Link the bridge talks on, or null while the device is unreachable. */
  link: string | null;
  usb: boolean;
  /** Whether BLE answers; "disabled" when the bridge runs with WATCHLING_BLE=off. */
  ble: boolean | "disabled";
  /** The device's Bluetooth switch; null until firmware that reports it says hello. */
  deviceBle: boolean | null;
  /** Firmware version from the last device hello. */
  fw: string | null;
  pendingPermissions: number;
}

const upDown = (up: boolean) => (up ? "answering" : "not answering");

export function formatBridgeStatus(status: BridgeStatus): string {
  const deviceBle = status.deviceBle === null ? "unknown" : status.deviceBle ? "on" : "off";
  const rows: [string, string][] = [
    ["bridge", "running"],
    ["link", status.link ?? "none (device unreachable)"],
    ["usb", upDown(status.usb)],
    ["ble", status.ble === "disabled" ? "disabled (WATCHLING_BLE=off)" : upDown(status.ble)],
    ["device", `fw ${status.fw ?? "unknown"}, bluetooth ${deviceBle}`],
    ["waiting", `${status.pendingPermissions} permission request${status.pendingPermissions === 1 ? "" : "s"}`],
  ];
  return rows.map(([key, value]) => `${key.padEnd(8)}${value}`).join("\n");
}
