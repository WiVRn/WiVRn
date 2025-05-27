#!/bin/sh

if [ -d ".git" ] ; then
	git config --worktree user.email "git-am@invalid"
	git config --worktree user.name "no name"
	git config --worktree commit.gpgsign false
	git am --abort 2> /dev/null
	git checkout .
	exec git am --committer-date-is-author-date "$@"
fi

# There is no reliable way to apply a patch and succeed if it was
# already applied. Just ignore errors...
for f in "$@" ; do
	patch -p1 --forward < "$f"
done

exit 0
