#!/usr/bin/env python3
"""
ZoneSpy Stream Viewer - display all live feeds in one window.
"""

import json, os, struct, sys, time
import numpy as np
import mmap
import win32event
import cv2

SHM_SIZE = 20 + 3840 * 2160 * 4
DISPLAY_WIDTH = 960


def load_config():
    path = os.path.join(os.environ.get("LOCALAPPDATA", ""), "ZoneSpy", "streams.json")
    if not os.path.exists(path):
        print(f"Config not found: {path}")
        return None
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def open_streams(streams):
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
    """Return (pixels, timestamp) or (None, reason_string)."""
    try:
        buf.seek(0)
        raw = buf.read(20)
        fc, ts, w, h = struct.unpack("I Q I I", raw)
        if fc == 0 and w == 0 and h == 0:
            return None, f"no_capture_yet(header=all_zero)"
        if not (0 < w <= 3840 and 0 < h <= 2160):
            return None, f"invalid_dims({w}x{h})"
        pixels = np.frombuffer(buf.read(w * h * 4), dtype=np.uint8).reshape(h, w, 4)
        return pixels, ts
    except Exception as e:
        return None, f"read_error({e})"


def main():
    config = load_config()
    if config is None:
        return 1

    streams = config.get("streams", [])
    if not streams:
        print("No streams in config.")
        return 1

    print(f"Connecting to {len(streams)} stream(s)...")
    opened = open_streams(streams)
    if not opened:
        print("Could not open any streams.")
        return 1

    print(f"Opened {len(opened)} stream(s).")
    frames = [None] * len(opened)
    display_w = DISPLAY_WIDTH
    frame_count = 0
    last_report = 0

    handles = [e for _, _, _, e in opened if e is not None]
    statuses = [""] * len(opened)

    while True:
        if len(handles) == len(opened):
            try:
                win32event.WaitForMultipleObjects(handles, False, 100)
            except:
                pass

        any_new = False
        for i, (sid, name, buf, evt) in enumerate(opened):
            frame, reason = read_frame(buf)
            if frame is not None:
                frames[i] = frame
                statuses[i] = f"OK ({frame.shape[1]}x{frame.shape[0]})"
                any_new = True
            else:
                if isinstance(reason, str):
                    statuses[i] = reason

        # Report status periodically
        now = time.time()
        if now - last_report > 3:
            last_report = now
            lines = [f"  [{time.strftime('%H:%M:%S')}] Stream status:"]
            for i, (sid, name, buf, evt) in enumerate(opened):
                lines.append(f"    [{sid}] {name}: {statuses[i]}")
            print("\n".join(lines))

        if not any_new:
            time.sleep(0.01)
            continue

        rows = []
        valid_pairs = [(f, n) for f, (_, n, _, _) in zip(frames, opened) if f is not None]
        for frame, name in valid_pairs:
            h, w = frame.shape[:2]
            scale = display_w / w
            new_h = int(h * scale)
            resized = cv2.resize(frame, (display_w, new_h))
            bgr = cv2.cvtColor(resized, cv2.COLOR_BGRA2BGR)
            cv2.putText(bgr, name, (8, 24), cv2.FONT_HERSHEY_SIMPLEX,
                        0.6, (0, 255, 0), 2)
            rows.append(bgr)

        if not rows:
            continue

        display = np.vstack(rows)
        cv2.imshow("ZoneSpy Stream Viewer", display)
        key = cv2.waitKey(30) & 0xFF
        if key == ord("q"):
            break
        elif key == ord("f"):
            display_w = 1920 if display_w == 960 else 960

        frame_count += 1
        if frame_count == 1:
            print(f"  First frame displayed! Size: {display.shape[1]}x{display.shape[0]}")

    for _, _, buf, _ in opened:
        buf.close()
    cv2.destroyAllWindows()
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
