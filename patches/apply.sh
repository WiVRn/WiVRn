#!/bin/sh
# There is no reliable way to apply a patch and succeed if it was
# already applied. Just ignore errors...
for f in "$@"/* ; do
	patch -p1 --forward < "$f"
done

exit 0
