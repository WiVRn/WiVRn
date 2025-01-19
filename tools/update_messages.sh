#!/bin/sh

cd $(dirname $0)/..

WIVRN_CLIENT_POT=$(mktemp)

xgettext \
	--c++ --from-code=UTF-8                 \
	--keyword=_:1,1t                              \
	--keyword=_S:1,1t                             \
	--keyword=_F:1,1t                             \
	--output=$WIVRN_CLIENT_POT                    \
	--package-name=WiVRn                          \
	$(find client/ -name "*.cpp" | sort)

sed -i 's/charset=CHARSET/charset=UTF-8/g' $WIVRN_CLIENT_POT

LANGS="es fr it ja"

for i in $LANGS
do
	mkdir -p locale/$i

	if [ ! -f locale/$i/wivrn.po ]
	then
		cp $WIVRN_CLIENT_POT locale/$i/wivrn.po
	else
		msgmerge --quiet --no-fuzzy-matching --update locale/$i/wivrn.po $WIVRN_CLIENT_POT
	fi
done

rm $WIVRN_CLIENT_POT

lupdate dashboard/*.{cpp,ui} -ts dashboard/wivrn_*.ts
