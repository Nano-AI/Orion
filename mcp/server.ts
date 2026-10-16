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

// `region: [x, y, w, h]`, display-space fractions from the top-left corner.
// An array rather than an object so a model writes four numbers in the order
// it already reads them in every other tool's output; the binary refuses one
// outside 0..1 or with no area (exit 2), so there is no clamping here either.
const REGION = z
  .array(z.number())
  .length(4)
  .describe("[x, y, w, h] as fractions of the displayed picture, from its top-left corner");
const regionArg = (r: number[] | undefined) => (r ? ["--region", r.join(",")] : []);

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

// The product's six preset groups plus `masks` (maskComponents + layers),
// the vocabulary `Orion --agent sync --groups` takes.
const SYNC_GROUPS = ["whiteBalance", "light", "color", "curve", "detail", "effects", "masks"] as const;

const ORION_INSTRUCTIONS = `Orion is a RAW photo editor and this server is its agent surface. The photographer is looking at one photo in Orion; when they say 'this image', 'this photo', 'the current one' or give no path, that photo is the one \`current_photo\` returns, and every tool's \`path\` defaults to it, so never ask which file. Start any edit with \`describe_edits\` and \`get_stats\`; \`get_edits\` reads the current or proposed settings without writing anything. All values are absolute, never deltas. Local adjustments run three to four times a global's at the top of the scale: a blown sky near luma 0.98 barely moves under a small masked exposure pull, so probe strong and ease off. Numbers before pixels: \`get_stats\` with \`region\` reads a patch (luma, clippedHigh, shading, noise) for a fraction of a proxy's cost, and \`detect_faces\` places a radial mask on a portrait. Judge sharpening, skin and mask edges with \`get_proxy\`'s \`region\`, never a whole 2048 px frame. \`detect_subject\` lifts the subject, a person or the sky into a matte for a kind-4 mask row, and \`sync_edits\` carries one approved photo's look onto the rest of a shoot as proposals. Read \`capture.iso\` from \`get_stats\` or \`current_photo\` before deciding how hard to denoise. Proposals appear live in Orion's compare view; the photographer approves or rejects there, so after \`propose_edit\` say what you changed and stop; do not call \`approve_edit\` unless asked.`;

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
        "updated, capture}. capture is what the camera recorded - {camera, lens, focalLength " +
        "(mm), aperture (f-number), shutter (seconds, 0.004 = 1/250), iso, captured (ISO " +
        "8601)} - with any field the file does not record left out. Call this to find out what the user has open before asking them for a path " +
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
        "look interesting (blown highlights, empty shadows, an unrated keeper). state: " +
        "'proposed' reads the numbers the PROPOSED edit would produce instead of the current " +
        "ones, so 'did that pull actually work' is a number rather than a picture - read it " +
        "before spending a proxy on the answer. region: [x, y, w, h] in display space adds a " +
        "`region` object measuring just that patch: luma, saturation, hue (degrees, 0 red / " +
        "120 green / 240 blue) with hueStrength (how much the patch agrees about it, 0..1), " +
        "red/green/blue means, clippedHigh and clippedLow (the fraction of pixels at 254/255 " +
        "and at 1/255 - a sky above about 0.01 clippedHigh is gone, not recoverable), and " +
        "shading (the standard deviation over the mean of block means: how much modelling the " +
        "patch has. Measure a cheek before and after and keep it within about 15% of as shot " +
        "when you pull highlights or lift shadows over a face - that is the number that says " +
        "whether a face went flat), and noise (fine grain: the RMS of luma against its own " +
        "3x3 mean, in luma's units. Read it before and after on the SAME flat patch, never " +
        "across patches - texture reads as noise too. shading is the number a face keeps, " +
        "noise is the number a wall loses). The region object also carries `frame`: {centerX, " +
        "centerY, radiusX, radiusY}, the same rectangle in FRAME space (where masks live) as " +
        "the ellipse covering it - paste it straight into a kind-2 radial, or aim a kind-1 " +
        "gradient's centerX/centerY at it; never convert a display region to a mask by hand. " +
        "The answer also carries `capture`: {camera, lens, focalLength (mm), aperture " +
        "(f-number), shutter (seconds, 0.004 = 1/250), iso, captured (ISO 8601)}, with any " +
        "field the file does not record left out - read iso before choosing denoise strength. " +
        "path is optional; defaults to the photo open in Orion.",
      inputSchema: {
        path: z.string().optional(),
        state: z.enum(["current", "proposed"]).default("current"),
        region: REGION.optional(),
      },
    },
    async ({ path: raw, state, region }) => {
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        const args = ["stats", p, ...regionArg(region)];
        const proposed = proposedPath(p);
        if (state === "proposed" && (await fs.access(proposed).then(() => true, () => false))) {
          args.push("--state", proposed);
        }
        return text(await orion(args));
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
        "call this when you need to actually see the photo. maxPx is capped at 2048, which on " +
        "a 42 MP frame is a twentieth of its width: sharpening, skin texture, fringing and the " +
        "edge of a mask are all finer than that, so a whole-frame proxy CANNOT show them and " +
        "reading one as 'the sharpening looks fine' is reading a picture that does not contain " +
        "the answer. region: [x, y, w, h] in display space is how to see them - it renders at " +
        "full resolution, crops to that rectangle and only then scales down if the crop's long " +
        "edge is over maxPx, so a small region comes back at native pixels. Use it on any mask " +
        "edge you have proposed and on skin before approving. path is optional; defaults to " +
        "the photo open in Orion.",
      inputSchema: {
        path: z.string().optional(),
        maxPx: z.number().int().min(64).max(2048).default(1024),
        state: z.enum(["current", "proposed"]).default("current"),
        region: REGION.optional(),
      },
    },
    async ({ path: raw, maxPx, state, region }) => {
      let dir: string | undefined;
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        dir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-"));
        const out = path.join(dir, "proxy.jpg");
        const args = ["proxy", p, "--max", String(maxPx), "--out", out, ...regionArg(region)];
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
    "get_edits",
    {
      description:
        "Cheap and read-only: the develop settings as JSON, about 40-100 tokens, and it never " +
        "writes anything - no proposed file, no sidecar. The way to see the current settings " +
        "(state: 'current', the default: the sidecar's, or as shot when there is none) or the " +
        "proposal so far (state: 'proposed'; falls back to current when there is no proposal). " +
        "Returns {path, source: 'proposed' | 'sidecar' | 'as-shot', state, hasProposed}; " +
        "`state` is the same full object propose_edit returns, so composite fields (curve, " +
        "layers, maskComponents, spots, the grade wheels) come back whole for read-modify-" +
        "write. Do not send an empty propose_edit to read settings - that writes a proposal. " +
        "path is optional; defaults to the photo open in Orion.",
      inputSchema: { path: z.string().optional(), state: z.enum(["current", "proposed"]).default("current") },
    },
    async ({ path: raw, state }) => {
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        const args = ["state", p];
        const proposed = proposedPath(p);
        const hasProposed = await fs.access(proposed).then(() => true, () => false);
        if (state === "proposed" && hasProposed) {
          args.push("--state", proposed);
        }
        return text({ ...(await orion(args)), hasProposed });
      } catch (e) {
        return fail(e);
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
    "detect_faces",
    {
      description:
        "Vision's face rectangles on the developed photo, about 30 tokens a face and far " +
        "cheaper than looking. Each face carries the display box (x, y, w, h, the same space " +
        "get_proxy and get_stats take a region in) AND centerX/centerY/radiusX/radiusY in " +
        "FRAME space, which drop straight into a kind-2 radial in maskComponents with no " +
        "conversion - those are two different spaces and a turned frame is not the identity " +
        "between them, so never paste a display number into a mask. Call this FIRST on a " +
        "portrait: it is how you put a mask on the face rather than guessing at the middle of " +
        "the frame. Returns {faces: []} when there is no face, which is not an error. ⚠ Vision " +
        "jitters by about 0.01 between calls on the same photograph, so do not read these to " +
        "more than two decimals or expect two calls to agree exactly. path is optional; " +
        "defaults to the photo open in Orion.",
      inputSchema: { path: z.string().optional(), state: z.enum(["current", "proposed"]).default("current") },
    },
    async ({ path: raw, state }) => {
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        const args = ["faces", p];
        const proposed = proposedPath(p);
        if (state === "proposed" && (await fs.access(proposed).then(() => true, () => false))) {
          args.push("--state", proposed);
        }
        return text(await orion(args));
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "detect_subject",
    {
      description:
        "The panel's Subject / Person / Sky button: runs the same Vision model on the " +
        "developed photo, writes the matte as a PNG beside it and returns a ready mask row, " +
        "about 60 tokens plus one Vision inference (a second or two). kind 'subject' is " +
        "class-agnostic foreground lifting (whatever stands out: a climber, a car, a dog), " +
        "'person' is the person matte, 'sky' is the sky. The answer carries `component`, a " +
        "COMPLETE kind-4 maskComponents row: APPEND it to maskComponents in propose_edit - " +
        "read the current list with get_edits first and send the whole array back - together " +
        "with a matching entry appended to `layers`, because its startsLayer: true begins a " +
        "new layer. To subtract it from the previous layer instead ('everything but the " +
        "climber'), set startsLayer: false and compose: 1 and add no layer. found: false is a " +
        "real answer (the model found nothing to select), not an error. The matte is a PNG " +
        "beside the photo, kept as long as a proposal, sidecar or version names it. coverage " +
        "is the fraction of the frame selected: near 0 or near 1 means the model grabbed the " +
        "wrong thing, so check an edge with get_proxy's region before approving. A local " +
        "adjustment through a matte runs three to four times a global's at the top of the " +
        "scale, so probe strong and ease off with get_stats state: 'proposed'. state: " +
        "'proposed' runs the model on the proposal instead of the current settings. path is " +
        "optional; defaults to the photo open in Orion.",
      inputSchema: {
        path: z.string().optional(),
        kind: z.enum(["subject", "person", "sky"]).default("subject"),
        state: z.enum(["current", "proposed"]).default("current"),
      },
    },
    async ({ path: raw, kind, state }) => {
      try {
        const p = await resolve(raw, "photo");
        if (!p) return noPhoto();
        const args = ["matte", p, "--kind", kind];
        const proposed = proposedPath(p);
        if (state === "proposed" && (await fs.access(proposed).then(() => true, () => false))) {
          args.push("--state", proposed);
        }
        return text(await orion(args));
      } catch (e) {
        return fail(e);
      }
    }
  );

  server.registerTool(
    "sync_edits",
    {
      description:
        "Carries one photograph's look onto others, the product's Sync. The workflow for a " +
        "shoot is: edit one photograph with the photographer until they approve it, then " +
        "sync_edits from it to the rest - one look across a set of frames of the same event " +
        "and light, like a climbing competition shot from one spot. list_folder is how to get " +
        "the target paths. groups default to light, color, curve and effects, which carry the " +
        "look and leave whiteBalance and detail alone (those are properties of the camera " +
        "and ISO, frame by frame); name them to include them. masks carries the layers: " +
        "every Subject / Person / Sky matte row is re-detected on each target, while " +
        "gradients and radials copy by position in frame space, so check one or two targets " +
        "with get_stats' region or get_proxy before approving them. By default each target " +
        "gets a proposal, which REPLACES any proposal already there (nothing accumulates); " +
        "the photographer approves each in Orion or asks you to approve_edit it by path. " +
        "commit: true writes the sidecars immediately and cannot be undone, so never pass it " +
        "without the photographer's explicit say-so; the photo open in Orion still gets a " +
        "proposal (named in openPhotoProposed), because Orion would overwrite a sidecar " +
        "written behind its back. sourceState: 'proposed' syncs the source's proposal " +
        "instead of its current settings. A target that fails comes back in `results` with " +
        "an `error` and does not fail the call. About 30 tokens per target, plus a decode " +
        "per target that has no sidecar, plus an inference per matte row per target. source " +
        "is optional; defaults to the photo open in Orion.",
      inputSchema: {
        source: z.string().optional(),
        targets: z.array(z.string()).min(1),
        groups: z
          .array(z.enum(SYNC_GROUPS))
          .min(1)
          .default(["light", "color", "curve", "effects"]),
        sourceState: z.enum(["current", "proposed"]).default("current"),
        commit: z.boolean().default(false),
      },
    },
    async ({ source: given, targets, groups, sourceState, commit }) => {
      let dir: string | undefined;
      try {
        const src = await resolve(given, "photo");
        if (!src) return noPhoto();
        dir = await fs.mkdtemp(path.join(os.tmpdir(), "orion-"));
        const base = ["sync", src, "--groups", groups.join(",")];
        const proposed = proposedPath(src);
        if (sourceState === "proposed" && (await fs.access(proposed).then(() => true, () => false))) {
          base.push("--state", proposed);
        }
        const run = async (list: string[], mode: "propose" | "commit") => {
          const file = path.join(dir!, `targets-${mode}.json`);
          await fs.writeFile(file, JSON.stringify(list));
          return orion([...base, "--targets", file, "--mode", mode]);
        };

        // The open photo rule: Orion holds that photo's state in memory and
        // its autosave would overwrite a sidecar written behind its back, so
        // under commit it gets a proposal, which shows up live in compare.
        const open = commit ? (await readCurrent())?.photo ?? null : null;
        const isOpen = (t: string) =>
          open !== null && path.resolve(t) === path.resolve(open) && path.resolve(t) !== path.resolve(src);
        const openTargets = targets.filter(isOpen);
        if (!commit) return text(await run(targets, "propose"));
        if (openTargets.length === 0) return text(await run(targets, "commit"));

        const others = targets.filter((t) => !isOpen(t));
        const live = await run(openTargets, "propose");
        if (others.length === 0) return text({ ...live, mode: "commit", openPhotoProposed: openTargets[0] });
        const committed = await run(others, "commit");
        const order = (r: { path?: string }) => {
          const i = targets.indexOf(r.path ?? "");
          return i === -1 ? targets.length : i;
        };
        const results = [...committed.results, ...live.results].sort((a, b) => order(a) - order(b));
        return text({
          ...committed,
          results,
          written: committed.written + live.written,
          failed: committed.failed + live.failed,
          skipped: committed.skipped + live.skipped,
          openPhotoProposed: openTargets[0],
        });
      } catch (e) {
        return fail(e);
      } finally {
        if (dir) await fs.rm(dir, { recursive: true, force: true });
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
