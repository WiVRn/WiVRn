#!/usr/bin/env python3

import csv

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

    def duration(self, begin="wake_up", end="display", stream=None, begin_selector=min, end_selector=max):
        try:
            if stream is None:
                streams = list(self.streams.values())
            else:
                streams = [self.streams[stream]]
            if begin in self.events:
                t0 = self.events[begin]
            else:
                t0 = begin_selector([s[begin] for s in streams])
            if end in self.events:
                t1 = self.events[end]
            else:
                t1 = end_selector([s[end] for s in streams])
            return (t1 - t0)/1_000_000
        except (KeyError, ValueError):
            return None


def read(file, skip=0, duration_ms=-1):
    timings = csv.reader(file)

    origin = None
    frames = []
    for event, frame, timestamp, stream, *extra in timings:
        frame = int(frame) - skip

        if frame < 0:
            continue
        timestamp = int(timestamp)
        stream = int(stream)

        if origin is None:
            origin = timestamp

        if not timestamp:
            continue

        timestamp -= origin

        if duration_ms > 0 and timestamp > duration_ms * 1_000_000:
            continue

        while len(frames) < frame + 1:
            frames.append(Frame(len(frames)))

        frames[frame].set(event, timestamp, stream)
        for x in extra:
            frames[frame].flag(stream, x)
    return frames

def durations(frames, stream=None, flag=None, *args, **kwargs):
    def filter(frame):
        if flag is None:
            return True
        return flag in frame.flags.get(stream, ())
    res = [frame.duration(*args, stream=stream, **kwargs) for frame in frames if filter(frame)]
    return [d for d in res if d is not None]
