Freestanding C and C++ headers for the kernel build. The kernel links no libc
and no libstdc++, so ImGui and the MD3 sources get these instead: they declare
what the kernel already provides (memcpy, vsnprintf, sort) and implement the
small pieces it does not (the math subset, containers, operator new).

They are deliberately minimal -- only what these sources use. The host check
`tests/run_kernel_checks.sh` builds the UI once with `-nostdinc++` against this
directory, so the C++ half is compiled and run off-device; the C half
(string.h, stdio.h, math.h ...) only resolves against kernel symbols.
