#!/system/bin/sh
# Run from the extracted diagnostic package; keep the child's failure status.
cd "$(dirname "$0")" || exit 1
binary=${1:-./AImGui}
if [ ! -x "$binary" ]; then
    printf 'Executable not found or not executable: %s\n' "$binary" >&2
    exit 1
fi
umask 077
run_dir="diagnostics-$(date +%Y%m%d-%H%M%S)-$$"
mkdir "$run_dir" || exit 1
{
    printf 'Android: '; getprop ro.build.version.release
    printf 'SDK: '; getprop ro.build.version.sdk
    printf 'Model: '; getprop ro.product.model
    printf 'ABI: '; getprop ro.product.cpu.abi
} > "$run_dir/device.txt" 2>&1
printf 'Running %s; startup output is saved in %s/startup.txt\n' "$binary" "$run_dir"
"$binary" > "$run_dir/startup.txt" 2>&1 &
app_pid=$!
wait "$app_pid"
result=$?
cat "$run_dir/startup.txt"
printf '\nExecutable exit status: %s\n' "$result"
printf '%s\n' "$result" > "$run_dir/exit-status.txt"
# Restrict collection to this run's process and the app's own log tags.
logcat -d --pid="$app_pid" -v threadtime -s AImGui:V AImGui_Font:V MD3Demo:V '*:S' > "$run_dir/android-log.txt" 2>&1
printf 'Diagnostic files: %s/%s\n' "$PWD" "$run_dir"
exit "$result"
