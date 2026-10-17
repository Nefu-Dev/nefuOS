#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Sweep all nefuOS apps via --shot, classify frames as WHITE / BLACK / OK."""
import subprocess, os, sys, time, struct, csv

EXE = r"D:\mycppos1\nefuOS\dist\nefuOS.exe"
WORK = r"D:\mycppos1\nefuOS"
OUTDIR = r"D:\mycppos1\nefuOS\_sweep_out"
os.makedirs(OUTDIR, exist_ok=True)

# app ids 0..113 with gaps (71=LVGL demo only)
APPS = list(range(0, 114))
APPS.remove(71)

def analyze_bmp(path):
    """Return (white_ratio, black_ratio, distinct_colors_sampled)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        if len(data) < 54 + 800*600*4:
            return (-1, -1, 0)
        # top-down 32bpp BGRA
        off = 54
        W, H = 800, 600
        # sample a 40x30 grid
        xs = [int(i * (W-1) / 39) for i in range(40)]
        ys = [int(j * (H-1) / 29) for j in range(30)]
        white = black = 0
        colors = set()
        total = 0
        for y in ys:
            row = off + y * W * 4
            for x in xs:
                p = row + x * 4
                b, g, r = data[p], data[p+1], data[p+2]
                total += 1
                colors.add((r >> 4, g >> 4, b >> 4))
                if r > 245 and g > 245 and b > 245:
                    white += 1
                elif r < 10 and g < 10 and b < 10:
                    black += 1
        return (white / total, black / total, len(colors))
    except Exception as e:
        return (-2, -2, 0)

def run_one(app_id, timeout=22):
    shot = os.path.join(OUTDIR, f"app_{app_id}.bmp")
    if os.path.exists(shot):
        os.remove(shot)
    try:
        p = subprocess.Popen([EXE, "--shot", str(app_id), shot, "--shotdelay", "4"],
                             cwd=WORK, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            return ("TIMEOUT", 0, 0, 0)
        if not os.path.exists(shot):
            return ("NOFRAME", 0, 0, 0)
        w, b, c = analyze_bmp(shot)
        if w < 0:
            return ("BADBMP", w, b, c)
        if w > 0.93:
            return ("WHITE", w, b, c)
        if b > 0.93:
            return ("BLACK", w, b, c)
        return ("OK", w, b, c)
    except Exception as e:
        return ("ERR", 0, 0, str(e)[:40])

def main():
    start = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    end = int(sys.argv[2]) if len(sys.argv) > 2 else max(APPS) + 1
    csv_path = os.path.join(OUTDIR, f"summary_{start}_{end}.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["app", "state", "white_ratio", "black_ratio", "colors"])
        for app in APPS:
            if app < start or app > end:
                continue
            state, wr, br, colors = run_one(app)
            w.writerow([app, state, round(wr, 3), round(br, 3), colors])
            f.flush()
            print(f"app {app}: {state} white={wr:.2f} black={br:.2f} colors={colors}", flush=True)

if __name__ == "__main__":
    main()
