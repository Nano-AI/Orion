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

test("tools/list names exactly the thirteen tools", async () => {
  const { tools } = await client.listTools();
  const names = tools.map((t) => t.name).sort();
  assert.deepEqual(names, [
    "approve_edit",
    "current_photo",
    "describe_edits",
    "detect_faces",
    "detect_subject",
    "get_edits",
    "get_proxy",
    "get_stats",
    "list_folder",
    "propose_edit",
    "reject_edit",
    "set_flag",
    "sync_edits",
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

test("get_stats passes the capture block through untouched", async () => {
  const result = await client.callTool({ name: "get_stats", arguments: { path: "/x.arw" } });
  const content = result.content as Array<{ type: string; text?: string }>;
  const stats = JSON.parse(content[0].text!);
  assert.deepEqual(stats.capture, {
    camera: "Fake",
    lens: "Fake 35mm",
    focalLength: 35,
    aperture: 1.8,
    shutter: 0.004,
    iso: 6400,
    captured: "2026-09-14T00:00:00Z",
  });
});

test("current_photo carries capture from current.json", async () => {
  const file = path.join(tmpDir, "current-capture.json");
  const current = {
    photo: "/shoot/a.arw",
    folder: "/shoot",
    updated: "2026-09-14T00:00:00Z",
    capture: { camera: "Fake", iso: 3200, shutter: 0.01 },
  };
  await fs.writeFile(file, JSON.stringify(current));
  process.env.ORION_CURRENT = file;
  try {
    const result = await client.callTool({ name: "current_photo", arguments: {} });
    const content = result.content as Array<{ type: string; text?: string }>;
    assert.deepEqual(JSON.parse(content[0].text!).capture, current.capture);
  } finally {
    delete process.env.ORION_CURRENT;
  }
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
                       "clippedHigh", "clippedLow", "shading", "noise"]) {
    assert.equal(typeof region[field], "number", field);
  }
  // Passed through as the binary measured it, not recomputed or dropped.
  assert.equal(region.noise, 0.01);
  // The same rectangle as a frame-space ellipse, ready for a kind-2 radial.
  for (const field of ["centerX", "centerY", "radiusX", "radiusY"]) {
    assert.equal(typeof region.frame?.[field], "number", `frame.${field}`);
  }
});

test("get_edits defaults to current: no --state, and hasProposed false with no proposal", async () => {
  const raw = path.join(tmpDir, "edits-current.arw");
  await fs.writeFile(raw, "");
  const stateFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-edits-state.txt");
  await fs.rm(stateFile, { force: true });

  const result = await client.callTool({ name: "get_edits", arguments: { path: raw } });
  assert.equal(result.isError, undefined);
  assert.equal((await fs.readFile(stateFile, "utf8")).trim(), "<none>");
  const content = result.content as Array<{ type: string; text?: string }>;
  const body = JSON.parse(content[0].text!);
  assert.equal(body.hasProposed, false);
  assert.equal(body.source, "as-shot");
  assert.equal(body.state.exposureEv, 0);
  assert.deepEqual(body.state.curve.master, [{ x: 0, y: 0 }, { x: 1, y: 1 }], "composites come back whole");
});

test("get_edits state: proposed passes --state only when the proposed file exists", async () => {
  const raw = path.join(tmpDir, "edits-proposed.arw");
  await fs.writeFile(raw, "");
  const stateFile = path.join(process.env.TMPDIR || "/tmp", "orion-mcp-test-last-edits-state.txt");
  const proposed = raw.replace(/\.[^.]+$/, ".proposed.json");

  const none = await client.callTool({ name: "get_edits", arguments: { path: raw, state: "proposed" } });
  assert.equal((await fs.readFile(stateFile, "utf8")).trim(), "<none>", "no proposal, nothing to pass");
  assert.equal(JSON.parse((none.content as Array<{ text?: string }>)[0].text!).hasProposed, false);

  await client.callTool({ name: "propose_edit", arguments: { path: raw, edits: { exposureEv: 1.0 } } });
  const got = await client.callTool({ name: "get_edits", arguments: { path: raw, state: "proposed" } });
  assert.equal((await fs.readFile(stateFile, "utf8")).trim(), proposed);
  const body = JSON.parse((got.content as Array<{ text?: string }>)[0].text!);
  assert.equal(body.hasProposed, true);
  assert.equal(body.source, "proposed");

  const current = await client.callTool({ name: "get_edits", arguments: { path: raw } });
  assert.equal((await fs.readFile(stateFile, "utf8")).trim(), "<none>", "current ignores the proposal");
  assert.equal(
    JSON.parse((current.content as Array<{ text?: string }>)[0].text!).hasProposed,
    true,
    "hasProposed says a proposal exists even when reading current"
  );
  await client.callTool({ name: "reject_edit", arguments: { path: raw } });
});

test("get_edits never creates a proposed file", async () => {
  const raw = path.join(tmpDir, "edits-readonly.arw");
  await fs.writeFile(raw, "");
  const proposed = raw.replace(/\.[^.]+$/, ".proposed.json");
  for (const state of ["current", "proposed"]) {
    const result = await client.callTool({ name: "get_edits", arguments: { path: raw, state } });
    assert.equal(result.isError, undefined);
    await assert.rejects(() => fs.access(proposed), `get_edits state: ${state} wrote ${proposed}`);
  }
  assert.deepEqual((await fs.readdir(tmpDir)).filter((f) => f.startsWith("edits-readonly")), ["edits-readonly.arw"]);
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

const TMP = process.env.TMPDIR || "/tmp";
const recorded = async (name: string) => (await fs.readFile(path.join(TMP, `orion-mcp-test-${name}.txt`), "utf8")).trim();
const body = (result: unknown) => JSON.parse(((result as { content: Array<{ text?: string }> }).content)[0].text!);
const exists = (p: string) => fs.access(p).then(() => true, () => false);

test("detect_subject passes --kind and returns a kind-4 row with its matte beside the photo", async () => {
  const raw = path.join(tmpDir, "climber.arw");
  await fs.writeFile(raw, "");
  const result = await client.callTool({ name: "detect_subject", arguments: { path: raw, kind: "person" } });
  assert.equal(result.isError, undefined);
  assert.equal(await recorded("last-matte-kind"), "person");
  assert.equal(await recorded("last-matte-state"), "<none>");
  const answer = body(result);
  assert.equal(answer.found, true);
  assert.equal(answer.component.kind, 4);
  assert.equal(answer.component.matteId, answer.matteId);
  assert.equal(answer.component.startsLayer, true);
  assert.ok(await exists(path.join(tmpDir, `climber.orion-matte-${answer.matteId}.png`)));

  await client.callTool({ name: "detect_subject", arguments: { path: raw } });
  assert.equal(await recorded("last-matte-kind"), "subject", "kind defaults to subject");
});

test("detect_subject state: proposed passes --state only when the proposed file exists", async () => {
  const raw = path.join(tmpDir, "subject-state.arw");
  await fs.writeFile(raw, "");
  await client.callTool({ name: "detect_subject", arguments: { path: raw, state: "proposed" } });
  assert.equal(await recorded("last-matte-state"), "<none>", "no proposal, nothing to pass");
  await client.callTool({ name: "propose_edit", arguments: { path: raw, edits: { exposureEv: 0.5 } } });
  await client.callTool({ name: "detect_subject", arguments: { path: raw, state: "proposed" } });
  assert.equal(await recorded("last-matte-state"), raw.replace(/\.[^.]+$/, ".proposed.json"));
  await client.callTool({ name: "detect_subject", arguments: { path: raw } });
  assert.equal(await recorded("last-matte-state"), "<none>", "default is current");
  await client.callTool({ name: "reject_edit", arguments: { path: raw } });
});

test("detect_subject finding nothing is an answer, not a tool error", async () => {
  const result = await client.callTool({ name: "detect_subject", arguments: { path: "/x.arw", kind: "sky" } });
  assert.equal(result.isError, undefined);
  const answer = body(result);
  assert.equal(answer.found, false);
  assert.match(answer.reason, /Nothing was found/);
});

async function shoot(name: string, count: number): Promise<string[]> {
  const dir = path.join(tmpDir, name);
  await fs.mkdir(dir, { recursive: true });
  const raws = [];
  for (let i = 0; i < count; i++) {
    const raw = path.join(dir, `frame${i}.arw`);
    await fs.writeFile(raw, "");
    raws.push(raw);
  }
  return raws;
}

test("sync_edits writes exactly the targets and passes the default groups in propose mode", async () => {
  const [source, ...targets] = await shoot("sync-default", 3);
  const result = await client.callTool({ name: "sync_edits", arguments: { source, targets } });
  assert.equal(result.isError, undefined);
  assert.deepEqual(JSON.parse(await recorded("last-sync-targets")), targets);
  assert.equal(await recorded("last-sync-groups"), "light,color,curve,effects");
  assert.equal(await recorded("last-sync-mode"), "propose");
  assert.equal(await recorded("last-sync-state"), "<none>");
  const answer = body(result);
  assert.equal(answer.written, 2);
  assert.deepEqual(answer.results.map((r: { wrote: string }) => r.wrote), ["proposed", "proposed"]);
  for (const t of targets) assert.ok(await exists(t.replace(/\.[^.]+$/, ".proposed.json")), t);
  assert.equal(answer.openPhotoProposed, undefined);
});

test("sync_edits passes a custom group list and commit: true as --mode commit", async () => {
  const [source, target] = await shoot("sync-commit", 2);
  const result = await client.callTool({
    name: "sync_edits",
    arguments: { source, targets: [target], groups: ["whiteBalance", "masks"], commit: true },
  });
  assert.equal(result.isError, undefined);
  assert.equal(await recorded("last-sync-groups"), "whiteBalance,masks");
  assert.equal(await recorded("last-sync-mode"), "commit");
  assert.ok(await exists(target.replace(/\.[^.]+$/, ".xmp")));
  assert.equal(body(result).results[0].wrote, "sidecar");
});

test("sync_edits commit: the photo open in Orion gets a proposal, the rest get sidecars", async () => {
  const [source, a, open, b] = await shoot("sync-open", 4);
  const current = path.join(tmpDir, "current-sync.json");
  await fs.writeFile(current, JSON.stringify({ photo: open, folder: path.dirname(open), updated: "2026-10-17T00:00:00Z" }));
  const calls = path.join(TMP, "orion-mcp-test-sync-calls.txt");
  await fs.rm(calls, { force: true });
  process.env.ORION_CURRENT = current;
  try {
    const result = await client.callTool({ name: "sync_edits", arguments: { source, targets: [a, open, b], commit: true } });
    assert.equal(result.isError, undefined);
    const lines = (await fs.readFile(calls, "utf8")).trim().split("\n");
    const byMode = Object.fromEntries(lines.map((l) => [l.slice(0, l.indexOf(" ")), JSON.parse(l.slice(l.indexOf(" ") + 1))]));
    assert.deepEqual(byMode.propose, [open]);
    assert.deepEqual(byMode.commit, [a, b]);
    assert.equal(lines.length, 2);

    const answer = body(result);
    assert.equal(answer.openPhotoProposed, open);
    assert.equal(answer.written, 3);
    assert.equal(answer.failed, 0);
    assert.deepEqual(
      answer.results.map((r: { path: string; wrote: string }) => [r.path, r.wrote]),
      [[a, "sidecar"], [open, "proposed"], [b, "sidecar"]],
      "results are merged in the order the targets were given"
    );
    assert.ok(await exists(open.replace(/\.[^.]+$/, ".proposed.json")));
    assert.equal(await exists(open.replace(/\.[^.]+$/, ".xmp")), false, "the open photo's sidecar is never written");
  } finally {
    delete process.env.ORION_CURRENT;
  }
});

test("sync_edits sourceState: proposed passes --state only when the source's proposal exists", async () => {
  const [source, target] = await shoot("sync-state", 2);
  await client.callTool({ name: "sync_edits", arguments: { source, targets: [target], sourceState: "proposed" } });
  assert.equal(await recorded("last-sync-state"), "<none>", "no proposal, nothing to pass");
  await client.callTool({ name: "propose_edit", arguments: { path: source, edits: { exposureEv: 1.0 } } });
  await client.callTool({ name: "sync_edits", arguments: { source, targets: [target], sourceState: "proposed" } });
  assert.equal(await recorded("last-sync-state"), source.replace(/\.[^.]+$/, ".proposed.json"));
  await client.callTool({ name: "sync_edits", arguments: { source, targets: [target] } });
  assert.equal(await recorded("last-sync-state"), "<none>", "default is current");
});

test("sync_edits refuses zero targets and an unknown group at the schema", async () => {
  const modeFile = path.join(TMP, "orion-mcp-test-last-sync-mode.txt");
  await fs.rm(modeFile, { force: true });
  const empty = await client.callTool({ name: "sync_edits", arguments: { source: "/x.arw", targets: [] } });
  assert.equal(empty.isError, true);
  assert.match((empty.content as Array<{ text?: string }>)[0].text!, /targets/);
  const unknown = await client.callTool({
    name: "sync_edits",
    arguments: { source: "/x.arw", targets: ["/y.arw"], groups: ["light", "sharpness"] },
  });
  assert.equal(unknown.isError, true);
  assert.match((unknown.content as Array<{ text?: string }>)[0].text!, /groups/);
  assert.equal(await exists(modeFile), false, "the binary must never have been run");
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
      ["detect_subject", {}],
      ["sync_edits", { targets: ["/shoot/b.arw"] }],
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

// The same words go into AssistantModel.OrionContext, which is what Orion's
// own assistant column appends to Claude's system prompt. Two copies that drift
// are two different sets of editing advice depending on which door the model
// came in by, so the app's literal is compared here rather than by eye.
test("the app's OrionContext literal is identical to these instructions", async () => {
  const source = await fs.readFile(
    path.resolve(import.meta.dirname, "../shell/Slices/Chrome/AssistantProcess.cs"),
    "utf8"
  );
  const match = source.match(/const string OrionContext = "([^"]*)"/);
  assert.ok(match, "AssistantProcess.cs should hold an OrionContext string literal");
  assert.equal(match[1], client.getInstructions());
});

test("entry point runs when its own path contains a space", async () => {
  const dir = path.join(import.meta.dirname, "test", "has space");
  await fs.mkdir(dir, { recursive: true });
  const copiedServer = path.join(dir, "server.ts");
  await fs.copyFile(path.join(import.meta.dirname, "server.ts"), copiedServer);
  await fs.copyFile(path.join(import.meta.dirname, "limits.ts"), path.join(dir, "limits.ts"));
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
