#!/bin/sh

cd $(dirname $0)/..

echo Generating template
WIVRN_POT=$(mktemp)
xgettext --keyword=_:1,1t --keyword=_S:1,1t --keyword=_F:1,1t $(find client/ -name "*.cpp") --output=$WIVRN_POT --package-name=WiVRn
sed -i 's/charset=CHARSET/charset=UTF-8/g' $WIVRN_POT

for i in $(find client/locale -name wivrn.po)
do
	echo Updating $i
	msgmerge --quiet --no-fuzzy-matching --update $i $WIVRN_POT
done

rm $WIVRN_POT

lupdate dashboard/*.{cpp,ui} -ts dashboard/wivrn_*.ts
