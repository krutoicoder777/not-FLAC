#!/bin/bash

set -e

DATA_DIR="tests/data"
REAL_DIR="$DATA_DIR/real"
SYNTH_DIR="$DATA_DIR/synth"
NOTFLAC="$(pwd)/build/notflac"
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

if [ ! -x "$NOTFLAC" ]; then
    echo "Error: $NOTFLAC not found."
    exit 1
fi

HAVE_FLAC=0
HAVE_WAVPACK=0
HAVE_ALAC=0

command -v flac &>/dev/null && HAVE_FLAC=1
command -v wavpack &>/dev/null && HAVE_WAVPACK=1
command -v ffmpeg &>/dev/null && HAVE_ALAC=1

echo "Detected codecs:"
[ "$HAVE_FLAC" = 1 ] && echo "  flac:    yes"
[ "$HAVE_WAVPACK" = 1 ] && echo "  wavpack: yes"
[ "$HAVE_ALAC" = 1 ] && echo "  alac:    yes (via ffmpeg)"
echo "  notflac: yes (build/notflac)"
echo ""

get_duration() {
    ffprobe -v error -show_entries format=duration -of csv=p=0 "$1" 2>/dev/null
}

calc_bitrate() {
    local comp_size="$1"
    local duration="$2"

    if [ -z "$duration" ] || [ "$duration" = "N/A" ]; then
        echo "0"
        return
    fi

    echo "scale=0; $comp_size * 8 / $duration / 1000" | bc
}

calc_mbps() {
    local size="$1"
    local time="$2"

    if [ -z "$time" ] || (( $(echo "$time <= 0" | bc -l) )); then
        echo "0"
        return
    fi

    echo "scale=2; ($size / 1000000) / $time" | bc
}

run_bench() {
    local label="$1"
    local dir="$2"
    local results_file="$TMP_DIR/results_$label.tsv"

    : > "$results_file"

    printf "%-40s %-20s %10s %8s %8s %10s %10s %10s\n" \
           "File" "Codec" "Compressed" "Ratio" "Bitrate" "CmpMB/s" "DecMB/s" "Dec(s)"
    printf "%-40s %-20s %10s %8s %8s %10s %10s %10s\n" \
           "----------------------------------------" "--------------------" "----------" "--------" "--------" "--------" "--------" "----------"

    for wav in "$dir"/*.wav; do
        [ -f "$wav" ] || continue
        base=$(basename "$wav")
        orig_size=$(stat -c%s "$wav")
        duration=$(get_duration "$wav")

        for codec in flac wavpack alac notflac-lpc-rice; do
            comp_out=""
            dec_out="$TMP_DIR/$base.dec.wav"
            comp_time=""
            dec_time=""
            rm -f "$dec_out"

            case "$codec" in
                flac)
                    [ "$HAVE_FLAC" = 1 ] || continue
                    comp_out="$TMP_DIR/$base.flac"
                    start=$(date +%s.%N)
                    flac -8 -s -f -o "$comp_out" "$wav" 2>/dev/null
                    end=$(date +%s.%N)
                    comp_time=$(echo "$end - $start" | bc)
                    start=$(date +%s.%N)
                    flac -d -s -f -o "$dec_out" "$comp_out" 2>/dev/null
                    end=$(date +%s.%N)
                    dec_time=$(echo "$end - $start" | bc)
                    ;;
                wavpack)
                    [ "$HAVE_WAVPACK" = 1 ] || continue
                    comp_out="$TMP_DIR/$base.wv"
                    start=$(date +%s.%N)
                    wavpack -hh -q -y -o "$comp_out" "$wav" 2>/dev/null
                    end=$(date +%s.%N)
                    comp_time=$(echo "$end - $start" | bc)
                    start=$(date +%s.%N)
                    wvunpack -q -y -o "$dec_out" "$comp_out" 2>/dev/null
                    end=$(date +%s.%N)
                    dec_time=$(echo "$end - $start" | bc)
                    ;;
                alac)
                    [ "$HAVE_ALAC" = 1 ] || continue
                    comp_out="$TMP_DIR/$base.m4a"
                    start=$(date +%s.%N)
                    ffmpeg -y -loglevel error -i "$wav" -c:a alac -compression_level 2 "$comp_out"
                    end=$(date +%s.%N)
                    comp_time=$(echo "$end - $start" | bc)
                    start=$(date +%s.%N)
                    ffmpeg -y -loglevel error -i "$comp_out" -c:a pcm_s16le "$dec_out"
                    end=$(date +%s.%N)
                    dec_time=$(echo "$end - $start" | bc)
                    ;;
                notflac-lpc-rice)
                    comp_out="$TMP_DIR/$base.lpc_rice.notflac"
                    start=$(date +%s.%N)
                    "$NOTFLAC" compress "$wav" "$comp_out" 0 >/dev/null
                    end=$(date +%s.%N)
                    comp_time=$(echo "$end - $start" | bc)
                    start=$(date +%s.%N)
                    "$NOTFLAC" decompress "$comp_out" "$dec_out" >/dev/null
                    end=$(date +%s.%N)
                    dec_time=$(echo "$end - $start" | bc)
                    ;;
            esac

            [ -f "$comp_out" ] || continue
            comp_size=$(stat -c%s "$comp_out")
            ratio=$(echo "scale=2; $comp_size * 100 / $orig_size" | bc)
            bitrate=$(calc_bitrate "$comp_size" "$duration")
            cmp_mbps=$(calc_mbps "$orig_size" "$comp_time")
            dec_mbps=$(calc_mbps "$orig_size" "$dec_time")

            printf "%-40s %-20s %10d %7s%% %7s k %8s %8s %10s\n" \
                   "$base" "$codec" "$comp_size" "$ratio" "$bitrate" "$cmp_mbps" "$dec_mbps" "$dec_time"

            printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
                   "$base" "$codec" "$comp_size" "$ratio" "$bitrate" "$cmp_mbps" "$dec_mbps" "$dec_time" >> "$results_file"

            rm -f "$comp_out" "$dec_out"
        done
        echo ""
    done

    echo "========================================================================"
    echo " SUMMARY ($label)"
    echo "========================================================================"
    echo ""

    print_summary "$results_file" "notflac-lpc-rice" "flac"
    print_summary "$results_file" "notflac-lpc-rice" "wavpack"
    print_summary "$results_file" "notflac-lpc-rice" "alac"

    echo ""
}

print_summary() {
    local results_file="$1"
    local nf_codec="$2"
    local other="$3"

    local total=0
    local wins_bitrate=0
    local wins_cmp=0
    local wins_dec=0
    local sum_bitrate_nf=0
    local sum_bitrate_other=0
    local sum_cmp_nf=0
    local sum_cmp_other=0
    local sum_dec_nf=0
    local sum_dec_other=0

    while IFS= read -r line; do
        local base
        base=$(echo "$line" | cut -f1)

        local nf_line
        nf_line=$(grep -P "^$base\t$nf_codec\t" "$results_file" || true)
        [ -z "$nf_line" ] && continue

        local other_line
        other_line=$(grep -P "^$base\t$other\t" "$results_file" || true)
        [ -z "$other_line" ] && continue

        local nf_bitrate nf_cmp nf_dec o_bitrate o_cmp o_dec
        nf_bitrate=$(echo "$nf_line" | cut -f5)
        nf_cmp=$(echo "$nf_line" | cut -f6)
        nf_dec=$(echo "$nf_line" | cut -f7)
        o_bitrate=$(echo "$other_line" | cut -f5)
        o_cmp=$(echo "$other_line" | cut -f6)
        o_dec=$(echo "$other_line" | cut -f7)

        total=$((total + 1))
        sum_bitrate_nf=$(echo "$sum_bitrate_nf + $nf_bitrate" | bc)
        sum_bitrate_other=$(echo "$sum_bitrate_other + $o_bitrate" | bc)
        sum_cmp_nf=$(echo "$sum_cmp_nf + $nf_cmp" | bc)
        sum_cmp_other=$(echo "$sum_cmp_other + $o_cmp" | bc)
        sum_dec_nf=$(echo "$sum_dec_nf + $nf_dec" | bc)
        sum_dec_other=$(echo "$sum_dec_other + $o_dec" | bc)

        if (( $(echo "$nf_bitrate < $o_bitrate" | bc -l) )); then wins_bitrate=$((wins_bitrate + 1)); fi
        if (( $(echo "$nf_cmp > $o_cmp" | bc -l) )); then wins_cmp=$((wins_cmp + 1)); fi
        if (( $(echo "$nf_dec > $o_dec" | bc -l) )); then wins_dec=$((wins_dec + 1)); fi
    done < <(grep -P "\t$nf_codec\t" "$results_file")

    [ "$total" -eq 0 ] && return

    local avg_bitrate_nf avg_bitrate_other avg_cmp_nf avg_cmp_other avg_dec_nf avg_dec_other
    avg_bitrate_nf=$(echo "scale=0; $sum_bitrate_nf / $total" | bc)
    avg_bitrate_other=$(echo "scale=0; $sum_bitrate_other / $total" | bc)
    avg_cmp_nf=$(echo "scale=2; $sum_cmp_nf / $total" | bc)
    avg_cmp_other=$(echo "scale=2; $sum_cmp_other / $total" | bc)
    avg_dec_nf=$(echo "scale=2; $sum_dec_nf / $total" | bc)
    avg_dec_other=$(echo "scale=2; $sum_dec_other / $total" | bc)

    echo "vs $other ($total files):"
    echo "  bitrate:       notflac wins $wins_bitrate/$total   avg $(printf '%d' $avg_bitrate_nf) kbps vs $(printf '%d' $avg_bitrate_other) kbps"
    echo "  compress MB/s: notflac wins $wins_cmp/$total   avg $(printf '%.2f' $avg_cmp_nf) MB/s vs $(printf '%.2f' $avg_cmp_other) MB/s"
    echo "  decode MB/s:   notflac wins $wins_dec/$total   avg $(printf '%.2f' $avg_dec_nf) MB/s vs $(printf '%.2f' $avg_dec_other) MB/s"
    echo ""
}

if [ -d "$REAL_DIR" ]; then
    echo "########################################################################"
    echo "# REAL MUSIC ($(ls "$REAL_DIR"/*.wav 2>/dev/null | wc -l) files)"
    echo "########################################################################"
    echo ""
    run_bench "real" "$REAL_DIR"
fi

if [ -d "$SYNTH_DIR" ]; then
    echo "########################################################################"
    echo "# SYNTHETIC ($(ls "$SYNTH_DIR"/*.wav 2>/dev/null | wc -l) files)"
    echo "########################################################################"
    echo ""
    run_bench "synth" "$SYNTH_DIR"
fi

echo "Done. All temp files removed."
