#!/usr/bin/env bash

set -euvx

NSS="nss-3.125"
NSS_ARCHIVE="nss-3.125-with-nspr-4.39.tar.gz"
NSS_URL="https://ftp.mozilla.org/pub/security/nss/releases/NSS_3_125_RTM/src/${NSS_ARCHIVE}"
NSS_SHA256="fa3e7dcd151a7f3331a2dbf4cc57bcf4444e4c9c2b67ac00363409a7c4ecfa9d"

# End of configuration.

if [[ ! -f "$(pwd)/build-all.sh" ]]
then
    echo "build-all.sh must be executed from within the libs/ directory."
    exit 1
fi

if [[ "${#}" -ne 1 ]]
then
    echo "Usage:"
    echo "./build-all.sh [ios|android|desktop]"
    exit 1
fi

PLATFORM="${1}"

abspath () { case "${1}" in /*)printf "%s\\n" "${1}";; *)printf "%s\\n" "${PWD}/${1}";; esac; }
export -f abspath

if ! [[ -x "$(command -v gyp)" ]]; then
  echo 'Error: gyp needs to be installed and executable. Install gyp-next (https://github.com/nodejs/gyp-next); see docs/building.md for instructions.' >&2
  exit 1
fi

if ! [[ -x "$(command -v ninja)" ]]; then
  echo 'Error: ninja needs to be installed and executable. See https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages for install instructions.' >&2
  exit 1
fi

rm -rf "${NSS}"
if [[ ! -e "${NSS_ARCHIVE}" ]]; then
  echo "Downloading ${NSS_ARCHIVE}"
  curl -sfSL --retry 5 --retry-delay 10 -O "${NSS_URL}"
else
  echo "Using ${NSS_ARCHIVE}"
fi
# Integrity check for NSS
if ! echo "${NSS_SHA256}  ${NSS_ARCHIVE}" | shasum -a 256 -c - 
then
    echo "Error: ${NSS_ARCHIVE} was corrupted. Please try running this build script again."
    rm -f "${NSS_ARCHIVE}" # remove corrupted file
    exit 2
fi
tar xfz "${NSS_ARCHIVE}"
NSS_SRC_PATH=$(abspath "${NSS}")

# Some NSS symbols clash with OpenSSL symbols, rename them using
# C preprocessor define macros.
echo $'\
diff -r 65efa74ef84a coreconf/config.gypi
--- a/coreconf/config.gypi      Thu May 16 09:43:04 2019 +0000
+++ b/coreconf/config.gypi      Thu May 23 19:46:44 2019 -0400
@@ -138,6 +138,23 @@
       \'<(nspr_include_dir)\',
       \'<(nss_dist_dir)/private/<(module)\',
     ],
+    \'defines\': [
+      \'HMAC_Update=NSS_HMAC_Update\',
+      \'HMAC_Init=NSS_HMAC_Init\',
+      \'CMAC_Update=NSS_CMAC_Update\',
+      \'CMAC_Init=NSS_CMAC_Init\',
+      \'MD5_Update=NSS_MD5_Update\',
+      \'SHA1_Update=NSS_SHA1_Update\',
+      \'SHA256_Update=NSS_SHA256_Update\',
+      \'SHA224_Update=NSS_SHA224_Update\',
+      \'SHA512_Update=NSS_SHA512_Update\',
+      \'SHA384_Update=NSS_SHA384_Update\',
+      \'SEED_set_key=NSS_SEED_set_key\',
+      \'SEED_encrypt=NSS_SEED_encrypt\',
+      \'SEED_decrypt=NSS_SEED_decrypt\',
+      \'SEED_ecb_encrypt=NSS_SEED_ecb_encrypt\',
+      \'SEED_cbc_encrypt=NSS_SEED_cbc_encrypt\',
+    ],
     \'conditions\': [
       [ \'mozpkix_only==1 and OS=="linux"\', {
         \'include_dirs\': [
' | patch "${NSS_SRC_PATH}/nss/coreconf/config.gypi"

# Early return hack to prevent NSPR Android setup
# which does not work with ndk unified headers and clang.
echo $'\
@@ -2662,6 +2662,9 @@

 case "$target" in
 *-android*|*-linuxandroid*)
+    $as_echo "#define ANDROID 1" >>confdefs.h
+    ;;
+    unreachable)
     if test -z "$android_ndk" ; then
        as_fn_error $? "You must specify --with-android-ndk=/path/to/ndk when targeting Android." "$LINENO" 5
     fi
' | patch "${NSS_SRC_PATH}/nspr/configure"

if [[ "${PLATFORM}" == "ios" ]]
then
  ./build-all-ios.sh "${NSS_SRC_PATH}"
elif [[ "${PLATFORM}" == "android" ]]
then
  ./build-all-android.sh "${NSS_SRC_PATH}"
elif [[ "${PLATFORM}" == "desktop" ]]
then
  ./build-nss-desktop.sh "${NSS_SRC_PATH}"
elif [[ "${PLATFORM}" == "darwin" ]]
then
  ./build-nss-desktop.sh "${NSS_SRC_PATH}" "${PLATFORM}"
else
  echo "Unrecognized platform"
  exit 1
fi

echo "Cleaning up"
rm -rf "${NSS_SRC_PATH}"

echo "Done"
