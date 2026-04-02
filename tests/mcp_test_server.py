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
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"resources": {}},
                    "serverInfo": {"name": "test-mcp", "version": "1.0"},
                },
            }
        )
    elif method == "notifications/initialized":
        continue
    elif method == "resources/list":
        send_message(
            {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "resources": [
                        {
                            "uri": "memo://alpha",
                            "name": "Alpha memo",
                            "mimeType": "text/plain",
                        }
                    ]
                },
            }
        )
    elif method == "resources/read":
        uri = (msg.get("params") or {}).get("uri")
        if uri == "memo://alpha":
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "contents": [
                            {
                                "uri": "memo://alpha",
                                "mimeType": "text/plain",
                                "text": "alpha contents",
                            }
                        ]
                    },
                }
            )
        else:
            send_message(
                {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "error": {"code": -32001, "message": "Resource not found"},
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
