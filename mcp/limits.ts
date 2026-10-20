// How many `Orion --agent` processes may be alive at once, how long one may
// run, and making sure none outlives this server (#311).
//
// Every tool call is a separate process that decodes a 42 MP RAW and renders
// it on the GPU, peaking at 4 to 14 GB. About ten agents driving one server,
// one to three calls each in flight, took a 36 GB Mac to about 70 GB and froze
// it (feedback/2026-10-19-mcp-batch-edit-findings.md, A1). Nothing here makes a
// call cheaper; it only stops N of them from running at once.
import { execFile, type ChildProcess } from "node:child_process";
import os from "node:os";

const GiB = 1024 ** 3;

// One process per 16 GiB of physical memory, between 1 and 4: a 16 GB Mac runs
// one, a 36 GB Mac two, and nothing runs more than four, because the GPU is one
// device and a fifth render only queues there instead of here.
export function defaultConcurrency(totalBytes: number): number {
  return Math.min(4, Math.max(1, Math.floor(totalBytes / (16 * GiB))));
}

export const DEFAULT_TIMEOUT_MS = 300_000;

// Both read once, when createServer() builds its runner, so a test sets the
// environment and then creates a server. A bad value is not fatal: a server
// that refuses to start over a typo is a worse failure than the default, and
// stderr is where an MCP client shows a stdio server's log.
function positiveIntEnv(name: string, fallback: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === "") return fallback;
  const n = Number(raw);
  if (Number.isInteger(n) && n > 0) return n;
  process.stderr.write(`orion-mcp: ignoring ${name}=${raw}, not a positive integer; using ${fallback}\n`);
  return fallback;
}

// A FIFO counting semaphore. FIFO, not "whoever wakes first", so ten agents
// waiting behind two slots are served in the order they asked and none starves.
// Hand-written because every bundled npm package has to be allow-listed in
// tools/mcp-licenses.py and ends up in the single executable.
export class Semaphore {
  #free: number;
  #waiting: Array<() => void> = [];

  constructor(slots: number) {
    this.#free = slots;
  }

  async run<T>(work: () => Promise<T>): Promise<T> {
    if (this.#free > 0) this.#free--;
    else await new Promise<void>((wake) => this.#waiting.push(wake));
    try {
      return await work();
    } finally {
      // The slot passes straight to the next waiter rather than going back to
      // the pool, so a caller arriving now cannot jump the queue.
      const next = this.#waiting.shift();
      if (next) next();
      else this.#free++;
    }
  }
}

// A non-zero exit throws with the stderr line (or the exit code) so tool
// handlers can turn it into an MCP tool error.
export class OrionError extends Error {
  exitCode?: number;
  constructor(message: string, exitCode?: number) {
    super(message);
    this.exitCode = exitCode;
  }
}

// Every live child, so a dying server can take them with it.
const children = new Set<ChildProcess>();

// One process of the binary under the cap and the timeout. Returns stdout.
function spawnAgent(bin: string, args: string[], timeoutMs: number): Promise<string> {
  return new Promise<string>((resolve, reject) => {
    const child = execFile(
      bin,
      ["--agent", ...args],
      // SIGKILL, not the default SIGTERM: a render wedged in a GPU wait may
      // never get to handle a polite signal, and the point is the slot back.
      // 16 MiB because sync_edits over a 70-frame shoot answers with one large
      // JSON object and Node's default of 1 MiB kills the process mid-answer.
      { timeout: timeoutMs, killSignal: "SIGKILL", maxBuffer: 16 * 1024 * 1024 },
      (err, stdout, stderr) => {
        children.delete(child);
        if (!err) return resolve(stdout);
        // ⚠ A timeout reads as a plain sentence for the same reason a
        // rejection reads REJECTED: (server.ts, fail): a model that sees a
        // bare "Command failed" or a signal name takes it for a rendering
        // glitch and retries the same thing, or worse, dials an edit back.
        if (err.killed && err.signal === "SIGKILL") {
          return reject(
            new OrionError(
              `TIMED OUT: Orion --agent ${args[0]} did not finish within ${timeoutMs} ms and the process was killed. ` +
                "This is not a rendering result and says nothing about the photo. " +
                "After a propose_edit, approve_edit or sync_edits, read get_edits before assuming whether anything was written."
            )
          );
        }
        const code = typeof (err as NodeJS.ErrnoException).code === "number" ? (err as any).code : undefined;
        reject(new OrionError(stderr.trim() || err.message, code));
      }
    );
    children.add(child);
  });
}

// The `orion()` each server's tools call: stdout is one JSON object per the
// spec's verb table. One semaphore per server, which is one per process in
// both entry points; the limit is on processes, not tool calls, so a tool that
// spawns twice (sync_edits under commit) takes a slot for each in turn.
export function agentRunner(bin: () => string): (args: string[]) => Promise<any> {
  const slots = positiveIntEnv("ORION_MCP_CONCURRENCY", defaultConcurrency(os.totalmem()));
  const timeoutMs = positiveIntEnv("ORION_MCP_TIMEOUT_MS", DEFAULT_TIMEOUT_MS);
  const gate = new Semaphore(slots);
  return async (args) => JSON.parse(await gate.run(() => spawnAgent(bin(), args, timeoutMs)));
}

function killChildren() {
  for (const child of children) child.kill("SIGKILL");
  children.clear();
}

// Called by both entry points (server.ts's own and sea.ts), never at import:
// server.test.ts imports createServer over an in-memory transport, and a
// signal handler installed there would change how the test runner dies.
//
// ⚠ An `exit` handler alone is not enough. Node's default SIGTERM, SIGINT and
// SIGHUP handling ends the process without emitting `exit`, so each signal is
// handled explicitly. A client that simply goes away closes our stdin, and the
// stdio transport does not notice; without the `end` listener the server
// would idle on, holding renders nobody will read. SIGKILL of this server
// cannot be caught, and its children survive that one.
let installed = false;
export function installProcessGuards() {
  if (installed) return;
  installed = true;
  process.on("exit", killChildren);
  for (const signal of ["SIGTERM", "SIGINT", "SIGHUP"] as const) {
    process.on(signal, () => {
      killChildren();
      process.exit(128 + os.constants.signals[signal]);
    });
  }
  const gone = () => {
    killChildren();
    process.exit(0);
  };
  process.stdin.on("end", gone);
  process.stdin.on("close", gone);
}
