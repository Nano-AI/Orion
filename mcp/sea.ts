// Entry point of the packaged `orion-mcp`, a Node single-executable application
// (tools/package-app.sh, #310): esbuild bundles this and server.ts into one
// CommonJS file, which is injected into the official Node binary. `node
// server.ts` never runs this file.
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { createServer } from "./server.ts";

createServer().connect(new StdioServerTransport());
