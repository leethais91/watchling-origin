import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { isBridgeRunning, launchAgentPlist } from "./bridge-process.js";

const dir = await mkdtemp(join(tmpdir(), "watchling-bridge-"));
after(() => rm(dir, { recursive: true, force: true }));

test("reports no bridge when nothing listens on the socket", async () => {
  assert.equal(await isBridgeRunning(join(dir, "missing.sock")), false);
});

test("reports a bridge while a server listens on the socket", async () => {
  const socketPath = join(dir, "live.sock");
  const server = createServer();
  await new Promise<void>((resolve) => server.listen(socketPath, resolve));
  try {
    assert.equal(await isBridgeRunning(socketPath), true);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
  assert.equal(await isBridgeRunning(socketPath), false, "closed server no longer counts");
});

test("the launch agent runs one Node script on demand only", () => {
  const plist = launchAgentPlist({
    nodePath: "/opt/node 22/bin/node",
    script: "/repo/pc-bridge/out/bridge.js",
    cwd: "/repo/pc-bridge",
    logPath: "/Users/me/.watchling/bridge.log",
  });
  assert.match(plist, /<key>Label<\/key><string>com\.watchling\.bridge<\/string>/);
  assert.ok(
    plist.includes("<array><string>/opt/node 22/bin/node</string><string>/repo/pc-bridge/out/bridge.js</string></array>"),
  );
  assert.ok(plist.includes("<key>RunAtLoad</key><false/>"), "not started at login");
  assert.ok(plist.includes("<key>KeepAlive</key><false/>"), "stays stopped after bridge:stop");
  assert.equal(plist.match(/bridge\.log/g)?.length, 2, "stdout and stderr both logged");
});

test("the launch agent escapes XML in paths", () => {
  const plist = launchAgentPlist({ nodePath: "/a&b/<node>", script: "/s", cwd: "/c", logPath: "/l" });
  assert.ok(plist.includes("<string>/a&amp;b/&lt;node&gt;</string>"));
});
