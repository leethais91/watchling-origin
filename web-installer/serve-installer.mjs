import http from "node:http";
import { createHash } from "node:crypto";
import { readFile, realpath, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execFile } from "node:child_process";

export async function verifyBundle(root) {
  root = await realpath(root);
  const inventory = await readFile(path.join(root, "SHA256SUMS"), "utf8");
  const lines = inventory.trim().split("\n");
  if (!lines.length || !inventory.trim()) throw new Error("Empty checksum inventory");
  for (const line of lines) {
    const match = line.match(/^([a-f0-9]{64})  ([A-Za-z0-9_./-]+)$/);
    if (!match) throw new Error("Invalid checksum inventory");
    const filename = path.resolve(root, match[2]);
    if (!filename.startsWith(`${path.resolve(root)}${path.sep}`)) throw new Error("Unsafe checksum path");
    const resolved = await realpath(filename);
    if (!resolved.startsWith(`${root}${path.sep}`)) throw new Error("Unsafe checksum path");
    const actual = createHash("sha256").update(await readFile(resolved)).digest("hex");
    if (actual !== match[1]) throw new Error(`Checksum failed: ${match[2]}`);
  }
}

const mime = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8", ".json": "application/json",
  ".bin": "application/octet-stream", ".txt": "text/plain; charset=utf-8",
};

export function createInstallerServer(root) {
  return http.createServer(async (request, response) => {
    const host = request.headers.host;
    if (!/^127\.0\.0\.1:\d+$/.test(host ?? "")) {
      response.writeHead(403).end();
      return;
    }
    if (!["GET", "HEAD"].includes(request.method)) {
      response.writeHead(405, { Allow: "GET, HEAD" }).end();
      return;
    }
    try {
      const url = new URL(request.url, `http://${host}`);
      const relative = decodeURIComponent(url.pathname).replace(/^\/+/, "") || "index.html";
      const base = await realpath(root);
      const filename = await realpath(path.resolve(base, relative));
      if (!filename.startsWith(`${base}${path.sep}`) || !(await stat(filename)).isFile()) {
        response.writeHead(404).end();
        return;
      }
      const body = await readFile(filename);
      response.writeHead(200, {
        "Content-Type": mime[path.extname(filename)] ?? "text/plain; charset=utf-8",
        "Content-Length": body.length, "Cache-Control": "no-store",
        "X-Content-Type-Options": "nosniff", "Referrer-Policy": "no-referrer",
        "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; worker-src 'self' blob:; object-src 'none'; frame-ancestors 'none'",
      });
      response.end(request.method === "HEAD" ? undefined : body);
    } catch {
      response.writeHead(404).end();
    }
  });
}

async function main() {
  if (Number(process.versions.node.split(".")[0]) < 22) throw new Error("Node.js 22 or newer is required");
  const root = await realpath(path.dirname(fileURLToPath(import.meta.url)));
  await verifyBundle(root);
  const server = createInstallerServer(root);
  server.on("error", (error) => {
    console.error(error.code === "EADDRINUSE"
      ? "Port 8765 is in use. Close the previous installer before starting another."
      : error.message);
    process.exitCode = 1;
  });
  server.listen(8765, "127.0.0.1", () => {
    const url = "http://127.0.0.1:8765";
    console.log(`Verified release. Open ${url} in Chrome or Edge. Press Ctrl-C when finished.`);
    if (process.platform === "darwin" && !process.env.WATCHLING_NO_OPEN) {
      execFile("open", ["-a", "Google Chrome", url], (error) => {
        if (error) console.log("Open the address manually in Chrome or Edge.");
      });
    }
  });
  for (const signal of ["SIGINT", "SIGTERM"]) process.on(signal, () => server.close());
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => { console.error(error.message); process.exitCode = 1; });
}
