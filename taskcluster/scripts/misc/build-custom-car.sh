#!/bin/bash
set -x -e -v

# This script is for building a custom version of chromium-as-release

# First argument must be the artifact name
ARTIFACT_NAME=$(basename $TOOLCHAIN_ARTIFACT)
shift

# Use the rest of the arguments as the build config for gn
CONFIG=$(echo $* | tr -d "'")

# Android build flag
IS_ANDROID=false
if [[ "$ARTIFACT_NAME" == *"android"* ]]; then
  IS_ANDROID=true
fi

# Windows has a 260 character MAX_PATH limit, and some Chromium GCS
# dependencies (chromium-bidi's node_modules) unpack to paths that overflow it
# Use shorter directory names to avoid this.
CAR_DIR_NAME=custom_car
CHROMIUM_DIR_NAME=chromium
if [[ $(uname -o) == "Msys" ]]; then
  CAR_DIR_NAME=car
  CHROMIUM_DIR_NAME=cr
fi

mkdir "$CAR_DIR_NAME"
cd "$CAR_DIR_NAME"
CUSTOM_CAR_DIR=$PWD

# Setup depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git

# vpython resolves its deps through mozilla's no-index pip.conf mirror which lacks
# some pinned revisions. Bypass it with VPYTHON_BYPASS and directly grab them from
# chrome's artifact registry and temporarily override the pip.conf.
if [[ $(uname -s) == "Darwin" ]]; then
  # uv installs an arm64 native CPython 3.11 standalone (required version)
  if [[ -d "${MOZ_FETCHES_DIR}/uv" ]]; then
    export PATH="${MOZ_FETCHES_DIR}/uv:$PATH"
    export UV_PYTHON_INSTALL_DIR="${MOZ_FETCHES_DIR}/uv-python"
    uv venv --seed --python-preference only-managed --python 3.11 "${MOZ_FETCHES_DIR}/uv-venv"
    export VIRTUAL_ENV="${MOZ_FETCHES_DIR}/uv-venv"
    export PATH="${MOZ_FETCHES_DIR}/uv-venv/bin:$PATH"
  fi
  export VPYTHON_BYPASS="manually managed python not supported by chrome operations"
  PIP_CONFIG_FILE=/dev/null python3 -m pip install --quiet \
    --index-url https://us-python.pkg.dev/chrome-python-ar/chrome-python-ar/simple/ \
    "httplib2[socks]==0.13.1" "six==1.10.0" "requests==2.31.0" \
    "charset-normalizer==2.0.4" "urllib3==1.26.6" "idna==2.8" "brotli==1.0.9" \
    "tzdata==2023.4" "python-dateutil==2.7.3" "certifi==2021.5.30" \
    "zstandard==0.16.0"
  # Force cipd to mac-arm64 otherwise it may grab x64 rollup libs and the arm64 rollup native module goes missing.
  export ARCH_MAC_OVERRIDE=arm64
fi
export PATH="$PATH:$CUSTOM_CAR_DIR/depot_tools"
# Bug 1901936 changes to config upstream for depot tools path
if [[ $(uname -s) == "Linux" ]]; then
  export XDG_CONFIG_HOME=$CUSTOM_CAR_DIR
fi

# Log the current revision of depot tools for easier tracking in the future
DEPOT_TOOLS_REV=$(cd depot_tools && git rev-parse HEAD && cd ..)
echo "Current depot_tools revision: $DEPOT_TOOLS_REV"

# Bug 2070172 - upstream `fetch` now runs through depot_tools' hermetic
# python-bin wrapper, which refuses to run on a checkout that has never been
# bootstrapped, and nothing bootstraps it before the `fetch` below. Windows
# is already covered by the `gclient` call it runs first.
if [[ $(uname -o) != "Msys" ]]; then
  ensure_bootstrap
fi


# Set up some env variables depending on the target OS
# Linux is the default case, with minor adjustments for
# android since it is built with a linux host

# Final folder structure before compressing is
# the same for linux and windows
FINAL_BIN_PATH="src/out/Default"

# Final binary name for autoninja build sequence
FINAL_BIN=chrome

# Unique substring for PGO data for Linux
PGO_SUBSTR="chrome-linux-main"

# Default (non android) fetch name for upstream src
FETCH_NAME=chromium

# Android specific vars
if [ "$IS_ANDROID" = true ]; then
  FETCH_NAME=android
  PGO_SUBSTR="android64"
  FINAL_BIN_PATH="src/out/Default/apks"
  FINAL_BIN=chrome_public_apk
fi

# Logic for macosx64
if [[ $(uname -s) == "Darwin" ]]; then
  MACOS_SYSROOT=$(ls -d "$MOZ_FETCHES_DIR"/MacOSX*.sdk)
  export MACOS_SYSROOT
  CONFIG=$(echo $CONFIG mac_sdk_path='"//out/Default/MacOSX.sdk"')

  PGO_SUBSTR="chrome-mac-arm-main"

  # macOS final build folder is different than linux/win
  FINAL_BIN_PATH="src/out/Default/Chromium.app"
fi

# Logic for win64 using the mingw environment
if [[ $(uname -o) == "Msys" ]]; then
  # Setup VS 2022
  . $GECKO_PATH/taskcluster/scripts/misc/vs-setup.sh

  # Setup some environment variables for chromium build scripts
  export DEPOT_TOOLS_WIN_TOOLCHAIN=0
  export GYP_MSVS_OVERRIDE_PATH="$MOZ_FETCHES_DIR/VS"
  export GYP_MSVS_VERSION=2026
  export vs2026_install="$MOZ_FETCHES_DIR/VS"
  export WINDOWSSDKDIR="$MOZ_FETCHES_DIR/VS/Windows Kits/10"
  export DEPOT_TOOLS_UPDATE=1
  export GCLIENT_PY3=1
  # Fool GYP
  touch "$MOZ_FETCHES_DIR/VS/VC/vcvarsall.bat"

  # Construct some of our own dirs and move VS dlls + other files
  # to a path that chromium build files & scripts are expecting
  mkdir chrome_dll
  cd chrome_dll
  mkdir system32
  cd ../
  pushd "$WINDOWSSDKDIR"
  mkdir -p Debuggers/x64/
  popd
  MSVC_REDIST_VERSION=$(ls "$MOZ_FETCHES_DIR/VS/VC/Redist/MSVC" | grep -E '^[0-9]' | sort -V | tail -1)
  [[ -n "$MSVC_REDIST_VERSION" ]] || { echo "ERROR: no numeric MSVC redist version found"; exit 1; }
  mv "$MOZ_FETCHES_DIR/VS/VC/Redist/MSVC/$MSVC_REDIST_VERSION/x64"/Microsoft.VC*.CRT/* chrome_dll/system32/
  mv "$WINDOWSSDKDIR/App Certification Kit/"* "$WINDOWSSDKDIR"/Debuggers/x64/
  export WINDIR="$PWD/chrome_dll"

  # Run glcient once first to get some windows deps
  gclient

  # Ensure we don't use WIN32 profdata with this unique sub string
  PGO_SUBSTR="chrome-win64-main"
fi

# Get chromium source code and dependencies
mkdir "$CHROMIUM_DIR_NAME"
cd "$CHROMIUM_DIR_NAME"

fetch --no-history --nohooks $FETCH_NAME

# Setup the .gclient file to ensure pgo profiles are downloaded.
# For some reason we need to set --name flag even though it already exists.
# Currently the gclient.py file does NOT recognize --custom-var as it's own argument
gclient config --name src "https://chromium.googlesource.com/chromium/src.git" --custom-var="checkout_pgo_profiles=True" --unmanaged

cd src

# Log the current revision of the chromium src for easier tracking in the future
CHROMIUM_REV=$(git rev-parse HEAD)
echo "Current chromium revision: $CHROMIUM_REV"

# Amend gclient file
if [ "$IS_ANDROID" = true ]; then
  echo "target_os = [ 'android' ]" >> ../.gclient
fi

if [[ $(uname -o) == "Msys" ]]; then
  # For fast fetches it seems we will be missing some dummy files in windows.
  # We can create a dummy this way to satisfy the rest of the build sequence.
  # This is ok because we are not doing any development here and don't need
  # the development history, but this file is still needed to proceed.
  python3 build/util/lastchange.py -o build/util/LASTCHANGE

  # Override the SDK version hardcoded upstream to match our fetched Windows SDK.
  echo "Using Windows SDK version: ${SDK_VERSION}"
  sed -i "s/SDK_VERSION = '[0-9.]*'/SDK_VERSION = '${SDK_VERSION}'/" build/vs_toolchain.py
  sed -i "s/SDK_VERSION = '[0-9.]*'/SDK_VERSION = '${SDK_VERSION}'/" build/toolchain/win/setup_toolchain.py

  # Upstream targets a newer NTDDI level than our packaged SDK defines. An
  # unknown NTDDI_VERSION token silently evaluates to 0 in the preprocessor,
  # which turns off every SDK version guard and breaks windows.h itself, so
  # clamp it to the newest level this SDK knows about (WDK_NTDDI_VERSION).
  SDKDDKVER_H="$WINDOWSSDKDIR/Include/$SDK_VERSION/shared/sdkddkver.h"
  NTDDI_MAX=$(tr -d '\r' < "$SDKDDKVER_H" | awk '$1 == "#define" && $2 == "WDK_NTDDI_VERSION" { print $3 }')
  [[ -n "$NTDDI_MAX" ]] || { echo "ERROR: could not read WDK_NTDDI_VERSION from $SDKDDKVER_H"; exit 1; }
  echo "Clamping NTDDI_VERSION to: ${NTDDI_MAX}"
  sed -i "s/NTDDI_VERSION=NTDDI_[A-Z0-9_]*/NTDDI_VERSION=${NTDDI_MAX}/" build/config/win/BUILD.gn
  grep -q "NTDDI_VERSION=${NTDDI_MAX}" build/config/win/BUILD.gn || \
    { echo "ERROR: NTDDI_VERSION patch failed - upstream BUILD.gn may have changed"; exit 1; }

  # Large ts_library() targets (e.g. settings) overflow Windows' 32K command
  # line limit with locally executed actions, so pass --in_files via a response file.
  python3 - <<'EOF'
import sys
gni = "tools/typescript/ts_library.gni"
py = "tools/typescript/ts_library.py"
patches = [
    (gni, 'args += [ "--in_files" ] + in_files',
     'response_file_contents = in_files\n      args += [ "--in_files_rsp", "{{response_file_name}}" ]'),
    (py, "  args = parser.parse_args(argv)\n",
     "  parser.add_argument('--in_files_rsp')\n"
     "  args = parser.parse_args(argv)\n"
     "  if args.in_files_rsp:\n"
     "    with open(args.in_files_rsp) as f:\n"
     "      args.in_files = f.read().split()\n"),
]
for path, old, new in patches:
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if src.count(old) != 1:
        sys.exit(f"ERROR: ts_library patch failed - upstream {path} may have changed")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(src.replace(old, new))
EOF
fi

if [[ $(uname -s) == "Linux" ]] || [[ $(uname -s) == "Darwin" ]]; then
  # Bug 1847210
  # Modifications to how the dirname and depot_tools and other env variables
  # change how cipd is setup for Mac and Linux.
  # Easily resolved by just running the setup script.
  source ./third_party/depot_tools/cipd_bin_setup.sh
  cipd_bin_setup
fi

# fetch --nohooks skips CIPD deps. re-sync on macOS & Android to pull them.
if [ "$IS_ANDROID" = true ] || [[ $(uname -s) == "Darwin" ]]; then
  gclient sync
fi

# Now we can run hooks and fetch PGO + everything else
gclient runhooks

# PGO data should be in src/chrome/build/pgo_profiles/
# with a name like "chrome-{OS}-<some unique identifier>"
export PGO_DATA_DIR="$CUSTOM_CAR_DIR/$CHROMIUM_DIR_NAME/src/chrome/build/pgo_profiles"
for entry in "$PGO_DATA_DIR"/*
do
  if [ -f "$entry" ]; then
    if [[ "$entry" == *"$PGO_SUBSTR"* ]]; then
        echo "Found the correct profdata"
        export PGO_DATA_PATH="$entry"
        break
    fi
  fi
done

PGO_FILE=$PGO_DATA_PATH
if [[ $(uname -o) == "Msys" ]]; then
  # gn resolves a bare `inputs` entry relative to the directory of the BUILD.gn
  # declaring it, so hand the build just the file name and move the profile into
  # that directory below.
  PGO_FILE=$(basename "$PGO_DATA_PATH")
  mv $PGO_DATA_PATH build/config/compiler/pgo/
fi

CONFIG=$(echo $CONFIG pgo_data_path='"'$PGO_FILE'"')

# Set up then build chrome
if [[ $(uname -s) == "Darwin" ]]; then
  # Bug 2045375/2066139: Chromium requires sdk_inputs to be inside root_build_dir.
  mkdir -p out/Default
  ln -s "$MACOS_SYSROOT" out/Default/MacOSX.sdk
fi

gn gen out/Default --args="$CONFIG"
autoninja -C out/Default $FINAL_BIN

# Make artifact smaller for win/linux
if [[ $(uname -s) == "Linux" ]] || [[ $(uname -o) == "Msys" ]]; then
  if [ "$IS_ANDROID" = false ]; then
    rm -rf out/Default/.ninjadeps
    rm -rf out/Default/*_deps
    rm -rf out/Default/gen/*
    rm -rf out/Default/obj/*
    rm -rf out/Default/thinlto-cache
    rm -rf out/Default/*/gen/*
    rm -rf out/Default/*/obj/*
  fi
fi

# Gather binary and related files into a zip, and upload it
cd ..
mkdir chromium

mv "$FINAL_BIN_PATH" chromium
chmod -R +x chromium

tar -c chromium | python3 $GECKO_PATH/taskcluster/scripts/misc/zstdpy > $ARTIFACT_NAME

mkdir -p $UPLOAD_DIR
mv "$ARTIFACT_NAME" "$UPLOAD_DIR"

if [ "$IS_ANDROID" = true ]; then
  # Package up symbols (lib.unstripped) from android build separately.
  SYM_ARTIFACT="car_android_symbols.tar.zst"
  SYM_DIR="src/out/Default/lib.unstripped"
  tar -C "$(dirname "$SYM_DIR")" -c "$(basename "$SYM_DIR")" \
    | python3 "$GECKO_PATH/taskcluster/scripts/misc/zstdpy" > "$SYM_ARTIFACT"
  mv "$SYM_ARTIFACT" "$UPLOAD_DIR"
fi
