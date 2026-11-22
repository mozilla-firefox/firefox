#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -vex

. ./taskcluster/docker/updatebot/updatebot-version.sh # Get UPDATEBOT_REVISION

HOME=$(python3 -c "import os;print(os.path.expanduser('~'))")
export HOME
GECKO_PATH="$PWD"
UPDATEBOT_PATH="$MOZ_FETCHES_DIR/updatebot"

# MOZ_FETCHES_DIR is in Z:/ format.  When we update the PATH we need to use
#   /z/ format.  Fortunately, we can translate them like so:
cd "$MOZ_FETCHES_DIR"
MOZ_FETCHES_PATH="$PWD"

#########################################################
# Install dependencies

# Move depot_tools
cd "$MOZ_FETCHES_DIR"
mv depot_tools.git depot_tools


# Generating a new version of the preloaded depot_tools download can be done by:
# 1) Running the task, uncommenting the variable assignment below, uncommenting the
#    _GENERATE_DEPOT_TOOLS_BINARIES_ section in taskcluster/kinds/updatebot/kind.yml,
#    and ensuring that an angle update will actually take place (so it downloads the depot_tools)
# 2) Downloading and sanity-checking the depot_tools-preloaded-binaries-GIT_HASH-DATE.zip artifact
# 3) Adding it to tooltool
# 4) Updating the updatebot manifest
# Note that even for the same git revision the downloaded tools can change, so they are tagged
# with both the git hash and the date it was generated

# export GENERATE_DEPOT_TOOLS_BINARIES=1

if test -n "$GENERATE_DEPOT_TOOLS_BINARIES"; then
    cp -r depot_tools depot_tools-from-git
fi

# Git is at /c/Program Files/Git/cmd/git.exe
#  It's in PATH for this script (confusingly) but not in PATH so we need to add it
export PATH="/c/Program Files/Git/cmd:$PATH"

# php & arcanist
if [ -n "$TOOLTOOL_MANIFEST" ]; then
 . "$GECKO_PATH/taskcluster/scripts/misc/tooltool-download.sh"
fi

cp "$MOZ_FETCHES_DIR/vcruntime140.dll" "$MOZ_FETCHES_DIR/php-win"
cp "$GECKO_PATH/taskcluster/docker/updatebot/windows-php.ini" "$MOZ_FETCHES_DIR/php-win/php.ini"

cd "$MOZ_FETCHES_DIR/arcanist"
patch -p1 < "$GECKO_PATH/taskcluster/docker/updatebot/arcanist_windows_stream.patch"
patch -p1 < "$GECKO_PATH/taskcluster/docker/updatebot/arcanist_patch_size.patch"
cd "$MOZ_FETCHES_DIR"

export PATH="$MOZ_FETCHES_PATH/php-win:$PATH"
export PATH="$MOZ_FETCHES_PATH/arcanist/bin:$PATH"

# get Updatebot
cd "$MOZ_FETCHES_DIR"
git clone https://github.com/mozilla-services/updatebot.git
cd updatebot
git checkout "$UPDATEBOT_REVISION"

# base python needs
python3 -m pip install --no-warn-script-location --user -U pip==26.2.2
python3 -m pip install --no-warn-script-location --user poetry wheel requests setuptools

# updatebot dependencies
cd "$UPDATEBOT_PATH"
python3 -m poetry install

# taskcluster secrets and writing out localconfig
cd "$GECKO_PATH"
python3 ./taskcluster/docker/updatebot/run.py "$GECKO_PATH" "$UPDATEBOT_PATH" "$MOZ_FETCHES_PATH"

# mercurial configuration
cp "$GECKO_PATH/taskcluster/docker/updatebot/hgrc" "$HOME/.hgrc"
# Windows is not happy with $HOME in the hgrc so we need to do a hack to replace it
#   with the actual value
( echo "cat <<EOF" ; cat "$HOME/.hgrc" ) | sh > tmp
mv tmp "$HOME/.hgrc"

# ssh known hosts
cp "$GECKO_PATH/taskcluster/docker/push-to-try/known_hosts" "$HOME/ssh_known_hosts"

#########################################################
# Run it
export PYTHONIOENCODING=utf8
export PYTHONUNBUFFERED=1

cd "$UPDATEBOT_PATH"
python3 -m poetry run python3 ./automation.py

#########################################################
if test -n "$GENERATE_DEPOT_TOOLS_BINARIES"; then
    # Artifacts

    cd "$MOZ_FETCHES_PATH"
    mv depot_tools depot_tools-from-tc

    # Clean out unneeded files
    # Need to use cmd because for some reason rm from bash throws 'Access Denied'
    cmd '/c for /d /r %i in (*__pycache__) do rmdir /s /q %i'
    rm -rf depot_tools-from-git/.git || true

    # Delete the files that are already in git
    find depot_tools-from-git -mindepth 1 -maxdepth 1 | sed s/depot_tools-from-git/depot_tools-from-tc/ | while read -r d; do rm -rf "$d"; done

    # Make the artifact
    rm -rf depot_tools-preloaded-binaries #remove it if it existed (i.e. we probably have one from tooltool already)
    mv depot_tools-from-tc depot_tools-preloaded-binaries

    # zip can't add symbolic links, and exits with an error code. || true avoids a script crash
    zip -r depot_tools-preloaded-binaries.zip depot_tools-preloaded-binaries/ || true

    # Convoluted way to get the git hash, because we don't have a .git directory
    # Adding extra print statements just in case we need to debug it
    GIT_HASH=$(grep depot_tools -A 1 "$GECKO_PATH/taskcluster/kinds/fetch/updatebot.yml" | tee /dev/tty | grep revision | tee /dev/tty | awk -F': *' '{print $2}' | tee /dev/tty)
    DATE=$(date -I)
    mv depot_tools-preloaded-binaries.zip "depot_tools-preloaded-binaries-$GIT_HASH-$DATE.zip"

    # Put the artifact into the directory we will look for it
    mkdir -p "$GECKO_PATH/obj-build/depot_tools" || true
    mv "depot_tools-preloaded-binaries-$GIT_HASH-$DATE.zip" "$GECKO_PATH/obj-build/depot_tools"
fi

#########################################################
echo "Killing SQL Proxy"
taskkill -f -im cloud_sql_proxy.exe || true
