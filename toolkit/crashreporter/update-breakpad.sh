#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -v -e -x

# Usage: update-breakpad.sh <path to breakpad git clone> [rev, defaults to HEAD]

if [ $# -lt 1 ]; then
  echo "Usage: update-breakpad.sh /path/to/breakpad/src [rev]"
  exit 1
fi

crashreporter_dir=`realpath $(dirname $0)`
repo=${crashreporter_dir}/../..
moz_yaml=$(mktemp)
cp ${crashreporter_dir}/google-breakpad/moz.yaml ${moz_yaml}
rm -rf ${crashreporter_dir}/google-breakpad

breakpad_repo=$1
rev=${2-HEAD}
(cd $breakpad_repo; git archive --prefix=toolkit/crashreporter/google-breakpad/ $rev) | (cd $repo; tar xf -)
# Breakpad uses gclient for externals, so manually export what we need.
lss_rev=$(cd $breakpad_repo; git show ${rev}:DEPS | python -c "import sys; exec(sys.stdin.read()); sys.stdout.write('%s\n' % deps['src/src/third_party/lss'].split('@')[1])")
(cd $breakpad_repo/src/third_party/lss; git archive --prefix=toolkit/crashreporter/google-breakpad/src/third_party/lss/ $lss_rev) | (cd $repo; tar xf -)

# remove some extraneous bits
# We've forked src/client toolkit/crashreporter/breakpad-client.
rm -rf \
  ${crashreporter_dir}/google-breakpad/appveyor.yml \
  ${crashreporter_dir}/google-breakpad/autotools/ \
  ${crashreporter_dir}/google-breakpad/docs/ \
  ${crashreporter_dir}/google-breakpad/m4/ \
  ${crashreporter_dir}/google-breakpad/scripts/ \
  ${crashreporter_dir}/google-breakpad/src/client/ \
  ${crashreporter_dir}/google-breakpad/src/processor/testdata/ \
  ${crashreporter_dir}/google-breakpad/src/testing/ \
  ${crashreporter_dir}/google-breakpad/src/third_party/linux \
  ${crashreporter_dir}/google-breakpad/src/third_party/protobuf \
  ${crashreporter_dir}/google-breakpad/src/tools/gyp/ \
  ${crashreporter_dir}/google-breakpad/src/tools/windows/dump_syms/testdata/ \
  ${crashreporter_dir}/google-breakpad/.github/mistaken-pull-closer.yml \
  ${crashreporter_dir}/google-breakpad/.travis.yml

# restore our Makefile.ins
hg -R ${repo} st -n | grep "Makefile\.in$" | xargs hg revert --no-backup
# and moz.build files
hg -R ${repo} st -n | grep "moz\.build$" | xargs hg revert --no-backup
# and some other makefiles
hg -R ${repo} st -n | grep "objs\.mozbuild$" | xargs hg revert --no-backup

# Record git rev
breakpad_revision=$(cd $breakpad_repo; git rev-parse $rev)
echo ${breakpad_revision} > ${crashreporter_dir}/google-breakpad/GIT-INFO
sed -e "s/^  release: .*/  release: \"${breakpad_revision}\"/" \
    -e "s/^  revision: .*/  revision: \"${breakpad_revision}\"/" \
    ${moz_yaml} > ${crashreporter_dir}/google-breakpad/moz.yaml
rm ${moz_yaml}

# Apply any local patches
shopt -s nullglob
for p in ${crashreporter_dir}/breakpad-patches/*.patch; do
    if grep -q -e "--git" $p; then
        patch_opts="-p1"
    else
        patch_opts="-p0"
    fi
    echo "Applying $p"
    if ! filterdiff -x '*/Makefile*' $p | \
        patch -d ${crashreporter_dir}/google-breakpad ${patch_opts}; then
      echo "Failed to apply $p"
      exit 1
    fi
done
# remove any .orig files that snuck in
find ${crashreporter_dir}/google-breakpad -name "*.orig" -exec rm '{}' \;

hg addremove ${crashreporter_dir}/google-breakpad/
