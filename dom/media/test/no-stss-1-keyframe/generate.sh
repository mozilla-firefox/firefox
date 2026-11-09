#!/usr/bin/env bash

set -euo pipefail

temp="$(mktemp -d)"

generate_video() {
    printf -v filename "%q.mp4" "$1"
    shift
    ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=4" \
      -f lavfi -i testsrc=r=25:s=320x240:duration=4 -c:a aac "${@}" \
      -pix_fmt yuv420p \
      "$temp/out.mp4"

    n_keyframes="$(ffprobe -v error -select_streams v:0 -show_entries frame "$temp/out.mp4" | grep -c 'key_frame=1')"
    if [ "$n_keyframes" -ne "1" ]; then
        printf "Failed: %s has %s keyframes, want 1" "$filename" "$n_keyframes" 2>&1
        exit 1
    fi

    mp4edit --remove moov/trak/mdia/minf/stbl/stss "$temp/out.mp4" "$filename"
    rm -f "$temp/out.mp4"
    printf "Cache-control: no-store\n" > "$filename^headers^"
}


params_x264=( -c:v libx264 -crf 51 -x264-params open_gop=1:no-scenecut=1 -g 2000 -keyint_min 2000 )
params_x265=( -c:v libx265 -crf 51 -x265-params open_gop=1:no-scenecut=1 -g 2000 -keyint_min 2000 )
params_svtav1=( -c:v libsvtav1 -crf 63 -svtav1-params keyint=800 )
params_vp9=( -c:v libvpx-vp9 -crf 63 -g 2000 -keyint_min 2000 -level 2.0 )

generate_video "h264" "${params_x264[@]}"
generate_video "h265" "${params_x265[@]}"
generate_video "av1" "${params_svtav1[@]}"
generate_video "vp9" "${params_vp9[@]}"

rmdir "$temp"
