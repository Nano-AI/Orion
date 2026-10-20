// Tests for the concurrency cap, the timeout and the children that die with
// the server (#311, limits.ts). Separate from server.test.ts because each test
// here needs its own server built under its own environment, and because the
// fake binary writes into $TMPDIR: this file gets a TMPDIR of its own so its
// get_stats calls never race server.test.ts's, which node --test runs in
// parallel.
import { test, after, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { promises as fs, mkdtempSync } from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawn } from "node:child_process";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { createServer } from "./server.ts";
import { defaultConcurrency, Semaphore } from "./limits.ts";

const FAKE_BIN = path.resolve(import.meta.dirname, "test/fake-orion.sh");
process.env.ORION_BIN = FAKE_BIN;
const TMP = mkdtempSync(path.join(os.tmpdir(), "orion-mcp-limits-"));
process.env.TMPDIR = TMP;
const LOG = path.join(TMP, "orion-mcp-test-overlap.txt");
const GiB = 1024 ** 3;

const LIMIT_ENV = ["ORION_MCP_CONCURRENCY", "ORION_MCP_TIMEOUT_MS", "ORION_FAKE_SLEEP_MS"];

// The server reads the cap and the timeout once, in createServer(); the fake
// reads its sleep at every spawn, from this process's environment.
async function connect(env: Record<string, string>): Promise<Client> {
  Object.assign(process.env, env);
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: "limits-test", version: "0.0.0" });
  await Promise.all([client.connect(clientTransport), createServer().connect(serverTransport)]);
  return client;
}

type Event = { kind: "start" | "end"; at: number; pid: number };
async function readLog(): Promise<Event[]> {
  const body = await fs.readFile(LOG, "utf8").catch(() => "");
  return body
    .trim()
    .split("\n")
    .filter(Boolean)
    .map((line) => {
      const [kind, at, pid] = line.split(" ");
      return { kind: kind as Event["kind"], at: Number(at), pid: Number(pid) };
    });
}

// The most fakes asleep at any one moment. An end sorts before a start at the
// same millisecond, so back-to-back runs do not count as overlapping.
function maxOverlap(events: Event[]): number {
  const sorted = [...events].sort((a, b) => a.at - b.at || (a.kind === "end" ? -1 : 1));
  let live = 0;
  let most = 0;
  for (const e of sorted) {
    live += e.kind === "start" ? 1 : -1;
    most = Math.max(most, live);
  }
  return most;
}

const alive = (pid: number) => {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
};

// A child SIGKILLed by a server that then exits is reparented and reaped by
// launchd, not instantly; until then kill(pid, 0) still finds the zombie.
async function dies(pid: number): Promise<boolean> {
  const deadline = Date.now() + 2000;
  while (alive(pid)) {
    if (Date.now() > deadline) return false;
    await new Promise((r) => setTimeout(r, 25));
  }
  return true;
}

const stats = (client: Client) => client.callTool({ name: "get_stats", arguments: { path: "/x.arw" } });
const textOf = (r: Awaited<ReturnType<typeof stats>>) => (r.content as Array<{ text?: string }>)[0].text!;

beforeEach(async () => {
  for (const k of LIMIT_ENV) delete process.env[k];
  await fs.rm(LOG, { force: true });
});

after(async () => {
  await fs.rm(TMP, { recursive: true, force: true });
});

test("defaultConcurrency is one process per 16 GiB of memory, between 1 and 4", () => {
  assert.equal(defaultConcurrency(8 * GiB), 1);
  assert.equal(defaultConcurrency(16 * GiB), 1);
  assert.equal(defaultConcurrency(36 * GiB), 2);
  assert.equal(defaultConcurrency(36e9), 2);
  assert.equal(defaultConcurrency(64 * GiB), 4);
  assert.equal(defaultConcurrency(128 * GiB), 4);
  assert.equal(defaultConcurrency(0), 1);
});

test("the semaphore serves waiters in the order they arrived", async () => {
  const gate = new Semaphore(1);
  const order: number[] = [];
  let open!: () => void;
  const first = gate.run(() => new Promise<void>((r) => (open = r)));
  const rest = [1, 2, 3].map((n) => gate.run(async () => void order.push(n)));
  open();
  await Promise.all([first, ...rest]);
  assert.deepEqual(order, [1, 2, 3]);
});

test("ORION_MCP_CONCURRENCY=2: six calls at once all succeed, two at a time, queued", async () => {
  const client = await connect({ ORION_MCP_CONCURRENCY: "2", ORION_FAKE_SLEEP_MS: "300" });
  const started = Date.now();
  const results = await Promise.all(Array.from({ length: 6 }, () => stats(client)));
  const wall = Date.now() - started;
  for (const r of results) {
    assert.equal(r.isError, undefined, textOf(r));
    assert.equal(JSON.parse(textOf(r)).width, 100);
  }
  const events = await readLog();
  assert.equal(events.filter((e) => e.kind === "start").length, 6);
  assert.equal(maxOverlap(events), 2);
  // Three rounds of two. Under the cap but without a queue, four of the six
  // would have failed instead, and the wall time would be one round.
  assert.ok(wall >= 3 * 300, `six 300 ms calls two at a time took ${wall} ms`);
});

test("ORION_MCP_TIMEOUT_MS: a hung call is killed and says so in plain words", async () => {
  const client = await connect({ ORION_MCP_TIMEOUT_MS: "200", ORION_FAKE_SLEEP_MS: "2000" });
  const started = Date.now();
  const result = await stats(client);
  const elapsed = Date.now() - started;
  assert.equal(result.isError, true);
  assert.match(textOf(result), /^TIMED OUT: .* within 200 ms and the process was killed/);
  assert.match(textOf(result), /not a rendering result/);
  assert.ok(elapsed < 1500, `the timed-out call took ${elapsed} ms to come back`);
  const [start] = (await readLog()).filter((e) => e.kind === "start");
  assert.ok(start, "the fake should have logged its start");
  assert.equal(alive(start.pid), false, `fake ${start.pid} should be dead`);
});

test("a timed-out call gives its slot back: the next one runs promptly", async () => {
  const client = await connect({ ORION_MCP_CONCURRENCY: "1", ORION_MCP_TIMEOUT_MS: "200", ORION_FAKE_SLEEP_MS: "2000" });
  assert.equal((await stats(client)).isError, true);
  delete process.env.ORION_FAKE_SLEEP_MS;
  const started = Date.now();
  // With one slot, a leaked one hangs this call forever; the race turns that
  // into a failure with a reason instead of a stuck test run.
  const result = await Promise.race([
    stats(client),
    new Promise<never>((_, reject) => setTimeout(() => reject(new Error("the slot was never released")), 3000)),
  ]);
  assert.equal(result.isError, undefined, textOf(result));
  assert.ok(Date.now() - started < 1000, `the next call took ${Date.now() - started} ms`);
});

// The real entry point, `node server.ts`, over stdio, with one get_stats in
// flight against a fake that sleeps ten seconds. Resolves once the fake has
// logged its start, with the server process and the fake's PID.
async function serverWithCallInFlight() {
  const child = spawn("node", ["--experimental-strip-types", path.join(import.meta.dirname, "server.ts")], {
    env: { ...process.env, ORION_FAKE_SLEEP_MS: "10000" },
    stdio: ["pipe", "pipe", "pipe"],
  });
  const send = (msg: object) => child.stdin.write(JSON.stringify({ jsonrpc: "2.0", ...msg }) + "\n");
  send({
    id: 1,
    method: "initialize",
    params: { protocolVersion: "2025-03-26", capabilities: {}, clientInfo: { name: "x", version: "0" } },
  });
  send({ method: "notifications/initialized" });
  send({ id: 2, method: "tools/call", params: { name: "get_stats", arguments: { path: "/x.arw" } } });
  const deadline = Date.now() + 5000;
  for (;;) {
    const start = (await readLog()).find((e) => e.kind === "start");
    if (start) return { child, pid: start.pid };
    if (Date.now() > deadline) {
      child.kill("SIGKILL");
      throw new Error("the fake never started");
    }
    await new Promise((r) => setTimeout(r, 25));
  }
}

const exited = (child: ReturnType<typeof spawn>) =>
  new Promise<number | null>((resolve) => child.once("exit", (code) => resolve(code)));

test("SIGTERM to the server kills its live children before it exits", async () => {
  const { child, pid } = await serverWithCallInFlight();
  assert.equal(alive(pid), true);
  const exit = exited(child);
  child.kill("SIGTERM");
  assert.equal(await exit, 128 + os.constants.signals.SIGTERM);
  assert.equal(await dies(pid), true, `fake ${pid} outlived the server`);
  // Killed, not left to finish: without the guard the server idles until the
  // ten-second render ends on its own, and exits cleanly with nothing dead.
  assert.equal((await readLog()).some((e) => e.kind === "end"), false, "the fake ran to completion");
});

test("the client going away (stdin closed) kills the server's live children", async () => {
  const { child, pid } = await serverWithCallInFlight();
  const exit = exited(child);
  child.stdin.end();
  assert.equal(await exit, 0);
  assert.equal(await dies(pid), true, `fake ${pid} outlived the server`);
  // Killed, not left to finish: without the guard the server idles until the
  // ten-second render ends on its own, and exits cleanly with nothing dead.
  assert.equal((await readLog()).some((e) => e.kind === "end"), false, "the fake ran to completion");
});
