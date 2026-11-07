"""Small localhost client for the UE editor's MCP discovery tools."""
import json
import sys
import urllib.request

URL = "http://127.0.0.1:18000/mcp"


def request(method, params, session=None, request_id=1):
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if session:
        headers["Mcp-Session-Id"] = session
    body = {"jsonrpc": "2.0", "method": method, "params": params, "id": request_id}
    req = urllib.request.Request(URL, json.dumps(body).encode(), headers)
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.load(response), response.headers.get("Mcp-Session-Id")


if __name__ == "__main__":
    _, session = request("initialize", {
        "protocolVersion": "2025-11-25", "capabilities": {},
        "clientInfo": {"name": "codex-cracktwin", "version": "1.0"}})
    result, _ = request("tools/call", {
        "name": sys.argv[1], "arguments": json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    }, session, 2)
    print(json.dumps(result, ensure_ascii=False, indent=2))
