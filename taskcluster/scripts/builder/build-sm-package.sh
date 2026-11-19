#!/bin/bash

set -xe

# Default variables values.
: ${WORK:=$HOME/workspace}

mkdir -p $UPLOAD_DIR

# Package up the sources into the release tarball.
AUTOMATION=1 DIST=$UPLOAD_DIR $GECKO_PATH/js/src/make-source-package.py

# Extract the tarball into a new directory in the workspace.

PACKAGE_DIR=$WORK/sm-package

# Do not use -p option because the package directory should not exist.
mkdir $PACKAGE_DIR
pushd $PACKAGE_DIR

tar -xvf $UPLOAD_DIR/mozjs-*.tar.*z*

: ${PYTHON3:=python3}

status=0
(
    # Build the freshly extracted, packaged SpiderMonkey.
    cd ./mozjs-*
    AUTOMATION=1 $PYTHON3 js/src/devtools/automation/autospider.py --skip-tests=checks $SPIDERMONKEY_VARIANT
) || status=$?

if [ $status -eq 0 ]; then
    set +e
    (
        set -e
        cd ./mozjs-*
        INSTALL_DIR=$WORK/sm-install
        MOZCONFIG=mozconfig.autospider DESTDIR=$INSTALL_DIR $PYTHON3 mach build install
        prefix=$INSTALL_DIR$(pwd -P)/obj-spider/dist
        v=$(sed -n '/^[0-9]/{s/\.[0-9.]*//p;q}' config/milestone.txt)
        major=${v%%[ab]*}
        for file in lib/pkgconfig/mozjs-$v.pc lib/libmozjs-$v.so include/mozjs-$v/jsapi.h bin/js$major-config bin/js$major; do
            if [ ! -e "$prefix/$file" ]; then
                echo "TEST-UNEXPECTED-FAIL | build-sm-package.sh | $file was not installed"
                exit 1
            fi
        done
        cd "$prefix"
        find . -type f -printf '%m %P\n' | sort > $UPLOAD_DIR/install-files.txt
        find include -type f | sort | xargs sha256sum >> $UPLOAD_DIR/install-files.txt
    )
    status=$?
    set -e
fi

# Copy artifacts for upload by TaskCluster
cp -rL ./mozjs-*/obj-spider/dist/bin/{js,jsapi-tests,js-gdb.py,libmozjs*} $UPLOAD_DIR

exit $status
