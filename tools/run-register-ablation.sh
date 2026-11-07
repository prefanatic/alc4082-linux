#!/usr/bin/env bash

set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
probe="$project_dir/alc4082-reg"
device=${ALSA_DEVICE:-hw:4,0}
card=${device#hw:}
card=${card%%,*}
test_mode=${TEST_MODE:-hot-ablation}
duration=${DURATION_SECONDS:-5}
settle=${SETTLE_SECONDS:-1}
output_dir=${1:-"$project_dir/results/ablation-$(date +%Y%m%d-%H%M%S)"}
registers=(0x0319 0x0303 0x050a 0x031a 0x0510)
case "$test_mode" in
    hot-ablation)
        cases=(
            full_before
            widget_only
            omit_0319
            omit_0303
            omit_050a
            omit_031a
            omit_0510
            full_after
        )
        ;;
    cold-transition)
        cases=(cold_baseline widget_only full_registers)
        ;;
    *)
        echo "unknown TEST_MODE: $test_mode" >&2
        exit 1
        ;;
esac
recorder_pid=

for command in arecord ffmpeg awk date; do
    if ! command -v "$command" >/dev/null; then
        echo "missing required command: $command" >&2
        exit 1
    fi
done

if ! [[ $duration =~ ^[1-9][0-9]*$ && $settle =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "DURATION_SECONDS must be a positive integer and SETTLE_SECONDS a non-negative number" >&2
    exit 1
fi

mkdir -p -- "$output_dir"
make -C "$project_dir" alc4082-reg >/dev/null

if [[ $test_mode == cold-transition ]]; then
    installed_srcversion=$(modinfo -F srcversion snd_usb_audio)
    loaded_srcversion=$(< /sys/module/snd_usb_audio/srcversion)
    if [[ $installed_srcversion != "$loaded_srcversion" ]]; then
        echo "cold-transition requires the installed stock snd-usb-audio module" >&2
        echo "installed srcversion: $installed_srcversion" >&2
        echo "loaded srcversion:    $loaded_srcversion" >&2
        exit 1
    fi
fi

restore_device() {
    local status=$?

    trap - EXIT INT TERM
    if [[ -n $recorder_pid ]]; then
        kill -INT "$recorder_pid" 2>/dev/null || true
        wait "$recorder_pid" 2>/dev/null || true
    fi
    "$probe" enable-rear-capture >/dev/null ||
        echo "warning: failed to restore the full rear-capture sequence" >&2
    exit "$status"
}
trap restore_device EXIT INT TERM

apply_case() {
    local name=$1

    if [[ $test_mode == cold-transition ]]; then
        case "$name" in
            cold_baseline)
                return
                ;;
            widget_only)
                "$probe" activate-rear-mic
                return
                ;;
            full_registers)
                "$probe" enable-rear-registers
                return
                ;;
        esac
    fi

    "$probe" disable-rear-registers
    case "$name" in
        widget_only)
            "$probe" activate-rear-mic
            ;;
        full_before|full_after)
            "$probe" enable-rear-capture
            ;;
        omit_*)
            "$probe" enable-rear-capture --omit "0x${name#omit_}"
            ;;
        *)
            echo "unknown case: $name" >&2
            return 1
            ;;
    esac
}

{
    printf 'date: %s\n' "$(date --iso-8601=seconds)"
    printf 'kernel: %s\n' "$(uname -r)"
    printf 'alsa_device: %s\n' "$device"
    printf 'test_mode: %s\n' "$test_mode"
    printf 'format: S16_LE\nrate: 48000\nchannels: 2\n'
    printf 'measurement_seconds: %s\nsettle_seconds: %s\n\n' \
        "$duration" "$settle"
    if [[ $test_mode == hot-ablation ]]; then
        printf '%s\n' \
            'This is a hot-state ablation in one continuous capture stream.' \
            'It prevents the loaded experimental module from reapplying the' \
            'full sequence between cases. It does not replace cold-power' \
            'tests or establish private symbolic bit names.'
    else
        printf '%s\n' \
            'This is a reset-state transition under the installed stock' \
            'module: untouched baseline, widget command, then registers.'
    fi
} >"$output_dir/manifest.txt"

amixer -c "$card" scontents >"$output_dir/mixer.txt" 2>/dev/null || true
"$probe" read "${registers[@]}" >"$output_dir/registers-before.txt"
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    case start_seconds duration_seconds full_mean_dbfs full_max_dbfs \
    tone_mean_dbfs tone_max_dbfs >"$output_dir/results.tsv"

session="$output_dir/session.wav"
recording_start_ns=$(date +%s%N)
arecord -q -D "$device" -f S16_LE -r 48000 -c 2 -t wav "$session" &
recorder_pid=$!
sleep 1
if ! kill -0 "$recorder_pid" 2>/dev/null; then
    wait "$recorder_pid"
fi

for name in "${cases[@]}"; do
    echo "applying case: $name"
    apply_case "$name"
    "$probe" read "${registers[@]}" >"$output_dir/$name.registers.txt"
    sleep "$settle"

    now_ns=$(date +%s%N)
    start_seconds=$(awk -v start="$recording_start_ns" -v now="$now_ns" \
        'BEGIN { printf "%.6f", (now - start) / 1000000000 }')
    sleep "$duration"

    printf '%s\t%s\t%s\t\t\t\t\n' "$name" "$start_seconds" "$duration" \
        >>"$output_dir/results.tsv"
done

kill -INT "$recorder_pid" 2>/dev/null || true
wait "$recorder_pid" 2>/dev/null || true
recorder_pid=

while IFS=$'\t' read -r name start_seconds case_duration _; do
    [[ $name == case ]] && continue
    segment="$output_dir/$name.wav"
    log="$output_dir/$name.ffmpeg.txt"
    tone_log="$output_dir/$name.tone.ffmpeg.txt"
    ffmpeg -hide_banner -nostdin -loglevel info -ss "$start_seconds" \
        -t "$case_duration" -i "$session" -af volumedetect -f null - \
        > /dev/null 2>"$log"
    mean_volume=$(awk '/mean_volume:/ { value=$5 } END { print value }' "$log")
    max_volume=$(awk '/max_volume:/ { value=$5 } END { print value }' "$log")
    ffmpeg -hide_banner -nostdin -loglevel info -ss "$start_seconds" \
        -t "$case_duration" -i "$session" \
        -af 'bandpass=f=1000:w=100:t=h,volumedetect' -f null - \
        > /dev/null 2>"$tone_log"
    tone_mean=$(awk '/mean_volume:/ { value=$5 } END { print value }' \
        "$tone_log")
    tone_max=$(awk '/max_volume:/ { value=$5 } END { print value }' \
        "$tone_log")
    ffmpeg -hide_banner -nostdin -loglevel error -ss "$start_seconds" \
        -t "$case_duration" -i "$session" -c copy "$segment"
    awk -F '\t' -v OFS='\t' -v target="$name" -v mean="$mean_volume" \
        -v max="$max_volume" -v tone_mean="$tone_mean" \
        -v tone_max="$tone_max" \
        '{
            if ($1 == target) {
                $4 = mean
                $5 = max
                $6 = tone_mean
                $7 = tone_max
            }
            print
        }' \
        "$output_dir/results.tsv" >"$output_dir/results.tsv.new"
    mv -- "$output_dir/results.tsv.new" "$output_dir/results.tsv"
done <"$output_dir/results.tsv"

"$probe" enable-rear-capture >/dev/null
"$probe" read "${registers[@]}" >"$output_dir/registers-after.txt"
trap - EXIT INT TERM

echo
echo "results: $output_dir/results.tsv"
column -t -s $'\t' "$output_dir/results.tsv" 2>/dev/null ||
    sed -n '1,20p' "$output_dir/results.tsv"
