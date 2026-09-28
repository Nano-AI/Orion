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

test("tools/list names exactly the ten tools", async () => {
  const { tools } = await client.listTools();
  const names = tools.map((t) => t.name).sort();
  assert.deepEqual(names, [
    "approve_edit",
    "current_photo",
    "describe_edits",
    "detect_faces",
    "get_proxy",
    "get_stats",
    "list_folder",
    "propose_edit",
    "reject_edit",
    "set_flag",
  ]);
});

test("describe_edits returns keys containing temperatureK with absolute true", async () => {
  const result = await client.callTool({ name: "describe_edits", arguments: {} });
  assert.equal(result.isError, undefined);
  const content = result.content as Array<{ type: string; text?: string }>;
  const { keys } = JSON.parse(content[0].text!);
  const temperatureK = keys.find((k: { name: string }) => k.name === "temperatureK");
  assert.ok(temperatureK, "expected a temperatureK key");
  assert.equal(temperatureK.absolute, true);
  assert.equal(temperatureK.unit, "kelvin");
  assert.equal(temperatureK.min, 2000);
  assert.equal(temperatureK.max, 50000);
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

test("a verb exiting 2 becomes a tool error carrying the stderr line, prefixed REJECTED:", async () => {
  const raw = path.join(tmpDir, "unknown-key.arw");
  await fs.writeFile(raw, "");
  const result = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { notARealField: 1 } },
  });
  assert.equal(result.isError, true);
  const content = result.content as Array<{ type: string; text?: string }>;
  assert.match(content[0].text!, /^REJECTED: /);
  assert.match(content[0].text!, /unknown keys/);
});

test("propose_edit with an out-of-range temperatureK is REJECTED, not merged", async () => {
  const raw = path.join(tmpDir, "too-cold.arw");
  await fs.writeFile(raw, "");
  const result = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { temperatureK: 150, tint: 15 } },
  });
  assert.equal(result.isError, true);
  const content = result.content as Array<{ type: string; text?: string }>;
  assert.match(content[0].text!, /^REJECTED: temperatureK 150/);
});

test("propose_edit accumulates onto an existing proposed file: second call passes --state", async () => {
  const raw = path.join(tmpDir, "accumulate.arw");
  await fs.writeFile(raw, "");
  const stateFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-state.txt");

  const first = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { exposureEv: 0.5 } },
  });
  assert.equal(first.isError, undefined);
  const afterFirst = (await fs.readFile(stateFile, "utf8")).trim();
  assert.equal(afterFirst, "<none>", "first call has no existing proposed file to pass as --state");

  const second = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { tint: 5 } },
  });
  assert.equal(second.isError, undefined);
  const afterSecond = (await fs.readFile(stateFile, "utf8")).trim();
  assert.equal(afterSecond, raw.replace(/\.[^.]+$/, ".proposed.json"), "second call passes the existing proposed file as --state");
});

test("propose_edit reset: true ignores an existing proposed file: no --state passed", async () => {
  const raw = path.join(tmpDir, "reset.arw");
  await fs.writeFile(raw, "");
  const stateFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-state.txt");

  await client.callTool({ name: "propose_edit", arguments: { path: raw, edits: { exposureEv: 0.5 } } });
  const resetResult = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { tint: 5 }, reset: true },
  });
  assert.equal(resetResult.isError, undefined);
  const afterReset = (await fs.readFile(stateFile, "utf8")).trim();
  assert.equal(afterReset, "<none>", "reset: true must not pass --state even though a proposed file exists");
});

test("propose_edit result text includes the merged state", async () => {
  const raw = path.join(tmpDir, "state-passthrough.arw");
  await fs.writeFile(raw, "");
  const result = await client.callTool({
    name: "propose_edit",
    arguments: { path: raw, edits: { exposureEv: 0.5 } },
  });
  assert.equal(result.isError, undefined);
  const content = result.content as Array<{ type: string; text?: string }>;
  assert.match(content[0].text!, /"state"/);
  const parsed = JSON.parse(content[0].text!);
  assert.equal(typeof parsed.state, "object");
});

// ⚠ This used to be a silent clamp - Math.min(maxPx, 2048) inside the handler
// - so a model asking for 4096 got 2048 back and no way to learn the cap
// existed. The schema carries the cap now, so the refusal names it.
test("get_proxy refuses a maxPx over 2048 at the schema rather than clamping it", async () => {
  const maxpxFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-maxpx.txt");
  await fs.rm(maxpxFile, { force: true });
  const refused = await client.callTool({ name: "get_proxy", arguments: { path: "/x.arw", maxPx: 3000 } });
  assert.equal(refused.isError, true);
  const refusedContent = refused.content as Array<{ type: string; text?: string }>;
  assert.match(refusedContent[0].text!, /2048/, "the refusal names the cap");
  await assert.rejects(() => fs.access(maxpxFile), "the binary must never have been run");

  const ok = await client.callTool({ name: "get_proxy", arguments: { path: "/x.arw", maxPx: 2048 } });
  assert.equal(ok.isError, undefined);
  assert.equal((await fs.readFile(maxpxFile, "utf8")).trim(), "2048");
});

test("get_proxy passes region through as --region x,y,w,h", async () => {
  const regionFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-region.txt");
  await fs.rm(regionFile, { force: true });

  const plain = await client.callTool({ name: "get_proxy", arguments: { path: "/x.arw" } });
  assert.equal(plain.isError, undefined);
  assert.equal((await fs.readFile(regionFile, "utf8")).trim(), "<none>");

  const cropped = await client.callTool({
    name: "get_proxy",
    arguments: { path: "/x.arw", region: [0.25, 0.1, 0.5, 0.4] },
  });
  assert.equal(cropped.isError, undefined);
  assert.equal((await fs.readFile(regionFile, "utf8")).trim(), "0.25,0.1,0.5,0.4");
});

test("get_proxy rejects a region that is not four numbers", async () => {
  const result = await client.callTool({
    name: "get_proxy",
    arguments: { path: "/x.arw", region: [0.1, 0.2, 0.3] },
  });
  assert.equal(result.isError, true);
  const content = result.content as Array<{ type: string; text?: string }>;
  assert.match(content[0].text!, /region/, "the refusal names the argument");
});

test("get_stats passes region through and returns the region object", async () => {
  const regionFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-stats-region.txt");
  await fs.rm(regionFile, { force: true });

  const plain = await client.callTool({ name: "get_stats", arguments: { path: "/x.arw" } });
  const plainContent = plain.content as Array<{ type: string; text?: string }>;
  assert.equal((await fs.readFile(regionFile, "utf8")).trim(), "<none>");
  assert.equal(JSON.parse(plainContent[0].text!).region, undefined);

  const patch = await client.callTool({
    name: "get_stats",
    arguments: { path: "/x.arw", region: [0, 0, 0.5, 0.5] },
  });
  assert.equal(patch.isError, undefined);
  assert.equal((await fs.readFile(regionFile, "utf8")).trim(), "0,0,0.5,0.5");
  const content = patch.content as Array<{ type: string; text?: string }>;
  const { region } = JSON.parse(content[0].text!);
  for (const field of ["luma", "saturation", "hue", "hueStrength", "red", "green", "blue",
                       "clippedHigh", "clippedLow", "shading"]) {
    assert.equal(typeof region[field], "number", field);
  }
});

test("get_stats state: proposed passes the proposed file as --state, and only when it exists", async () => {
  const raw = path.join(tmpDir, "stats-state.arw");
  await fs.writeFile(raw, "");
  const stateFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-stats-state.txt");

  await client.callTool({ name: "get_stats", arguments: { path: raw, state: "proposed" } });
  assert.equal(
    (await fs.readFile(stateFile, "utf8")).trim(),
    "<none>",
    "no proposal yet, so there is nothing to restore"
  );

  await client.callTool({ name: "propose_edit", arguments: { path: raw, edits: { exposureEv: 0.5 } } });
  await client.callTool({ name: "get_stats", arguments: { path: raw, state: "proposed" } });
  assert.equal(
    (await fs.readFile(stateFile, "utf8")).trim(),
    raw.replace(/\.[^.]+$/, ".proposed.json")
  );

  await client.callTool({ name: "get_stats", arguments: { path: raw } });
  assert.equal((await fs.readFile(stateFile, "utf8")).trim(), "<none>", "default is current");
  await client.callTool({ name: "reject_edit", arguments: { path: raw } });
});

test("detect_faces returns a face carrying both spaces", async () => {
  const result = await client.callTool({ name: "detect_faces", arguments: { path: "/x.arw" } });
  assert.equal(result.isError, undefined);
  const content = result.content as Array<{ type: string; text?: string }>;
  const { faces } = JSON.parse(content[0].text!);
  assert.equal(faces.length, 1);
  for (const field of ["x", "y", "w", "h", "centerX", "centerY", "radiusX", "radiusY"]) {
    assert.equal(typeof faces[0][field], "number", field);
  }
});

test("current_photo returns the contents of current.json", async () => {
  const file = path.join(tmpDir, "current-1.json");
  const current = { photo: "/shoot/a.arw", folder: "/shoot", updated: "2026-09-14T00:00:00Z" };
  await fs.writeFile(file, JSON.stringify(current));
  process.env.ORION_CURRENT = file;
  try {
    const result = await client.callTool({ name: "current_photo", arguments: {} });
    assert.equal(result.isError, undefined);
    const content = result.content as Array<{ type: string; text?: string }>;
    assert.deepEqual(JSON.parse(content[0].text!), current);
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("current_photo with a missing current.json is a non-error hint, not a tool error", async () => {
  process.env.ORION_CURRENT = path.join(tmpDir, "does-not-exist.json");
  try {
    const result = await client.callTool({ name: "current_photo", arguments: {} });
    assert.equal(result.isError, undefined);
    const content = result.content as Array<{ type: string; text?: string }>;
    assert.deepEqual(JSON.parse(content[0].text!), {
      photo: null,
      hint: "open a photo in Orion, or pass path explicitly",
    });
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("current_photo with photo: null in current.json is the same non-error hint", async () => {
  const file = path.join(tmpDir, "current-null.json");
  await fs.writeFile(file, JSON.stringify({ photo: null, folder: null, updated: "2026-09-14T00:00:00Z" }));
  process.env.ORION_CURRENT = file;
  try {
    const result = await client.callTool({ name: "current_photo", arguments: {} });
    assert.equal(result.isError, undefined);
    const content = result.content as Array<{ type: string; text?: string }>;
    assert.deepEqual(JSON.parse(content[0].text!), {
      photo: null,
      hint: "open a photo in Orion, or pass path explicitly",
    });
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("get_stats without a path resolves the photo from current.json", async () => {
  const raw = path.join(tmpDir, "resolved.arw");
  await fs.writeFile(raw, "");
  const file = path.join(tmpDir, "current-2.json");
  await fs.writeFile(file, JSON.stringify({ photo: raw, folder: tmpDir, updated: "2026-09-14T00:00:00Z" }));
  process.env.ORION_CURRENT = file;
  try {
    const result = await client.callTool({ name: "get_stats", arguments: {} });
    assert.equal(result.isError, undefined);
    const content = result.content as Array<{ type: string; text?: string }>;
    assert.equal(JSON.parse(content[0].text!).path, raw);
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("list_folder without a folder resolves it from current.json's folder field", async () => {
  const file = path.join(tmpDir, "current-3.json");
  await fs.writeFile(file, JSON.stringify({ photo: null, folder: tmpDir, updated: "2026-09-14T00:00:00Z" }));
  process.env.ORION_CURRENT = file;
  try {
    const result = await client.callTool({ name: "list_folder", arguments: {} });
    assert.equal(result.isError, undefined);
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("every optional-path tool gives the same isError when nothing is open and no path is given", async () => {
  process.env.ORION_CURRENT = path.join(tmpDir, "does-not-exist-2.json");
  try {
    const calls: Array<[string, Record<string, unknown>]> = [
      ["get_stats", {}],
      ["get_proxy", {}],
      ["propose_edit", { edits: { exposureEv: 0.1 } }],
      ["approve_edit", {}],
      ["reject_edit", {}],
      ["set_flag", { rating: 3 }],
      ["detect_faces", {}],
      ["list_folder", {}],
    ];
    for (const [name, args] of calls) {
      const result = await client.callTool({ name, arguments: args });
      assert.equal(result.isError, true, name);
      const content = result.content as Array<{ type: string; text?: string }>;
      assert.equal(content[0].text, "no photo is open in Orion and no path was given", name);
    }
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("describe_edits passes a composite key (curve) through unchanged", async () => {
  const result = await client.callTool({ name: "describe_edits", arguments: {} });
  const content = result.content as Array<{ type: string; text?: string }>;
  const { keys } = JSON.parse(content[0].text!);
  const curve = keys.find((k: { name: string }) => k.name === "curve");
  assert.ok(curve, "expected a curve key");
  assert.equal(curve.type, "array");
  assert.equal(curve.absolute, true);
  assert.deepEqual(curve.example, [
    { x: 0, y: 0 },
    { x: 1, y: 1 },
  ]);
  assert.equal(curve.min, undefined);
  assert.equal(curve.max, undefined);
});

test("propose_edit sends a composite value whole and gets it back unchanged in state", async () => {
  const raw = path.join(tmpDir, "composite.arw");
  await fs.writeFile(raw, "");
  const curve = [
    { x: 0, y: 0 },
    { x: 0.5, y: 0.65 },
    { x: 1, y: 1 },
  ];
  const result = await client.callTool({ name: "propose_edit", arguments: { path: raw, edits: { curve } } });
  assert.equal(result.isError, undefined);
  const content = result.content as Array<{ type: string; text?: string }>;
  assert.deepEqual(JSON.parse(content[0].text!).state.curve, curve);
});

test("server instructions mention current_photo and never ask which file", async () => {
  const instructions = client.getInstructions();
  assert.ok(instructions, "server should have instructions");
  assert.match(instructions, /current_photo/i, "instructions should mention current_photo");
  assert.match(instructions, /never ask which file/i, "instructions should say never ask which file");
  assert.match(instructions, /region/i, "instructions should point at the region arguments");
  assert.ok(
    Buffer.byteLength(instructions, "utf8") < 1536,
    `instructions are ${Buffer.byteLength(instructions, "utf8")} bytes; the budget is about 1.5 KB`
  );
});

// The same words go into AssistantProcess.orionContext, which is what Orion's
// own assistant panel appends to Claude's system prompt. Two copies that drift
// are two different sets of editing advice depending on which door the model
// came in by, so the Swift literal is compared here rather than by eye.
test("the Swift orionContext literal is identical to these instructions", async () => {
  const swift = await fs.readFile(
    path.resolve(import.meta.dirname, "../app/AssistantProcess.swift"),
    "utf8"
  );
  const match = swift.match(/static let orionContext = "([^"]*)"/);
  assert.ok(match, "AssistantProcess.swift should hold a orionContext string literal");
  assert.equal(match[1], client.getInstructions());
});

test("entry point runs when its own path contains a space", async () => {
  const dir = path.join(import.meta.dirname, "test", "has space");
  await fs.mkdir(dir, { recursive: true });
  const copiedServer = path.join(dir, "server.ts");
  await fs.copyFile(path.join(import.meta.dirname, "server.ts"), copiedServer);
  try {
    const child = spawn("node", ["--experimental-strip-types", copiedServer], {
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
