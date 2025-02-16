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

    def set(self, event, timestamp, stream):
        if stream == 255:
            self.events[event] = timestamp
        else:
            if stream not in self.streams:
                self.streams[stream] = dict()
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
    stream_cols = ["encode_begin", "encode_end", "send_begin", "send_end", "receive_begin", "receive_end", "decode_begin", "decode_end", "blit", "display"]

    columns = ["frame"] + common_cols + stream_cols
    for i in range(streams + 1):
        columns += [f"{c}_{i}" for c in stream_cols]

    result = pandas.DataFrame(columns=columns)
    for f in frames:
        row = [f.num] + [f.events.get(c, 0) for c in common_cols]
        for col in stream_cols:
            op = min if col.endswith("_begin") else max
            items = [s[col] for s in f.streams.values() if col in s]
            row.append(op(items) if items else 0)

        for i in range(streams + 1):
            row += [f.streams.get(i, {}).get(c, 0) for c in stream_cols]

        result.loc[len(result)] = row
    return result
