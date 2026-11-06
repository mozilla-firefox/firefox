#!/usr/bin/env bash
# This file is used by build.sh to setup sanitizers.

sanitizer_flags=""
sanitizers=()

# This tracks what sanitizers are enabled so they don't get enabled twice.  This
# means that doing things that enable the same sanitizer twice (such as enabling
# both --asan and --fuzz) is order-dependent: only the first is used.
enable_sanitizer()
{
    local san="$1"
    for i in "${sanitizers[@]}"; do
        [ "$san" = "$i" ] && return
    done
    sanitizers+=("$san")

    if [ -z "$sanitizer_flags" ]; then
        gyp_params+=(-Dno_zdefs=1)
    fi

    local cflags
    cflags=$(${python:-python3} "${cwd}/coreconf/sanitizers.py" "$@")
    if [ $? -ne 0 ]; then
        exit 1
    fi
    sanitizer_flags="$sanitizer_flags $cflags"
}

enable_sancov()
{
    local clang_version
    clang_version=$($CC --version | grep -oE '([0-9]{1,}\.)+[0-9]{1,}')
    if [[ ${clang_version:0:1} -lt 4 && ${clang_version:0:1} -eq 3 && ${clang_version:2:1} -lt 9 ]]; then
        echo "Need at least clang-3.9 (better 4.0) for sancov." 1>&2
        exit 1
    fi

    local sancov
    if [ -n "$1" ]; then
        sancov="$1"
    elif [ "${clang_version:0:3}" = "3.9" ]; then
        sancov=edge,indirect-calls,8bit-counters
    else
        sancov=trace-pc-guard,trace-cmp
    fi
    enable_sanitizer sancov "$sancov"
}

enable_sourcecov()
{
    enable_sanitizer sourcecov
}

enable_ubsan()
{
    local ubsan
    if [ -n "$1" ]; then
        ubsan="$1"
    else
        ubsan=undefined,local-bounds
    fi
    enable_sanitizer ubsan "$ubsan"
}

# Sets up a clang-cl sanitizer build.
asan_runtime_dll=
clang_cl_sanitizers()
{
    local f flags=""
    for f in $sanitizer_flags; do
        case "$f" in
            -fsanitize*|-fno-sanitize*) flags="$flags $f" ;;
            # config.gypi adds these in clang-cl's spelling.
            -fno-omit-frame-pointer|-fno-optimize-sibling-calls) ;;
            *)
                echo "$f is not supported with clang-cl" 1>&2
                exit 1
                ;;
        esac
    done
    sanitizer_flags="$flags"

    # clang's runtime directory must come first in LIB (MSVC ships its own,
    # incompatible ASan libraries under the same names). Programs that run
    # during the build, such as shlibsign, need the runtime DLL on PATH.
    local rt_dir
    rt_dir=$($CC --print-runtime-dir)
    rt_dir=$(cygpath -u "$rt_dir")
    export LIB="$rt_dir${LIB:+:$LIB}" PATH="$PATH:$rt_dir"
    for f in "${sanitizers[@]}"; do
        [ "$f" = asan ] || continue
        local arch seh
        case "$target_arch" in
            ia32) arch=i386; seh=___asan_seh_interceptor ;;
            x64) arch=x86_64; seh=__asan_seh_interceptor ;;
            *)
                echo "clang-cl has no ASan runtime for $target_arch" 1>&2
                exit 1
                ;;
        esac
        asan_runtime_dll="$rt_dir/clang_rt.asan_dynamic-$arch.dll"
        if [ ! -f "$asan_runtime_dll" ]; then
            echo "Can't find $asan_runtime_dll, aborting" 1>&2
            exit 1
        fi
        gyp_params+=(-Dclang_rt_asan_arch="$arch")
        # NSPR links its DLLs with the linker directly rather than through
        # clang-cl, so it needs the same runtime options as config.gypi.
        nspr_dllflags="clang_rt.asan_dynamic-$arch.lib -include:$seh"
        nspr_dllflags="$nspr_dllflags -wholearchive:clang_rt.asan_dynamic_runtime_thunk-$arch.lib"
    done
}

# Not strictly a sanitizer, but the pattern fits
scanbuild=()
enable_scanbuild()
{
    [ "${#scanbuild[@]}" -gt 0 ] && return

    scanbuild=(scan-build)
    if [ -n "$1" ]; then
        scanbuild+=(-o "$1")
    fi
    # pass on CC and CCC to scanbuild
    if [ -n "$CC" ]; then
        scanbuild+=(--use-cc="$CC")
    fi
    if [ -n "$CCC" ]; then
        scanbuild+=(--use-c++="$CCC")
    fi
}

run_scanbuild()
{
    "${scanbuild[@]}" "$@"
}
