"""Encode deterministic Prism-5 PNG frames into a portfolio preview MP4."""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", type=Path, help="Directory containing frame_####.png")
    parser.add_argument("output", type=Path, help="Output MP4 path")
    parser.add_argument("--fps", type=float, default=24.0)
    args = parser.parse_args()

    frame_paths = sorted(args.frames.glob("frame_*.png"))
    if not frame_paths:
        raise SystemExit(f"No Prism reel frames found in {args.frames}")

    first = cv2.imread(str(frame_paths[0]), cv2.IMREAD_COLOR)
    if first is None:
        raise SystemExit(f"Cannot decode {frame_paths[0]}")
    height, width = first.shape[:2]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    writer = cv2.VideoWriter(
        str(args.output),
        cv2.VideoWriter_fourcc(*"mp4v"),
        args.fps,
        (width, height),
    )
    if not writer.isOpened():
        raise SystemExit("OpenCV could not initialize the mp4v video encoder")

    try:
        for frame_path in frame_paths:
            frame = cv2.imread(str(frame_path), cv2.IMREAD_COLOR)
            if frame is None or frame.shape[:2] != (height, width):
                raise SystemExit(f"Invalid or mismatched frame: {frame_path}")
            writer.write(frame)
    finally:
        writer.release()

    duration = len(frame_paths) / args.fps
    print(
        f"Encoded {len(frame_paths)} frames at {args.fps:g} fps "
        f"({duration:.1f} s): {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
