#!/bin/sh
# Generate the 32-bit ARM JIT opcode tables (compemu.c, compstbl.c, comptbl.h) with
# jit/gencomp_arm.c on the build host. They replace the x86 tables checked in under
# sources/src/jit for this build only.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/sources/src"
OUT=$(mktemp -d)

${HOSTCC:-cc} -O1 -w \
	-I"$SRC" -I"$SRC/include" -I"$SRC/jit" -I"$ROOT" -I"$ROOT/retrodep" \
	-I"$ROOT/libretro" -I"$ROOT/libretro-common/include" \
	"$SRC/jit/gencomp_arm.c" "$SRC/cpudefs.c" "$SRC/readcpu.c" "$SRC/gen.c" \
	-o "$OUT/gencomp_arm"

(cd "$OUT" && ./gencomp_arm)

# gencomp_arm writes Amiberry-style includes: wrap the files like the rest of PUAE's JIT.
for f in compemu.c compstbl.c; do
	{
		printf '#include "sysconfig.h"\n#if defined(JIT)\n'
		sed -e 's|"compiler/compemu.h"|"compemu.h"|' -e 's|"m68k.h"|"options.h"|' "$OUT/$f"
		printf '\n#endif /* JIT */\n'
	} > "$SRC/jit/$f"
done
cp "$OUT/comptbl.h" "$SRC/jit/comptbl.h"
wc -l "$SRC/jit/compemu.c" "$SRC/jit/compstbl.c" "$SRC/jit/comptbl.h"
