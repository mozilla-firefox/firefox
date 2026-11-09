#!/usr/bin/env bash

set -euo pipefail

temp="$(mktemp -d)"

generate_video() {
    printf -v filename "%q.mp4" "$1"
    shift
    ffmpeg \
      -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=4" \
      -f lavfi -i nullsrc=r=25:s=320x240:duration=4 \
      -vf "drawbox=enable='between(n,0,25)':color=0x0000ff:t=fill, \
      drawbox=enable='between(n,25,50)':color=0x00ff00:t=fill, \
      drawbox=enable='between(n,50,75)':color=0xff0000:t=fill, \
      drawbox=enable='between(n,75,100)':color=0xffff00:t=fill" \
      -c:a aac -pix_fmt yuv420p "${@}" \
      -colorspace bt709 \
      -color_range pc \
      -output_ts_offset 0.02 \
      "$temp/out.mp4"

    n_keyframes="$(ffprobe -v error -select_streams v:0 -show_entries frame "$temp/out.mp4" | grep -c 'key_frame=1')"
    if [ "$n_keyframes" -ne "4" ]; then
        printf "Failed: %s has %s keyframes, want 4" "$filename" "$n_keyframes" 2>&1
        exit 1
    fi

    mp4edit --remove moov/trak/mdia/minf/stbl/stss "$temp/out.mp4" "$filename"
    rm -f "$temp/out.mp4"
    printf "Cache-control: no-store\n" > "$filename^headers^"
}


params_x264=( -c:v libx264 -g 25 -keyint_min 25 )
params_x265=( -c:v libx265 -g 25 -keyint_min 25 )
params_svtav1=( -c:v libsvtav1 -g 25 -keyint_min 25 )
# the flags might not do anything, but auto scenecuts work too
params_vp9=( -c:v libvpx-vp9 -g 25 -keyint_min 25 -level 2.0 )

generate_video "h264" "${params_x264[@]}"
generate_video "h265" "${params_x265[@]}"
generate_video "av1" "${params_svtav1[@]}"
generate_video "vp9" "${params_vp9[@]}"

rmdir "$temp"
