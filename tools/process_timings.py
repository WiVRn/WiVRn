#!/usr/bin/env python3

import csv
import argparse

PIXEL_SCALE = 10/1_000_000 # 10px/ms
LINE_HEIGHT = 10

COLOURS = ["#ea5545", "#f46a9b", "#ef9b20", "#edbf33", "#ede15b", "#bdcf32", "#87bc45", "#27aeef", "#b33dc6"]

class Frame:
    def __init__(self, num):
        self.num = num
        self.events = dict()
        self.streams = dict()

    def set(self, event, timestamp, stream):
        if stream == 255:
            self.events[event] = timestamp
        else:
            if stream not in self.streams:
                self.streams[stream] = dict()
            self.streams[stream][event] = timestamp


    def draw(self, out):
        out.write("<!-- frame {} -->\n".format(self.num))
        if "wake_up" in self.events and "submit" in self.events:
            wake_up = self.events["wake_up"]
            submit = self.events["submit"]
            out.write('<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" class="compositor"/>\n'.format(
                x = wake_up * PIXEL_SCALE,
                w = (submit - wake_up) * PIXEL_SCALE,
                y = 0,
                h = LINE_HEIGHT,
                fill = COLOURS[0]
                ))
        if "begin" in self.events:
            begin = self.events["begin"]
            out.write('<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" class="begin"/>\n'.format(
                x = begin * PIXEL_SCALE,
                y = 0,
                y2 = LINE_HEIGHT
                ))

        for stream, events in self.streams.items():
            y_offset = LINE_HEIGHT * (1 + stream * 2)
            if "encode_begin" in events and "rs_end" in events:
                begin = events["encode_begin"]
                end = events["encode_end"]
                send_start = events["send_start"]
                rs_end = events["rs_end"]
                out.write('<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" class="encode"/>\n'.format(
                    x = begin * PIXEL_SCALE,
                    y = y_offset,
                    h = LINE_HEIGHT,
                    w = (end - begin) * PIXEL_SCALE,
                    fill = COLOURS[1]
                    ))
                out.write('<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" class="send"/>\n'.format(
                    x = send_start * PIXEL_SCALE,
                    y = y_offset + 0.2 * LINE_HEIGHT,
                    h =  0.6 * LINE_HEIGHT,
                    w = (rs_end - send_start) * PIXEL_SCALE,
                    fill = COLOURS[2]
                    ))

            y_offset = LINE_HEIGHT * (2 + stream * 2)

            if "receive_start" in events and "receive_end" in events:
                begin = events["receive_start"]
                end = events["receive_end"]
                out.write('<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" class="receive"/>\n'.format(
                    x = begin * PIXEL_SCALE,
                    y = y_offset,
                    h = LINE_HEIGHT,
                    w = (end - begin) * PIXEL_SCALE,
                    fill = COLOURS[3]
                    ))

            if "reconstructed" in events:
                t = events["reconstructed"]
                out.write('<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" class="reconstructed"/>\n'.format(
                    x = t * PIXEL_SCALE,
                    y = y_offset,
                    y2 = y_offset + LINE_HEIGHT
                    ))

            if "decode_start" in events and "decode_end" in events:
                begin = events["decode_start"]
                end = events["decode_end"]
                out.write('<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" class="decode"/>\n'.format(
                    x = begin * PIXEL_SCALE,
                    y = y_offset,
                    h = LINE_HEIGHT,
                    w = (end - begin) * PIXEL_SCALE,
                    fill = COLOURS[4]
                    ))

            if "blit" in events:
                t = events["blit"]
                out.write('<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" stroke="{stroke}" class="blit"/>\n'.format(
                    x = t * PIXEL_SCALE,
                    y = y_offset,
                    y2 = y_offset + LINE_HEIGHT,
                    stroke = COLOURS[0]
                    ))

            if "display" in events:
                t = events["display"]
                out.write('<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" stroke="{stroke}" class="display"/>\n'.format(
                    x = t * PIXEL_SCALE,
                    y = y_offset,
                    y2 = y_offset + LINE_HEIGHT,
                    stroke = COLOURS[1]
                    ))


if __name__ == "__main__":
    parser = argparse.ArgumentParser("Parser for WiVRn timing dumps")
    parser.add_argument("CSV", type=argparse.FileType("r"))
    parser.add_argument("--out", type=argparse.FileType("w"), default="-")
    parser.add_argument("--skip", type=int, default=0)
    parser.add_argument("--duration_ms", type=int, default=100)

    args = parser.parse_args()

    timings = csv.reader(args.CSV)

    origin = None
    frames = []
    for event, frame, timestamp, stream in timings:
        frame = int(frame)

        if frame < args.skip:
            continue
        timestamp = int(timestamp)
        stream = int(stream)

        if origin is None:
            origin = timestamp

        if not timestamp:
            continue

        timestamp -= origin

        if timestamp > args.duration_ms * 1_000_000:
            continue

        while len(frames) < frame + 1:
            frames.append(Frame(len(frames)))

        frames[frame].set(event, timestamp, stream)

    out = args.out
    out.write('''<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
''')

    for frame in reversed(frames):
        frame.draw(out)

    out.write("</svg>")
