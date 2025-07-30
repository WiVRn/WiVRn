#!/bin/sh

cd $(dirname $0)/..

WIVRN_CLIENT_POT=$(mktemp)
WIVRN_DASHBOARD_POT=$(mktemp)

xgettext \
	--c++ --from-code=UTF-8              \
	--keyword=_:1,1t                     \
	--keyword=_S:1,1t                    \
	--keyword=_cS:1c,2,2t                \
	--keyword=_F:1,1t                    \
	--output=$WIVRN_CLIENT_POT           \
	--package-name=WiVRn                 \
	$(find client/ -name "*.cpp" | sort)

xgettext \
	--c++ --kde --from-code=UTF-8        \
	--keyword=i18n:1                     \
	--keyword=i18nc:1c,2                 \
	--keyword=i18np:1,2                  \
	--keyword=i18ncp:1c,2,3              \
	--output=$WIVRN_DASHBOARD_POT        \
	--package-name=WiVRn-dashboard       \
	$(find dashboard/ -name "*.qml" -o -name "*.cpp" | sort)

sed -i 's/charset=CHARSET/charset=UTF-8/g' $WIVRN_CLIENT_POT $WIVRN_DASHBOARD_POT

if [ "$#" -ge 1 ] ; then
	LANGS="$@"
else
	LANGS="$(ls -1 locale)"
fi

for i in $LANGS
do
	mkdir -p locale/$i

	if [ ! -f locale/$i/wivrn.po ]
	then
		cp $WIVRN_CLIENT_POT locale/$i/wivrn.po
	else
		msgmerge --quiet --no-fuzzy-matching --update locale/$i/wivrn.po $WIVRN_CLIENT_POT
	fi

	if [ ! -f locale/$i/wivrn-dashboard.po ]
	then
		cp $WIVRN_DASHBOARD_POT locale/$i/wivrn-dashboard.po
	else
		msgmerge --quiet --no-fuzzy-matching --update locale/$i/wivrn-dashboard.po $WIVRN_DASHBOARD_POT
	fi
done

rm $WIVRN_CLIENT_POT $WIVRN_DASHBOARD_POT
