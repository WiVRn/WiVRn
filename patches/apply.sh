#!/bin/sh

set -e

if [ -d ".git" ] ; then
	git config user.email > /dev/null || git config user.email "git-am@invalid"
	git config user.name > /dev/null || git config user.name "no name"
	git config --local commit.gpgsign false
	git checkout .
	exec git am "$@"/*
fi

# There is no reliable way to apply a patch and succeed if it was
# already applied. Just ignore errors...
for f in "$@"/* ; do
	patch -p1 --forward < "$f"
done

exit 0
