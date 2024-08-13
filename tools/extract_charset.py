#!/usr/bin/env python3

import sys
import os.path

print(
"""// Generated file, do not edit
#include <map>
#include <string>
#include <vector>

std::map<std::string, std::vector<int>> glyph_set_per_language = {""")

for po_file in sys.argv[1:]:
	lang = os.path.basename(os.path.dirname(po_file))

	with open(po_file) as f:
		charset = sorted(set(f.read()))

		# Basic Latin and Latin-1 Supplement are always included
		charset = ', '.join([str(ord(c)) for c in charset if ord(c) > 255])

	print("\t{{ \"{}\", {{{}}}}},".format(lang, charset))

print("};")
