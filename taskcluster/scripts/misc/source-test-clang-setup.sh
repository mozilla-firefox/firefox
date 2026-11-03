#!/bin/bash
source $HOME/checkouts/gecko/taskcluster/scripts/misc/source-test-common.sh

# Add clang-tidy to PATH
export PATH=$MOZ_FETCHES_DIR/clang-tidy/bin:$PATH

# Use toolchain clang
export LD_LIBRARY_PATH=$MOZ_FETCHES_DIR/clang/lib

export MOZ_OBJDIR=$HOME/workspace/obj-static-analysis

# Write custom mozconfig
export MOZCONFIG=$GECKO_PATH/mozconfig

# Add to mozconfig all the appropriate options
cat <<EOT >> $MOZCONFIG
# Enable debug mode
ac_add_options --enable-debug
# Enable GC zeal, a testing and debugging feature that helps find GC-related bugs in JSAPI applications.
ac_add_options --enable-gczeal
# Do not treat warnings as errors
ac_add_options --disable-warnings-as-errors
# Avoid redundnant entries in compile_commands
ac_add_options --disable-unified-build
# We don't want to analyse tests
ac_add_options --disable-tests
# Generate the static-analysis compilation database during configure instead
# of in a separate config.status pass, and skip the unused FasterMake and
# Clangd backends
ac_add_options --build-backends=RecursiveMake,StaticAnalysis
EOT

# Mach lookup clang-tidy in clang-tools
mkdir -p $MOZBUILD_STATE_PATH/clang-tools
ln -s $MOZ_FETCHES_DIR/clang-tidy $MOZBUILD_STATE_PATH/clang-tools/clang-tidy
