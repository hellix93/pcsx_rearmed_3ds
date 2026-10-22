/***************************************************************************
 *   PCSX-Revolution - PlayStation Emulator for Nintendo Wii               *
 *   Copyright (C) 2009-2010  PCSX-Revolution Dev Team                     *
 *   <http://code.google.com/p/pcsx-revolution/>                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

/*
* GTE functions.
*/

#include "gte.h"
#include "gte_arm.h"
#include "psxmem.h"
#include "../include/compiler_features.h"
#include "../include/arm_features.h"

#ifndef GTE_LOG
#define GTE_LOG(...)
#endif

#ifdef FLAGLESS
#define NF _nf
#define NM(n) n##_nf
#else
#define NF
#define NM(n) n
#endif

#define VX(n) (n < 3 ? regs->CP2D.p[n << 1].sw.l : regs->CP2D.p[9].sw.l)
#define VY(n) (n < 3 ? regs->CP2D.p[n << 1].sw.h : regs->CP2D.p[10].sw.l)
#define VZ(n) (n < 3 ? regs->CP2D.p[(n << 1) + 1].sw.l : regs->CP2D.p[11].sw.l)

#define fSX(n) ((regs->CP2D.p)[((n) + 12)].sw.l)
#define fSY(n) ((regs->CP2D.p)[((n) + 12)].sw.h)
#define fSZ(n) ((regs->CP2D.p)[((n) + 17)].w.l) /* (n == 0) => SZ1; */

#define fR(x)    (regs->CP2D.p[(x) + 20].b.l)
#define fG(x)    (regs->CP2D.p[(x) + 20].b.h)
#define fB(x)    (regs->CP2D.p[(x) + 20].b.h2)
#define fCODE(x) (regs->CP2D.p[(x) + 20].b.h3)

#define gteVXY0 (regs->CP2D.r[0])
#define gteVX0  (regs->CP2D.p[0].sw.l)
#define gteVY0  (regs->CP2D.p[0].sw.h)
#define gteVZ0  (regs->CP2D.p[1].sw.l)
#define gteVXY1 (regs->CP2D.r[2])
#define gteVX1  (regs->CP2D.p[2].sw.l)
#define gteVY1  (regs->CP2D.p[2].sw.h)
#define gteVZ1  (regs->CP2D.p[3].sw.l)
#define gteVXY2 (regs->CP2D.r[4])
#define gteVX2  (regs->CP2D.p[4].sw.l)
#define gteVY2  (regs->CP2D.p[4].sw.h)
#define gteVZ2  (regs->CP2D.p[5].sw.l)
#define gteRGB  (regs->CP2D.r[6])
#define gteR    (regs->CP2D.p[6].b.l)
#define gteG    (regs->CP2D.p[6].b.h)
#define gteB    (regs->CP2D.p[6].b.h2)
#define gteCODE (regs->CP2D.p[6].b.h3)
#define gteOTZ  (regs->CP2D.p[7].w.l)
#define gteIR0  (regs->CP2D.p[8].sw.l)
#define gteIR1  (regs->CP2D.p[9].sw.l)
#define gteIR2  (regs->CP2D.p[10].sw.l)
#define gteIR3  (regs->CP2D.p[11].sw.l)
#define gteSXY0 (regs->CP2D.r[12])
#define gteSX0  (regs->CP2D.p[12].sw.l)
#define gteSY0  (regs->CP2D.p[12].sw.h)
#define gteSXY1 (regs->CP2D.r[13])
#define gteSX1  (regs->CP2D.p[13].sw.l)
#define gteSY1  (regs->CP2D.p[13].sw.h)
#define gteSXY2 (regs->CP2D.r[14])
#define gteSX2  (regs->CP2D.p[14].sw.l)
#define gteSY2  (regs->CP2D.p[14].sw.h)
#define gteSXYP (regs->CP2D.r[15])
#define gteSXP  (regs->CP2D.p[15].sw.l)
#define gteSYP  (regs->CP2D.p[15].sw.h)
#define gteSZ0  (regs->CP2D.p[16].w.l)
#define gteSZ1  (regs->CP2D.p[17].w.l)
#define gteSZ2  (regs->CP2D.p[18].w.l)
#define gteSZ3  (regs->CP2D.p[19].w.l)
#define gteRGB0  (regs->CP2D.r[20])
#define gteR0    (regs->CP2D.p[20].b.l)
#define gteG0    (regs->CP2D.p[20].b.h)
#define gteB0    (regs->CP2D.p[20].b.h2)
#define gteCODE0 (regs->CP2D.p[20].b.h3)
#define gteRGB1  (regs->CP2D.r[21])
#define gteR1    (regs->CP2D.p[21].b.l)
#define gteG1    (regs->CP2D.p[21].b.h)
#define gteB1    (regs->CP2D.p[21].b.h2)
#define gteCODE1 (regs->CP2D.p[21].b.h3)
#define gteRGB2  (regs->CP2D.r[22])
#define gteR2    (regs->CP2D.p[22].b.l)
#define gteG2    (regs->CP2D.p[22].b.h)
#define gteB2    (regs->CP2D.p[22].b.h2)
#define gteCODE2 (regs->CP2D.p[22].b.h3)
#define gteRES1  (regs->CP2D.r[23])
#define gteMAC0  (((s32 *)regs->CP2D.r)[24])
#define gteMAC1  (((s32 *)regs->CP2D.r)[25])
#define gteMAC2  (((s32 *)regs->CP2D.r)[26])
#define gteMAC3  (((s32 *)regs->CP2D.r)[27])
#define gteIRGB  (regs->CP2D.r[28])
#define gteORGB  (regs->CP2D.r[29])
#define gteLZCS  (regs->CP2D.r[30])
#define gteLZCR  (regs->CP2D.r[31])

#define gteR11R12 (((s32 *)regs->CP2C.r)[0])
#define gteR22R23 (((s32 *)regs->CP2C.r)[2])
#define gteR11 (regs->CP2C.p[0].sw.l)
#define gteR12 (regs->CP2C.p[0].sw.h)
#define gteR13 (regs->CP2C.p[1].sw.l)
#define gteR21 (regs->CP2C.p[1].sw.h)
#define gteR22 (regs->CP2C.p[2].sw.l)
#define gteR23 (regs->CP2C.p[2].sw.h)
#define gteR31 (regs->CP2C.p[3].sw.l)
#define gteR32 (regs->CP2C.p[3].sw.h)
#define gteR33 (regs->CP2C.p[4].sw.l)
#define gteTRX (((s32 *)regs->CP2C.r)[5])
#define gteTRY (((s32 *)regs->CP2C.r)[6])
#define gteTRZ (((s32 *)regs->CP2C.r)[7])
#define gteL11 (regs->CP2C.p[8].sw.l)
#define gteL12 (regs->CP2C.p[8].sw.h)
#define gteL13 (regs->CP2C.p[9].sw.l)
#define gteL21 (regs->CP2C.p[9].sw.h)
#define gteL22 (regs->CP2C.p[10].sw.l)
#define gteL23 (regs->CP2C.p[10].sw.h)
#define gteL31 (regs->CP2C.p[11].sw.l)
#define gteL32 (regs->CP2C.p[11].sw.h)
#define gteL33 (regs->CP2C.p[12].sw.l)
#define gteRBK (((s32 *)regs->CP2C.r)[13])
#define gteGBK (((s32 *)regs->CP2C.r)[14])
#define gteBBK (((s32 *)regs->CP2C.r)[15])
#define gteLR1 (regs->CP2C.p[16].sw.l)
#define gteLR2 (regs->CP2C.p[16].sw.h)
#define gteLR3 (regs->CP2C.p[17].sw.l)
#define gteLG1 (regs->CP2C.p[17].sw.h)
#define gteLG2 (regs->CP2C.p[18].sw.l)
#define gteLG3 (regs->CP2C.p[18].sw.h)
#define gteLB1 (regs->CP2C.p[19].sw.l)
#define gteLB2 (regs->CP2C.p[19].sw.h)
#define gteLB3 (regs->CP2C.p[20].sw.l)
#define gteRFC (((s32 *)regs->CP2C.r)[21])
#define gteGFC (((s32 *)regs->CP2C.r)[22])
#define gteBFC (((s32 *)regs->CP2C.r)[23])
#define gteOFX (((s32 *)regs->CP2C.r)[24])
#define gteOFY (((s32 *)regs->CP2C.r)[25])
#define gteH   (regs->CP2C.p[26].w.l)
#define gteDQA (regs->CP2C.p[27].sw.l)
#define gteDQB (((s32 *)regs->CP2C.r)[28])
#define gteZSF3 (regs->CP2C.p[29].sw.l)
#define gteZSF4 (regs->CP2C.p[30].sw.l)
#define gteFLAG (regs->CP2C.r[31])

#define GTE_SF(op) ((op >> 19) & 1)
#define GTE_MX(op) ((op >> 17) & 3)
#define GTE_V(op) ((op >> 15) & 3)
#define GTE_CV(op) ((op >> 13) & 3)
#define GTE_CD(op) ((op >> 11) & 3) /* not used */
#define GTE_LM(op) ((op >> 10) & 1)
#define GTE_CT(op) ((op >> 6) & 15) /* not used */

// shift the gte 44bit accumulator to 64bit
#define MAC123_SHIFT (32-12)

#if defined(FLAGLESS) || (defined(__arm__) && !defined(HAVE_ARMV5))

static inline s64 mac123add4(u32 id, u32 *flags, s32 a1, s32 a2, s32 a3, s32 a4, int shift) {
	return (((s64)a1 << 12) + a2 + a3 + a4) >> shift;
}

static inline s32 mac123add_s12(u32 id, u32 *flags, s32 in12, s32 addend, int shift) {
	return (((s64)in12 << 12) + addend) >> shift;
}

static inline s32 mac123sub_s12(u32 id, u32 *flags, s32 in12, s32 subtrahend, int shift) {
	return (((s64)in12 << 12) - subtrahend) >> shift;
}

#else

static inline s64 mac123add(u32 id, u32 *flags, s64 in, s32 addend) {
	s64 a;
#if defined(__arm__)
	u32 flag = 1u << (31 - id);
	asm("adds %Q[a], %Q[in], %[add], lsl #20\n"
	    "adcs %R[a], %R[in], %[add], asr #12\n"
	    "movpl %[flag], %[flag], lsr #3\n"
	    "orrvs %[flags], %[flags], %[flag]"
	    : [a]"=&r"(a), [flags]"+&r"(*flags), [flag]"+&r"(flag)
	    : [in]"r"(in), [add]"r"(addend)
	    : "cc");
#else
	int o = __builtin_add_overflow(in, (s64)addend << MAC123_SHIFT, &a);
	*flags |= (o && a <  0) << (31 - id);
	*flags |= (o && a >= 0) << (28 - id);
#endif
	return a;
}

static inline s64 mac123add4(u32 id, u32 *flags, s32 a1, s32 a2, s32 a3, s32 a4, int shift) {
	s64 a = (s64)a1 << (12 + MAC123_SHIFT);
	a = mac123add(id, flags, a, a2);
	a = mac123add(id, flags, a, a3);
	a = mac123add(id, flags, a, a4);
	return a >> (shift + MAC123_SHIFT);
}

static inline s32 mac123add_s12(u32 id, u32 *flags, s32 in12, s32 addend, int shift) {
	return mac123add(id, flags, (s64)in12 << (12+MAC123_SHIFT), addend) >> (shift+MAC123_SHIFT);
}

static inline s64 mac123sub_s12(u32 id, u32 *flags, s32 in12, s32 subtrahend, int shift) {
	s64 a;
#if defined(__arm__)
	u32 flag = 1u << (31 - id);
	s64 in = (s64)in12 << (12+MAC123_SHIFT);
	asm("subs %Q[a], %Q[in], %[sub], lsl #20\n"
	    "sbcs %R[a], %R[in], %[sub], asr #12\n"
	    "movpl %[flag], %[flag], lsr #3\n"
	    "orrvs %[flags], %[flags], %[flag]"
	    : [a]"=&r"(a), [flags]"+&r"(*flags), [flag]"+&r"(flag)
	    : [in]"r"(in), [sub]"r"(subtrahend)
	    : "cc");
#else
	int o = __builtin_sub_overflow((s64)in12 << (12+MAC123_SHIFT), (s64)subtrahend << MAC123_SHIFT, &a);
	*flags |= (o && subtrahend <  0) << (31 - id);
	*flags |= (o && subtrahend >= 0) << (28 - id);
#endif
	return a >> (shift+MAC123_SHIFT);
}

#endif // !FLAGLESS for mac 123

#ifndef FLAGLESS

static inline s64 mac0flags(u32 *flags, s64 a) {
	if (a != (s32)a)
		*flags |= 1u << (16 + (a >> 63));
	return a;
}

#if defined(__arm__)

#define LIM(flags_, value_, max_, min_, flag_) \
({s32 r_ = value_; \
  asm("cmp   %[val], %[max]\n" \
      "movgt %[val], %[max]\n" \
      "orrgt %[flags], %[flag]\n" \
      "cmp   %[val], %[min]\n" \
      "movlt %[val], %[min]\n" \
      "orrlt %[flags], %[flag]\n" \
      : [val]"+&r"(r_), [flags]"+&r"(*(flags_)) \
      : [max]"r"(max_), [min]"r"(min_), [flag]"i"(flag_) \
      : "cc"); \
  r_;})

#elif defined(__aarch64__)

#define LIM(flags_, value_, max_, min_, flagc_) \
({s32 r_ = value_; \
  u32 flag_o_, flag_ = flagc_; \
  asm("cmp  %w[val], %w[max]\n" \
      "csel %w[val], %w[max], %w[val], gt\n" \
      "csel %w[flag_o], %w[flag], wzr, gt\n" \
      "cmp  %w[val], %w[min]\n" \
      "csel %w[val], %w[min], %w[val], lt\n" \
      "csel %w[flag_o], %w[flag], %w[flag_o], lt\n" \
      : [val]"+&r"(r_), [flag_o]"=&r"(flag_o_) \
      : [max]"r"(max_), [min]"r"(min_), [flag]"r"(flag_) \
      : "cc"); \
  *(flags_) |= flag_o_; \
  r_;})

#else

static inline s32 LIM(u32 *flags, s32 value, s32 max, s32 min, u32 flag) {
	s32 ret = value;
	if (ret > max)
		ret = max;
	if (ret < min)
		ret = min;
	if (ret != value)
		*flags |= flag;
	return ret;
}

#endif

static inline void LIMF(
