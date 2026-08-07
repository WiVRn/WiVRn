#!/usr/bin/env python3
# Perfetto pftrace summarizer — pairs BEGIN/END by trusted_packet_sequence_id.
# Single file: per-slice stats table.
# Two files:   side-by-side diff table (pass in either order).

import collections
import statistics
import sys


def read_varint(buf, off):
    v = 0
    shift = 0
    while True:
        b = buf[off]
        off += 1
        v |= (b & 0x7F) << shift
        if not (b & 0x80):
            return v, off
        shift += 7


def read_tag(buf, off):
    tag, off = read_varint(buf, off)
    return tag >> 3, tag & 7, off


def skip(buf, off, wt):
    if wt == 0:
        _, off = read_varint(buf, off)
        return off
    if wt == 1:
        return off + 8
    if wt == 2:
        ln, off = read_varint(buf, off)
        return off + ln
    if wt == 5:
        return off + 4
    raise ValueError(f"unknown wt {wt}")


def parse_track_event(buf, off, end):
    name = None
    ev_type = None
    while off < end:
        fid, wt, off = read_tag(buf, off)
        if fid == 23 and wt == 2:
            ln, off = read_varint(buf, off)
            name = buf[off : off + ln].decode("utf-8", "replace")
            off += ln
        elif fid == 9 and wt == 0:
            ev_type, off = read_varint(buf, off)
        else:
            off = skip(buf, off, wt)
    return name, ev_type


def parse(path):
    with open(path, "rb") as f:
        buf = f.read()
    durations = collections.defaultdict(list)
    open_stack = collections.defaultdict(list)  # seq_id -> [(name, ts)]
    ts_min = ts_max = None

    off = 0
    n = len(buf)
    while off < n:
        fid, wt, off = read_tag(buf, off)
        if fid != 1 or wt != 2:
            off = skip(buf, off, wt)
            continue
        ln, off = read_varint(buf, off)
        end = off + ln
        ts = None
        seq = None
        te_name = None
        te_type = None
        while off < end:
            f2, wt2, off = read_tag(buf, off)
            if f2 == 8 and wt2 == 0:
                ts, off = read_varint(buf, off)
            elif f2 == 10 and wt2 == 0:
                seq, off = read_varint(buf, off)
            elif f2 == 11 and wt2 == 2:
                ln2, off = read_varint(buf, off)
                te_name, te_type = parse_track_event(buf, off, off + ln2)
                off += ln2
            else:
                off = skip(buf, off, wt2)
        if (
            ts is not None and te_type is not None
        ):  # only real events, not metadata packets with ts=0
            if ts_min is None or ts < ts_min:
                ts_min = ts
            if ts_max is None or ts > ts_max:
                ts_max = ts
        if te_type is not None:
            if te_type == 1 and te_name and ts is not None:
                open_stack[seq].append((te_name, ts))
            elif te_type == 2 and ts is not None and open_stack[seq]:
                bname, bts = open_stack[seq].pop()
                durations[bname].append(ts - bts)

    span = (ts_max - ts_min) if ts_min is not None else 0
    return dict(durations), span, len(buf)


def _is_instant(dur_list):
    return all(d == 0 for d in dur_list)


def _stats(dur_list, span):
    srt = sorted(dur_list)
    p99 = srt[int(len(srt) * 0.99)] if len(srt) > 1 else srt[0]
    rate = len(dur_list) / (span / 1e9) if span else 0
    return (
        len(dur_list),
        statistics.mean(dur_list) / 1e6,
        statistics.median(dur_list) / 1e6,
        p99 / 1e6,
        max(dur_list) / 1e6,
        rate,
    )


def summarize(path, exclude=()):
    durations, span, size = parse(path)
    if exclude:
        durations = {n: d for n, d in durations.items() if n not in exclude}
    print(f"\n=== {path} ===")
    print(f"  file size:     {size:,} bytes")
    print(f"  trace span:    {span / 1e9:.3f} s")

    rows = []
    for name, dur_list in durations.items():
        if not dur_list or _is_instant(dur_list):
            continue
        rows.append((name,) + _stats(dur_list, span))
    rows.sort(key=lambda r: -r[1])
    print(
        f"  {'slice':<32} {'n':>6} {'mean ms':>9} {'p50 ms':>8} {'p99 ms':>8} {'max ms':>8} {'rate/s':>8}"
    )
    for r in rows[:30]:
        print(
            f"  {r[0]:<32} {r[1]:>6} {r[2]:>9.3f} {r[3]:>8.3f} {r[4]:>8.3f} {r[5]:>8.3f} {r[6]:>8.1f}"
        )

    instants = [(n, _stats(d, span)) for n, d in durations.items() if d and _is_instant(d)]
    if instants:
        instants.sort(key=lambda x: -x[1][5])
        print(f"\n  {'instant':<32} {'n':>6} {'rate/s':>8}")
        for name, st in instants:
            print(f"  {name:<32} {st[0]:>6} {st[5]:>8.1f}")


def compare(path_a, path_b, exclude=()):
    dur_a, span_a, size_a = parse(path_a)
    dur_b, span_b, size_b = parse(path_b)
    if exclude:
        dur_a = {n: d for n, d in dur_a.items() if n not in exclude}
        dur_b = {n: d for n, d in dur_b.items() if n not in exclude}

    label_a = path_a.rsplit("/", 1)[-1]
    label_b = path_b.rsplit("/", 1)[-1]

    print(f"\n=== {label_a}  vs  {label_b} ===")
    print(
        f"  A: {size_a:,} bytes  {span_a / 1e9:.3f} s    B: {size_b:,} bytes  {span_b / 1e9:.3f} s"
    )

    all_names = sorted(set(dur_a) | set(dur_b))
    span_names = [
        n
        for n in all_names
        if (dur_a.get(n) and not _is_instant(dur_a[n]))
        or (dur_b.get(n) and not _is_instant(dur_b[n]))
    ]

    def fmt_cell(dur_list, span):
        if not dur_list or _is_instant(dur_list):
            return f"{'–':>5}  {'–':>8}  {'–':>8}  {'–':>8}"
        st = _stats(dur_list, span)
        return f"{st[0]:>5}  {st[1]:>8.3f}  {st[2]:>8.3f}  {st[3]:>8.3f}"

    def fmt_delta(da, db):
        if not da or _is_instant(da) or not db or _is_instant(db):
            return ""
        ma = statistics.mean(da) / 1e6
        mb = statistics.mean(db) / 1e6
        diff = mb - ma
        pct = diff / ma * 100 if ma else 0
        sign = "+" if diff >= 0 else ""
        return f"{sign}{diff:.3f} ms ({sign}{pct:.0f}%)"

    col_w = 36  # n + mean + p50 + p99
    print(
        f"\n  {'slice':<32}  {'── A: ' + label_a[:26]:<{col_w}}  {'── B: ' + label_b[:26]:<{col_w}}  delta"
    )
    print(
        f"  {'':32}  {'n':>5}  {'mean ms':>8}  {'p50 ms':>8}  {'p99 ms':>8}"
        f"  {'n':>5}  {'mean ms':>8}  {'p50 ms':>8}  {'p99 ms':>8}"
    )
    print("  " + "-" * 110)

    # Sort by combined mean (largest first = most interesting)
    def sort_key(n):
        ma = statistics.mean(dur_a[n]) if dur_a.get(n) and not _is_instant(dur_a[n]) else 0
        mb = statistics.mean(dur_b[n]) if dur_b.get(n) and not _is_instant(dur_b[n]) else 0
        return -(ma + mb)

    for name in sorted(span_names, key=sort_key):
        da = dur_a.get(name, [])
        db = dur_b.get(name, [])
        print(f"  {name:<32}  {fmt_cell(da, span_a)}  {fmt_cell(db, span_b)}  {fmt_delta(da, db)}")

    instant_names = [
        n
        for n in all_names
        if (dur_a.get(n) and _is_instant(dur_a[n])) or (dur_b.get(n) and _is_instant(dur_b[n]))
    ]
    if instant_names:
        print(f"\n  {'instant':<32}  {'rate/s (A)':>12}  {'rate/s (B)':>12}")
        for name in sorted(instant_names):
            da = dur_a.get(name, [])
            db = dur_b.get(name, [])
            ra = f"{len(da) / (span_a / 1e9):.1f}" if da and span_a else "–"
            rb = f"{len(db) / (span_b / 1e9):.1f}" if db and span_b else "–"
            print(f"  {name:<32}  {ra:>12}  {rb:>12}")


if __name__ == "__main__":
    argv = sys.argv[1:]
    exclude = ()
    if "--exclude" in argv:  # --exclude name1,name2 : drop those slices from the report
        i = argv.index("--exclude")
        exclude = tuple(x for x in argv[i + 1].split(",") if x) if i + 1 < len(argv) else ()
        del argv[i : i + 2]
    if not argv:
        print(
            "usage: pftrace_summary.py [--exclude n1,n2] <trace.pftrace> [second.pftrace]",
            file=sys.stderr,
        )
        sys.exit(2)
    if len(argv) == 2:
        compare(argv[0], argv[1], exclude=exclude)
    else:
        for p in argv:
            summarize(p, exclude=exclude)
    print("\n  open in https://ui.perfetto.dev for the visual timeline")
