#!/usr/bin/env python3

import re

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import NoteSection
import os
import sys
import subprocess


def get_lib_list(path):
	def get_build_id(filename):
		with open(filename, 'rb') as f:
			for sect in ELFFile(f).iter_sections():
				if not isinstance(sect, NoteSection):
					continue
				for note in sect.iter_notes():
					if note['n_type'] in {'NT_GNU_BUILD_ID', 'NT_GNU_GOLD_VERSION'}:
						return note['n_desc']
		return None

	libs = {}

	for root, dirs, files in os.walk(path):
		for name in files:
			if name.endswith('.so'):
				build_id = get_build_id(os.path.join(root, name))
				if build_id:
					libs[build_id] = os.path.join(root, name)

	return libs


libs = get_lib_list('build/intermediates/merged_native_libs')

r = re.compile("^.*#[0-9]+ pc ([0-9a-f]+) .*BuildId: ([0-9a-f]*).*")

for line in sys.stdin:
	m = r.match(line.rstrip())
	if m:
		address = m[1]
		build_id = m[2]

		if build_id in libs:
			lib = libs[build_id]
			subprocess.run(['llvm-symbolizer'], input=(lib + ' 0x' + address).encode("utf-8"))
