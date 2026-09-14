# Orion agent MCP server (POC)

Thin stdio MCP shell over `Orion --agent <verb>`. No XMP, no Metal, no state
of its own beyond a `.proposed.json` file beside each RAW. See
`docs/superpowers/specs/2026-09-13-agent-mcp-design.md`.

## Run

```bash
cd mcp && npm install
ORION_BIN=/path/to/Orion.app/Contents/MacOS/Orion node server.ts
```

`ORION_BIN` defaults to `<repo>/build/Orion.app/Contents/MacOS/Orion`.

## Test

```bash
cd mcp && npm test        # node --test against test/fake-orion.sh, no GPU needed
```

## Claude Code `.mcp.json`

```json
{ "mcpServers": { "orion": { "command": "node", "args": ["/ABS/PATH/Orion/mcp/server.ts"] } } }
```

Approve/deny for every edit happens in the MCP client's chat — this server
never writes a sidecar itself; only `approve_edit` does, and only on request.
