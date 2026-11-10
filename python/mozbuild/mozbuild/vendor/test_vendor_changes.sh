#!/bin/bash

if [[ ! -f "CLOBBER" ]]; then
	echo "Script should be run from mozilla-central root"
	exit 1
fi

echo "THIS SCRIPT WILL REVERT AND PURGE UNCOMMIT LOCAL CHANGES"
echo "TYPE ok TO CONTINUE"
read CONFIRMATION
if [[ $CONFIRMATION != "ok" ]]; then
	echo "Did not get 'ok', exiting"
	exit 0
fi

ALL_MOZ_YAML_FILES=$(find . -name moz.yaml)

for f in $ALL_MOZ_YAML_FILES; do
	IFS='' read -r -d '' INPUT <<"EOF"
import sys
import yaml
with open(sys.argv[1]) as yaml_in:
	o = yaml.safe_load(yaml_in)
for t in o.get("updatebot", {}).get("tasks", ()):
	if t["type"] == "vendoring" and t.get("enabled", True):
		print(sys.argv[1])
		break
EOF

	FILE=$(python3 -c "$INPUT" $f)
	
	if [[ ! -z $FILE ]]; then
		UPDATEBOT_YAML_FILES+=("$FILE")
	fi
done


for FILE in "${UPDATEBOT_YAML_FILES[@]}"; do
	REVISION=$(yq eval ".origin.revision" $FILE)
	HAS_PATCHES=$(yq eval ".vendoring.patches | (. != null)" $FILE)
	
	echo "$FILE - $REVISION"
	if [[ $HAS_PATCHES == "false" ]]; then
		./mach vendor $FILE --force --revision $REVISION
		if [[ $? == 1 ]]; then
			exit 1
		fi
	else
		./mach vendor $FILE --force --revision $REVISION --patch-mode=none
		if [[ $? == 1 ]]; then
			exit 1
		fi
		./mach vendor $FILE --force --revision $REVISION --patch-mode=only --ignore-modified
		if [[ $? == 1 ]]; then
			exit 1
		fi
	fi
	hg revert .
	hg purge
done
