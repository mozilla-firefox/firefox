#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# This script builds the official interpreter for the python language,
# while also packing in a few default extra packages.

set -e
set -x

# Required fetch artifact
python_src=${MOZ_FETCHES_DIR}/cpython-source
xz_prefix=${MOZ_FETCHES_DIR}/xz

# Make the compiler-rt available to clang.
env UPLOAD_DIR= $GECKO_PATH/taskcluster/scripts/misc/repack-clang.sh

# Extra setup per platform
case `uname -s` in
    Darwin)
        # Use taskcluster clang instead of host compiler on OSX
        . $GECKO_PATH/taskcluster/scripts/misc/macos-setup.sh
        # python records CC/CXX for later extension builds; keep them PATH-relative.
        export CC=clang
        export CXX=clang++
        # NOTE: both CFLAGS and CPPFLAGS need to be set here, otherwise
        # configure step fails.
        export CPPFLAGS="${MACOS_CFLAGS} -I${xz_prefix}/include"
        export CFLAGS=${MACOS_CFLAGS}
        export LDFLAGS="${MACOS_CFLAGS} -L${xz_prefix}/lib"

        if [ -d "${MOZ_FETCHES_DIR}/openssl" ]; then
            # Self-contained build: use the fetched openssl/xz toolchains rather
            # than relying on the worker shipping matching libraries (the arm64
            # workers ship neither). Their shared libs bake absolute install names
            # (/openssl, /xz) that don't exist at build time; the CI workers run
            # with SIP disabled, so DYLD_FALLBACK_LIBRARY_PATH lets the build-time
            # module import checks (and the pip bootstrap below) resolve them from
            # the fetch dirs by leaf name. The post-build fixup then rewrites the
            # references to @rpath so the shipped python stays relocatable.
            openssl_prefix=${MOZ_FETCHES_DIR}/openssl
            export DYLD_FALLBACK_LIBRARY_PATH=${openssl_prefix}/lib:${xz_prefix}/lib
            openssl_ssl_id=/openssl/lib/libssl.1.1.dylib
            openssl_crypto_id=/openssl/lib/libcrypto.1.1.dylib
            openssl_libssl_crypto_id=/openssl/lib/libcrypto.1.1.dylib
        else
            openssl_prefix=/usr/local/opt/openssl
            export DYLD_FALLBACK_LIBRARY_PATH=${xz_prefix}/lib:/usr/local/lib:/usr/lib
            openssl_ssl_id=/usr/local/opt/openssl@1.1/lib/libssl.1.1.dylib
            openssl_crypto_id=/usr/local/opt/openssl@1.1/lib/libcrypto.1.1.dylib
            openssl_libssl_crypto_id=/usr/local/Cellar/openssl@1.1/1.1.1h/lib/libcrypto.1.1.dylib
        fi
        configure_flags_extra=--with-openssl=${openssl_prefix}

        # see https://bugs.python.org/issue44065
        sed -i -e 's,$CC --print-multiarch,:,' ${python_src}/configure
        export LDFLAGS="${LDFLAGS} -Wl,-rpath -Wl,@loader_path/../.."
        ;;
    Linux)
        # Use host gcc on Linux
        export LDFLAGS="${LDFLAGS} -Wl,-rpath,\\\$ORIGIN/../.."
        # Without pkg-config, Python 3.12+ only finds the ncursesw headers through the include path.
        export CPPFLAGS="${CPPFLAGS} -I/usr/include/ncursesw"
        ;;
esac

# Patch Python to honor MOZPYTHONHOME instead of PYTHONHOME. That way we have a
# relocatable python for free, while not interfering with the system Python that
# already honors PYTHONHOME.
# Only rewrite the files that need it: touching the others would make the
# Makefile regenerate sources such as Modules/_blake2/blake2s_impl.c with the
# host python3, which is too old for Tools/clinic since Python 3.12.
grep -rlZ PYTHONHOME ${python_src} | xargs -0 perl -i -pe "s,PYTHONHOME,MOZPYTHONHOME,g"

# Actual build
work_dir=`pwd`
tardir=python

cd `mktemp -d`
${python_src}/configure --prefix=/${tardir} --enable-optimizations --with-lto ${configure_flags_extra} || { exit_status=$? && cat config.log && exit $exit_status ; }
# make re-adds these to the configure-recorded flags.
case `uname -s` in
    Darwin)
        unset CFLAGS CPPFLAGS LDFLAGS
        ;;
    Linux)
        # Escape $ORIGIN for make, which links the extension modules since Python 3.12.
        sed -i 's/\\\$ORIGIN/\\$$ORIGIN/g' Makefile
        unset CPPFLAGS LDFLAGS
        ;;
esac

export MAKEFLAGS=-j`nproc`
make
make DESTDIR=${work_dir} install
cd ${work_dir}

sysconfig_file=$(
  ls "${work_dir}/${tardir}/lib"/python3.*/*_sysconfigdata*.py 2>/dev/null \
  | head -n1
)
if [ -n "$sysconfig_file" ]; then
  cat >> "$sysconfig_file" << 'PYCODE'
import sys
build_time_vars = {
    k: v.replace("/python", sys.base_prefix) if isinstance(v, str) and v.startswith("/python") else v
    for k, v in build_time_vars.items()
}
PYCODE
fi

if ${work_dir}/python/bin/python3 -c "import sys; sys.exit(sys.version_info < (3, 12))"; then
    pip_version=24.0
else
    pip_version=23.0
fi
${work_dir}/python/bin/python3 -m pip install --upgrade pip==${pip_version}
${work_dir}/python/bin/python3 -m pip install -r ${GECKO_PATH}/build/psutil_requirements.txt -r ${GECKO_PATH}/build/zstandard_requirements.txt -r ${GECKO_PATH}/build/pyyaml_requirements.txt
${work_dir}/python/bin/python3 -c "import yaml; assert yaml.__with_libyaml__"
${work_dir}/python/bin/python3 -c "import curses"
case `uname -s` in
    Linux)
        readelf -d ${work_dir}/python/lib/python3.*/lib-dynload/_curses.*.so | grep -F '[$ORIGIN/../..]'
        ;;
esac

case `uname -s` in
    Darwin)

        cp ${openssl_prefix}/lib/libssl*.dylib ${work_dir}/python/lib/
        cp ${openssl_prefix}/lib/libcrypto*.dylib ${work_dir}/python/lib/
        cp ${xz_prefix}/lib/liblzma.dylib ${work_dir}/python/lib/
        cp ${xz_prefix}/lib/liblzma.5.dylib ${work_dir}/python/lib/

        # Instruct the loader to search for the lib in rpath instead of the one used during linking
        install_name_tool -change ${openssl_ssl_id} @rpath/libssl.1.1.dylib ${work_dir}/python/lib/python3.*/lib-dynload/_ssl.cpython-3*-darwin.so
        install_name_tool -change ${openssl_crypto_id} @rpath/libcrypto.1.1.dylib ${work_dir}/python/lib/python3.*/lib-dynload/_ssl.cpython-3*-darwin.so
        otool -L ${work_dir}/python/lib/python3.*/lib-dynload/_ssl.cpython-3*-darwin.so | grep @rpath/libssl.1.1.dylib

        # _hashlib links libcrypto too
        install_name_tool -change ${openssl_crypto_id} @rpath/libcrypto.1.1.dylib ${work_dir}/python/lib/python3.*/lib-dynload/_hashlib.cpython-3*-darwin.so
        otool -L ${work_dir}/python/lib/python3.*/lib-dynload/_hashlib.cpython-3*-darwin.so | grep @rpath/libcrypto.1.1.dylib


        install_name_tool -change /xz/lib/liblzma.5.dylib @rpath/liblzma.5.dylib ${work_dir}/python/lib/python3.*/lib-dynload/_lzma.cpython-3*-darwin.so
        otool -L ${work_dir}/python/lib/python3.*/lib-dynload/_lzma.cpython-3*-darwin.so | grep @rpath/liblzma.5.dylib

        # Also modify the shipped libssl to use the shipped libcrypto
        install_name_tool -change ${openssl_libssl_crypto_id} @rpath/libcrypto.1.1.dylib ${work_dir}/python/lib/libssl.1.1.dylib
        otool -L ${work_dir}/python/lib/libssl.1.1.dylib | grep @rpath/libcrypto.1.1.dylib

        # sanity check
        ${work_dir}/python/bin/python3 -c "import ssl"
        ${work_dir}/python/bin/python3 -c "import lzma"

        # We may not have access to system certificate on OSX
        ${work_dir}/python/bin/python3 -m pip install certifi==2026.10.19
        ;;
    Linux)
        cp /usr/lib/$(uname -m)-linux-gnu/libffi.so.* ${work_dir}/python/lib/
        cp /usr/lib/$(uname -m)-linux-gnu/libssl.so.* ${work_dir}/python/lib/
        cp /usr/lib/$(uname -m)-linux-gnu/libcrypto.so.* ${work_dir}/python/lib/
        cp /lib/$(uname -m)-linux-gnu/libncursesw.so.* ${work_dir}/python/lib/
        cp /lib/$(uname -m)-linux-gnu/libtinfo.so.* ${work_dir}/python/lib/
        ;;
esac

$(dirname $0)/pack.sh ${tardir}
