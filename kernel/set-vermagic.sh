#!/usr/bin/env bash
# Rewrites the version magic recorded in a built module.
#
#   kernel/set-vermagic.sh qimgui.ko "$(adb shell uname -r)"
#
# A module records the release of the kernel it was built against, and the
# kernel refuses one whose string does not match its own. Within a single GKI
# KMI generation the interface is frozen, so a module built against that
# generation's common kernel normally works on any of its releases, and this
# makes the string say so.
#
# It is still a claim rather than a fact: the right fix is to build against the
# source of the kernel that will load the module. If the module misbehaves
# after this, rebuild instead of arguing with it. The new release string must
# fit in the old one's bytes; if it does not, rebuild with
# CONFIG_LOCALVERSION set so that it does.
set -euo pipefail

module="${1:-}"
release="${2:-}"
if [[ -z "$module" || -z "$release" ]]; then
    echo "usage: $0 <module.ko> <kernel-release>   # e.g. 6.6.144-android15-8-gabc123" >&2
    exit 2
fi
[[ -f "$module" ]] || { echo "no such module: $module" >&2; exit 2; }

current=$(strings -a "$module" | grep -m1 '^vermagic=' || true)
if [[ -z "$current" ]]; then
    echo "no vermagic in $module: is it a kernel module?" >&2
    exit 2
fi
echo "current: $current"

rest=${current#vermagic=}
rest=${rest#* }                       # Everything after the release string.
wanted="vermagic=$release $rest"
echo "wanted : $wanted"

if (( ${#wanted} > ${#current} )); then
    cat >&2 <<MESSAGE
The new string is ${#wanted} bytes and only ${#current} fit.
Rebuild with a longer CONFIG_LOCALVERSION, or against the device's own kernel.
MESSAGE
    exit 1
fi

# Pad with spaces: the kernel compares the whole string, and the record is
# fixed length in the section.
padded=$(printf '%s%*s' "$wanted" $(( ${#current} - ${#wanted} )) '')
python3 - "$module" "$current" "$padded" <<'PYTHON'
import sys
path, old, new = sys.argv[1], sys.argv[2].encode(), sys.argv[3].encode()
data = open(path, 'rb').read()
if data.count(old) != 1:
    sys.exit(f"expected one vermagic record, found {data.count(old)}")
open(path, 'wb').write(data.replace(old, new))
PYTHON
echo "patched: $module"
