#!/usr/bin/env python3
"""Compare two benchmark captures (ROADMAP_v2 P1.8).

Usage:
    python tools/bench_compare.py BASELINE NEW [--threshold 0.05] [--noise-ms 0.15]
                                  [--prof mesh_tris,particle]

Both files are captures of the benchmark scene's USB/ISViewer output (any
extra log lines are ignored; only BENCH rows are read). Steps are matched by
(kind, param). A step regresses when:
  * CPU time grows by more than `threshold` (relative) AND more than `noise-ms`
    (absolute) — small steps are noisy in relative terms, or
  * the baseline held 60 FPS (>= 59.5) and the new run does not.
RDP busy time is reported but not gated (it is shared with the RDP-bound
fill-rate steps, where it is the metric; see the FILLRATE rows).

--prof lists profiler slots from the BENCH_PROF rows (names as in
BENCH_PROF_HDR without "_us") for every step, with the time per drawn
triangle: the per-primitive view that CPU averages hide (they include the
audio mix's wait, D38). Not gated.

Exit status: 0 = no regressions, 1 = regression(s), 2 = usage / parse error.
Runs on the host (Windows `python`, macOS `python3`) or in the container.
"""
import argparse
import sys

FIELDS = ["kind", "step", "param", "frames", "fps", "avg_ms", "p99_ms", "low1_fps",
          "cpu_avg_ms", "cpu_max_ms", "rdp_busy_ms", "rdp_busy_pct", "tris",
          "tex_uploads", "heap_kb"]


def read_text(path):
    """Read a capture as text: UTF-8, or UTF-16 as written by Windows
    PowerShell 5.1 redirection (`>`, Tee-Object, Out-File)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    return data.decode("utf-8-sig", errors="replace")


def load(path, prof=None):
    """(meta, {(kind, param): BENCH row}); with a dict as prof, also fills it
    with {(kind, param): {slot: us}} from the BENCH_PROF rows."""
    rows = {}
    meta = ""
    prof_fields = None
    for line in read_text(path).splitlines():
        line = line.strip()
        if line.startswith("BENCH_META,"):
            meta = line[len("BENCH_META,"):]
            continue
        if line.startswith("BENCH_PROF_HDR,"):
            prof_fields = [f[:-3] if f.endswith("_us") else f for f in line.split(",")[1:]]
            continue
        if line.startswith("BENCH_PROF,") and prof is not None and prof_fields:
            rec = dict(zip(prof_fields, line.split(",")[1:]))
            if "param" in rec:
                prof[(rec["kind"], int(rec["param"]))] = {
                    k: float(v) for k, v in rec.items() if k not in ("kind", "step", "param")}
            continue
        if not line.startswith("BENCH,") or line.startswith(("BENCH,END", "BENCH,ABORTED")):
            continue
        parts = line.split(",")[1:]
        if len(parts) != len(FIELDS):
            continue
        rec = dict(zip(FIELDS, parts))
        for k in FIELDS[2:]:
            rec[k] = float(rec[k])
        rows[(rec["kind"], int(rec["param"]))] = rec
    if not rows:
        raise ValueError(f"no BENCH rows in {path}")
    return meta, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline")
    ap.add_argument("new")
    ap.add_argument("--threshold", type=float, default=0.05, help="relative CPU increase allowed (default 0.05)")
    ap.add_argument("--noise-ms", type=float, default=0.15, help="absolute CPU increase ignored (default 0.15 ms)")
    ap.add_argument("--prof", metavar="SLOTS",
                    help="comma-separated BENCH_PROF slots to list per step, e.g. mesh_tris,particle")
    args = ap.parse_args()

    base_prof, new_prof = {}, {}
    try:
        base_meta, base = load(args.baseline, base_prof)
        new_meta, new = load(args.new, new_prof)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    print(f"baseline: {args.baseline}  [{base_meta}]")
    print(f"new:      {args.new}  [{new_meta}]")
    print()
    hdr = f"{'bench':<10}{'param':>6}  {'fps':>11}  {'cpu ms':>15}  {'cpu %':>7}  {'rdp ms':>13}  status"
    print(hdr)
    print("-" * len(hdr))

    regressions = 0
    for key in sorted(base, key=lambda k: base[k]["step"]):
        b = base[key]
        n = new.get(key)
        if n is None:
            print(f"{key[0]:<10}{key[1]:>6}  {'missing in new run':>40}")
            continue
        d_cpu = n["cpu_avg_ms"] - b["cpu_avg_ms"]
        rel = d_cpu / b["cpu_avg_ms"] if b["cpu_avg_ms"] > 0 else 0.0
        fps_lost = b["fps"] >= 59.5 and n["fps"] < 59.5
        cpu_bad = rel > args.threshold and d_cpu > args.noise_ms
        status = "OK"
        if cpu_bad or fps_lost:
            status = "REGRESSION" + (" (fps)" if fps_lost else "")
            regressions += 1
        elif rel < -args.threshold and -d_cpu > args.noise_ms:
            status = "improved"
        print(f"{key[0]:<10}{key[1]:>6}  {b['fps']:>5.1f}->{n['fps']:<5.1f}  "
              f"{b['cpu_avg_ms']:>6.2f}->{n['cpu_avg_ms']:<6.2f}  {rel * 100:>+6.1f}%  "
              f"{b['rdp_busy_ms']:>5.2f}->{n['rdp_busy_ms']:<5.2f}  {status}")

    extra = sorted(set(new) - set(base))
    for key in extra:
        print(f"{key[0]:<10}{key[1]:>6}  (new step, no baseline)")

    if args.prof:
        # commas or spaces: PowerShell passes a,b to native programs as "a b"
        print_prof(args.prof.replace(",", " ").split(), base, new, base_prof, new_prof)

    print()
    print(f"{regressions} regression(s) at threshold {args.threshold:.0%} / {args.noise_ms} ms")
    return 1 if regressions else 0


def print_prof(slots, base, new, base_prof, new_prof):
    print()
    hdr = f"{'bench':<10}{'param':>6}  {'slot':<12}{'us':>17}  {'%':>7}  {'us per drawn tri':>17}"
    print(hdr)
    print("-" * len(hdr))
    for key in sorted(base, key=lambda k: base[k]["step"]):
        bp, np_ = base_prof.get(key), new_prof.get(key)
        if bp is None or np_ is None or key not in new:
            continue
        for slot in slots:
            if slot not in bp or slot not in np_:
                print(f"{key[0]:<10}{key[1]:>6}  {slot:<12}  (no such slot)")
                continue
            b, n = bp[slot], np_[slot]
            rel = f"{(n - b) / b * 100:+6.1f}%" if b > 0 else "      -"
            bt, nt = base[key]["tris"], new[key]["tris"]
            per_tri = f"{b / bt:7.2f}->{n / nt:<7.2f}" if bt > 0 and nt > 0 else ""
            print(f"{key[0]:<10}{key[1]:>6}  {slot:<12}{b:>7.0f}->{n:<8.0f}  {rel}  {per_tri}")


if __name__ == "__main__":
    sys.exit(main())
