#!/usr/bin/env python3

import pandas
import csv
import typing

class Frame:
    def __init__(self, num):
        self.num = num
        self.events = dict()
        self.streams = dict()
        self.flags = dict()
        self.len = 1

    def set(self, event, timestamp, stream):
        if stream == 255:
            self.events[event] = timestamp
        else:
            if stream not in self.streams:
                self.streams[stream] = dict()
            if event in ("blit", "display"):
                if event not in self.streams[stream]:
                    self.streams[stream][event] = [timestamp]
                else:
                    l = self.streams[stream][event]
                    l.append(timestamp)
                    self.len = max(self.len, len(l))
            else:
                self.streams[stream][event] = timestamp

    def flag(self, stream, flag):
        self.flags[stream if stream != 255 else None] = flag

def read(file: typing.TextIO) -> pandas.DataFrame:
    timings = csv.reader(file)

    origin = None
    frames = []
    streams = 0
    for event, frame, timestamp, stream, *extra in timings:

        if event == "event":
            continue

        frame = int(frame)

        if frame < 0:
            continue
        timestamp = int(timestamp)
        stream = int(stream)

        if origin is None:
            origin = timestamp

        if not timestamp:
            continue

        if stream != 255:
            streams = max(streams, stream)

        timestamp -= origin

        while len(frames) < frame + 1:
            frames.append(Frame(len(frames)))

        frames[frame].set(event, timestamp, stream)
        for x in extra:
            frames[frame].flag(stream, x)

    common_cols = ["wake_up", "begin", "submit"]
    stream_cols = ["encode_begin", "encode_end", "send_begin", "send_end", "receive_begin", "receive_end", "decode_begin", "decode_end"]
    display_cols = ["blit", "display"]

    columns = ["frame"] + common_cols + stream_cols + display_cols
    for i in range(streams + 1):
        columns += [f"{c}_{i}" for c in stream_cols + display_cols]

    result = pandas.DataFrame(columns=columns)
    for f in frames:
        for j in range(f.len):
            row = [f.num] + [f.events.get(c, None) for c in common_cols]
            for col in stream_cols:
                op = min if col.endswith("_begin") else max
                items = [s[col] for s in f.streams.values() if col in s]
                row.append(op(items) if items else None)

            for col in display_cols:
                items = [s[col][j] for s in f.streams.values() if col in s and len(s[col]) > j]
                row.append(max(items) if items else None)

            for i in range(streams + 1):
                stream = f.streams.get(i, {})
                row += [stream.get(c, None) for c in stream_cols]

                for col in display_cols:
                    items = stream.get(col, [])
                    row.append(items[j] if len(items) > j else None)

            result.loc[len(result)] = row
    return result
