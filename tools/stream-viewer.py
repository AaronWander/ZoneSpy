#!/usr/bin/env python3
"""
ZoneSpy Stream Viewer - display all live feeds in one window.
"""

import json, os, struct, sys, time
import numpy as np
import mmap
import win32event
import cv2

SHM_SIZE = 20 + 3840 * 2160 * 4  # 33,177,620 bytes (matches C++ allocation)
DISPLAY_WIDTH = 960


def load_config():
    path = os.path.join(os.environ.get("LOCALAPPDATA", ""), "ZoneSpy", "streams.json")
    if not os.path.exists(path):
        print(f"Config not found: {path}")
        print("Is ZoneSpy running with at least one cropped window?")
        return None
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def open_streams(streams):
    """Open shared memory and events for each stream. Return list of (id, name, buf, evt)."""
    result = []
    for s in streams:
        sid = s["id"]
        name = s.get("name", f"Stream {sid}")
        try:
            buf = mmap.mmap(-1, SHM_SIZE, tagname=s["shm"])
        except Exception as e:
            print(f"  [{sid}] {name} - shm error: {e}")
            continue
        try:
            evt = win32event.OpenEvent(win32event.SYNCHRONIZE, False, s["evt"])
        except Exception as e:
            print(f"  [{sid}] {name} - event error: {e}")
            evt = None
        result.append((sid, name, buf, evt))
    return result


def read_frame(buf):
    """Read one frame from shared memory. Return (frame, timestamp) or (None, 0)."""
    try:
        buf.seek(0)
        fc = struct.unpack("I", buf.read(4))[0]
        ts = struct.unpack("Q", buf.read(8))[0]
        w = struct.unpack("I", buf.read(4))[0]
        h = struct.unpack("I", buf.read(4))[0]
        if not (0 < w <= 3840 and 0 < h <= 2160):
            return None, 0
        pixels = np.frombuffer(buf.read(w * h * 4), dtype=np.uint8).reshape(h, w, 4)
        return pixels, ts
    except Exception:
        return None, 0


def main():
    config = load_config()
    if config is None:
        return 1

    streams = config.get("streams", [])
    if not streams:
        print("No streams in config. Create a cropped window first.")
        return 1

    print(f"Connecting to {len(streams)} stream(s)...")
    opened = open_streams(streams)
    if not opened:
        print("Could not open any streams.")
        return 1

    print(f"Opened {len(opened)} stream(s). Press 'q' to quit, 'f' to toggle size.")
    frames = [None] * len(opened)
    last_frame = [-1] * len(opened)
    display_w = DISPLAY_WIDTH

    while True:
        # Wait for any event (100ms timeout = ~10fps polling fallback)
        handles = [e for _, _, _, e in opened if e is not None]
        if len(handles) == len(opened):
            win32event.WaitForMultipleObjects(handles, False, 100)

        # Read all streams
        any_new = False
        for i, (sid, name, buf, evt) in enumerate(opened):
            frame, ts = read_frame(buf)
            if frame is not None:
                frames[i] = frame
                any_new = True

        if not any_new:
            continue

        # Build display: resize all to common width, stack vertically
        rows = []
        valid_pairs = [(f, n) for f, (_, n, _, _) in zip(frames, opened) if f is not None]
        for frame, name in valid_pairs:
            h, w = frame.shape[:2]
            scale = display_w / w
            new_h = int(h * scale)
            resized = cv2.resize(frame, (display_w, new_h))
            bgr = cv2.cvtColor(resized, cv2.COLOR_BGRA2BGR)
            # Add label
            cv2.putText(bgr, name, (8, 24), cv2.FONT_HERSHEY_SIMPLEX,
                        0.6, (0, 255, 0), 2)
            rows.append(bgr)

        if not rows:
            continue

        display = np.vstack(rows)
        cv2.imshow("ZoneSpy Stream Viewer", display)

        key = cv2.waitKey(1) & 0xFF
        if key == ord("q"):
            break
        elif key == ord("f"):
            display_w = 1920 if display_w == 960 else 960

    for _, _, buf, _ in opened:
        buf.close()
    cv2.destroyAllWindows()
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
