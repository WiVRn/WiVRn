#!/bin/bash

cd $(dirname $0)/..

WIVRN_POT=$(mktemp)
xgettext --keyword=_:1,1t --keyword=_S:1,1t --keyword=_F:1,1t $(find client/ -name "*.cpp") --output=$WIVRN_POT --package-name=WiVRn
sed -i 's/charset=CHARSET/charset=UTF-8/g' $WIVRN_POT

RC=0

for i in $(find client/locale -name wivrn.po)
do
	msgmerge --quiet --no-fuzzy-matching $i $WIVRN_POT --output-file=$i.new

	pot_creation_date=$(grep -o "POT-Creation-Date: [-0-9:+ ]*" $i)
	sed -i "s/POT-Creation-Date: [-0-9:+ ]*/$pot_creation_date/" $i.new

	diff -U 3 -I '#.*' $i $i.new

	if [ $? != 0 ]
	then
		RC=1

		echo ::warning file=$i::$i is not up to date
	fi
done

rm $WIVRN_POT

for i in $(find dashboard -name "wivrn_*.ts")
do
	cp $i $i.bak
	lupdate -silent dashboard/*.{cpp,ui} -ts $i
	mv $i $i.new
	mv $i.bak $i

	diff -U 3 -I '.*<location.*/>' $i $i.new

	if [ $? != 0 ]
	then
		RC=1

		echo ::warning file=$i::$i is not up to date
	fi

	rm $i.new
done


exit $RC
