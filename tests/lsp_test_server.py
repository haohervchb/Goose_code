#!/usr/bin/env python3
import json
import sys


def read_message():
    content_length = None
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        if line.lower().startswith(b"content-length:"):
            content_length = int(line.split(b":", 1)[1].strip())
    if content_length is None:
        return None
    body = sys.stdin.buffer.read(content_length)
    if not body:
        return None
    return json.loads(body.decode("utf-8"))


def send_message(payload):
    body = json.dumps(payload).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(body))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


while True:
    msg = read_message()
    if msg is None:
        break

    method = msg.get("method")
    msg_id = msg.get("id")

    if method == "initialize":
        send_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "capabilities": {
                        "hoverProvider": True,
                        "definitionProvider": True,
                        "documentSymbolProvider": True,
                    }
                },
            }
        )
    elif method == "initialized":
        continue
    elif method == "textDocument/didOpen":
        continue
    elif method == "textDocument/hover":
        send_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "contents": {"kind": "markdown", "value": "hover information"}
                },
            }
        )
    elif method == "textDocument/definition":
        send_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": [
                    {
                        "uri": "file:///tmp/example.rs",
                        "range": {
                            "start": {"line": 0, "character": 3},
                            "end": {"line": 0, "character": 6},
                        },
                    }
                ],
            }
        )
    elif method == "textDocument/documentSymbol":
        send_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": [
                    {
                        "name": "main",
                        "kind": 12,
                        "range": {
                            "start": {"line": 0, "character": 0},
                            "end": {"line": 2, "character": 1},
                        },
                        "selectionRange": {
                            "start": {"line": 0, "character": 3},
                            "end": {"line": 0, "character": 7},
                        },
                    }
                ],
            }
        )
    else:
        send_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32601, "message": f"Unknown method: {method}"},
            }
        )
