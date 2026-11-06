#!/bin/bash

set -e

DATA_DIR="tests/data"
NOTFLAC="$(pwd)/build/notflac"
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

if [ ! -x "$NOTFLAC" ]; then
    echo "Error: $NOTFLAC not found. Build first:"
    echo "  mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build ."
    exit 1
fi

if [ ! -d "$DATA_DIR" ]; then
    echo "Error: $DATA_DIR not found"
    exit 1
fi

HAVE_FLAC=0
HAVE_WAVPACK=0
HAVE_ALAC=0
HAVE_MAC=0

command -v flac &>/dev/null && HAVE_FLAC=1
command -v wavpack &>/dev/null && HAVE_WAVPACK=1
command -v ffmpeg &>/dev/null && HAVE_ALAC=1
command -v mac &>/dev/null && HAVE_MAC=1

echo "Detected codecs:"
[ "$HAVE_FLAC" = 1 ] && echo "  flac:    yes"
[ "$HAVE_WAVPACK" = 1 ] && echo "  wavpack: yes"
[ "$HAVE_ALAC" = 1 ] && echo "  alac:    yes (via ffmpeg)"
[ "$HAVE_MAC" = 1 ] && echo "  mac:     yes (Monkey's Audio)"
echo "  notflac: yes (build/notflac)"
echo ""

printf "%-32s %-22s %10s %10s %8s %10s\n" \
       "File" "Codec" "Original" "Compressed" "Ratio" "Time"
printf "%-32s %-22s %10s %10s %8s %10s\n" \
       "--------------------------------" "----------------------" "----------" "----------" "--------" "----------"

for wav in "$DATA_DIR"/*.wav; do
    [ -f "$wav" ] || continue
    base=$(basename "$wav")
    orig_size=$(stat -c%s "$wav")

    for codec in flac wavpack alac mac notflac-lpc-rice notflac-lpc-range notflac-delta-rice notflac-delta-range; do
        case "$codec" in
            flac)
                [ "$HAVE_FLAC" = 1 ] || continue
                out="$TMP_DIR/$base.flac"
                start=$(date +%s.%N)
                flac -8 -s -f -o "$out" "$wav" 2>/dev/null
                end=$(date +%s.%N)
                ;;
            wavpack)
                [ "$HAVE_WAVPACK" = 1 ] || continue
                out="$TMP_DIR/$base.wv"
                start=$(date +%s.%N)
                wavpack -hh -q -y -o "$out" "$wav" 2>/dev/null
                end=$(date +%s.%N)
                ;;
            alac)
                [ "$HAVE_ALAC" = 1 ] || continue
                out="$TMP_DIR/$base.m4a"
                start=$(date +%s.%N)
                ffmpeg -y -loglevel error -i "$wav" -c:a alac -compression_level 2 "$out"
                end=$(date +%s.%N)
                ;;
            mac)
                [ "$HAVE_MAC" = 1 ] || continue
                out="$TMP_DIR/$base.ape"
                start=$(date +%s.%N)
                mac "$wav" "$out" -c4000 >/dev/null 2>&1 || true
                end=$(date +%s.%N)
                ;;
            notflac-lpc-rice)
                out="$TMP_DIR/$base.lpc_rice.notflac"
                start=$(date +%s.%N)
                "$NOTFLAC" compress "$wav" "$out" 1 1 >/dev/null
                end=$(date +%s.%N)
                ;;
            notflac-lpc-range)
                out="$TMP_DIR/$base.lpc_range.notflac"
                start=$(date +%s.%N)
                "$NOTFLAC" compress "$wav" "$out" 1 0 >/dev/null
                end=$(date +%s.%N)
                ;;
            notflac-delta-rice)
                out="$TMP_DIR/$base.delta_rice.notflac"
                start=$(date +%s.%N)
                "$NOTFLAC" compress "$wav" "$out" 0 1 >/dev/null
                end=$(date +%s.%N)
                ;;
            notflac-delta-range)
                out="$TMP_DIR/$base.delta_range.notflac"
                start=$(date +%s.%N)
                "$NOTFLAC" compress "$wav" "$out" 0 0 >/dev/null
                end=$(date +%s.%N)
                ;;
        esac

        [ -f "$out" ] || continue
        comp_size=$(stat -c%s "$out")
        elapsed=$(echo "$end - $start" | bc)
        ratio=$(echo "scale=2; $comp_size * 100 / $orig_size" | bc)

        printf "%-32s %-22s %10d %10d %7s%% %9ss\n" \
               "$base" "$codec" "$orig_size" "$comp_size" "$ratio" "$elapsed"
        rm -f "$out"
    done
    echo ""
done

echo "Done. All temp files removed."
