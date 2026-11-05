#!/usr/bin/env node
// Stdio MCP server that exposes the agent bridge in the custom Bambu Studio build. Holds no state:
// each call re-reads the discovery file, so Studio can start and stop while the client stays connected.
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { CallToolRequestSchema, ListToolsRequestSchema } from '@modelcontextprotocol/sdk/types.js';

import { BridgeCallError, callBridgeTool } from './bridge-client.js';
import { TOOLS } from './tool-definitions.js';

const server = new Server({ name: 'bambu-studio', version: '0.1.0' }, { capabilities: { tools: {} } });

server.setRequestHandler(ListToolsRequestSchema, async () => ({ tools: TOOLS }));

server.setRequestHandler(CallToolRequestSchema, async ({ params }) => {
  try {
    const result = await callBridgeTool(process.env, params.name, params.arguments);
    // Compact JSON: agents read it fine, and indentation would only cost tokens.
    return { content: [{ type: 'text', text: JSON.stringify(result) }] };
  } catch (error) {
    const code = error instanceof BridgeCallError ? error.code : 'adapter_error';
    return { isError: true, content: [{ type: 'text', text: `${code}: ${error.message}` }] };
  }
});

await server.connect(new StdioServerTransport());
