import assert from "node:assert/strict";
import { test } from "node:test";
import { doneSpeech, permissionSpeech } from "./speech-text.js";

test("permission phrases name the action, not the raw payload", () => {
  assert.equal(permissionSpeech({ tool: "Bash", summary: "npm test" }), "Claude wants to run npm test");
  assert.equal(
    permissionSpeech({ tool: "Bash", summary: "git push origin main && rm -rf dist" }),
    "Claude wants to run git push origin main",
  );
  assert.equal(
    permissionSpeech({ tool: "Bash", summary: "docker run --rm -it -v /a:/b image sh -c ls" }),
    "Claude wants to run docker run --rm -it -v /a:/b and more",
  );
  assert.equal(permissionSpeech({ tool: "Edit", summary: "src/bridge.ts" }), "Claude wants to edit bridge.ts");
  assert.equal(permissionSpeech({ tool: "WebFetch", summary: "https://docs.lvgl.io/9.6/x" }), "Claude wants to fetch docs.lvgl.io");
  assert.equal(
    permissionSpeech({ tool: "mcp__github__create_issue", summary: "{}" }),
    "Claude wants to use create issue from github",
  );
});

test("done phrases keep the first plain sentence", () => {
  assert.equal(
    doneSpeech("claude", "**Fixed** the `broker` timeout. Also updated tests."),
    "Claude finished. Fixed the broker timeout.",
  );
  assert.equal(doneSpeech("codex", ""), "Codex finished.");
  const long = doneSpeech("claude", "word ".repeat(60));
  assert.ok(long.length <= "Claude finished. ".length + 140 && !long.endsWith(" "));
});
