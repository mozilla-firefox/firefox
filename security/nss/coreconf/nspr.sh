#!/usr/bin/env bash
# This script builds NSPR for NSS.
#
# This build system is still under development.  It does not yet support all
# the features or platforms that the regular NSPR build supports.

# variables
nspr_cflags=
nspr_cxxflags=
nspr_ldflags=
nspr_dllflags=
nspr_opt=

# Try to avoid bmake on OS X and BSD systems
if hash gmake 2>/dev/null; then
    make() { command gmake "$@"; }
fi

nspr_set_flags()
{
    nspr_cflags="$CFLAGS $@"
    nspr_cxxflags="$CXXFLAGS $@"
    nspr_ldflags="$LDFLAGS $@"
    if [[ "$platform" = *_NT* ]]; then
        nspr_opt="--enable-win32-target=WIN95" # Use the same NSPR Windows target as Gecko.
    fi
}

# Echo the path from $2 to $1, given two absolute, symlink-free directories.
# Falls back to $1 unchanged if the two share no directory below the root.
nspr_relative_path()
{
    local src="$1" base="$2" up= shared=0 src_top base_top rel
    while true; do
        src_top="${src#/}"; src_top="${src_top%%/*}"
        base_top="${base#/}"; base_top="${base_top%%/*}"
        [ -n "$src_top" ] && [ "$src_top" = "$base_top" ] || break
        src="${src#"/$src_top"}"
        base="${base#"/$base_top"}"
        shared=1
    done
    if [ "$shared" = 0 ]; then
        echo "$src"
        return
    fi
    while [ -n "${base#/}" ]; do
        base="${base%/*}"
        up="../$up"
    done
    rel="$up${src#/}"
    rel="${rel%/}"
    echo "${rel:-.}"
}

nspr_build()
{
    # NSPR is built out of tree, into the dist it is installed into. Several
    # NSS checkouts routinely share one NSPR source tree, so an in-tree object
    # directory makes their builds clobber each other; keying it to the dist
    # also keeps separate --dist trees from one checkout independent.
    local nspr_src nspr_dir
    nspr_src=$(cd "$cwd"/../nspr && pwd -P)
    nspr_dir="$dist_dir"/nspr/$target
    mkdir -p "$nspr_dir"

    # NSPR's configure copies the path it was invoked by straight into
    # topsrcdir, srcdir and VPATH in every makefile it generates, so that path
    # has to be one the make that runs next can resolve. On Windows the shell
    # is MSYS but make is a native Windows program, and the two disagree about
    # absolute paths ("/d/x" versus "D:/x"); a relative path is read the same
    # way by both.
    local nspr_configure
    nspr_configure=$(nspr_relative_path "$nspr_src" "$nspr_dir")/configure

    # These NSPR options are directory-specific, so they don't need to be
    # included in nspr_opt and changing them doesn't force a rebuild of NSPR.
    extra_params=(--prefix="$dist_dir"/$target)
    if [ "$opt_build" = 1 ]; then
        extra_params+=(--disable-debug --enable-optimize)
    fi
    if [ "$target_arch" = "x64" ]; then
        extra_params+=(--enable-64bit)
    elif [ "$target_arch" = "arm64" ] || [ "$target_arch" = "aarch64" ]; then
        extra_params+=(--enable-64bit)
        if [ "$msvc" = 1 ]; then
            extra_params+=(--host=aarch64-pc-mingw32)
        fi
    fi
    extra_params+=($nspr_opt)

    if [[ -n "$CC" && -n "$build_tools_cc" && "$CC" != "$build_tools_cc" ]]; then
        # If build_tools_cc is specified, we expect CC to include a target
        # triple e.g. "CC=powerpc-linux-gnu-gcc". NSPR, confusingly, uses
        # "HOST_CC" to build tools like nsinstall that are called during the
        # build process and it uses the parameter "--host" to specify the
        # target triple for the build.
        HOST_CC="$build_tools_cc"
        extra_params+=(--host="${CC%-*}")
    fi

    echo "NSPR [1/5] configure ..."
    pushd "$nspr_dir" >/dev/null

    CFLAGS="$nspr_cflags" CXXFLAGS="$nspr_cxxflags" \
          LDFLAGS="$nspr_ldflags" DLLFLAGS="$nspr_dllflags" \
          HOST_CC="$HOST_CC" CC="$CC" CXX="$CCC" \
          run_verbose "$nspr_configure" "${extra_params[@]}" "$@"
    popd >/dev/null
    echo "NSPR [2/5] make ..."
    run_verbose make -C "$nspr_dir"

    if [ "$build_nspr_tests" = 1 ]; then
      echo "NSPR [3/5] build tests ..."
      run_verbose make -C "$nspr_dir/pr/tests"
    else
        echo "NSPR [3/5] NOT building tests"
    fi

    if [[ "$build_nspr_tests" = 1 && "$run_nspr_tests" = 1 ]]; then
      echo "NSPR [4/5] run tests ..."
      run_verbose make -C "$nspr_dir/pr/tests" runtests
    else
        echo "NSPR [4/5] NOT running tests"
    fi

    echo "NSPR [5/5] install ..."
    run_verbose make -C "$nspr_dir" install
}

nspr_clean()
{
    rm -rf "$dist_dir"/nspr/$target
}

set_nspr_path()
{
    local include=$(echo "$1" | cut -d: -f1)
    local lib=$(echo "$1" | cut -d: -f2)
    # Only the WINNT target defines WINNT in prcpucfg.h.
    if [[ "$platform" = *_NT* ]] && [ -f "$include/prcpucfg.h" ] &&
           grep -q 'define WINNT' "$include/prcpucfg.h"; then
        echo "$include is a WINNT-target NSPR; NSS only supports WIN95." 1>&2
        exit 1
    fi
    gyp_params+=(-Dnspr_include_dir="$include")
    gyp_params+=(-Dnspr_lib_dir="$lib")
}
