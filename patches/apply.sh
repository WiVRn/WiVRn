#!/bin/sh

if [ -d ".git" ] ; then
	git config --worktree user.email "git-am@invalid"
	git config --worktree user.name "no name"
	git config --worktree commit.gpgsign false
	git am --abort 2> /dev/null
	git checkout .
	exec git am --committer-date-is-author-date "$@"
fi

if ! type patch 2> /dev/null ; then
	echo "patch command not found" >&1
	exit 1
fi

# Skip patches that are already applied, fail on any other error.
for f in "$@" ; do
	if patch -p1 --reverse --force --dry-run --fuzz=0 < "$f" > /dev/null 2>&1 ; then
		echo "already applied: $f"
		continue
	fi
	patch -p1 --forward --batch --fuzz=0 < "$f" || exit 1
done

exit 0
