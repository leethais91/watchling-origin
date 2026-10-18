/**
 * HTTP over a user-only unix socket. Routes:
 *   POST /permission  PermissionRequest hook input → { decision: "approve" | "deny" | null }
 *                     The response is held until the device answers; if the hook process
 *                     exits first, the request is cancelled on the device.
 *   POST /statusline  Claude Code statusline JSON → 204
 *   POST /event       AgentEventMessage without `t` → 204
 *   POST /activity    AgentActivityMessage without `t` → 204
 *   POST /shutdown    {} → 204, then the bridge exits (frees the serial port for flashing)
 */
import { chmod, mkdir, unlink } from "node:fs/promises";
import { createServer, type IncomingMessage, type Server } from "node:http";
import type { AgentActivityMessage, AgentEventMessage, Decision } from "../protocol/messages.js";
import type { PermissionInput } from "../permission/permission-summary.js";
import type { StatuslineInput } from "../usage/usage-store.js";
import { isBridgeRunning } from "./bridge-process.js";
import { WATCHLING_DIR, SOCKET_PATH } from "./paths.js";

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
  shutdown: () => void;
}

export async function startIpcServer(handlers: IpcHandlers): Promise<Server> {
  await mkdir(WATCHLING_DIR, { recursive: true, mode: 0o700 });
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
      case "/shutdown":
        res.writeHead(204).end(() => handlers.shutdown());
        return;
      default:
        res.writeHead(404).end();
    }
  });
  // Permission requests can legitimately wait minutes for a tap.
  server.requestTimeout = 0;

  // Bind before touching an existing socket file: when two sessions start a bridge at
  // once, the loser sees EADDRINUSE on a live socket instead of unlinking the winner's.
  try {
    await listen(server);
  } catch (err) {
    if ((err as NodeJS.ErrnoException).code !== "EADDRINUSE") throw err;
    if (await isBridgeRunning()) throw new Error(`another bridge is already listening on ${SOCKET_PATH}`);
    await unlink(SOCKET_PATH).catch(() => {}); // left behind by a bridge that crashed
    await listen(server).catch((retryErr: NodeJS.ErrnoException) => {
      if (retryErr.code !== "EADDRINUSE") throw retryErr;
      throw new Error(`another bridge started at the same time on ${SOCKET_PATH}`);
    });
  }
  await chmod(SOCKET_PATH, 0o600);
  return server;
}

function listen(server: Server): Promise<void> {
  return new Promise((resolve, reject) => {
    const onError = (err: Error) => reject(err);
    server.once("error", onError);
    server.listen(SOCKET_PATH, () => {
      server.off("error", onError);
      resolve();
    });
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
