#!/usr/bin/env python3
"""極簡 x64dbg MCP client（給 WorkBuddy 透過 HTTP 直接驅動 x32dbg/x64dbg 用）"""
import json
import sys
import urllib.request

URL = "http://127.0.0.1:3000/mcp"


def call(method, params=None, _id=1):
    body = json.dumps({"jsonrpc": "2.0", "id": _id, "method": method,
                       "params": params or {}}).encode("utf-8")
    req = urllib.request.Request(URL, data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json, text/event-stream")
    with urllib.request.urlopen(req, timeout=30) as resp:
        raw = resp.read().decode("utf-8", "replace")
    # 可能是 SSE（data: {...}）或純 JSON
    if raw.lstrip().startswith("event:") or raw.lstrip().startswith("data:"):
        for line in raw.splitlines():
            if line.startswith("data:"):
                raw = line[5:].strip()
                break
    return json.loads(raw)


def tool(name, args=None, _id=1):
    r = call("tools/call", {"name": name, "arguments": args or {}}, _id)
    if "error" in r:
        return {"error": r["error"]}
    res = r.get("result", {})
    # MCP 回傳 content[] 陣列，多為 text（JSON 字串）
    out = []
    for c in res.get("content", []):
        if c.get("type") == "text":
            t = c.get("text", "")
            try:
                out.append(json.loads(t))
            except Exception:
                out.append(t)
        else:
            out.append(c)
    return out[0] if len(out) == 1 else out


if __name__ == "__main__":
    name = sys.argv[1]
    args = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(tool(name, args), ensure_ascii=False, indent=2))
