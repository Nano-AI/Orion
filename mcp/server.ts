// Orion MCP server (POC): a thin stdio shell over `Orion --agent <verb>`.
// No XMP, no Metal, no state of its own beyond a `.proposed.json` file beside
// the RAW. See docs/superpowers/specs/2026-09-13-agent-mcp-design.md, Part B.
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { execFile } from "node:child_process";
import { promises as fs } from "node:fs";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";

const RAW_EXT = new Set([".arw", ".nef", ".cr2", ".cr3", ".dng", ".raf", ".orf", ".rw2"]);
const bin = () =>
  process.env.ORION_BIN ?? path.resolve(import.meta.dirname, "../build/Orion.app/Contents/MacOS/Orion");
const proposedPath = (raw: string) => raw.replace(/\.[^.]+$/, ".proposed.json");

// One execFile of the binary; stdout is one JSON object per the spec's verb
// table. A non-zero exit throws with the stderr line (or the exit code) so
// tool handlers can turn it into an MCP tool error.
class OrionError extends Error {
  exitCode?: number;
  constructor(message: string, exitCode?: number) {
    super(message);
    this.exitCode = exitCode;
  }
}

async function orion(args: string[]): Promise<any> {
  const stdout = await new Promise<string>((resolve, reject) => {
    execFile(bin(), ["--agent", ...args], (err, stdout, stderr) => {
      if (err) {
        const code = typeof (err as NodeJS.ErrnoException).code === "number" ? (err as any).code : undefined;
        reject(new OrionError(stderr.trim() || err.message, code));
      } else {
        resolve(stdout);
      }
    });
  });
  return JSON.parse(stdout);
}

// Exit 2 is the binary's "rejected" contract (out-of-range value, unknown
// key). Prefixing REJECTED: is the whole fix for the incident this server
// shipped: a model that reads a plain error line as a rendering glitch and
// "dials back" onto a still-poisoned proposed file. Every tool routes errors
// through here, so this one prefix covers all of them.
const fail = (e: unknown) => {
  const err = e as OrionError;
  const text = err.exitCode === 2 ? `REJECTED: ${err.message}` : err.message;
  return { content: [{ type: "text" as const, text }], isError: true };
};
const text = (v: unknown) => ({ content: [{ type: "text" as const, text: JSON.stringify(v) }] });

// current.json is written atomically by Orion on every photo change:
// {"photo": "/abs/path.ARW" | null, "folder": "/abs/dir" | null, "updated": "<ISO 8601>"}
const currentPath = () =>
  process.env.ORION_CURRENT ?? path.join(os.homedir(), "Library/Application Support/Orion/current.json");

async function readCurrent(): Promise<{ photo: string | null; folder: string | null } | null> {
  try {
    return JSON.parse(await fs.readFile(currentPath(), "utf8"));
  } catch {
    return null;
  }
}

const NO_PHOTO_MSG = "no photo is open in Orion and no path was given";
const noPhoto = () => ({ content: [{ type: "text" as const, text: NO_PHOTO_MSG }], isError: true });

// The one resolution helper every path/folder-optional tool routes through:
// an explicit argument wins, otherwise fall back to current.json.
async function resolve(given: string | undefined, key: "photo" | "folder"): Promise<string | null> {
  if (given) return given;
  return (await readCurrent())?.[key] ?? null;
}

const ORION_INSTRUCTIONS = `Orion is a RAW photo editor and this server is its agent surface. The photographer is looking at one photo in Orion; when they say 'this image', 'this photo', 'the current one' or give no path, that photo is the one \`current_photo\` returns, and every tool's \`path\` defaults to it, so never ask which file. Start any edit with \`describe_edits\` and \`get_stats\`. All values are absolute, never deltas. Proposals appear live in Orion's compare view; the photographer approves or rejects there, so after \`propose_edit\` say what you changed and stop; do not call \`approve_edit\` unless asked. A 512 px proxy costs about 220 tokens; prefer \`get_stats\` when numbers will do.`;

export function createServer() {
  const server = new McpServer(
    { name: "orion", version: "0.1.0" },
    { instructions: ORION_INSTRUCTIONS }
  );

  server.registerTool(
    "current_photo",
    {
      description:
        "Returns the photo currently open in Orion (from current.json): {photo, folder, " +
        "updated}. Call this to find out what the user has open before asking them for a path " +
        "— every other tool's path/folder argument already defaults to this. Cheap, about " +
        "20-30 tokens.",
      inputSchema: {},
    },
    async () => {
      const current = await readCurrent();
      if (!current?.photo) return text({ photo: null, hint: "open a photo in Orion, or pass path explicitly" });
      return text(current);
    }
  );

  server.registerTool(
    "list_folder",
    {
      description:
        "Lists RAW files in a folder (~10-40 tokens per file). Call this first to see what's " +
        "in a shoot before spending tokens on get_stats or get_proxy for individual photos. " +
        "folder is optional; defaults to the folder of the photo open in Orion.",
      inputSchema: { folder: z.string().optional() },
    },
    async ({ folder: given }) => {
      try {
        const folder = await resolve(given, "folder");
        if (!folder) return noPhoto();
        const entries = await fs.readdir(folder, { withFileTypes: true });
        const raws = entries.filter((e) => e.isFile() && RAW_EXT.has(path.extname(e.name).toLowerCase()));
        const out = await Promise.all(
          raws.map(async (e) => {
            const p = path.join(folder, e.name);
            const hasProposed = await fs
              .access(proposedPath(p))
              .then(() => true)
              .catch(() => false);
            return { path: p, hasProposed };
          })
        );
        return text(out);
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "get_stats",
    {
      description:
        "Cheap: a histogram summary as JSON, about 60-80 tokens. Prefer this over get_proxy " +
        "to decide whether a photo needs a closer look — reach for get_proxy only after stats " +
        "look interesting (blown highlights, empty shadows, an unrated keeper). path is " +
        "optional; defaults to the photo open in Orion.",
      inputSchema: { path: z.string().optional() },
    },
    async ({ path: raw }) => {
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        return text(await orion(["stats", p]));
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "get_proxy",
    {
      description:
        "Expensive: a JPEG proxy as an image content block. A 512px proxy is about 220 tokens, " +
        "a 1024px proxy about 700-900, and cost grows with maxPx — get_stats first, and only " +
        "call this when you need to actually see the photo. path is optional; defaults to the " +
        "photo open in Orion.",
      inputSchema: { path: z.string().optional(), maxPx: z.number().int().min(64).default(1024), state: z.enum(["current", "proposed"]).default("current") },
    },
    async ({ path: raw, maxPx, state }) => {
      let dir: string | undefined;
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        dir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-"));
        const out = path.join(dir, "proxy.jpg");
        const args = ["proxy", p, "--max", String(Math.min(maxPx, 2048)), "--out", out];
        const proposed = proposedPath(p);
        if (state === "proposed" && (await fs.access(proposed).then(() => true, () => false))) {
          args.push("--state", proposed);
        }
        await orion(args);
        const data = await fs.readFile(out);
        return { content: [{ type: "image" as const, data: data.toString("base64"), mimeType: "image/jpeg" }] };
      } catch (e) {
        return fail(e);
      } finally {
        if (dir) await fs.rm(dir, { recursive: true, force: true });
      }
    }
  );

  server.registerTool(
    "describe_edits",
    {
      description:
        "Call this before your first propose_edit. Lists every editable key with unit, range " +
        "and default. All values are ABSOLUTE settings, never deltas: temperatureK is the white " +
        "balance in kelvin (thousands), not an offset.",
      inputSchema: {},
    },
    async () => {
      try {
        return text(await orion(["keys"]));
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "propose_edit",
    {
      description:
        "Writes a proposed edit beside the RAW (not the sidecar — nothing here touches the " +
        "photographer's XMP). ALL VALUES ARE ABSOLUTE, never deltas: call describe_edits first " +
        "for every key's unit, range and default, and read the current temperatureK/tint from " +
        "get_stats before changing white balance — 'make it warmer' is not '+150'. Edits " +
        "ACCUMULATE onto any existing proposed file, so a second call keeps everything the " +
        "first call set; pass reset: true to start over from the photo's current state instead " +
        "of merging. An out-of-range value is rejected with an error naming the valid range — " +
        "that is a rejection, not a rendering glitch, and nothing was written. Cheap: returns " +
        "the merged state as JSON, about 40-100 tokens, including a `state` object (everything " +
        "the proposed file now holds) — read it before deciding the edit worked. Composite " +
        "controls (curve, gradeShadow/Midtone/Highlight, hueShift/satShift/lumShift, layers, " +
        "spots, maskComponents) replace the whole value: read the current one from a previous " +
        "result's `state` or from describe_edits' `example`, edit it, send it complete. Partial " +
        "elements are rejected. path is optional; defaults to the photo open in Orion.",
      inputSchema: { path: z.string().optional(), edits: z.record(z.string(), z.unknown()), reset: z.boolean().optional() },
    },
    async ({ path: raw, edits, reset }) => {
      let dir: string | undefined;
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        dir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-"));
        const editsFile = path.join(dir, "edits.json");
        await fs.writeFile(editsFile, JSON.stringify(edits));
        const proposed = proposedPath(p);
        const args = ["apply", p, "--edits", editsFile, "--out", proposed];
        if (!reset && (await fs.access(proposed).then(() => true, () => false))) {
          args.push("--state", proposed);
        }
        return text(await orion(args));
      } catch (e) {
        return fail(e);
      } finally {
        if (dir) await fs.rm(dir, { recursive: true, force: true });
      }
    }
  );

  server.registerTool(
    "approve_edit",
    {
      description:
        "Human-approved only: commits the proposed edit to the real XMP sidecar and deletes " +
        "the proposal. Cheap: returns the sidecar path, about 20 tokens. Never call this without " +
        "the user's explicit approval of the proposed edit. path is optional; defaults to the " +
        "photo open in Orion.",
      inputSchema: { path: z.string().optional() },
    },
    async ({ path: raw }) => {
      const p = await resolve(raw, "photo");
      if (!p) return noPhoto();
      const proposed = proposedPath(p);
      try {
        const result = await orion(["commit", p, "--state", proposed]);
        await fs.rm(proposed, { force: true });
        return text(result);
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "reject_edit",
    {
      description:
        "Deletes the proposed edit file without touching the sidecar. Cheap: returns " +
        "{deleted}, about 10 tokens. Safe to call even when there is no proposal. path is " +
        "optional; defaults to the photo open in Orion.",
      inputSchema: { path: z.string().optional() },
    },
    async ({ path: raw }) => {
      const p = await resolve(raw, "photo");
      if (!p) return noPhoto();
      const proposed = proposedPath(p);
      const deleted = await fs.access(proposed).then(() => true, () => false);
      try {
        if (deleted) await fs.rm(proposed);
        return text({ deleted });
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "set_flag",
    {
      description:
        "Sets rating (0-5) and/or reject on the real sidecar directly — this is not a proposal, " +
        "it writes immediately. Cheap: returns {rating, rejected}, about 20 tokens. path is " +
        "optional; defaults to the photo open in Orion.",
      inputSchema: { path: z.string().optional(), rating: z.number().int().min(0).max(5).optional(), reject: z.boolean().optional() },
    },
    async ({ path: raw, rating, reject }) => {
      const p = await resolve(raw, "photo");
      if (!p) return noPhoto();
      try {
        const args = ["flag", p];
        if (rating !== undefined) args.push("--rating", String(rating));
        if (reject !== undefined) args.push("--reject", reject ? "1" : "0");
        return text(await orion(args));
      } catch (e) {
        return fail(e);
      }
    }
  );

  return server;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await createServer().connect(new StdioServerTransport());
}
