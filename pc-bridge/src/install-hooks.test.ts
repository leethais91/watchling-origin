import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdtemp, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { promisify } from "node:util";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const run = promisify(execFile);
const script = fileURLToPath(new URL("./install-hooks.ts", import.meta.url));
const macOnly = { skip: process.platform !== "darwin" };
async function fixture() {
  const home = await mkdtemp(join(tmpdir(), "watchling-installer-"));
  const bin = join(home, "bin");
  await mkdir(bin);
  // All launchctl calls are intercepted, with no access to real user configuration.
  await writeFile(join(bin, "launchctl"), '#!/bin/sh\nprintf "%s\\n" "$*" >> "$HOME/launchctl.log"\n', { mode: 0o755 });
  return {
    home, claude: join(home, ".claude/settings.json"), codex: join(home, ".codex/hooks.json"),
    agent: join(home, "Library/LaunchAgents/com.watchling.bridge.plist"),
    cleanup: () => rm(home, { recursive: true, force: true }),
    async write(path: string, data: unknown) {
      await mkdir(dirname(path), { recursive: true });
      await writeFile(path, JSON.stringify(data));
    },
    invoke(action: "install" | "uninstall", codex = false) {
      return run(process.execPath, ["--import", "tsx", script, action, ...(codex ? ["--codex"] : [])], {
        env: { ...process.env, HOME: home, PATH: `${bin}:${process.env.PATH}` },
      });
    },
  };
}
for (const codexFirst of [false, true]) {
  test(`shared service survives first uninstall (${codexFirst ? "Codex" : "Claude"}) and is removed after last`, macOnly, async () => {
    const f = await fixture();
    try {
      const unrelated = { type: "command", command: "echo unrelated" };
      await f.write(f.claude, { hooks: { Stop: [{ hooks: [unrelated] }] }, statusLine: { type: "command", command: "echo status", padding: 2 }, custom: true });
      await f.invoke("install");
      await f.invoke("install", true);
      await f.invoke("uninstall", codexFirst);
      assert.match(await readFile(f.agent, "utf8"), /com.watchling.bridge/);
      await f.invoke("uninstall", !codexFirst);
      await assert.rejects(readFile(f.agent), { code: "ENOENT" });
      const restored = JSON.parse(await readFile(f.claude, "utf8"));
      assert.deepEqual(restored.hooks, { Stop: [{ hooks: [unrelated] }] });
      assert.deepEqual(restored.statusLine, { type: "command", command: "echo status", padding: 2 });
      assert.equal(restored.custom, true);
      assert.ok((await readdir(dirname(f.claude))).some((name) => name.includes("watchling-backup")));
    } finally { await f.cleanup(); }
  });
}
test("uninstall tolerates both missing configurations without creating them", macOnly, async () => {
  const f = await fixture();
  try {
    await f.invoke("uninstall");
    await f.invoke("uninstall", true);
    await assert.rejects(readFile(f.claude), { code: "ENOENT" });
    await assert.rejects(readFile(f.codex), { code: "ENOENT" });
  } finally { await f.cleanup(); }
});
test("last integration is removed when the other configuration is absent", macOnly, async () => {
  const f = await fixture();
  try {
    await f.invoke("install", true);
    await f.invoke("uninstall", true);
    await assert.rejects(readFile(f.agent), { code: "ENOENT" });
  } finally { await f.cleanup(); }
});
for (const malformed of ["{broken", '{"hooks":{"SessionStart":{}}}']) {
  test(`unknown other integration state preserves service (${malformed})`, macOnly, async () => {
    const f = await fixture();
    try {
      await f.invoke("install");
      await mkdir(dirname(f.codex));
      await writeFile(f.codex, malformed);
      const result = await f.invoke("uninstall");
      assert.match(result.stderr, /keeping the shared bridge/);
      assert.match(await readFile(f.agent, "utf8"), /com.watchling.bridge/);
      assert.equal(await readFile(f.codex, "utf8"), malformed);
    } finally { await f.cleanup(); }
  });
}

test("unrelated scripts in another pc-bridge/dist tree survive install and uninstall", macOnly, async () => {
  const f = await fixture();
  try {
    const hooks = { SessionStart: [{ hooks: [{ type: "command", command: 'node "/another product/pc-bridge/dist/hook.js" session-start' }] }] };
    const statusLine = { type: "command", command: 'node "/another product/pc-bridge/dist/statusline.js"' };
    await f.write(f.claude, { hooks, statusLine });
    await f.invoke("install");
    await f.invoke("install");
    const installed = JSON.parse(await readFile(f.claude, "utf8"));
    assert.equal(installed.hooks.SessionStart.length, 2, "unrelated hook and one Watchling hook");
    await f.invoke("uninstall");
    const restored = JSON.parse(await readFile(f.claude, "utf8"));
    assert.deepEqual(restored.hooks, hooks);
    assert.deepEqual(restored.statusLine, statusLine);
  } finally { await f.cleanup(); }
});

test("paths extending this checkout's script names do not establish ownership", macOnly, async () => {
  const f = await fixture();
  try {
    const hooks = { Stop: [{ hooks: [{ type: "command", command: `node "${join(dirname(script), "hook.js.other")}" stop` }] }] };
    const statusLine = { type: "command", command: `node "${join(dirname(script), "statusline.js.other")}"` };
    await f.write(f.claude, { hooks, statusLine });
    await f.invoke("install");
    await f.invoke("uninstall");
    const restored = JSON.parse(await readFile(f.claude, "utf8"));
    assert.deepEqual(restored.hooks, hooks);
    assert.deepEqual(restored.statusLine, statusLine);
  } finally { await f.cleanup(); }
});

test("unsupported platforms fail before creating configuration or invoking launchctl", async () => {
  const f = await fixture();
  try {
    const code = `Object.defineProperty(process, "platform", { value: "linux" }); process.argv = [process.execPath, ${JSON.stringify(script)}, "install"]; await import(${JSON.stringify(script)});`;
    await assert.rejects(
      run(process.execPath, ["--import", "tsx", "--input-type=module", "-e", code], { env: { ...process.env, HOME: f.home } }),
      (error: unknown) => {
        assert.match((error as { stderr: string }).stderr, /requires macOS/);
        return true;
      },
    );
    assert.deepEqual(await readdir(f.home), ["bin"]);
  } finally { await f.cleanup(); }
});

test("unrelated prompt and agent hooks survive install and uninstall", macOnly, async () => {
  const f = await fixture();
  try {
    const hooks = { Stop: [{ matcher: "*", hooks: [
      { type: "prompt", prompt: "Check the result.", timeout: 20 },
      { type: "agent", prompt: "Verify all requirements.", model: "example" },
    ] }] };
    await f.write(f.claude, { hooks });
    await f.invoke("install");
    await f.invoke("uninstall");
    assert.deepEqual(JSON.parse(await readFile(f.claude, "utf8")).hooks, hooks);
  } finally { await f.cleanup(); }
});

for (const statusLine of [null, "invalid", [], { type: "command", command: 42 }]) {
  test(`malformed statusLine is rejected before any mutation (${JSON.stringify(statusLine)})`, macOnly, async () => {
    const f = await fixture();
    try {
      await f.write(f.claude, { statusLine });
      const before = await readFile(f.claude, "utf8");
      await assert.rejects(f.invoke("install"), (error: unknown) => {
        assert.match((error as { stderr: string }).stderr, /Invalid statusLine/);
        return true;
      });
      assert.equal(await readFile(f.claude, "utf8"), before);
      assert.deepEqual(await readdir(dirname(f.claude)), ["settings.json"]);
      await assert.rejects(readFile(f.agent), { code: "ENOENT" });
    } finally { await f.cleanup(); }
  });
}
