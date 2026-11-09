#!/usr/bin/env bash

set -euo pipefail

temp="$(mktemp -d)"

generate_video() {
    printf -v filename_1 "stss-1st-wrong-%q.mp4" "$1"
    printf -v filename_2 "stss-2nd-wrong-%q.mp4" "$1"
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

    frames="$(ffprobe -v error -select_streams v:0 -show_entries frame=key_frame,pict_type -of csv=print_section=0 "$temp/out.mp4")"
    declare -a keyframes
    readarray -t keyframes < <(<<< "$frames" grep -n "^1,I" | cut -d: -f1)
    if [ "${#keyframes[@]}" -ne "2" ] || [ "${keyframes[0]}" -ne "1" ] || [ "${keyframes[1]}" -ne "51" ]; then
        printf "Failed: unexpected keyframe positions: "
        printf "%s " "${keyframes[@]}"
        exit 1
    fi
    readarray -t iframes < <(<<< "$frames" grep -n "^0,I" | cut -d: -f1)
    if [ "${#iframes[@]}" -ne "2" ] || [ "${iframes[0]}" -ne "26" ] || [ "${iframes[1]}" -ne "76" ]; then
        printf "Failed: unexpected I-frame positions: "
        printf "%s " "${iframes[@]}"
        exit 1
    fi

    mp4extract moov/trak/mdia/minf/stbl/stss "$temp/out.mp4" "$temp/stss-1st-wrong.bin"
    if [ $(stat --format="%s" "$temp/stss-1st-wrong.bin") -ne "24" ]; then
        printf "Failed: unexpected stss layout\n" 2>&1
        exit 1
    fi
    cp "$temp/stss-1st-wrong.bin" "$temp/stss-2nd-wrong.bin"
    printf "\nOriginal stss:\n"
    xxd "$temp/stss-1st-wrong.bin"

    # Patch stss: change 1st entry to point to the 1st I-frame instead of an IDR
    idx="${iframes[0]}" perl -0777 -pi -e '$byte = chr($ENV{idx}); s/stss([\x00-\xFF]{8})[\x00-\xFF]{4}/stss$1\x00\x00\x00$byte/' "$temp/stss-1st-wrong.bin"
    printf "\nPatched stss (1st IDR):\n"
    xxd "$temp/stss-1st-wrong.bin"
    mp4edit --replace moov/trak/mdia/minf/stbl/stss:"$temp/stss-1st-wrong.bin" "$temp/out.mp4" "$filename_1"

    # Patch stss: Leave 1st of 2 entries intact, change 2nd entry to point to the second I-frame instead of an IDR
    idx="${iframes[1]}" perl -0777 -pi -e '$byte = chr($ENV{idx}); s/stss([\x00-\xFF]{12})[\x00-\xFF]{4}/stss$1\x00\x00\x00$byte/' "$temp/stss-2nd-wrong.bin"
    printf "\nPatched stss (2nd IDR):\n"
    xxd "$temp/stss-2nd-wrong.bin"
    mp4edit --replace moov/trak/mdia/minf/stbl/stss:"$temp/stss-2nd-wrong.bin" "$temp/out.mp4" "$filename_2"

    rm "$temp/stss-1st-wrong.bin" "$temp/stss-2nd-wrong.bin" "$temp/out.mp4"

    printf "Cache-control: no-store\n" > "$filename_1^headers^"
    printf "Cache-control: no-store\n" > "$filename_2^headers^"
}


params_x264=( -c:v libx264 -g 50 -keyint_min 50 )
# disable open GOP for H265 to emit IDR_W_RADL for the second keyframe, instead of CRA_NUT which Apple VideoToolbox can't seek to.
params_x265=( -c:v libx265 -g 50 -keyint_min 50 -x265-params no-open-gop=1:keyint=50:min_keyint=50:hist_scenecut=1 -bsf:v hevc_metadata=video_full_range_flag=1 )
# svt-av1: enabling open GOP enables I-frames but disables keyint parameters, can't easily generate the needed structure.
# same for vpx-vp9

generate_video "h264" "${params_x264[@]}"
generate_video "h265" "${params_x265[@]}"

generate_extra_entries() {
    printf -v filename "stss-extra-entries-%q.mp4" "$1"
    shift
    ffmpeg \
      -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=7" \
      -f lavfi -i nullsrc=r=25:s=320x240:duration=7 \
      -vf "drawbox=enable='between(n,0,12)':color=0x0000ff:t=fill, \
      drawbox=enable='between(n,12,25)':color=0x00ff00:t=fill, \
      drawbox=enable='between(n,25,75)':color=0xff0000:t=fill, \
      drawbox=enable='between(n,75,175)':color=0xffff00:t=fill" \
      -c:a aac -pix_fmt yuv420p "${@}" \
      -colorspace bt709 \
      -color_range pc \
      -output_ts_offset 0.02 \
      "$temp/out.mp4"

    frames="$(ffprobe -v error -select_streams v:0 -show_entries frame=key_frame,pict_type -of csv=print_section=0 "$temp/out.mp4")"
    declare -a keyframes
    readarray -t keyframes < <(<<< "$frames" grep -n "^1,I" | cut -d: -f1)
    if [ "${#keyframes[@]}" -ne "3" ] || [ "${keyframes[0]}" -ne "1" ] || [ "${keyframes[1]}" -ne "76" ] || [ "${keyframes[2]}" -ne "151" ]; then
        printf "Failed: unexpected keyframe positions: "
        printf "%s " "${keyframes[@]}"
        exit 1
    fi
    readarray -t iframes < <(<<< "$frames" grep -n "^0,I" | cut -d: -f1)
    if [ "${#iframes[@]}" -ne "2" ] || [ "${iframes[0]}" -ne "13" ] || [ "${iframes[1]}" -ne "26" ]; then
        printf "Failed: unexpected I-frame positions: "
        printf "%s " "${iframes[@]}"
        exit 1
    fi

    mp4extract moov/trak/mdia/minf/stbl/stss "$temp/out.mp4" "$temp/stss.bin"
    if [ $(stat --format="%s" "$temp/stss.bin") -ne "28" ]; then
        printf "Failed: unexpected stss layout\n" 2>&1
        exit 1
    fi
    printf "\nOriginal stss:\n"
    xxd "$temp/stss.bin"

    # Patch stss:
    #  - Add new entry inbetween, pointing to the first IDR
    idx1="${iframes[0]}" idx2="${iframes[1]}" perl -0777 -pi -e '
    $byte1 = chr($ENV{idx1});
    $byte2 = chr($ENV{idx2});
    s/\x1cstss([\x00-\xFF]{7})\x03(.{4})(.{8})/\x24stss$1\x05$2\x00\x00\x00$byte1\x00\x00\x00$byte2$3/
    ' "$temp/stss.bin"
    printf "\nPatched stss (extra entry IDR):\n"
    xxd "$temp/stss.bin"
    mp4edit --replace moov/trak/mdia/minf/stbl/stss:"$temp/stss.bin" "$temp/out.mp4" "$filename"

    rm "$temp/stss.bin" "$temp/out.mp4"

    printf "Cache-control: no-store\n" > "$filename^headers^"
}

params_x264=( -c:v libx264 -g 75 -keyint_min 75 -x264-params keyint=75:keyint_min=75 )
params_x265=( -c:v libx265 -g 75 -keyint_min 75 -x265-params no-open-gop=1:keyint=75:min_keyint=75:hist_scenecut=1 -bsf:v hevc_metadata=video_full_range_flag=1 )

generate_extra_entries "h264" "${params_x264[@]}"
generate_extra_entries "h265" "${params_x265[@]}"

rmdir "$temp"
