#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.."; pwd)
SDK=${ANDROID_SDK_ROOT:?set ANDROID_SDK_ROOT}
R8=${R8_JAR:?set R8_JAR to r8.jar}
OUT=${1:-$ROOT/release/ime-host.dex}
mkdir -p "$ROOT/build/ime-host" "$(dirname "$OUT")"
javac -source 8 -target 8 -classpath "$SDK/platforms/android-35/android.jar" -d "$ROOT/build/ime-host/classes" "$ROOT/java/ImeHost.java"
java -cp "$R8" com.android.tools.r8.D8 --min-api 30 --output "$(dirname "$OUT")" "$ROOT/build/ime-host/classes"
mv "$(dirname "$OUT")/classes.dex" "$OUT"
