/**
 * Lists serial ports and marks Espressif devices, so the ESP-VoCat USB-Serial-JTAG
 * port can be identified after flashing.
 */
import { SerialPort } from "serialport";
import { ESPRESSIF_VENDOR_ID } from "./transport/serial-transport.js";

const ports = await SerialPort.list();

if (ports.length === 0) {
  console.log("No serial ports found.");
}

for (const port of ports) {
  const isEspressif = port.vendorId?.toLowerCase() === ESPRESSIF_VENDOR_ID;
  const ids = port.vendorId ? `${port.vendorId}:${port.productId}` : "no USB id";
  console.log(`${isEspressif ? "*" : " "} ${port.path}  (${ids}) ${port.manufacturer ?? ""}`.trimEnd());
}

if (!ports.some((p) => p.vendorId?.toLowerCase() === ESPRESSIF_VENDOR_ID)) {
  console.log("\nNo Espressif device detected. Check the USB-C cable carries data.");
}
