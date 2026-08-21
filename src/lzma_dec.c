/* Minimal LZMA1 raw decoder. ~2 KB of code with -Os.
 * Layout follows the LZMA specification (LzmaSpec.cpp), stripped down to the
 * single case this project needs: a whole raw stream in memory, decoded into
 * one flat output buffer that doubles as the dictionary.
 */
#include "lzma_dec.h"

#ifndef VID_LZMA_LC
#define VID_LZMA_LC 1
#endif
#ifndef VID_LZMA_LP
#define VID_LZMA_LP 0
#endif
#ifndef VID_LZMA_PB
#define VID_LZMA_PB 0
#endif

#define LC VID_LZMA_LC
#define LP VID_LZMA_LP
#define PB VID_LZMA_PB

typedef unsigned short CProb;
typedef unsigned int u32;

#define kTop         (1u << 24)
#define kNumStates   12
#define kPosBitsMax  4
#define kLenToPos    4
#define kAlignBits   4
#define kEndPosModel 14
#define kFullDist    (1u << (kEndPosModel >> 1))
#define PROB_INIT    1024

typedef struct {
	CProb choice;
	CProb choice2;
	CProb low[16 << 3];
	CProb mid[16 << 3];
	CProb high[256];
} CLen;

static struct {
	CProb IsMatch[kNumStates << kPosBitsMax];
	CProb IsRep[kNumStates];
	CProb IsRepG0[kNumStates];
	CProb IsRepG1[kNumStates];
	CProb IsRepG2[kNumStates];
	CProb IsRep0Long[kNumStates << kPosBitsMax];
	CProb PosSlot[kLenToPos << 6];
	CProb SpecPos[kFullDist - kEndPosModel + 1];
	CProb Align[1 << kAlignBits];
	CLen Len;
	CLen RepLen;
	CProb Lit[(u32)0x300 << (LC + LP)];
} P;

static const unsigned char *g_in;
static const unsigned char *g_in_end;
static u32 Range, Code;
static int Bad;

static void nrm(void)
{
	if (Range < kTop) {
		Range <<= 8;
		Code = (Code << 8) | (g_in < g_in_end ? *g_in++ : (Bad = 1, 0));
	}
}

static unsigned bit(CProb *p)
{
	u32 v = *p;
	u32 bound = (Range >> 11) * v;
	if (Code < bound) {
		Range = bound;
		*p = (CProb)(v + ((2048 - v) >> 5));
		nrm();
		return 0;
	}
	Range -= bound;
	Code -= bound;
	*p = (CProb)(v - (v >> 5));
	nrm();
	return 1;
}

static unsigned tree(CProb *p, unsigned n)
{
	unsigned m = 1, i = n;
	do {
		m = (m << 1) | bit(p + m);
	} while (--i);
	return m - (1u << n);
}

static unsigned rtree(CProb *p, unsigned n)
{
	unsigned m = 1, sym = 0, i = 0;
	for (; i < n; i++) {
		unsigned b = bit(p + m);
		m = (m << 1) | b;
		sym |= b << i;
	}
	return sym;
}

static u32 direct(unsigned n)
{
	u32 res = 0;
	do {
		u32 t;
		Range >>= 1;
		Code -= Range;
		t = 0u - (Code >> 31);
		Code += Range & t;
		nrm();
		res = (res << 1) + t + 1;
	} while (--n);
	return res;
}

static unsigned len_dec(CLen *l, unsigned ps)
{
	if (!bit(&l->choice))
		return tree(l->low + (ps << 3), 3);
	if (!bit(&l->choice2))
		return 8 + tree(l->mid + (ps << 3), 3);
	return 16 + tree(l->high, 8);
}

int lzma_raw_decode(const unsigned char *in, unsigned long in_size,
                    unsigned char *out, unsigned long out_size)
{
	unsigned long pos = 0;
	unsigned state = 0;
	u32 rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
	CProb *q = (CProb *)&P;
	unsigned long i = sizeof(P) / sizeof(CProb);
	unsigned k;

	while (i--)
		*q++ = PROB_INIT;

	if (in_size < 5)
		return 1;
	g_in = in;
	g_in_end = in + in_size;
	Bad = 0;
	if (*g_in++ != 0)
		return 1;
	Code = 0;
	for (k = 0; k < 4; k++)
		Code = (Code << 8) | *g_in++;
	Range = 0xFFFFFFFFu;

	while (pos < out_size) {
		unsigned ps = (unsigned)pos & ((1u << PB) - 1);
		unsigned len;

		if (!bit(&P.IsMatch[(state << kPosBitsMax) + ps])) {
			unsigned prev = pos ? out[pos - 1] : 0;
			CProb *pr = P.Lit + (u32)0x300 *
				((((unsigned)pos & ((1u << LP) - 1)) << LC) +
				 (prev >> (8 - LC)));
			unsigned sym = 1;

			if (state >= 7) {
				unsigned mb = out[pos - rep0 - 1];
				do {
					unsigned mbit = (mb >> 7) & 1;
					unsigned b;
					mb <<= 1;
					b = bit(pr + ((1 + mbit) << 8) + sym);
					sym = (sym << 1) | b;
					if (mbit != b)
						break;
				} while (sym < 0x100);
			}
			while (sym < 0x100)
				sym = (sym << 1) | bit(pr + sym);

			out[pos++] = (unsigned char)sym;
			state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
			if (Bad)
				return 1;
			continue;
		}

		if (bit(&P.IsRep[state])) {
			if (!bit(&P.IsRepG0[state])) {
				if (!bit(&P.IsRep0Long[(state << kPosBitsMax) + ps])) {
					state = state < 7 ? 9 : 11;
					out[pos] = out[pos - rep0 - 1];
					pos++;
					continue;
				}
			} else {
				u32 d;
				if (!bit(&P.IsRepG1[state])) {
					d = rep1;
				} else {
					if (!bit(&P.IsRepG2[state])) {
						d = rep2;
					} else {
						d = rep3;
						rep3 = rep2;
					}
					rep2 = rep1;
				}
				rep1 = rep0;
				rep0 = d;
			}
			len = len_dec(&P.RepLen, ps) + 2;
			state = state < 7 ? 8 : 11;
		} else {
			unsigned slot, ltp;
			rep3 = rep2;
			rep2 = rep1;
			rep1 = rep0;
			len = len_dec(&P.Len, ps) + 2;
			state = state < 7 ? 7 : 10;

			ltp = len - 2 < kLenToPos ? len - 2 : kLenToPos - 1;
			slot = tree(P.PosSlot + (ltp << 6), 6);
			if (slot < 4) {
				rep0 = slot;
			} else {
				unsigned nd = (slot >> 1) - 1;
				rep0 = (2 | (slot & 1)) << nd;
				if (slot < kEndPosModel)
					rep0 += rtree(P.SpecPos + rep0 - slot, nd);
				else
					rep0 += (direct(nd - kAlignBits) << kAlignBits) +
					        rtree(P.Align, kAlignBits);
				if (rep0 == 0xFFFFFFFFu)
					break; 
			}
		}

		if (Bad || (unsigned long)rep0 >= pos)
			return 1;
		if (pos + len > out_size)
			len = (unsigned)(out_size - pos);
		do {
			out[pos] = out[pos - rep0 - 1];
			pos++;
		} while (--len);
	}

	return (Bad || pos != out_size) ? 1 : 0;
}
