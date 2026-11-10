#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
ANDROID_JAR="${ANDROID_JAR:-$SDK/platforms/android-35/android.jar}"
D8="${D8:-$SDK/build-tools/35.0.0/d8}"
JAVAC="${JAVA_HOME:+$JAVA_HOME/bin/}javac"
JAR="${JAVA_HOME:+$JAVA_HOME/bin/}jar"
JAVA="${JAVA_HOME:+$JAVA_HOME/bin/}java"
OUT="${1:-$ROOT/libs/arm64-v8a/ime-host.dex}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes" "$WORK/dex" "$(dirname "$OUT")"
[[ -f "$ANDROID_JAR" ]] || { echo "Android platform JAR not found: $ANDROID_JAR" >&2; exit 1; }
"$JAVAC" --release 8 -classpath "$ANDROID_JAR" -d "$WORK/classes" "$ROOT/java/ImeHost.java"
"$JAR" cf "$WORK/ime-host.jar" -C "$WORK/classes" .
if [[ -n "${R8_JAR:-}" ]]; then
    "$JAVA" -cp "$R8_JAR" com.android.tools.r8.D8 --lib "$ANDROID_JAR" --min-api 26 --output "$WORK/dex" "$WORK/ime-host.jar"
else
    "$D8" --lib "$ANDROID_JAR" --min-api 26 --output "$WORK/dex" "$WORK/ime-host.jar"
fi
cp "$WORK/dex/classes.dex" "$OUT"
printf 'wrote %s\n' "$OUT"
