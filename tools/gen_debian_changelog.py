#!/usr/bin/env python3

import xml.etree.ElementTree as ET
import argparse
import os
import sys
import typing
import datetime
import email.utils
import subprocess

HEADER = """wivrn ({version}-1) {distribution}; urgency=medium

"""

ITEM = """  * {text}
"""

FOOTER = """
 -- {author}  {date}

"""

def write_entry(
    out: typing.TextIO,
    version: str,
    data: list[str],
    author: str,
    distribution: str,
    date: datetime.datetime
    ) -> None:
    out.write(HEADER.format(version=version, distribution=distribution))
    for item in data:
        out.write(ITEM.format(text=item))
    out.write(FOOTER.format(author=author, date=email.utils.format_datetime(date)))


def main():
    parser = argparse.ArgumentParser("Metainfo to Debian changelog")
    parser.add_argument("--metainfo", type=argparse.FileType("r"))
    parser.add_argument("--out", type=argparse.FileType("w"), default=sys.stdout)
    parser.add_argument("--nightly", action="store_true")
    parser.add_argument("--author", required=True)
    parser.add_argument("--distribution", required=True)

    args = parser.parse_args()

    if not args.metainfo:
        args.metainfo = open(os.path.join(os.path.dirname(__file__), "../dashboard/io.github.wivrn.wivrn.metainfo.xml"))

    tree = ET.parse(args.metainfo)

    if args.nightly:
        version = subprocess.check_output(["git", "describe", "--tags", "--always"]).decode().strip()
        version = version[1:] # remove the initial "v"
        write_entry(
                out=args.out,
                version=version,
                data=["Nightly build"],
                author=args.author,
                distribution=args.distribution,
                date=datetime.datetime.now()
                )


    for release in tree.find("releases").findall("release"):
        write_entry(
                out=args.out,
                version=release.get("version"),
                data=[item.text for item in release.findall("description/ul/li")],
                author=args.author,
                distribution=args.distribution,
                date=datetime.datetime.fromisoformat(release.get("date"))
                )


if __name__ == "__main__":
    main()
