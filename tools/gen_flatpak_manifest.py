#!/usr/bin/env python3

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from urllib.request import urlopen

TEMPLATE = "flatpak/io.github.wivrn.wivrn.yml.in"

# Generated cargo sources, committed to the repository and regenerated
# automatically when the xrizer commit changes.
CARGO_SOURCES = "flatpak/xrizer-gen-src.json"
CARGO_SOURCES_COMMIT = "flatpak/xrizer-gen-src.commit"
# Local cache of the monado archive sha256 (not committed), recomputed when
# monado-rev changes.
MONADO_SHA256 = "flatpak/monado.sha256"

XRIZER_REPO = "https://github.com/Supreeeme/xrizer"
MONADO_ARCHIVE = "https://gitlab.freedesktop.org/monado/monado/-/archive/{0}/monado-{0}.tar.gz"


def read_file(path: str) -> str:
    try:
        with open(path) as f:
            return f.read().strip()
    except FileNotFoundError:
        return ""


def fetch(url: str, attempts: int = 4) -> bytes:
    for attempt in range(1, attempts + 1):
        try:
            with urlopen(url, timeout=120) as response:
                return response.read()
        except OSError as e:
            if attempt == attempts:
                raise
            print(f"Downloading {url} failed ({e}), retrying", file=sys.stderr)
            time.sleep(30 * attempt)


def update_cargo_sources(root: str, xrizer_commit: str):
    try:
        import asyncio
        import json

        import flatpakcargogenerator
        import toml
    except ImportError as e:
        print(
            f"Regenerating {CARGO_SOURCES} requires python modules toml and aiohttp: {e}",
            file=sys.stderr,
        )
        sys.exit(2)

    print(f"Regenerating {CARGO_SOURCES} for xrizer {xrizer_commit}", file=sys.stderr)
    lock = fetch(f"{XRIZER_REPO}/raw/{xrizer_commit}/Cargo.lock")
    gen_src = asyncio.run(flatpakcargogenerator.generate_sources(toml.loads(lock.decode())))
    with open(os.path.join(root, CARGO_SOURCES), "w") as f:
        json.dump(gen_src, f, indent=4, sort_keys=False)
        f.write("\n")
    with open(os.path.join(root, CARGO_SOURCES_COMMIT), "w") as f:
        f.write(xrizer_commit + "\n")


def update_monado_sha256(root: str, monado_commit: str) -> str:
    print(f"Computing sha256 of monado archive for {monado_commit}", file=sys.stderr)
    sha256 = hashlib.sha256(fetch(MONADO_ARCHIVE.format(monado_commit))).hexdigest()
    with open(os.path.join(root, MONADO_SHA256), "w") as f:
        f.write(f"{monado_commit} {sha256}\n")
    return sha256


class CMakeFile:
    def __init__(self, filename: str):
        self.entries = {}
        with open(filename) as f:
            data = f.read()
        for match in re.findall("FetchContent_Declare\\(([^)]*)\\)", data):
            words = [word for word in match.replace("\n", " ").split(" ") if word]
            self.entries[words[0]] = words[1:]

    def get(self, target: str, key: str) -> str:
        entry = self.entries[target]
        index = entry.index(key)
        return entry[index + 1]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--git")
    parser.add_argument("--gitlocal", action="store_true")
    parser.add_argument("--dir", action="store_true")
    parser.add_argument(
        "--out", help="directory where to write manifest and additional files", default="."
    )
    parser.add_argument(
        "--update-cargo-sources",
        action="store_true",
        help=f"regenerate {CARGO_SOURCES} even if it matches the xrizer commit",
    )
    parser.add_argument(
        "--update-monado-sha",
        action="store_true",
        help=f"recompute {MONADO_SHA256} even if it matches monado-rev",
    )

    args = parser.parse_args()
    if not (args.git or args.dir):
        args.gitlocal = True

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    cmake = CMakeFile(os.path.join(root, "CMakeLists.txt"))
    with open(os.path.join(root, TEMPLATE)) as f:
        template = f.read()

    # Cargo dependencies for xrizer
    xrizer_commit = re.findall(f"url: {XRIZER_REPO}/archive/([a-f0-9]{{40}})\\.tar\\.gz", template)[
        0
    ]
    if (
        args.update_cargo_sources
        or read_file(os.path.join(root, CARGO_SOURCES_COMMIT)) != xrizer_commit
    ):
        update_cargo_sources(root, xrizer_commit)
        print(f"::warning::{CARGO_SOURCES} was regenerated, commit it", file=sys.stderr)
    cargo_sources = os.path.join(root, CARGO_SOURCES)
    out_cargo_sources = os.path.join(args.out, os.path.basename(CARGO_SOURCES))
    if not os.path.exists(out_cargo_sources) or not os.path.samefile(
        cargo_sources, out_cargo_sources
    ):
        shutil.copyfile(cargo_sources, out_cargo_sources)

    monado_commit = read_file(os.path.join(root, "monado-rev"))
    recorded = read_file(os.path.join(root, MONADO_SHA256)).split()
    if args.update_monado_sha or len(recorded) != 2 or recorded[0] != monado_commit:
        monado_sha256 = update_monado_sha256(root, monado_commit)
    else:
        monado_sha256 = recorded[1]
    boost_url = cmake.get("boost", "URL")
    boost_sha256 = cmake.get("boost", "URL_HASH").split("=")[-1]

    try:
        git_tag = (
            subprocess.check_output(
                ["git", "describe", "--exact-match", "--tags"], cwd=root, stderr=subprocess.DEVNULL
            )
            .decode()
            .strip()
        )
        git_commit = ""
        git_desc = ""
    except subprocess.CalledProcessError:
        git_tag = ""
        git_desc = (
            subprocess.check_output(["git", "describe", "--tags", "--always"], cwd=root)
            .decode()
            .strip()
        )
    git_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root).decode().strip()

    if args.git or args.gitlocal:
        template = template.replace("WIVRN_SRC1", "type: git")
        if args.git:
            template = template.replace("WIVRN_SRC2", f"url: {args.git}")
        else:
            template = template.replace("WIVRN_SRC2", f"url: {root}")
        template = template.replace("WIVRN_SRC3", f"commit: {git_commit}")
    else:
        template = template.replace("WIVRN_SRC1", "type: dir")
        template = template.replace("WIVRN_SRC2", f"path: {root}")
        template = template.replace("WIVRN_SRC3", "")

    template = template.replace("WIVRN_GIT_TAG", git_tag)
    if git_tag != "":
        template = template.replace("WIVRN_GIT_DESC", "")
        template = template.replace("WIVRN_GIT_COMMIT", "")
    else:
        template = template.replace("WIVRN_GIT_DESC", git_desc)
        template = template.replace("WIVRN_GIT_COMMIT", git_commit)

    template = template.replace("BOOST_URL", boost_url)
    template = template.replace("BOOST_SHA256", boost_sha256)
    template = template.replace("MONADO_SHA256", monado_sha256)
    template = template.replace("MONADO_COMMIT", monado_commit)

    with open(os.path.join(args.out, "io.github.wivrn.wivrn.yml"), "w") as f:
        f.write(template)
