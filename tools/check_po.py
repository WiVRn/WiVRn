#!/usr/bin/env python

import sys
import polib
import argparse
import pathlib

parser = argparse.ArgumentParser(
	prog='check_po.py',
	description='Check that all strings are translated in a .po file')

parser.add_argument('ref', type=pathlib.Path)
parser.add_argument('file', type=pathlib.Path)
parser.add_argument('lang', type=str)

args = parser.parse_args()

po_ref = polib.pofile(args.ref)

po_file = polib.pofile(args.file)
entries = [i.msgid for i in po_file.translated_entries()]

# Map language code to a country flag
countries = {
	'es': 'es',
	'fr': 'fr',
	'it': 'it',
	'ja': 'jp',
	'zh_TW': 'tw',
	'pt_BR': 'br'
}

if args.lang in countries:
	country = countries[args.lang]
else:
	country = args.lang

flag = ''
for i in country.upper():
	flag += chr(ord(i) + ord('🇦') - ord('A'))

missing = 0
for i in po_ref:
	if not i.obsolete:
		if not i.msgid in entries:
			missing = missing + 1
			print(f"::notice file={args.file}::{flag} Translation for {repr(i.msgid)} is missing")

if missing > 0:
	print(f"::warning file={args.file}::{flag} {missing} translations missing")
	sys.exit(1)
