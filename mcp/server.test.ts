// Tests for the Orion MCP server, run with `node --test server.test.ts`.
// Uses the SDK's in-memory transport and ORION_BIN pointed at
// mcp/test/fake-orion.sh, so this proves the wiring, the proposed-file
// lifecycle and error mapping without a GPU or a real Orion build.
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { promises as fs } from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawn } from "node:child_process";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { createServer } from "./server.ts";

const FAKE_BIN = path.resolve(import.meta.dirname, "test/fake-orion.sh");
process.env.ORION_BIN = FAKE_BIN;

let client: Client;
let tmpDir: string;

before(async () => {
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  client = new Client({ name: "test-client", version: "0.0.0" });
  const server = createServer();
  await Promise.all([client.connect(clientTransport), server.connect(serverTransport)]);
  tmpDir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-mcp-test-"));
});

after(async () => {
  await fs.rm(tmpDir, { recursive: true, force: true });
});

test("tools/list names exactly the seven tools", async () => {
  const { tools } = await client.listTools();
  const names = tools.map((t) => t.name).sort();
  assert.deepEqual(names, [
    "approve_edit",
    "get_proxy",
    "get_stats",
    "list_folder",
    "propose_edit",
    "reject_edit",
    "set_flag",
  ]);
});

test("get_stats returns text whose JSON has width === 100", async () => {
  const result = await client.callTool({ name: "get_stats", arguments: { path: "/x.arw" } });
  const content = result.content as Array<{ type: string; text?: string }>;
  const stats = JSON.parse(content[0].text!);
  assert.equal(stats.width, 100);
});

test("get_proxy returns an image content block decoding to a JPEG", async () => {
  const result = await client.callTool({ name: "get_proxy", arguments: { path: "/x.arw" } });
  const content = result.content as Array<{ type: string; data?: string; mimeType?: string }>;
  assert.equal(content[0].type, "image");
  assert.equal(content[0].mimeType, "image/jpeg");
  const bytes = Buffer.from(content[0].data!, "base64");
  assert.equal(bytes[0], 0xff);
  assert.equal(bytes[1], 0xd8);
});

test("propose_edit / approve_edit / reject_edit lifecycle", async () => {
  const raw = path.join(tmpDir, "shot.arw");
  await fs.writeFile(raw, "");
  const proposedPath = path.join(tmpDir, "shot.proposed.json");

  const proposed = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { exposureEv: 1.0 } },
  });
  assert.equal(proposed.isError, undefined);
  await fs.access(proposedPath); // exists

  const approved = await client.callTool({ name: "approve_edit", arguments: { path: raw } });
  assert.equal(approved.isError, undefined);
  await assert.rejects(() => fs.access(proposedPath)); // deleted
  await fs.access(path.join(tmpDir, "shot.xmp")); // fake-orion's commit touches this

  const rejected = await client.callTool({ name: "reject_edit", arguments: { path: raw } });
  const rejContent = rejected.content as Array<{ type: string; text?: string }>;
  assert.equal(rejected.isError, undefined);
  assert.deepEqual(JSON.parse(rejContent[0].text!), { deleted: false });
});

test("a verb exiting 2 becomes a tool error carrying the stderr line", async () => {
  const raw = path.join(tmpDir, "unknown-key.arw");
  await fs.writeFile(raw, "");
  const result = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { notARealField: 1 } },
  });
  assert.equal(result.isError, true);
  const content = result.content as Array<{ type: string; text?: string }>;
  assert.match(content[0].text!, /unknown keys/);
});

test("get_proxy clamps maxPx to 2048 before it ever reaches the binary", async () => {
  const maxpxFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-maxpx.txt");
  await fs.rm(maxpxFile, { force: true });
  const result = await client.callTool({ name: "get_proxy", arguments: { path: "/x.arw", maxPx: 3000 } });
  assert.equal(result.isError, undefined);
  const received = (await fs.readFile(maxpxFile, "utf8")).trim();
  assert.equal(received, "2048");
});

test("entry point runs when its own path contains a space", async () => {
  const dir = path.join(import.meta.dirname, "test", "has space");
  await fs.mkdir(dir, { recursive: true });
  const copiedServer = path.join(dir, "server.ts");
  await fs.copyFile(path.join(import.meta.dirname, "server.ts"), copiedServer);
  try {
    const child = spawn("node", [copiedServer], {
      env: { ...process.env, ORION_BIN: FAKE_BIN },
      stdio: ["pipe", "pipe", "pipe"],
    });
    const response = await new Promise<any>((resolve, reject) => {
      let buf = "";
      const timer = setTimeout(() => reject(new Error("timed out waiting for initialize response")), 5000);
      child.stdout.on("data", (chunk) => {
        buf += chunk.toString();
        const nl = buf.indexOf("\n");
        if (nl !== -1) {
          clearTimeout(timer);
          resolve(JSON.parse(buf.slice(0, nl)));
        }
      });
      child.on("error", (e) => {
        clearTimeout(timer);
        reject(e);
      });
      child.stdin.write(
        JSON.stringify({
          jsonrpc: "2.0",
          id: 1,
          method: "initialize",
          params: { protocolVersion: "2025-03-26", capabilities: {}, clientInfo: { name: "x", version: "0" } },
        }) + "\n"
      );
    });
    child.kill();
    assert.equal(response.result.serverInfo.name, "orion");
  } finally {
    await fs.rm(dir, { recursive: true, force: true });
  }
});
