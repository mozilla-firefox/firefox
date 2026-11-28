#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -e

# Update to an ICU release:
#   Usage: update-icu.sh <URL of ICU GIT> <release tag name>
#   E.g., for ICU 62.1: update-icu.sh https://github.com/unicode-org/icu.git release-62-1
#
# Update to an ICU maintenance branch:
#   Usage: update-icu.sh <URL of ICU GIT> <maintenance name>
#   E.g., for ICU 62.1: update-icu.sh https://github.com/unicode-org/icu.git maint/maint-62

if [ $# -lt 2 ]; then
  echo "Usage: update-icu.sh <URL of ICU GIT> <release tag name>"
  exit 1
fi

# Ensure that $Date$ in the checked-out git files expands timezone-agnostically,
# so that this script's behavior is consistent when run from any time zone.
export TZ=UTC

# Also ensure GIT-INFO is consistently English.
export LANG=en_US.UTF-8
export LANGUAGE=en_US
export LC_ALL=en_US.UTF-8

icu_dir=`dirname $0`/icu

# Remove intl/icu/source, then replace it with a clean export.
rm -rf ${icu_dir}/source
tmpclonedir=$(mktemp -d)
git clone --depth 1 --branch $2 $1 ${tmpclonedir}
cp -r ${tmpclonedir}/icu4c/source ${icu_dir}/source
cp ${tmpclonedir}/LICENSE ${icu_dir}/LICENSE

# Record `git log`.
# (This ensures that if ICU modifications are performed properly, it's always
# possible to run the command at the top of this script and make no changes to
# the tree.)
git -C ${tmpclonedir} log -1 > ${icu_dir}/GIT-INFO
icu_revision=$(git -C ${tmpclonedir} rev-parse HEAD)

# Clean up after ourselves.
rm -rf ${tmpclonedir}

# Remove layoutex, tests, and samples, but leave makefiles and test data in place.
find ${icu_dir}/source/layoutex -name '*Makefile.in' -prune -or -type f -print | xargs --no-run-if-empty rm
find ${icu_dir}/source/test -name '*Makefile.in' -prune -or -name 'testdata' -prune -or -type f -print | xargs --no-run-if-empty rm
find ${icu_dir}/source/samples -name '*Makefile.in' -prune -or -type f -print | xargs --no-run-if-empty rm

for patch in \
 bug-915735 \
 suppress-warnings.diff \
 bug-1198952-workaround-make-3.82-bug.diff \
 bug-1614941-dsb-hsb-dates.diff \
 bug-1636984-display-name-fractional-seconds.diff \
 bug-1636984-append-item-dayperiod-fractional-seconds.diff \
 bug-1706949-wasi-workaround.diff \
 bug-1790071-ICU-22132-standardize-vtzone-output.diff \
 double-conversion.diff \
 bug-1856290-ICU-20548-dateinterval-timezone.diff \
 bug-1954138-dtitvfmt-adopt-calendar.diff \
 bug-1972781-chinese-based-calendar.diff \
 bug-2000225-ICU-23264-increase-measure-unit-capacity.diff \
 bug-2000225-ICU-23262-missing-resource-error-for-iso8601-era.diff \
 bug-2002735-ICU-23277-coptic-single-era.diff \
 skip-norm-coll.diff \
 bug-1989738-remove-historical-eras.diff \
 bug-1989738-use-gregorian-era.diff \
 bug-1989738-gregorian-era-gannen.diff \
 bug-2028127-add-inherited-era-names.diff \
 bug-2028127-remove-historical-eras.diff \
 bug-2028443-CLDR-19362-CLDR-19382-london-tz-abbr.diff \
 bug-2029806-surrogates.diff \
 bug-2082211-CLDR-19827-windows-zones.diff \
; do
  echo "Applying local patch $patch"
  patch -d ${icu_dir}/../../ -p1 --no-backup-if-mismatch < ${icu_dir}/../icu-patches/$patch
done

topsrcdir=`dirname $0`/../
python3 ${topsrcdir}/js/src/tests/non262/String/make-normalize-generateddata-input.py $topsrcdir

# Update our moz.build files in config/external/icu, and build a new ICU data
# file.
python3 `dirname $0`/icu_sources_data.py $topsrcdir

# Record the release and revision for the SBOM.
icu_version=$(sed -n 's/^#define U_ICU_VERSION "\(.*\)"$/\1/p' ${icu_dir}/source/common/unicode/uvernum.h)
sed -e "s/^  release: .*/  release: \"${icu_version}\"/" \
    -e "s/^  revision: .*/  revision: \"${icu_revision}\"/" \
    ${icu_dir}/moz.yaml > ${icu_dir}/moz.yaml.new
mv ${icu_dir}/moz.yaml.new ${icu_dir}/moz.yaml

git -C "${topsrcdir}" add --all "intl/icu/source" "intl/icu/GIT-INFO" "intl/icu/LICENSE" "intl/icu/moz.yaml" "config/external/icu"

# Check local tzdata version.
`dirname $0`/update-tzdata.sh -c

# CLDR updates may lead to new language tag mappings, so we need to call make_intl_data.py, too.
echo "INFO: Please run 'js/src/builtin/intl/make_intl_data.py langtags' to update additional language tag files for SpiderMonkey."
