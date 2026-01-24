#!/usr/bin/env python3

import argparse
import asyncio
import json
import os
import re
import subprocess
from urllib.request import urlopen

import flatpakcargogenerator
import toml

TEMPLATE = "flatpak/io.github.wivrn.wivrn.yml.in"


class CMakeFile:
    def __init__(self, filename: str):
        self.entries = {}
        with open(filename) as f:
            data = f.read()
        for match in re.findall("FetchContent_Declare\\(([^)]*)\\)", data):
            words = [word for word
                     in match.replace("\n", " ").split(" ")
                     if word]
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
    parser.add_argument("--out", help="directory where to write manifest and additional files", default=".")

    args = parser.parse_args()
    if not (args.git or args.dir):
        args.gitlocal = True

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    cmake = CMakeFile(os.path.join(root, "CMakeLists.txt"))
    with open(os.path.join(root, TEMPLATE)) as f:
        template = f.read()

    # Get dependencies for xrizer
    xrizer_repo = "https://github.com/Supreeeme/xrizer"
    xrizer_commit = re.findall(f"url: {xrizer_repo}.git[ \n]*tag: ([a-f0-9]*)", template)[0]
    lock = urlopen(f"{xrizer_repo}/raw/{xrizer_commit}/Cargo.lock")
    gen_src = asyncio.run(flatpakcargogenerator.generate_sources(
            toml.loads(lock.read().decode())))
    with open(os.path.join(args.out, "xrizer-gen-src.json"), "w") as f:
        json.dump(gen_src, f, indent=4, sort_keys=False)


    monado_commit = open(os.path.join(root, "monado-rev")).read()
    boost_url = cmake.get("boost", "URL")
    boost_sha256 = cmake.get("boost", "URL_HASH").split("=")[-1]

    try:
        git_commit = subprocess.check_output(
                ["git", "describe", "--exact-match", "--tags"],
                cwd=root,
                stderr=subprocess.DEVNULL
                ).decode().strip()
    except subprocess.CalledProcessError:
        git_commit = subprocess.check_output(
                ["git", "rev-parse", "HEAD"],
                cwd=root
                ).decode().strip()
    git_desc = subprocess.check_output(
                ["git", "describe", "--tags", "--always"],
                cwd=root
                ).decode().strip()

    if args.git or args.gitlocal:
        template = template.replace("WIVRN_SRC1", "type: git")
        if args.git:
            template = template.replace("WIVRN_SRC2", f"url: {args.git}")
        else:
            template = template.replace("WIVRN_SRC2", f"url: {root}")
        template = template.replace("WIVRN_SRC3", f"tag: {git_commit}")
    else:
        template = template.replace("WIVRN_SRC1", "type: dir")
        template = template.replace("WIVRN_SRC2", f"path: {root}")
        template = template.replace("WIVRN_SRC3", "")

    template = template.replace("WIVRN_GIT_DESC", git_desc)
    template = template.replace("WIVRN_GIT_COMMIT", git_commit)

    template = template.replace("BOOST_URL", boost_url)
    template = template.replace("BOOST_SHA256", boost_sha256)
    template = template.replace("MONADO_COMMIT", monado_commit)

    with open(os.path.join(args.out, "io.github.wivrn.wivrn.yml"), "w") as f:
        f.write(template)
