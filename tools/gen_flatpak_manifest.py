#!/usr/bin/env python3

import argparse
import asyncio
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from urllib.request import urlopen

TEMPLATE = "flatpak/io.github.wivrn.wivrn.yml.in"

# Local caches, regenerated or downloaded only when the xrizer commit or
# monado-rev changes. The xrizer commit is part of the cache filename, so
# its presence alone tells us whether it's still valid.
CARGO_SOURCES = "flatpak/xrizer-gen-src-{0}.json"
CARGO_SOURCES_OUT_NAME = "xrizer-gen-src.json"
MONADO_ARCHIVE = "flatpak/monado-{0}.tar.gz"

XRIZER_REPO = "https://github.com/Supreeeme/xrizer"
MONADO_URL = "https://gitlab.freedesktop.org/monado/monado/-/archive/{0}/monado-{0}.tar.gz"


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


def write_cargo_sources(path: str, gen_src) -> None:
    with open(path, "w") as f:
        json.dump(gen_src, f, indent=4, sort_keys=False)
        f.write("\n")


def generate_cargo_sources(xrizer_commit: str):
    try:
        import flatpakcargogenerator
        import toml
    except ImportError as e:
        print(
            f"Generating {CARGO_SOURCES_OUT_NAME} requires python modules toml and aiohttp: {e}",
            file=sys.stderr,
        )
        sys.exit(2)

    print(f"Generating cargo sources for xrizer {xrizer_commit}", file=sys.stderr)
    lock = fetch(f"{XRIZER_REPO}/raw/{xrizer_commit}/Cargo.lock")
    return asyncio.run(flatpakcargogenerator.generate_sources(toml.loads(lock.decode())))


def monado_sha256(root: str, monado_commit: str) -> str:
    archive = os.path.join(root, MONADO_ARCHIVE.format(monado_commit))
    if not os.path.exists(archive):
        print(f"Downloading monado archive for {monado_commit}", file=sys.stderr)
        with open(archive + ".part", "wb") as f:
            f.write(fetch(MONADO_URL.format(monado_commit)))
        os.replace(archive + ".part", archive)
    with open(archive, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


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
    cargo_sources = os.path.join(root, CARGO_SOURCES.format(xrizer_commit))
    out_cargo_sources = os.path.join(args.out, CARGO_SOURCES_OUT_NAME)
    if os.path.exists(cargo_sources):
        if not os.path.exists(out_cargo_sources) or not os.path.samefile(
            cargo_sources, out_cargo_sources
        ):
            shutil.copyfile(cargo_sources, out_cargo_sources)
    else:
        gen_src = generate_cargo_sources(xrizer_commit)
        write_cargo_sources(cargo_sources, gen_src)
        write_cargo_sources(out_cargo_sources, gen_src)

    monado_commit = read_file(os.path.join(root, "monado-rev"))
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
    template = template.replace("MONADO_URL", MONADO_URL.format(monado_commit))
    template = template.replace("MONADO_SHA256", monado_sha256(root, monado_commit))

    with open(os.path.join(args.out, "io.github.wivrn.wivrn.yml"), "w") as f:
        f.write(template)
