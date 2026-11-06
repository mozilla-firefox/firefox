#!/usr/bin/env bash
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
################################################################################
#
# This script builds NSS with gyp and ninja.
#
# This build system is still under development.  It does not yet support all
# the features or platforms that NSS supports.

set -e

cwd=$(cd $(dirname $0); pwd -P)
dist_dir="$cwd/../dist"

# --dist has to be resolved before the main argument loop, because --rebuild
# reads the saved argument list from inside the dist directory.
dist_flag_pending=0
for arg in "$@"; do
    if [ "$dist_flag_pending" = 1 ]; then
        dist_dir="$arg"
        dist_flag_pending=0
        continue
    fi
    case "$arg" in
        --dist=?*) dist_dir="${arg#*=}" ;;
        --dist) dist_flag_pending=1 ;;
    esac
done

argsfile="$dist_dir/build_args"
source "$cwd/coreconf/nspr.sh"
source "$cwd/coreconf/sanitizers.sh"
GYP=${GYP:-gyp}

# Usage info
show_help()
{
    cat "$cwd/help.txt"
}

run_verbose()
{
    if [ "$verbose" = 1 ]; then
        echo "$@"
        exec 3>&1
        "$@" 1>&3 2>&3
        exec 3>&-
    else
        "$@" >/dev/null
    fi
}

# The prehistoric bash on Mac doesn't support @Q quoting.
# The consequences aren't that serious, unless there are odd arrangements of spaces.
if /usr/bin/env bash -c 'x=1;echo "${x@Q}"' >/dev/null 2>&1; then
    Q() { echo "${@@Q}"; }
else
    Q() { echo "$@"; }
fi

if [ -n "$CCC" ] && [ -z "$CXX" ]; then
    export CXX="$CCC"
fi

opt_build=0
build_64=0
clean=0
rebuild_gyp=0
rebuild_nspr=0
build_nspr_tests=0
run_nspr_tests=0
exit_after_nspr=0
target=Debug
verbose=0
fuzz=0
fuzz_tls=0
fuzz_oss=0
no_local_nspr=0
sslkeylogfile=1
static_libs=0
clang_cl=0

gyp_params=(--depth="$cwd" --generator-output=".")
ninja_params=()

# Assume that MSVC is wanted if this is running on windows.
platform=$(uname -s)
if [ "${platform%-*}" = "MINGW32_NT" -o "${platform%-*}" = "MINGW64_NT" -o "${platform%%-*}" = "MSYS_NT" ]; then
    msvc=1
fi

# Parse command line arguments.
all_args=("$@")
while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild)
            if [[ ! -e "$argsfile" ]]; then
                echo "Unable to rebuild" 1>&2
                exit 2
            fi
            IFS=$'\r\n' GLOBIGNORE='*' command eval  'previous_args=($(<"$argsfile"))'
            exec /usr/bin/env bash -c "$(Q "$0")"' "$@"' "$0" "${previous_args[@]}"
            ;;
        -c) clean=1 ;;
        -cc) clean_only=1 ;;
        -v) ninja_params+=(-v); verbose=1 ;;
        -j) ninja_params+=(-j "$2"); shift ;;
        --gyp|-g) rebuild_gyp=1 ;;
        --opt|-o) opt_build=1 ;;
        -m32|--m32) target_arch=ia32; echo 'Warning: use -t instead of -m32' 1>&2 ;;
        -t|--target) target_arch="$2"; shift ;;
        --target=*) target_arch="${1#*=}" ;;
        --build-tools-cc=*) build_tools_cc="${1#*=}" ;;
        --clang) export CC=clang; export CCC=clang++; export CXX=clang++; msvc=0 ;;
        --gcc) export CC=gcc; export CCC=g++; export CXX=g++; msvc=0 ;;
        --msvc) msvc=1 ;;
        --clang-cl) msvc=1; clang_cl=1 ;;
        --scan-build) enable_scanbuild  ;;
        --scan-build=?*) enable_scanbuild "${1#*=}" ;;
        --disable-tests) gyp_params+=(-Ddisable_tests=1) ;;
        --pprof) gyp_params+=(-Duse_pprof=1) ;;
        --asan) enable_sanitizer asan ;;
        --msan) enable_sanitizer msan ;;
        --tsan) enable_sanitizer tsan ;;
        --sourcecov) enable_sourcecov ;;
        --ubsan) enable_ubsan ;;
        --ubsan=?*) enable_ubsan "${1#*=}" ;;
        --fuzz) fuzz=1 ;;
        --fuzz=oss) fuzz=1; fuzz_oss=1 ;;
        --fuzz=tls) fuzz=1; fuzz_tls=1 ;;
        --sancov) enable_sancov; gyp_params+=(-Dcoverage=1) ;;
        --sancov=?*) enable_sancov "${1#*=}"; gyp_params+=(-Dcoverage=1) ;;
        --emit-llvm) gyp_params+=(-Demit_llvm=1 -Dsign_libs=0) ;;
        --no-zdefs) gyp_params+=(-Dno_zdefs=1) ;;
        --static) static_libs=1; gyp_params+=(-Dstatic_libs=1) ;;
        --ct-verif) gyp_params+=(-Dct_verif=1) ;;
        --nspr) rebuild_nspr=1 ;;
        --nspr-test-build) build_nspr_tests=1 ;;
        --nspr-test-run) run_nspr_tests=1 ;;
        --nspr-only) exit_after_nspr=1 ;;
        --dist=?*) ;; # resolved before this loop
        --dist) shift ;; # resolved before this loop
        --with-nspr=?*) set_nspr_path "${1#*=}"; no_local_nspr=1 ;;
        --system-nspr) set_nspr_path "/usr/include/nspr/:"; no_local_nspr=1 ;;
        --system-sqlite) gyp_params+=(-Duse_system_sqlite=1) ;;
        --enable-fips) gyp_params+=(-Ddisable_fips=0) ;;
        --fips-module-id) gyp_params+=(-Dfips_module_id="$2"); shift ;;
        --fips-module-id=?*) gyp_params+=(-Dfips_module_id="${1#*=}") ;;
        --enable-libpkix) gyp_params+=(-Ddisable_libpkix=0) ;;
        --mozpkix-only) gyp_params+=(-Dmozpkix_only=1 -Ddisable_tests=1 -Dsign_libs=0) ;;
        --disable-keylog) sslkeylogfile=0 ;;
        --enable-legacy-db) gyp_params+=(-Ddisable_dbm=0) ;;
        --mozilla-central) gyp_params+=(-Dmozilla_central=1) ;;
        --python) python="$2"; shift ;;
        --python=*) python="${1#*=}" ;;
        -D*) gyp_params+=("$1") ;;
        *) show_help; exit 2 ;;
    esac
    shift
done

if [ "$opt_build" = 1 ] && [ "$fuzz" = 1 ]; then
    echo "Specifiying --opt with --fuzz is not supported." >&2
    exit 1
fi

if [ -n "${sanitizers["tsan"]:-}" ] && ([ "$CC" = "gcc" ] ||
                                        [ "$CCC" = "g++" ] ||
                                        [ "$CXX" = "g++" ]); then
    echo "Specifying --tsan with gcc results in false positives whilst building NSPR." >&2
    exit 1
fi

if [ -n "$python" ]; then
    gyp_params+=(-Dpython="$python")
fi

if [ -z "$target_arch" ]; then
    # Assume that the target architecture is the same as the host by default.
    host_arch=$(${python:-python3} "$cwd/coreconf/detect_host_arch.py")
    target_arch=$host_arch
fi

# Set the target architecture and build type.
gyp_params+=(-Dtarget_arch="$target_arch")
if [ "$opt_build" = 1 ]; then
    target=Release
else
    target=Debug
fi

# When cross-compiling, system zlib for the target architecture may not be available
if [[ -n "$CC" && -n "$build_tools_cc" && "$CC" != "$build_tools_cc" ]]; then
    gyp_params+=(-Duse_system_zlib=0 -Dsign_libs=0)
fi

gyp_params+=(-Denable_sslkeylogfile="$sslkeylogfile")

# Do special setup.
if [ "$fuzz" = 1 ]; then
    source "$cwd/coreconf/fuzz.sh"
fi

if [ "$msvc" = 1 ]; then
    source "$cwd/coreconf/msvc.sh"
fi

# clang-cl compiles against the headers and libraries msvc.sh just set up.
if [ "$clang_cl" = 1 ]; then
    if ! clang_cl_path=$(command -v clang-cl); then
        echo "Can't find clang-cl on the path, aborting" 1>&2
        exit 1
    fi
    # ninja runs the tools natively and gyp would prefix a relative path, so
    # gyp needs the directory as an absolute Windows path.
    gyp_params+=(-Dclang_cl=1 -Dcc_is_clang=1)
    gyp_params+=(-Dllvm_bindir="$(cygpath -m "$(dirname "$clang_cl_path")")")
    # NSPR's configure takes the compiler from the environment.
    case "$target_arch" in
        ia32) CC="clang-cl -m32" ;;
        arm64|aarch64) CC="clang-cl --target=aarch64-pc-windows-msvc" ;;
        *) CC=clang-cl ;;
    esac
    export CC CCC="$CC" CXX="$CC"
    if [ -n "$sanitizer_flags" ]; then
        clang_cl_sanitizers
    fi
fi

nspr_set_flags $sanitizer_flags
if [ ! -z "$sanitizer_flags" ]; then
    gyp_params+=(-Dsanitizer_flags="$sanitizer_flags")
fi

# -c = clean first
if [ "$clean" = 1 -o "$clean_only" = 1 ]; then
    nspr_clean
    rm -rf "$cwd/out"
    rm -rf "$dist_dir"
    # -cc = only clean, don't build
    if [ "$clean_only" = 1 ]; then
        echo "Cleaned"
        exit 0
    fi
fi

# Setup build paths.
target_dir="$cwd/out/$target"
mkdir -p "$target_dir"
dist_dir=$(mkdir -p "$dist_dir"; cd "$dist_dir"; pwd -P)
gyp_params+=(-Dnss_dist_dir="$dist_dir")

# This saves a canonical representation of arguments that we are passing to gyp
# or the NSPR build so that we can work out if a rebuild is needed.
# Caveat: This can fail for arguments that are position-dependent.
# e.g., "-e 2 -f 1" and "-e 1 -f 2" canonicalize the same.
check_config()
{
    local newconf="$1".new oldconf="$1"
    shift
    mkdir -p $(dirname "$newconf")
    echo CC="$(Q "$CC")" >"$newconf"
    echo CCC="$(Q "$CCC")" >>"$newconf"
    echo CXX="$(Q "$CXX")" >>"$newconf"
    echo target_arch="$(Q "$target_arch")" >>"$newconf"
    for i in "$@"; do echo "$i"; done | sort >>"$newconf"

    # Note: The following diff fails if $oldconf isn't there as well, which
    # happens if we don't have a previous successful build.
    ! diff -q "$newconf" "$oldconf" >/dev/null 2>&1
}

gyp_config="$cwd/out/gyp_config"
nspr_config="$cwd/out/$target/nspr_config"

# Now check what needs to be rebuilt.
# If we don't have a build directory make sure that we rebuild.
if [ ! -d "$target_dir" ]; then
    rebuild_nspr=1
    rebuild_gyp=1
elif [ ! -d "$dist_dir/$target" ]; then
    rebuild_nspr=1
fi

if check_config "$nspr_config" \
                 nspr_cflags="$(Q "$nspr_cflags")" \
                 nspr_cxxflags="$(Q "$nspr_cxxflags")" \
                 nspr_ldflags="$(Q "$nspr_ldflags")" \
                 nspr_dllflags="$(Q "$nspr_dllflags")" \
                 nspr_opt="$(Q "$nspr_opt")"; then
    rebuild_nspr=1
fi

if check_config "$gyp_config" "$(Q "${gyp_params[@]}")"; then
    rebuild_gyp=1
fi

# Save the chosen target.
echo "$target" > "$dist_dir/latest"
for i in "${all_args[@]}"; do echo "$i"; done > "$argsfile"

# Install the asan runtime next to the executables for easier discovery.
if [ -n "$asan_runtime_dll" ]; then
    mkdir -p "$dist_dir/$target/bin"
    cp -f "$asan_runtime_dll" "$dist_dir/$target/bin/"
fi

# Build.
# NSPR.
if [[ "$rebuild_nspr" = 1 && "$no_local_nspr" = 0 ]]; then
    nspr_clean
    nspr_build
    mv -f "$nspr_config.new" "$nspr_config"
fi

if [ "$exit_after_nspr" = 1 ]; then
  exit 0
fi

# gyp.
if [ "$rebuild_gyp" = 1 ]; then
    if ! hash "$GYP" 2> /dev/null; then
        echo "Building NSS requires an installation of gyp: https://gyp.gsrc.io/" 1>&2
        exit 3
    fi
    # These extra arguments aren't used in determining whether to rebuild.
    obj_dir="$dist_dir/$target"
    gyp_params+=(-Dnss_dist_obj_dir="$obj_dir")
    if [ "$no_local_nspr" = 0 ]; then
        set_nspr_path "$obj_dir/include/nspr:$obj_dir/lib"
    fi

    run_verbose run_scanbuild ${GYP} -f ninja "${gyp_params[@]}" "$cwd/nss.gyp"

    mv -f "$gyp_config.new" "$gyp_config"
fi

# ninja.
if hash ninja-build 2>/dev/null; then
    ninja=ninja-build
elif hash ninja 2>/dev/null; then
    ninja=ninja
else
    echo "Building NSS requires an installation of ninja: https://ninja-build.org/" 1>&2
    exit 3
fi

run_scanbuild "$ninja" -C "$target_dir" "${ninja_params[@]}"

# pkg-config metadata, so that programs can link against this dist without
# hand-written -l and -I flags.
generate_pkg_config()
{
    local obj_dir="$dist_dir/$target"
    local nss_h="$cwd/lib/nss/nss.h"
    [ -f "$nss_h" ] || return 0

    local vmajor vminor vpatch nspr_version
    vmajor=$(sed -n 's/^#define NSS_VMAJOR \([0-9]*\).*/\1/p' "$nss_h")
    vminor=$(sed -n 's/^#define NSS_VMINOR \([0-9]*\).*/\1/p' "$nss_h")
    vpatch=$(sed -n 's/^#define NSS_VPATCH \([0-9]*\).*/\1/p' "$nss_h")
    [ -n "$vmajor" ] || return 0

    if [ "$no_local_nspr" = 0 ]; then
        nspr_version=$(sed -n 's/^Version: *//p' \
            "$obj_dir/lib/pkgconfig/nspr.pc" 2>/dev/null)
    fi
    : "${nspr_version:=4.32}"

    mkdir -p "$obj_dir/lib/pkgconfig"
    sed -e "s|%prefix%|$obj_dir|g" \
        -e "s|%exec_prefix%|\${prefix}|g" \
        -e "s|%libdir%|\${exec_prefix}/lib|g" \
        -e "s|%includedir%|$dist_dir/public/nss|g" \
        -e "s|%NSS_VERSION%|$vmajor.$vminor.$vpatch|g" \
        -e "s|%NSPR_VERSION%|$nspr_version|g" \
        "$cwd/pkg/pkg-config/nss.pc.in" > "$obj_dir/lib/pkgconfig/nss.pc"

    if [ "$static_libs" = 1 ]; then
        # A static link needs the internal libraries too, and which those are
        # depends on the architecture and on how NSS is currently divided up
        # into gyp targets. Ask gyp for the dependency graph of the build we
        # just did rather than keeping a list here that has to be remembered
        # whenever code moves between targets.
        local dump_params=("${gyp_params[@]}" -Dnss_dist_obj_dir="$obj_dir")
        if [ "$no_local_nspr" = 0 ]; then
            dump_params+=(-Dnspr_include_dir="$obj_dir/include/nspr"
                          -Dnspr_lib_dir="$obj_dir/lib")
        fi
        # A configuration that builds no NSS libraries at all, such as
        # --mozpkix-only, has nothing to describe here; warn rather than
        # failing a build that otherwise succeeded.
        local static_libs_flags=""
        if run_verbose ${GYP} -f dump_dependency_json -Goutput_dir="$target_dir" \
               "${dump_params[@]}" "$cwd/nss.gyp" &&
           static_libs_flags=$(${python:-python3} "$cwd/coreconf/static_libs.py" \
               "$target_dir/dump.json" "$obj_dir/lib"); then
            sed -e "s|%prefix%|$obj_dir|g" \
                -e "s|%exec_prefix%|\${prefix}|g" \
                -e "s|%libdir%|\${exec_prefix}/lib|g" \
                -e "s|%includedir%|$dist_dir/public/nss|g" \
                -e "s|%NSS_VERSION%|$vmajor.$vminor.$vpatch|g" \
                -e "s|%NSPR_VERSION%|$nspr_version|g" \
                -e "s|%STATIC_LIBS%|$static_libs_flags|g" \
                "$cwd/pkg/pkg-config/nss-static.pc.in" \
                > "$obj_dir/lib/pkgconfig/nss-static.pc"
        else
            echo "Warning: could not determine the static libraries;" \
                 "not writing nss-static.pc" 1>&2
        fi
    fi

    # The template resolves exec_prefix/libdir/includedir through pkg-config,
    # which cannot see an uninstalled dist tree. Seed them from the dist layout
    # instead; the command line flags still override, as they are parsed later.
    mkdir -p "$obj_dir/bin"
    sed -e "s|@prefix@|$obj_dir|g" \
        -e "s|@MOD_MAJOR_VERSION@|$vmajor|g" \
        -e "s|@MOD_MINOR_VERSION@|$vminor|g" \
        -e "s|@MOD_PATCH_VERSION@|$vpatch|g" \
        "$cwd/pkg/pkg-config/nss-config.in" \
    | awk -v inc="$dist_dir/public/nss" '
        /^prefix=/ && !done {
            print
            print "exec_prefix=${prefix}"
            print "libdir=${prefix}/lib"
            print "includedir=" inc
            done = 1
            next
        }
        { print }
      ' > "$obj_dir/bin/nss-config"
    chmod +x "$obj_dir/bin/nss-config"
}

generate_pkg_config
