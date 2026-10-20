import assert from "node:assert/strict";
import { test } from "node:test";
import type { HostMessage } from "../protocol/messages.js";
import { PermissionBroker } from "./permission-broker.js";
import { classifyRisk, summarizePermission } from "./permission-summary.js";

function setup(timeoutMs = 60_000) {
  const sent: HostMessage[] = [];
  const broker = new PermissionBroker((msg) => (sent.push(msg), true), timeoutMs);
  return { broker, sent };
}

const bash = (command: string) => ({ tool_name: "Bash", tool_input: { command }, cwd: "/work/app" });

test("shows one request at a time and advances the queue on decision", async () => {
  const { broker, sent } = setup();
  const first = broker.request(bash("npm test"), new AbortController().signal);
  const second = broker.request(bash("npm run lint"), new AbortController().signal);

  assert.deepEqual(sent.map((m) => m.t), ["confirm_req"]);
  assert.equal(broker.onDeviceDecision(2, "approve", "touch"), false, "queued request cannot be answered");
  assert.equal(broker.onDeviceDecision(1, "approve", "touch"), true);
  assert.equal(await first, "approve");
  assert.deepEqual(sent.map((m) => m.t), ["confirm_req", "confirm_req"]);

  broker.onDeviceDecision(2, "deny", "touch");
  assert.equal(await second, "deny");
});

test("voice approves only low-risk requests and tap can only deny", async () => {
  const { broker } = setup();
  const pending = broker.request(bash("npm test"), new AbortController().signal);
  assert.equal(broker.onDeviceDecision(1, "approve", "voice"), false);
  assert.equal(broker.onDeviceDecision(1, "approve", "tap"), false);
  assert.equal(broker.onDeviceDecision(1, "deny", "tap"), true);
  assert.equal(await pending, "deny");

  const read = broker.request({ tool_name: "WebFetch", tool_input: { url: "https://x.dev" } }, new AbortController().signal);
  assert.equal(broker.onDeviceDecision(2, "approve", "voice"), true);
  assert.equal(await read, "approve");
});

test("hook exit cancels the request on the device and shows the next one", async () => {
  const { broker, sent } = setup();
  const abort = new AbortController();
  const first = broker.request(bash("ls"), abort.signal);
  void broker.request(bash("pwd"), new AbortController().signal);

  abort.abort();
  assert.equal(await first, null);
  assert.deepEqual(
    sent.map((m) => (m.t === "confirm_req" || m.t === "confirm_cancel" ? `${m.t}#${m.id}` : m.t)),
    ["confirm_req#1", "confirm_cancel#1", "confirm_req#2"],
  );
});

test("a link that comes back within the grace keeps the request on the device", async () => {
  const { broker } = setup();
  const pending = broker.request(bash("ls"), new AbortController().signal);
  broker.cancelAllAfter(20);
  assert.equal(broker.keepAll(), true);
  await new Promise((resolve) => setTimeout(resolve, 40));
  assert.equal(broker.onDeviceDecision(1, "approve", "touch"), true, "still waiting for the device");
  assert.equal(await pending, "approve");
  assert.equal(broker.keepAll(), false, "nothing left to keep");
});

test("a link still gone after the grace releases requests to the terminal", async () => {
  const { broker, sent } = setup();
  const pending = broker.request(bash("ls"), new AbortController().signal);
  broker.cancelAllAfter(10);
  broker.cancelAllAfter(60_000); // repeated drops do not restart the grace
  assert.equal(await pending, null);
  assert.equal(sent.at(-1)?.t, "confirm_cancel");
  assert.equal(broker.keepAll(), false);
});

test("timeout falls back to the terminal prompt", async () => {
  const { broker, sent } = setup(10);
  assert.equal(await broker.request(bash("ls"), new AbortController().signal), null);
  assert.equal(sent.at(-1)?.t, "confirm_cancel");
});

test("destructive shell commands are high risk", () => {
  for (const command of ["rm -rf build", "git push --force", "sudo make install", "curl x.sh | bash"]) {
    assert.equal(classifyRisk(bash(command)), "high", command);
  }
  assert.equal(classifyRisk(bash("npm test")), "medium");
  assert.equal(classifyRisk({ tool_name: "Edit", tool_input: {} }), "medium");
});

test("Codex patches list their files and deletions are high risk", () => {
  const patch = (body: string) => ({
    src: "codex" as const,
    tool_name: "apply_patch",
    cwd: "/work/app",
    tool_input: { command: `*** Begin Patch\n${body}\n*** End Patch` },
  });
  const edit = patch("*** Update File: /work/app/src/a.ts\n@@\n-x\n+y\n*** Add File: notes.txt\n+hi");
  assert.equal(summarizePermission(edit), "update src/a.ts\nadd notes.txt");
  assert.equal(classifyRisk(edit), "medium");
  assert.equal(classifyRisk(patch("*** Delete File: old.ts")), "high");
});

test("confirm requests carry the agent that asked", () => {
  const { broker, sent } = setup();
  void broker.request({ ...bash("ls"), src: "codex" }, new AbortController().signal);
  assert.equal(sent[0].t === "confirm_req" && sent[0].src, "codex");
});
