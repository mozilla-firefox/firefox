#!/usr/bin/env bash
# Capture the after and before halves of a screenshot pair from one scratch
# browser-chrome test, reverting the committed files the change edited for the
# before run. ./mach mochitest installs the test file itself, so no rebuild is
# needed for it.
#
#   scripts/capture-pair.sh [--base REV] [--setpref name=value]... <test> <path>... [shots-dir]
#   scripts/capture-pair.sh --probe [--timeout-ms N] [--window-size WxH] [--chrome-setup-file F [--chrome-setup-async]] [--steps F] [--base REV] [--setpref name=value]... <script> <path>... [shots-dir]
#
# <test>      the scratch browser-chrome test that pastes capture.js
# --probe <script>  instead of a test, a chrome-context script for
#             desktop-theme-probing's firefox-probe.py --context chrome --async
#             that pastes capture.js and resolves with globalThis.__shots; runs
#             against the objdir's dist/bin/firefox, dist/*.app/Contents/MacOS/firefox
#             on macOS or dist/bin/firefox.exe on Windows (FIREFOX overrides), no
#             harness needed; the JSON it resolves with, readings included, lands
#             in <shots-dir>/{after,before}.log
# --setpref name=value  a pref for both halves (repeatable), such as a
#             layout.css.devPixelsPerPx=1.25 or browser.uidensity cell: forwarded
#             unchanged to firefox-probe.py or mochitest, which type it alike
#             (single-quote a string that looks like an integer: name='2').
#             The dpr= in each SHOT line confirms the ratio the run got.
# --base REV  the commit the before half takes <path> from (default HEAD~1)
# <path>...   the files the browser loads that the change edited, committed at
#             HEAD and present at REV. The objdir links most chrome files to the
#             source, so a revert is live at the next launch. A file it copies (a
#             preprocessed jar entry, * in jar.mn, such as browser.xhtml) gets
#             ./mach build faster before each half and after the restore, and on
#             Windows every run does. One it has no copy of (a *.tokens.json,
#             whose generated CSS is what loads) is refused; pass the generated
#             token CSS instead.
# [shots-dir] absolute directory for the PNGs (default: $PWD/artifacts/shots);
#             the last argument, told apart from a path by its leading / (or C:/
#             under MSYS). Name it for your run.
#
# --timeout-ms N  with --probe, firefox-probe.py's --timeout-ms for each half
#             (its default suits one capture, not a sweep)
# --window-size WxH  with --probe, firefox-probe.py's --window-size for each
#             half, so both run at one window size (such as 1200x800). The
#             mochitest route takes no window size: the test sets its own.
# --chrome-setup-file F, --chrome-setup-async, --steps F  with --probe,
#             forwarded to firefox-probe.py for each half: a chrome setup run
#             before the load (installing an add-on theme), and input steps run
#             before the script (opening a popup by typing); a `grab` step
#             needs --no-headless, which this script does not pass
# -h, --help  print this text
#
# The after half runs with shots.label=after. If it fails, prints no SHOT line or
# (with --probe) resolves with an error, the script exits 1 there, before any
# revert. Otherwise each <path> is checked out from REV into the working tree
# (the index is left alone) and the before half runs with shots.label=before.
# Run from the checkout root. Logs land beside the PNGs,
# <shots-dir>/{after,before}.log, and the SHOT lines from both are printed
# at the end; each log must have at least one. The tree at each <path> must be
# clean, both at the start and right before the revert; the files are restored
# on any exit, unless one no longer matches REV, which is left as found. MACH
# overrides ./mach, OBJDIR the objdir (default: the object directory ./mach
# environment reports).
set -euo pipefail

probe=0
prefs=()
timeout_ms=
window_size=
probe_extra=()
error_indent="  "
chrome_setup=0
chrome_setup_async=0
base=HEAD~1
while [ $# -gt 0 ]; do
  case $1 in
    -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p;}' "$0"; exit 0 ;;
    --probe) probe=1; shift ;;
    --timeout-ms) [ $# -ge 2 ] || { echo "--timeout-ms needs a value" >&2; exit 2; }
            timeout_ms=$2; shift 2 ;;
    --window-size) [ $# -ge 2 ] || { echo "--window-size needs WxH" >&2; exit 2; }
            window_size=$2; shift 2 ;;
    --chrome-setup-file|--steps) [ $# -ge 2 ] || { echo "$1 needs a file" >&2; exit 2; }
            [ -f "$2" ] || { echo "$1: no such file: $2" >&2; exit 2; }
            [ "$1" = --steps ] && error_indent="    "
            [ "$1" = --chrome-setup-file ] && chrome_setup=1
            probe_extra+=("$1" "$(realpath "$2")"); shift 2 ;;
    --chrome-setup-async) chrome_setup_async=1; probe_extra+=("$1"); shift ;;
    --setpref) [ $# -ge 2 ] || { echo "--setpref needs name=value" >&2; exit 2; }
            prefs+=("$2"); shift 2 ;;
    --base) [ $# -ge 2 ] || { echo "--base needs a revision" >&2; exit 2; }
            base=$2; shift 2 ;;
    *) break ;;
  esac
done
usage="usage: $0 [--probe [--timeout-ms N] [--window-size WxH] [--chrome-setup-file F [--chrome-setup-async]] [--steps F]] [--base REV] [--setpref name=value]... <test|script> <path>... [shots-dir]"
[ $# -ge 2 ] || { echo "$usage" >&2; exit 2; }
if [ "$chrome_setup_async" = 1 ] && [ "$chrome_setup" = 0 ]; then
  echo "--chrome-setup-async needs --chrome-setup-file" >&2
  exit 2
fi
if [ "$probe" = 0 ] && { [ -n "$timeout_ms" ] || [ -n "$window_size" ] || [ ${#probe_extra[@]} -gt 0 ]; }; then
  echo "--timeout-ms, --window-size, --chrome-setup-* and --steps need --probe; a mochitest sets its own window size and state" >&2
  exit 2
fi

test_file=$1
shift
shots_dir=$PWD/artifacts/shots
msys=0
case ${OSTYPE:-} in
  msys*|cygwin*|win32) msys=1 ;;
esac
case ${!#} in
  /*) shots_dir=${!#}; set -- "${@:1:$#-1}" ;;
  [A-Za-z]:/*) [ "$msys" = 1 ] && { shots_dir=${!#}; set -- "${@:1:$#-1}"; } ;;
esac
[ $# -ge 1 ] || { echo "$usage" >&2; exit 2; }
mach=${MACH:-./mach}
paths=()
for p in "$@"; do
  # ./a, a//b and the like name the same file; the objdir checks need one form.
  full=$(git ls-files --full-name -- "$p")
  if [ -z "$full" ] || [ "$full" != "${full%%$'\n'*}" ]; then
    echo "$p is not one tracked file (options go before the test or script, and shots-dir, an absolute path, goes last)" >&2
    exit 1
  fi
  paths+=("$full")
done

needs_build=$msys

objdir=${OBJDIR:-$({ "$mach" environment 2>/dev/null || true; } | sed -n '/^object directory:/{n;s/^[[:space:]]*//;p;}')}
if [ "$msys" = 1 ] && [ -n "$objdir" ] && command -v cygpath >/dev/null; then
  objdir=$(cygpath -u "$objdir")
fi
root=$(pwd -P)
check_clean() {
  local p
  for p in "${paths[@]}"; do
    if ! git diff --quiet -- "$p" || ! git diff --cached --quiet -- "$p"; then
      echo "$p has uncommitted changes${1:-; commit them first (SKILL.md §3)}" >&2
      exit 1
    fi
  done
}
check_clean
# What build faster installs, as type, destination and source path per line.
manifests=()
if [ -n "$objdir" ]; then
  for m in "$objdir"/faster/install_dist_bin*; do
    case $m in *.track) ;; *) [ -f "$m" ] && manifests+=("$m") ;; esac
  done
fi
installed() {
  local srcs=("$root/$1")
  if [ "$msys" = 1 ] && command -v cygpath >/dev/null; then
    srcs+=("$(cygpath -m "$root/$1")" "$(cygpath -w "$root/$1")")
  fi
  awk -F '\037' -v srcs="$(printf '%s\037' "${srcs[@]}")" '
    BEGIN { n = split(srcs, s, "\037"); for (i = 1; i < n; i++) want[s[i]] = 1 }
    $3 in want { found = 1; exit }
    END { exit !found }' "${manifests[@]}"
}
for p in "${paths[@]}"; do
  if ! git cat-file -e "HEAD:$p" 2>/dev/null || ! git cat-file -e "$base:$p" 2>/dev/null; then
    echo "$p is missing at HEAD or at $base; a file the change adds or removes cannot be reverted" >&2
    exit 1
  fi
  if git diff --quiet "$base" HEAD -- "$p"; then
    echo "$p is unchanged between $base and HEAD; nothing to compare" >&2
    exit 1
  fi
  if [ -z "$objdir" ] || [ ! -d "$objdir/dist/bin" ]; then
    echo "warning: no objdir found; cannot check that the browser loads $p live (set OBJDIR)" >&2
  elif [ -n "$(find "$objdir/dist/bin" -lname "$root/$p" -print -quit)" ]; then
    :
  elif { [ ${#manifests[@]} -gt 0 ] && installed "$p"; } ||
       { [ ${#manifests[@]} -eq 0 ] && [ -n "$(find "$objdir/dist/bin" -type f -name "$(basename "$p")" -print -quit)" ]; }; then
    echo "$p is copied into the objdir, not linked; running ./mach build faster before each half"
    needs_build=1
  else
    echo "$p has no copy in $objdir/dist/bin, so reverting it changes nothing the browser loads; pass the file it loads" >&2
    exit 1
  fi
done

case $shots_dir in
  /*) ;;
  [A-Za-z]:/*) [ "$msys" = 1 ] || { echo "shots-dir must be absolute: $shots_dir" >&2; exit 2; } ;;
  *) echo "shots-dir must be absolute: $shots_dir" >&2; exit 2 ;;
esac
mkdir -p "$shots_dir" artifacts
# Firefox's PathUtils takes only a native path: not /c/..., nor C:/....
ff_shots_dir=$shots_dir
if [ "$msys" = 1 ] && command -v cygpath >/dev/null; then
  ff_shots_dir=$(cygpath -w "$shots_dir")
fi

reverted=0
restore() {
  if [ "$reverted" = 1 ]; then
    reverted=0
    local p back=() kept=0
    for p in "${paths[@]}"; do
      if git diff --quiet HEAD -- "$p"; then
        :
      elif git diff --quiet "$base" -- "$p"; then
        back+=("$p")
      else
        echo "$p changed during the run; left as found, neither HEAD nor $base" >&2
        kept=1
      fi
    done
    if [ ${#back[@]} -gt 0 ]; then
      git restore --source=HEAD --worktree -- "${back[@]}"
    fi
    if [ "$needs_build" = 1 ]; then
      "$mach" build faster
    fi
    [ "$kept" = 0 ] || exit 1
  fi
}
trap restore EXIT

run() {
  local label=$1
  echo "capturing $label from $test_file"
  if [ "$probe" = 1 ]; then
    local runner binary
    runner=$(dirname "$0")/../../desktop-theme-probing/scripts/firefox-probe.py
    binary=${FIREFOX:-}
    if [ -z "$binary" ]; then
      for b in "$objdir"/dist/bin/firefox.exe "$objdir"/dist/*.app/Contents/MacOS/firefox "$objdir"/dist/bin/firefox; do
        if [ -f "$b" ]; then binary=$b; break; fi
      done
    fi
    if [ -z "$binary" ]; then
      echo "no firefox binary under ${objdir:-the objdir}/dist; set FIREFOX" >&2
      exit 1
    fi
    if [ "$msys" = 1 ] && command -v cygpath >/dev/null; then
      binary=$(cygpath -m "$binary")
    fi
    local extra=()
    for p in "${prefs[@]+"${prefs[@]}"}"; do extra+=(--setpref "$p"); done
    [ -n "$timeout_ms" ] && extra+=(--timeout-ms "$timeout_ms")
    [ -n "$window_size" ] && extra+=(--window-size "$window_size")
    extra+=("${probe_extra[@]+"${probe_extra[@]}"}")
    if ! "$mach" python --virtualenv wpt -- "$runner" --binary "$binary" \
        --context chrome --async --script-file "$test_file" \
        --setpref "shots.label=$label" --setpref "shots.dir=$ff_shots_dir" \
        "${extra[@]+"${extra[@]}"}" \
        > "$shots_dir/$label.log" 2>&1; then
      echo "probe failed for $label; see $shots_dir/$label.log" >&2
      exit 1
    fi
  else
    local extra=()
    for p in "${prefs[@]+"${prefs[@]}"}"; do extra+=(--setpref "$p"); done
    if ! "$mach" mochitest --headless \
        --setpref "shots.label=$label" --setpref "shots.dir=$ff_shots_dir" \
        "${extra[@]+"${extra[@]}"}" \
        "$test_file" > "$shots_dir/$label.log" 2>&1; then
      echo "mochitest failed for $label; see $shots_dir/$label.log" >&2
      exit 1
    fi
  fi
}

# Plain lines from the harness log, JSON-encoded strings from the probe's result.
check_shots() {
  grep -q 'SHOT ' "$shots_dir/$1.log" || {
    echo "no SHOT lines in $shots_dir/$1.log: the test must paste capture.js, and a --probe script must await each capture() call and include globalThis.__shots in what it resolves with" >&2
    exit 1
  }
  # firefox-probe.py prints the resolved value as indented JSON, nested under
  # "result" when --steps ran.
  if [ "$probe" = 1 ] && grep -q "^$error_indent\"error\": " "$shots_dir/$1.log"; then
    echo "the probe resolved with an error for $1; see $shots_dir/$1.log" >&2
    exit 1
  fi
}

if [ "$needs_build" = 1 ]; then
  "$mach" build faster
fi
run after
check_shots after

check_clean " made during the after half; not reverting it"
reverted=1
git restore --source="$base" --worktree -- "${paths[@]}"
if [ "$needs_build" = 1 ]; then
  "$mach" build faster
fi
run before
restore
trap - EXIT

check_shots before
grep -h -o 'SHOT [^"]*' "$shots_dir/after.log" "$shots_dir/before.log" | sort -u
