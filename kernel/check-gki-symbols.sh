#!/usr/bin/env bash
# Checks a built module against the GKI kernel module interface.
#
#   kernel/check-gki-symbols.sh .build/qimgui.ko /path/to/kernel-source
#
# A production GKI kernel exports only the symbols in its KMI list; anything
# else a module links against simply is not there, and insmod fails with
# "Unknown symbol". This lists what the module needs and is not in the list,
# so those calls can be resolved by name at runtime instead (see qk_file.c and
# qk_symbols.c) or dropped.
set -euo pipefail

module="${1:-}"
kernel="${2:-}"
if [[ -z "$module" || -z "$kernel" ]]; then
    echo "usage: $0 <module.ko> <kernel-source>" >&2
    exit 2
fi
[[ -f "$module" ]] || { echo "no such module: $module" >&2; exit 2; }
# The symbol lists only; the .stg beside them is an ABI dump, and matching
# against its text would call almost anything allowed. 6.12 moved the lists
# from android/abi_gki_aarch64* to gki/aarch64/symbols/.
mapfile -t lists < <({
    ls "$kernel"/android/abi_gki_aarch64* 2>/dev/null
    ls "$kernel"/gki/aarch64/symbols/* 2>/dev/null
} | grep -v '\.stg')
if (( ${#lists[@]} == 0 )); then
    echo "no GKI symbol lists in $kernel: not a GKI tree?" >&2
    exit 2
fi

nm="${NM:-$(command -v llvm-nm || command -v aarch64-linux-gnu-nm || command -v nm)}"
known=$(mktemp)
trap 'rm -f "$known"' EXIT
cat "${lists[@]}" 2>/dev/null | tr -d ' \t' | grep -vE '^(#|\[)' | grep -v '^$' |
    sort -u > "$known"

missing=()
while read -r symbol; do
    [[ -z "$symbol" ]] && continue
    grep -qx "$symbol" "$known" || missing+=("$symbol")
done < <("$nm" --undefined-only "$module" | awk 'NF == 2 && $1 == "U" {print $2}' | sort -u)

echo "module : $module"
echo "lists  : $(wc -l < "$known") symbols"
if ((${#missing[@]} == 0)); then
    echo "result : every symbol this module needs is in the GKI interface"
    exit 0
fi
echo "result : ${#missing[@]} symbol(s) outside the GKI interface:"
printf '  %s\n' "${missing[@]}"
echo
echo "Resolve these by name at runtime, or do without them. Compiler builtins"
echo "(cpu capability keys and the like) usually mean an inline helper reached"
echo "into the kernel's internals: call something else instead."
exit 1
