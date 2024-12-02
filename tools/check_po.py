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

args = parser.parse_args()

po_ref = polib.pofile(args.ref)

po_file = polib.pofile(args.file)
entries = [i.msgid for i in po_file.translated_entries()]
language = po_file.metadata['Language']

def flagize(str_in):
	str_out = ''
	for i in str_in:
		str_out += chr(ord(i) + ord('🇦') - ord('A'))
	return str_out

flags = {
	'Spanish': flagize('ES'),
	'French': flagize('FR'),
	'Italian': flagize('IT'),
	'Japanese': flagize('JP')
}

if language in flags:
	flag = flags[language]
else:
	flag = ''

missing = 0
for i in po_ref:
	if not i.obsolete:
		if not i.msgid in entries:
			missing = missing + 1
			print(f"::warning file={args.file}::{flag} Translation for {repr(i.msgid)} is missing")

if missing > 0:
	print(f"::warning file={args.file}::{flag} {missing} missing translation(s) in {language}")

	sys.exit(1)
