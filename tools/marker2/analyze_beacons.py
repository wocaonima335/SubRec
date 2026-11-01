#!/usr/bin/env python3
"""Measure A/V offset of marker2 beacons (white flash + 1 kHz beep) in a recording.

Offset convention (same as the P1 analysis): offset = audio onset - video onset,
positive = sound later than picture. Decoding goes through the ffmpeg/ffprobe
executables, so MP4 edit lists (AAC priming, positive video start) are applied
exactly as a player applies them.

Beacon correction: marker2 queues the beep behind the WASAPI render padding, so the
beep leaves later than the flash. marker2 logs beacon_delay_ms per marker
(beep_qpc - flash_qpc). The corrected offset subtracts the median of those values
(or --beacon-delay-ms when no log is given). That is the SubRec recording error.

Usage:
  analyze_beacons.py recording.mp4 [--marker-log marker.log] [--beacon-delay-ms 23.5]
                     [--csv pairs.csv] [--json summary.json]
  analyze_beacons.py --selftest      (needs ffmpeg with lavfi, an H.264 or MPEG-4 encoder and aac)

Only Python 3 standard library; ffmpeg/ffprobe must be on PATH or given with --ffmpeg/--ffprobe.
"""

import argparse
import array
import json
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile

SAMPLE_RATE = 48000
BLOCK = 48           # 1 ms analysis blocks
VIDEO_W, VIDEO_H = 64, 36


def run(cmd):
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError("command failed: %s\n%s" % (" ".join(cmd), result.stderr.decode(errors="replace")))
    return result.stdout


def stream_start(ffprobe, path, selector):
    out = run([ffprobe, "-v", "error", "-select_streams", selector, "-show_entries",
               "stream=start_time", "-of", "csv=p=0", path]).decode().strip().splitlines()
    try:
        return float(out[0])
    except (IndexError, ValueError):
        return 0.0


def decode_audio(ffmpeg, ffprobe, path):
    raw = run([ffmpeg, "-v", "error", "-i", path, "-map", "0:a:0", "-ac", "1", "-ar", str(SAMPLE_RATE),
               "-f", "f32le", "-"])
    samples = array.array("f")
    samples.frombytes(raw[: len(raw) // 4 * 4])
    return stream_start(ffprobe, path, "a:0"), samples


def decode_video(ffmpeg, ffprobe, path):
    lines = run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                 "frame=best_effort_timestamp_time", "-of", "csv=p=0", path]).decode().split()
    times = []
    for line in lines:
        value = line.strip().strip(",")   # some ffprobe versions append a separator
        if value and value != "N/A":
            times.append(float(value))
    raw = run([ffmpeg, "-v", "error", "-i", path, "-map", "0:v:0", "-fps_mode", "passthrough",
               "-vf", "scale=%d:%d,format=gray" % (VIDEO_W, VIDEO_H), "-f", "rawvideo", "-"])
    size = VIDEO_W * VIDEO_H
    count = min(len(raw) // size, len(times))
    luma = [sum(raw[i * size:(i + 1) * size]) / size for i in range(count)]
    return times[:count], luma


def percentile(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def flash_onsets(times, luma):
    if not luma:
        return []
    low, high = percentile(luma, 10), percentile(luma, 99)
    if high - low < 30:
        return []
    threshold = (low + high) / 2
    onsets, above = [], luma[0] > threshold
    for t, y in zip(times, luma):
        if y > threshold and not above:
            onsets.append(t)
        above = y > threshold
    return onsets


def goertzel_power(samples, start, n, coeff):
    s1 = s2 = 0.0
    for i in range(start, start + n):
        s0 = samples[i] + coeff * s1 - s2
        s2, s1 = s1, s0
    return s1 * s1 + s2 * s2 - coeff * s1 * s2


def beep_onsets(start_time, samples, tone_hz):
    blocks = len(samples) // BLOCK
    if blocks == 0:
        return []
    coeff = 2 * math.cos(2 * math.pi * tone_hz / SAMPLE_RATE)
    power = [goertzel_power(samples, b * BLOCK, BLOCK, coeff) for b in range(blocks)]
    peak = percentile(power, 99.5)
    if peak <= 0:
        return []
    threshold = 0.25 * peak
    min_gap = 100   # ms of tone-free signal before a new onset
    onsets, quiet = [], min_gap
    for b, p in enumerate(power):
        if p > threshold:
            if quiet >= min_gap:
                # Refine within the previous and current block: first sample above
                # 20% of the tone amplitude reached in this beep.
                lo = max(0, (b - 2) * BLOCK)
                hi = min(len(samples), (b + 20) * BLOCK)
                amp = max(abs(samples[i]) for i in range(lo, hi)) or 1.0
                first = next((i for i in range(lo, hi) if abs(samples[i]) >= 0.2 * amp), b * BLOCK)
                onsets.append(start_time + first / SAMPLE_RATE)
            quiet = 0
        else:
            quiet += 1
    return onsets


def pair(flashes, beeps, window=0.5):
    """One-to-one pairing in time order; each beep is used at most once."""
    pairs, j = [], 0
    for f in flashes:
        while j < len(beeps) and beeps[j] < f - window:
            j += 1
        if j < len(beeps) and abs(beeps[j] - f) <= window:
            pairs.append((f, beeps[j]))
            j += 1
    return pairs


def marker_delays(log_path):
    delays = []
    with open(log_path, encoding="utf-8", errors="replace") as log:
        for line in log:
            m = re.search(r"\bbeacon_delay_ms=(-?[0-9.]+)", line)
            if m:
                delays.append(float(m.group(1)))
    return delays


def linear_drift_ms_per_hour(points):
    if len(points) < 2:
        return 0.0
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx * 3600


def summarize(values, times):
    if not values:
        return {"count": 0}
    first = [v for v, t in zip(values, times) if t < times[0] + 600]
    last = [v for v, t in zip(values, times) if t > times[-1] - 600]
    return {
        "count": len(values),
        "median_ms": statistics.median(values),
        "mean_ms": statistics.fmean(values),
        "p5_ms": percentile(values, 5),
        "p95_ms": percentile(values, 95),
        "spread_p95_p5_ms": percentile(values, 95) - percentile(values, 5),
        "min_ms": min(values),
        "max_ms": max(values),
        "abs_over_100ms": sum(1 for v in values if abs(v) > 100),
        "first10min_median_ms": statistics.median(first) if first else float("nan"),
        "last10min_median_ms": statistics.median(last) if last else float("nan"),
        "drift_ms_per_hour": linear_drift_ms_per_hour(list(zip(times, values))),
    }


def analyze(path, args):
    a_start, samples = decode_audio(args.ffmpeg, args.ffprobe, path)
    times, luma = decode_video(args.ffmpeg, args.ffprobe, path)
    flashes = flash_onsets(times, luma)
    beeps = beep_onsets(a_start, samples, args.tone_hz)
    pairs = pair(flashes, beeps)
    delay, source = args.beacon_delay_ms, "argument"
    if args.marker_log:
        logged = marker_delays(args.marker_log)
        if logged:
            delay, source = statistics.median(logged), "marker log median of %d" % len(logged)
    if delay is None:
        delay, source = 0.0, "none"
    raw = [(b - f) * 1000 for f, b in pairs]
    corrected = [r - delay for r in raw]
    flash_times = [f for f, _ in pairs]
    result = {
        "file": os.path.abspath(path),
        "flashes": len(flashes),
        "beeps": len(beeps),
        "pairs": len(pairs),
        "unpaired_flashes": len(flashes) - len(pairs),
        "unpaired_beeps": len(beeps) - len(pairs),
        "beacon_delay_ms": delay,
        "beacon_delay_source": source,
        "raw": summarize(raw, flash_times),
        "corrected": summarize(corrected, flash_times),
    }
    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as out:
            out.write("index,flash_s,beep_s,raw_ms,corrected_ms\n")
            for i, ((f, b), r, c) in enumerate(zip(pairs, raw, corrected), 1):
                out.write("%d,%.6f,%.6f,%.3f,%.3f\n" % (i, f, b, r, c))
    return result


def print_result(result):
    print("file=%s flashes=%d beeps=%d pairs=%d unpaired_flashes=%d unpaired_beeps=%d"
          % (result["file"], result["flashes"], result["beeps"], result["pairs"],
             result["unpaired_flashes"], result["unpaired_beeps"]))
    print("beacon_delay_ms=%.3f (%s)" % (result["beacon_delay_ms"], result["beacon_delay_source"]))
    for key in ("raw", "corrected"):
        s = result[key]
        if not s["count"]:
            print("%s: no pairs" % key)
            continue
        print("%s: median=%.1f mean=%.1f p5=%.1f p95=%.1f spread=%.1f min=%.1f max=%.1f |>100|=%d "
              "first10=%.1f last10=%.1f drift=%.2f ms/h"
              % (key, s["median_ms"], s["mean_ms"], s["p5_ms"], s["p95_ms"], s["spread_p95_p5_ms"],
                 s["min_ms"], s["max_ms"], s["abs_over_100ms"], s["first10min_median_ms"],
                 s["last10min_median_ms"], s["drift_ms_per_hour"]))


def selftest(args):
    """Synthetic 30 s file: flash at 0.5 s + 2k s, beep 40 ms later; AAC priming via MP4."""
    encoders = run([args.ffmpeg, "-hide_banner", "-encoders"]).decode()
    vcodec = "libx264" if " libx264 " in encoders else "mpeg4"
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "beacons.mp4")
        video = ("color=c=black:s=160x90:r=30:d=30[b];color=c=white:s=160x90:r=30:d=30[w];"
                 "[b][w]overlay=enable='between(mod(t,2),0.5,0.69)',format=yuv420p[v]")
        audio = "aevalsrc='0.5*sin(2*PI*1000*t)*lt(mod(t+2-0.54,2),0.1)':s=48000:d=30[a]"
        run([args.ffmpeg, "-v", "error", "-y", "-filter_complex", video + ";" + audio,
             "-map", "[v]", "-map", "[a]", "-c:v", vcodec, "-g", "30", "-c:a", "aac", "-b:a", "128k", path])
        log = os.path.join(tmp, "marker.log")
        with open(log, "w") as out:
            for i in range(15):
                out.write("marker %d elapsed=0 beacon_delay_ms=%.3f\n" % (i + 1, 15.0 + (i % 3)))
        args.marker_log = log
        result = analyze(path, args)
    print_result(result)
    ok = (result["pairs"] == 15 and result["unpaired_beeps"] == 0
          and abs(result["raw"]["median_ms"] - 40) <= 2
          and abs(result["corrected"]["median_ms"] - 24) <= 2
          and result["raw"]["spread_p95_p5_ms"] <= 2)
    print("SELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("recording", nargs="?")
    parser.add_argument("--marker-log")
    parser.add_argument("--beacon-delay-ms", type=float, default=None)
    parser.add_argument("--tone-hz", type=float, default=1000.0)
    parser.add_argument("--csv")
    parser.add_argument("--json")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest(args)
    if not args.recording:
        parser.error("recording is required")
    result = analyze(args.recording, args)
    print_result(result)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump(result, out, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
