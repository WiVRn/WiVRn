#!/bin/bash

cd $(dirname $0)/..

WIVRN_CLIENT_POT=$(mktemp)
WIVRN_DASHBOARD_POT=$(mktemp)

xgettext \
	--c++ --from-code=UTF-8                       \
	--keyword=_:1,1t                              \
	--keyword=_S:1,1t                             \
	--keyword=_cS:1c,2,2t                         \
	--keyword=_F:1,1t                             \
	--output=$WIVRN_CLIENT_POT                    \
	--package-name=WiVRn                          \
	$(find client/ -name "*.cpp" | sort)

xgettext \
	--c++ --kde --from-code=UTF-8                 \
	--keyword=i18n:1                              \
	--keyword=i18nc:1c,2                          \
	--keyword=i18np:1,2                           \
	--keyword=i18ncp:1c,2,3                       \
	--output=$WIVRN_DASHBOARD_POT                 \
	--package-name=WiVRn-dashboard                \
	$(find dashboard/ -name "*.qml" -o -name "*.cpp" | sort)

sed -i 's/charset=CHARSET/charset=UTF-8/g' $WIVRN_CLIENT_POT $WIVRN_DASHBOARD_POT

RC=0

for i in locale/*
do
	if [ -f $i/wivrn.po ]
	then
		tools/check_po.py $WIVRN_CLIENT_POT $i/wivrn.po $(basename $i)
		if [ $? != 0 ]
		then
			RC=1
		fi
	fi

	if [ -f $i/wivrn-dashboard.po ]
	then
		tools/check_po.py $WIVRN_DASHBOARD_POT $i/wivrn-dashboard.po $(basename $i)

		if [ $? != 0 ]
		then
			RC=1
		fi
	fi
done

rm $WIVRN_CLIENT_POT $WIVRN_DASHBOARD_POT

exit $RC
