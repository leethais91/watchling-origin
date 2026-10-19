const status = document.querySelector("#status");
try {
  const getJSON = async (url) => {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`Cannot read ${url}`);
    return response.json();
  };
  const [manifest, release] = await Promise.all([getJSON("manifest.json"), getJSON("release.json")]);
  if (manifest.version !== release.version) throw new Error("Release versions do not match");
  document.querySelector("#version").textContent = release.version;
  for (const part of manifest.builds[0].parts) {
    const response = await fetch(part.path);
    if (!response.ok) throw new Error(`Missing firmware: ${part.path}`);
    const digest = await crypto.subtle.digest("SHA-256", await response.arrayBuffer());
    const hex = Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
    if (hex !== release.sha256[part.path]) throw new Error(`Checksum failed: ${part.path}`);
  }
  await import("./assets/installer.js");
  await customElements.whenDefined("esp-web-install-button");
  document.querySelector("#installer").hidden = false;
  status.textContent = "Firmware verified. Ready to connect.";
} catch (error) {
  status.classList.add("error");
  status.textContent = `${error.message}. Extract a fresh release ZIP and run start.command.`;
}
