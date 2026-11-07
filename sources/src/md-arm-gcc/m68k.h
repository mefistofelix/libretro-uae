/*
 * UAE - The Un*x Amiga Emulator
 *
 * MC68000 emulation - machine dependent bits for 32-bit ARM hosts with the JIT.
 *
 * The CCR flags are kept where the ARM CPSR has them (N=31, Z=30, C=29, V=28), so the
 * ARM JIT backend (jit/codegen_arm.c) can move them between the CPU and regflags with a
 * single MRS/MSR, exactly as md-i386-gcc mirrors the x86 flags for the x86 JIT.
 * X is kept separately as 0/1 in regflags.x, which is how the JIT writes it (setcc).
 */

struct flag_struct {
	union {
		unsigned int nzcv;	/* name used by the ARM JIT */
		unsigned int cznv;	/* name used by the interpreter */
	};
	unsigned int x;
};

extern struct flag_struct regflags;

#define FLAGBIT_N	31
#define FLAGBIT_Z	30
#define FLAGBIT_C	29
#define FLAGBIT_V	28
#define FLAGBIT_X	0

#define FLAGVAL_N	(1u << FLAGBIT_N)
#define FLAGVAL_Z	(1u << FLAGBIT_Z)
#define FLAGVAL_C	(1u << FLAGBIT_C)
#define FLAGVAL_V	(1u << FLAGBIT_V)
#define FLAGVAL_X	(1u << FLAGBIT_X)

#define SET_ZFLG(y)	(regflags.cznv = (regflags.cznv & ~FLAGVAL_Z) | (((y) ? 1u : 0u) << FLAGBIT_Z))
#define SET_CFLG(y)	(regflags.cznv = (regflags.cznv & ~FLAGVAL_C) | (((y) ? 1u : 0u) << FLAGBIT_C))
#define SET_VFLG(y)	(regflags.cznv = (regflags.cznv & ~FLAGVAL_V) | (((y) ? 1u : 0u) << FLAGBIT_V))
#define SET_NFLG(y)	(regflags.cznv = (regflags.cznv & ~FLAGVAL_N) | (((y) ? 1u : 0u) << FLAGBIT_N))
#define SET_XFLG(y)	(regflags.x    = ((y) ? 1u : 0u) << FLAGBIT_X)

#define GET_ZFLG()	((regflags.cznv >> FLAGBIT_Z) & 1)
#define GET_CFLG()	((regflags.cznv >> FLAGBIT_C) & 1)
#define GET_VFLG()	((regflags.cznv >> FLAGBIT_V) & 1)
#define GET_NFLG()	((regflags.cznv >> FLAGBIT_N) & 1)
#define GET_XFLG()	((regflags.x    >> FLAGBIT_X) & 1)

#define CLEAR_CZNV()	(regflags.cznv  = 0)
#define GET_CZNV	(regflags.cznv)
#define IOR_CZNV(X)	(regflags.cznv |= (X))
#define SET_CZNV(X)	(regflags.cznv  = (X))

/* X lives in its own word here, not at the C position as on x86 */
#define COPY_CARRY()	(regflags.x = (regflags.cznv >> FLAGBIT_C) & 1)

/*
 * Test CCR condition
 */
STATIC_INLINE int cctrue (int cc)
{
	uae_u32 f = regflags.cznv;
	int n = (f >> FLAGBIT_N) & 1, z = (f >> FLAGBIT_Z) & 1;
	int c = (f >> FLAGBIT_C) & 1, v = (f >> FLAGBIT_V) & 1;

	switch (cc) {
	case 0:  return 1;                    /* T  */
	case 1:  return 0;                    /* F  */
	case 2:  return !c && !z;             /* HI */
	case 3:  return c || z;               /* LS */
	case 4:  return !c;                   /* CC */
	case 5:  return c;                    /* CS */
	case 6:  return !z;                   /* NE */
	case 7:  return z;                    /* EQ */
	case 8:  return !v;                   /* VC */
	case 9:  return v;                    /* VS */
	case 10: return !n;                   /* PL */
	case 11: return n;                    /* MI */
	case 12: return n == v;               /* GE */
	case 13: return n != v;               /* LT */
	case 14: return !z && (n == v);       /* GT */
	case 15: return z || (n != v);        /* LE */
	}
	abort ();
	return 0;
}
