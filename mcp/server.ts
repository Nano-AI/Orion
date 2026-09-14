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

const RAW_EXT = new Set([".arw", ".nef", ".cr2", ".cr3", ".dng", ".raf", ".orf", ".rw2"]);
const bin = () =>
  process.env.ORION_BIN ?? path.resolve(import.meta.dirname, "../build/Orion.app/Contents/MacOS/Orion");
const proposedPath = (raw: string) => raw.replace(/\.[^.]+$/, ".proposed.json");

// One execFile of the binary; stdout is one JSON object per the spec's verb
// table. A non-zero exit throws with the stderr line (or the exit code) so
// tool handlers can turn it into an MCP tool error.
async function orion(args: string[]): Promise<any> {
  const stdout = await new Promise<string>((resolve, reject) => {
    execFile(bin(), ["--agent", ...args], (err, stdout, stderr) => {
      if (err) {
        reject(new Error(stderr.trim() || err.message));
      } else {
        resolve(stdout);
      }
    });
  });
  return JSON.parse(stdout);
}

const fail = (e: unknown) => ({ content: [{ type: "text" as const, text: String((e as Error).message) }], isError: true });
const text = (v: unknown) => ({ content: [{ type: "text" as const, text: JSON.stringify(v) }] });

export function createServer() {
  const server = new McpServer({ name: "orion", version: "0.1.0" });

  server.registerTool(
    "list_folder",
    {
      description:
        "Lists RAW files in a folder (~10-40 tokens per file). Call this first to see what's " +
        "in a shoot before spending tokens on get_stats or get_proxy for individual photos.",
      inputSchema: { folder: z.string() },
    },
    async ({ folder }) => {
      try {
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
        "look interesting (blown highlights, empty shadows, an unrated keeper).",
      inputSchema: { path: z.string() },
    },
    async ({ path: raw }) => {
      try {
        return text(await orion(["stats", raw]));
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
        "call this when you need to actually see the photo.",
      inputSchema: {
        path: z.string(),
        maxPx: z.number().int().positive().max(2048).default(1024),
        state: z.enum(["current", "proposed"]).default("current"),
      },
    },
    async ({ path: raw, maxPx, state }) => {
      const dir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-"));
      const out = path.join(dir, "proxy.jpg");
      try {
        const args = ["proxy", raw, "--max", String(Math.min(maxPx, 2048)), "--out", out];
        const proposed = proposedPath(raw);
        if (state === "proposed" && (await fs.access(proposed).then(() => true, () => false))) {
          args.push("--state", proposed);
        }
        await orion(args);
        const data = await fs.readFile(out);
        return { content: [{ type: "image" as const, data: data.toString("base64"), mimeType: "image/jpeg" }] };
      } catch (e) {
        return fail(e);
      } finally {
        await fs.rm(dir, { recursive: true, force: true });
      }
    }
  );

  server.registerTool(
    "propose_edit",
    {
      description:
        "Writes a proposed edit beside the RAW (not the sidecar — nothing here touches the " +
        "photographer's XMP). Merges onto any existing proposal. Cheap: returns the merged " +
        "state as JSON, about 40-100 tokens depending on the edit count.",
      inputSchema: { path: z.string(), edits: z.record(z.string(), z.unknown()) },
    },
    async ({ path: raw, edits }) => {
      const dir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-"));
      const editsFile = path.join(dir, "edits.json");
      try {
        await fs.writeFile(editsFile, JSON.stringify(edits));
        const proposed = proposedPath(raw);
        const args = ["apply", raw, "--edits", editsFile, "--out", proposed];
        if (await fs.access(proposed).then(() => true, () => false)) {
          args.push("--state", proposed);
        }
        return text(await orion(args));
      } catch (e) {
        return fail(e);
      } finally {
        await fs.rm(dir, { recursive: true, force: true });
      }
    }
  );

  server.registerTool(
    "approve_edit",
    {
      description:
        "Human-approved only: commits the proposed edit to the real XMP sidecar and deletes " +
        "the proposal. Cheap: returns the sidecar path, about 20 tokens. Never call this without " +
        "the user's explicit approval of the proposed edit.",
      inputSchema: { path: z.string() },
    },
    async ({ path: raw }) => {
      const proposed = proposedPath(raw);
      try {
        const result = await orion(["commit", raw, "--state", proposed]);
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
        "{deleted}, about 10 tokens. Safe to call even when there is no proposal.",
      inputSchema: { path: z.string() },
    },
    async ({ path: raw }) => {
      const proposed = proposedPath(raw);
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
        "it writes immediately. Cheap: returns {rating, rejected}, about 20 tokens.",
      inputSchema: { path: z.string(), rating: z.number().int().min(0).max(5).optional(), reject: z.boolean().optional() },
    },
    async ({ path: raw, rating, reject }) => {
      try {
        const args = ["flag", raw];
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

if (process.argv[1] === new URL(import.meta.url).pathname) {
  await createServer().connect(new StdioServerTransport());
}
