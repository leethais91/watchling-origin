import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdir, mkdtemp, rm, symlink, writeFile } from "node:fs/promises";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createInstallerServer, verifyBundle } from "../serve-installer.mjs";


async function fixture(t) {
  const parent = await mkdtemp(path.join(os.tmpdir(), "watchling-installer-"));
  const root = path.join(parent, "bundle");
  await mkdir(root);
  await writeFile(path.join(root, "index.html"), "<!doctype html><title>Watchling</title>");
  await writeFile(path.join(root, "app.js"), "export {};\n");
  t.after(() => rm(parent, { recursive: true, force: true }));
  return { parent, root };
}

async function startServer(t, root) {
  const server = createInstallerServer(root);
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  t.after(() => new Promise((resolve) => server.close(resolve)));
  return { server, port: server.address().port };
}

function request(port, pathname = "/", options = {}) {
  return new Promise((resolve, reject) => {
    const clientRequest = http.request({
      hostname: "127.0.0.1",
      port,
      path: pathname,
      method: options.method ?? "GET",
      headers: { Host: options.host ?? `127.0.0.1:${port}` },
    }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => resolve({
        status: response.statusCode,
        headers: response.headers,
        body: Buffer.concat(chunks).toString("utf8"),
      }));
    });
    clientRequest.on("error", reject);
    clientRequest.end();
  });
}

test("verifyBundle detects checksum tampering", async (t) => {
  const { root } = await fixture(t);
  const target = path.join(root, "app.js");
  const digest = createHash("sha256").update("export {};\n").digest("hex");
  await writeFile(path.join(root, "SHA256SUMS"), `${digest}  app.js\n`);
  await verifyBundle(root);

  await writeFile(target, "export const tampered = true;\n");
  await assert.rejects(verifyBundle(root), /Checksum failed: app\.js/);
});

test("verifyBundle rejects checksum paths outside the bundle", async (t) => {
  const { parent, root } = await fixture(t);
  const outside = path.join(parent, "outside.txt");
  await writeFile(outside, "outside");
  const digest = createHash("sha256").update("outside").digest("hex");
  await writeFile(path.join(root, "SHA256SUMS"), `${digest}  ../outside.txt\n`);
  await assert.rejects(verifyBundle(root), /Unsafe checksum path/);
});

test("server accepts only a numeric loopback Host header", async (t) => {
  const { root } = await fixture(t);
  const { port } = await startServer(t, root);
  assert.equal((await request(port)).status, 200);
  assert.equal((await request(port, "/", { host: `localhost:${port}` })).status, 403);
  assert.equal((await request(port, "/", { host: `127.0.0.1:${port}.example.com` })).status, 403);
});

test("server rejects unsupported methods and advertises allowed methods", async (t) => {
  const { root } = await fixture(t);
  const { port } = await startServer(t, root);
  const response = await request(port, "/", { method: "POST" });
  assert.equal(response.status, 405);
  assert.equal(response.headers.allow, "GET, HEAD");
});

test("server rejects traversal and symlinks that escape the bundle", async (t) => {
  const { parent, root } = await fixture(t);
  const outside = path.join(parent, "outside.txt");
  await writeFile(outside, "outside");
  await symlink(outside, path.join(root, "linked.txt"));
  const { port } = await startServer(t, root);

  assert.equal((await request(port, "/%2e%2e%2foutside.txt")).status, 404);
  assert.equal((await request(port, "/linked.txt")).status, 404);
});

test("server serves JavaScript with a nosniff-compatible MIME type", async (t) => {
  const { root } = await fixture(t);
  const { port } = await startServer(t, root);
  const response = await request(port, "/app.js");
  assert.equal(response.status, 200);
  assert.equal(response.headers["content-type"], "text/javascript; charset=utf-8");
  assert.equal(response.headers["x-content-type-options"], "nosniff");
  assert.equal(response.body, "export {};\n");

  const head = await request(port, "/app.js", { method: "HEAD" });
  assert.equal(head.status, 200);
  assert.equal(head.body, "");
  assert.equal(head.headers["content-length"], String(Buffer.byteLength("export {};\n")));
});
