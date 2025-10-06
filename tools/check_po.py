#!/usr/bin/env python

import polib
import argparse
import os
import pathlib
import subprocess
import tempfile

def list_files(base: str, exts: str|list[str]):
	if type(exts) == str:
		exts = [exts]
	for root, dirs, files in os.walk(base):
		for f in files:
			for ext in exts:
				if f.endswith(ext):
					yield os.path.join(root, f)

def flag(lang: str):
	# Map language code to a country flag
	countries = {
		'es': 'es',
		'fr': 'fr',
		'it': 'it',
		'ja': 'jp',
		'zh_TW': 'tw',
		'pt_BR': 'br'
	}

	country = countries.get(lang, lang)

	res = ''
	for i in country.upper():
		res += chr(ord(i) + ord('🇦') - ord('A'))
	return res

ISSUE_LABEL="localisation"
ISSUE_TITLE="[{lang}] Missing translations"

if __name__ == "__main__":
	root = os.path.dirname(os.path.dirname(__file__))
	locale_dir = os.path.join(root, "locale")
	cwd = os.getcwd()

	parser = argparse.ArgumentParser(
		prog='check_po.py',
		description='Check that all strings are translated in a .po file')

	parser.add_argument('lang', type=str, nargs="*")
	parser.add_argument('--github-repository')
	parser.add_argument('--github-token')
	parser.add_argument('--manage-issues', action="store_true")

	args = parser.parse_args()

	if len(args.lang) == 0:
		for d in sorted(os.listdir(locale_dir)):
			if os.path.isdir(os.path.join(locale_dir, d)):
				args.lang.append(d)

	if args.manage_issues:
		from github import Auth, Github, GithubException
		gh = Github(auth=Auth.Token(args.github_token))
		repo = gh.get_repo(args.github_repository)

	with tempfile.TemporaryDirectory() as tmpdir:
		client_pot = os.path.join(tmpdir, "client.pot")
		dashboard_pot = os.path.join(tmpdir, "dashboard.pot")

		subprocess.check_call(
				["xgettext",
				"--c++", "--from-code=UTF-8",
				"--keyword=_:1,1t",
				"--keyword=_S:1,1t",
				"--keyword=_cS:1c,2,2t",
				"--keyword=_F:1,1t",
				"--output", client_pot,
				"--package-name=WiVRn",
				] + sorted(list_files(os.path.join(root, "client"), ".cpp"))
				)

		subprocess.check_call(
				["xgettext",
				"--c++", "--kde", "--from-code=UTF-8",
				"--keyword=i18n:1",
				"--keyword=i18nc:1c,2",
				"--keyword=i18np:1,2",
				"--keyword=i18ncp:1c,2,3",
				"--output", dashboard_pot,
				"--package-name=WiVRn-dashboard",
				] + sorted(list_files(os.path.join(root, "dashboard"), [".cpp", ".qml"]))
				)

		client_ref = polib.pofile(client_pot)
		dashboard_ref = polib.pofile(dashboard_pot)

		for lang in args.lang:
			lang_issues = 0
			for po_ref, po in ((client_ref, os.path.join(locale_dir, lang, "wivrn.po")),
				   (dashboard_ref, os.path.join(locale_dir, lang, "wivrn-dashboard.po"))):
				if not os.path.exists(po):
					continue
				po_file = polib.pofile(po)
				po = os.path.relpath(po, cwd)
				entries = [i.msgid for i in po_file.translated_entries()]

				missing = 0
				for i in po_ref:
					if not i.obsolete:
						if not i.msgid in entries:
							missing = missing + 1
							print(f"::notice file={po}::{flag(lang)} Translation for {repr(i.msgid)} is missing")
				lang_issues += missing

				if missing > 0:
					print(f"::warning file={po}::{flag(lang)} {missing} translations missing")

			if args.manage_issues:
				issues = [i for i in repo.get_issues(state="all", labels = [ISSUE_LABEL]) if i.title == ISSUE_TITLE.format(lang=lang)]
				if lang_issues > 0:
					if issues:
						for issue in issues:
							if issue.state != "open":
								issue.edit(state="open")
					else:
						try:
							repo.create_issue(
									title=ISSUE_TITLE.format(lang=lang),
									labels=[ISSUE_LABEL]
									)
						except GithubException as e:
							# Ignore error when issues are disabled on repository
							if e.status != 410:
								raise
				else:
					for issue in issues:
						issue.edit(state="closed")
