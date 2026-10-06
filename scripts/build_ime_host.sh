#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.."; pwd)
SDK=${ANDROID_SDK_ROOT:?set ANDROID_SDK_ROOT}
R8=${R8_JAR:?set R8_JAR to r8.jar}
JAVA_HOME=${JAVA_HOME:?set JAVA_HOME to a JDK 17+ directory}
JAVA="$JAVA_HOME/bin"
OUT=${1:-$ROOT/release/ime-host.dex}
CLASS_DIR="$ROOT/build/ime-host/classes"
JAR="$ROOT/build/ime-host/ime-host.jar"
DEX_DIR="$ROOT/build/ime-host/dex"
mkdir -p "$CLASS_DIR" "$DEX_DIR" "$(dirname "$OUT")"
"$JAVA/javac" --release 8 -classpath "${ANDROID_JAR:-$SDK/android-35/android.jar}" -d "$CLASS_DIR" "$ROOT/java/ImeHost.java"
"$JAVA/jar" cf "$JAR" -C "$CLASS_DIR" .
"$JAVA/java" -cp "$R8" com.android.tools.r8.D8 --min-api 30 --output "$DEX_DIR" "$JAR"
cp "$DEX_DIR/classes.dex" "$OUT"
printf 'wrote %s\n' "$OUT"
