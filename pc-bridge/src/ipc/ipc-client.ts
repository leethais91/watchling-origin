/**
 * Client side of the bridge IPC, used by short-lived hook processes. Uses only
 * Node built-ins so hooks start fast under whatever Node Claude Code finds.
 */
import { request } from "node:http";
import { SOCKET_PATH } from "./paths.js";

/**
 * POSTs JSON to the bridge. Resolves null when the bridge is not running, times
 * out, or answers with no body, so callers can fall through silently.
 */
export function postToBridge<T>(path: string, body: unknown, timeoutMs?: number): Promise<T | null> {
  return new Promise((resolve) => {
    const req = request(
      { socketPath: SOCKET_PATH, path, method: "POST", headers: { "content-type": "application/json" } },
      (res) => {
        let data = "";
        res.setEncoding("utf8");
        res.on("data", (chunk: string) => (data += chunk));
        res.on("end", () => {
          try {
            resolve(data ? (JSON.parse(data) as T) : null);
          } catch {
            resolve(null);
          }
        });
      },
    );
    req.on("error", () => resolve(null));
    if (timeoutMs !== undefined) {
      req.setTimeout(timeoutMs, () => {
        req.destroy();
        resolve(null);
      });
    }
    req.end(JSON.stringify(body));
  });
}

export async function readStdin(): Promise<string> {
  let data = "";
  process.stdin.setEncoding("utf8");
  for await (const chunk of process.stdin) data += chunk;
  return data;
}
