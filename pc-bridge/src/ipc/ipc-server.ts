/**
 * HTTP over a user-only unix socket. Routes:
 *   POST /permission  PermissionRequest hook input → { decision: "approve" | "deny" | null }
 *                     The response is held until the device answers; if the hook process
 *                     exits first, the request is cancelled on the device.
 *   POST /statusline  Claude Code statusline JSON → 204
 *   POST /event       AgentEventMessage without `t` → 204
 *   POST /activity    AgentActivityMessage without `t` → 204
 */
import { chmod, mkdir, unlink } from "node:fs/promises";
import { connect } from "node:net";
import { createServer, type IncomingMessage, type Server } from "node:http";
import type { AgentActivityMessage, AgentEventMessage, Decision } from "../protocol/messages.js";
import type { PermissionInput } from "../permission/permission-summary.js";
import type { StatuslineInput } from "../usage/usage-store.js";
import { ECHOEAR_DIR, SOCKET_PATH } from "./paths.js";

/** Write tool payloads carry whole files; allow generous bodies. */
const MAX_BODY_BYTES = 16 * 1024 * 1024;

export interface PermissionResponse {
  decision: Decision | null;
}

export interface IpcHandlers {
  permission: (input: PermissionInput, signal: AbortSignal) => Promise<Decision | null>;
  statusline: (input: StatuslineInput) => void;
  event: (event: Omit<AgentEventMessage, "t">) => void;
  activity: (activity: Omit<AgentActivityMessage, "t">) => void;
}

export async function startIpcServer(handlers: IpcHandlers): Promise<Server> {
  await mkdir(ECHOEAR_DIR, { recursive: true, mode: 0o700 });
  if (await isSocketLive()) throw new Error(`another bridge is already listening on ${SOCKET_PATH}`);
  await unlink(SOCKET_PATH).catch(() => {});

  const server = createServer(async (req, res) => {
    let body: unknown;
    try {
      body = JSON.parse(await readBody(req));
    } catch {
      res.writeHead(400).end();
      return;
    }

    switch (req.url) {
      case "/permission": {
        const abort = new AbortController();
        res.on("close", () => {
          if (!res.writableEnded) abort.abort();
        });
        const decision = await handlers.permission(body as PermissionInput, abort.signal);
        if (!abort.signal.aborted) {
          const response: PermissionResponse = { decision };
          res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify(response));
        }
        return;
      }
      case "/statusline":
        handlers.statusline(body as StatuslineInput);
        res.writeHead(204).end();
        return;
      case "/event":
        handlers.event(body as Omit<AgentEventMessage, "t">);
        res.writeHead(204).end();
        return;
      case "/activity":
        handlers.activity(body as Omit<AgentActivityMessage, "t">);
        res.writeHead(204).end();
        return;
      default:
        res.writeHead(404).end();
    }
  });
  // Permission requests can legitimately wait minutes for a tap.
  server.requestTimeout = 0;

  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(SOCKET_PATH, resolve);
  });
  await chmod(SOCKET_PATH, 0o600);
  return server;
}

function isSocketLive(): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = connect(SOCKET_PATH);
    socket.once("connect", () => {
      socket.destroy();
      resolve(true);
    });
    socket.once("error", () => resolve(false));
  });
}

async function readBody(req: IncomingMessage): Promise<string> {
  let size = 0;
  const chunks: Buffer[] = [];
  for await (const chunk of req as AsyncIterable<Buffer>) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) throw new Error("body too large");
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString("utf8");
}
