"""Encode a completed SequencesFixed_* render directory to MP4.

Usage: python Tools/encode_sequence_videos.py Exports/SequencesFixed_<timestamp>
Uses the locally installed OpenCV MPEG-4 encoder; no downloads are required.
"""
import argparse
import json
from pathlib import Path
import re

import cv2


def encode(root):
    report = json.loads((root / "render_status.json").read_text(encoding="utf-8"))
    if report["status"] != "frames_rendered":
        raise RuntimeError("Render has not completed: " + report["status"])
    result = {"status": "encoding", "codec": "mp4v", "jobs": []}
    size = tuple(report["resolution"])
    for job in report["jobs"]:
        name = job["name"]
        numbered = []
        for path in (root / name).glob("*.png"):
            match = re.fullmatch(re.escape(name) + r"_(\d+)FinalImage\.png", path.name)
            if match:
                numbered.append((int(match[1]), path))
        numbered.sort()
        expected = job["end"] - job["start"]
        if len(numbered) != expected or [n for n, _ in numbered] != list(range(expected)):
            raise RuntimeError(f"{name}: expected contiguous {expected} frames; got {len(numbered)}")
        fps = job["fps"][0] / job["fps"][1]
        target = root / (name + ".mp4")
        partial = root / (name + ".partial.mp4")
        if target.exists() or partial.exists():
            raise FileExistsError("Refusing to overwrite existing video: " + str(target))
        writer = cv2.VideoWriter(str(partial), cv2.VideoWriter_fourcc(*"mp4v"), fps, size)
        if not writer.isOpened():
            raise RuntimeError("OpenCV MP4 encoder could not open")
        try:
            for _, path in numbered:
                frame = cv2.imread(str(path))
                if frame is None or (frame.shape[1], frame.shape[0]) != size:
                    raise RuntimeError("Unreadable or wrong-sized PNG: " + str(path))
                writer.write(frame)
        finally:
            writer.release()
        reader = cv2.VideoCapture(str(partial))
        decoded = 0
        try:
            while True:
                ok, frame = reader.read()
                if not ok:
                    break
                if (frame.shape[1], frame.shape[0]) != size:
                    raise RuntimeError("Decoded video resolution differs")
                decoded += 1
        finally:
            reader.release()
        if decoded != expected:
            raise RuntimeError(f"{name}: decoded {decoded}, expected {expected}")
        partial.rename(target)
        result["jobs"].append({"name": name, "file": str(target.resolve()),
                               "frames": decoded, "fps": fps,
                               "seconds": decoded / fps, "bytes": target.stat().st_size})
        print(f"Verified {name}: {decoded} frames at {fps} fps", flush=True)
        (root / "video_status.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    result["status"] = "encoded_and_decoded"
    (root / "video_status.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    encode(parser.parse_args().directory)
