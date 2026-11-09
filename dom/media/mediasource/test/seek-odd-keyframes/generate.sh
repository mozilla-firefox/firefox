#!/usr/bin/env bash

set -euo pipefail

generate_single_idr() {
    codec="$1"
    shift
    prefix="single-idr-$codec"

    ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=4.5" \
        -f lavfi -i testsrc=r=25:s=320x240:duration=4.5 \
        -pix_fmt yuv420p "${@}" \
        -movflags empty_moov+frag_keyframe+dash \
        -f dash -dash_segment_type mp4 -seg_duration 4 -adaptation_sets "id=0,streams=v id=1,streams=a"\
        -single_file 1 -single_file_name "$prefix-\$RepresentationID\$.mp4" "$prefix.mpd"
    rm "$prefix.mpd"

    frames="$(ffprobe -v error -select_streams v:0 -show_entries frame=pict_type,key_frame -of csv=print_section=0 "$prefix-0.mp4")"
    n_keyframes="$(printf "%s" "$frames" | grep -c '^1')"
    if [ "$n_keyframes" -ne "1" ]; then
        printf "Failed: %s has %s keyframes, want 1" "$prefix" "$n_keyframes" 1>&2
        exit 1
    fi

    # Badly muxed MP4 where every sample is falsely marked as sync.
    # In the fragment's `tfhd` box, change default sample flags from 0x01010000
    # to 0x20000000, unsetting the `sample_is_non_sync_sample` bit.
    # See 8.8.3.1 in ISO/IEC 14496-12
    prefix_bad="sync-lies-$codec"
    cp "$prefix-0.mp4" "$prefix_bad-0.mp4"
    # Isn't this lovely
    perl -0777 -pi -e 's/tfhd([\x00-\xFF]{16})\x01\x01\x00\x00/tfhd$1\x02\x00\x00\x00/' "$prefix_bad-0.mp4"
}

params_x264=( -c:v libx264 -crf 51 -x264-params open_gop=1:no-scenecut=1 -force_key_frames "expr:gte(t,n_forced)" -g 200 -keyint_min 200 )
params_x265=( -c:v libx265 -crf 51 -x265-params open_gop=1:no-scenecut=1 -force_key_frames "expr:gte(t,n_forced)" -g 200 -keyint_min 200 )
params_svtav1=( -c:v libsvtav1 -crf 63 -svtav1-params keyint=200 )
params_vp9=( -c:v libvpx-vp9 -crf 63 -g 200 -keyint_min 200 -level 2.0 )

generate_single_idr "h264" "${params_x264[@]}"
generate_single_idr "h265" "${params_x265[@]}"
generate_single_idr "av1" "${params_svtav1[@]}"
generate_single_idr "vp9" "${params_vp9[@]}"

generate_idr_then_nidr() {
    codec="$1"
    shift
    prefix="idr-then-nidr-$codec"

    # Inject one black frame to trigger a scenecut and an intra refresh right after an IDR frame
    ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=4.5" \
        -f lavfi -i testsrc=r=25:s=320x240:duration=4.5 \
        -vf "drawbox=enable='eq(n,10)':color=black:t=fill" \
        -pix_fmt yuv420p "${@}" \
        -movflags empty_moov+frag_keyframe+dash \
        -f dash -dash_segment_type mp4 -seg_duration 4 -adaptation_sets "id=0,streams=v id=1,streams=a"\
        -single_file 1 -single_file_name "$prefix-\$RepresentationID\$.mp4" "$prefix.mpd"
    rm "$prefix.mpd"

    frames="$(ffprobe -v error -select_streams v:0 -show_entries frame=pict_type,key_frame -of csv=print_section=0 "$prefix-0.mp4")"
    n_keyframes="$(printf "%s" "$frames" | grep -c '^1'; true)"
    if [ "$n_keyframes" -ne "1" ]; then
        printf "Failed: %s has %s keyframes, want 1" "$prefix" "$n_keyframes" 1>&2
        exit 1
    fi
    n_iframes="$(printf "%s" "$frames" | grep -c '^0,I'; true)"
    n_iframes_hd="$(printf "%s" "$frames" | (head -n15 || true) | grep -c '^0,I'; true)"
    if [ "$n_iframes" -ne "1" ] || [ "$n_iframes_hd" -ne "1" ]; then
        printf "Failed: %s has %s total I-frames and %s around the start, want 1" "$prefix" "$n_iframes" "$n_iframes_hd" 1>&2
        exit 1
    fi
}

params_x264=( -c:v libx264 -crf 51 -x264-params scenecut=40:min-keyint=100:keyint=120 )
params_x265=( -c:v libx265 -crf 51 -x265-params scenecut=40:min-keyint=100:keyint=120 )

generate_idr_then_nidr "h264" "${params_x264[@]}"
generate_idr_then_nidr "h265" "${params_x265[@]}"
# scenecut trick doesn't really work with svtav1 or vpx

find . -path '*.m4s' -or -path '*.mp4' -exec sh -c 'printf "Cache-Control: no-store\n" > "$1^headers^"' sh {} \;
