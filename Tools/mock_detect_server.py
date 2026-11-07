"""Local upload fixture. Only POST /inspections/3/detect; never the real API.

python Tools/mock_detect_server.py --port 18080 --output Tools/UploadEvidence/run
The upload-stop request ends this fixture after responding (for status-0 testing).
"""
import argparse
import json
import re
import threading
import time
from email.parser import BytesParser
from email.policy import default
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            try:
                assert self.path == "/inspections/3/detect", self.path
                size = int(self.headers["Content-Length"])
                assert 0 < size < 16 * 1024 * 1024, size
                body = self.rfile.read(size)
                message = BytesParser(policy=default).parsebytes(
                    ("Content-Type: " + self.headers["Content-Type"] + "\r\nMIME-Version: 1.0\r\n\r\n").encode() + body
                )
                parts = {part.get_param("name", header="content-disposition"): part for part in message.iter_parts()}
                assert set(parts) == {"meta", "image"}, list(parts)
                meta_bytes = parts["meta"].get_payload(decode=True)
                meta = json.loads(meta_bytes)
                capture_id = meta["id"]
                assert re.fullmatch(r"[A-Za-z0-9_-]{1,80}", capture_id), capture_id
                assert meta["type"] == "capture_result" and meta["hitOk"] is True, meta
                assert meta["structureId"] == 1 and meta["distanceCm"] > 0, meta
                assert parts["image"].get_content_type() == "image/jpeg"
                image_path = args.output / (capture_id + "_orig.jpg")
                image_path.write_bytes(parts["image"].get_payload(decode=True))
                with Image.open(image_path) as image:
                    assert image.format == "JPEG" and image.size == (1920, 1080), (image.format, image.size)
                    image.verify()
                (args.output / (capture_id + "_meta.json")).write_bytes(meta_bytes)
                (args.output / (capture_id + "_multipart.bin")).write_bytes(body)
                time.sleep(0.8)  # allow a second request while HTTP is in progress
                status = 422 if capture_id == "upload-error" else 200
                response = ('{"detail":"검증 \\"오류\\"\\n테스트"}' if status == 422 else
                            ' {"type": "detect_result", "inspectionId": 3, "captureId": "' + capture_id + '", "cracks": []}\n')
                data = response.encode("utf-8")
                (args.output / (capture_id + "_response.txt")).write_bytes(data)
                self.send_response(status)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                if capture_id == "upload-truncated":
                    # Claim a full body, then close before sending it.
                    self.wfile.write(data[:8])
                    self.wfile.flush()
                    self.close_connection = True
                    print("TRUNCATED upload-truncated: HTTP 200 headers, incomplete body", flush=True)
                    return
                self.wfile.write(data)
                print(f"VALIDATED {capture_id}: JPEG 1920x1080, meta intact, HTTP {status}", flush=True)
                if capture_id == "upload-stop":
                    threading.Thread(target=self.server.shutdown, daemon=True).start()
            except Exception as error:
                (args.output / "failure.txt").write_text(repr(error), encoding="utf-8")
                self.send_error(422, "Fixture validation failed")
                print(f"FAILED: {error!r}", flush=True)

    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        print(f"LISTENING 127.0.0.1:{args.port}", flush=True)
        server.serve_forever(poll_interval=0.05)
    print("STOPPED", flush=True)


if __name__ == "__main__":
    main()
