/***************************************************************************
*   Copyright (C) 2010 PCSX4ALL Team                                      *
*   Copyright (C) 2010 Unai                                               *
*   Copyright (C) 2016 Senquack (dansilsby <AT> gmail <DOT> com)          *
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

#ifndef GPU_UNAI_H
#define GPU_UNAI_H

#include <stdint.h>
#include "gpu.h"

// Header shared between both standalone gpu_unai (gpu.cpp) and new
// gpulib-compatible gpu_unai (gpulib_if.cpp)
// -> Anything here should be for gpu_unai's private use. <-

///////////////////////////////////////////////////////////////////////////////
//  Compile Options

//#define ENABLE_GPU_NULL_SUPPORT   // Enables NullGPU support
//#define ENABLE_GPU_LOG_SUPPORT    // Enables gpu logger, very slow only for windows debugging
//#define ENABLE_GPU_ARMV7			// Enables ARMv7 optimized assembly

//Poly routine options (default is integer math and accurate division)
//#define GPU_UNAI_USE_FLOATMATH         // Use float math in poly routines
//#define GPU_UNAI_USE_FLOAT_DIV_MULTINV // If GPU_UNAI_USE_FLOATMATH is defined,
                                         //  use multiply-by-inverse for division
//#define GPU_UNAI_USE_INT_DIV_MULTINV   // If GPU_UNAI_USE_FLOATMATH is *not*
                                         //  defined, use old inaccurate division


#define GPU_INLINE static inline __attribute__((always_inline))
#define INLINE     static inline __attribute__((always_inline))

#define u8  uint8_t
#define s8  int8_t
#define u16 uint16_t
#define s16 int16_t
#define u32 uint32_t
#define s32 int32_t
#define s64 int64_t
#define u64 uint64_t

union gcol_t {
	struct {
		u16 r, g, b;
#ifdef HAVE_ARMV6
		u16 counter;
#else
		u16 unused;
#endif
	} c;
#if defined(HAVE_ARMV6) || (defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 4)
	u32 raw32[2];
#else
	u64 raw;
#endif

	inline gcol_t & operator+=(const gcol_t &rhs)
	{
#ifdef HAVE_ARMV6
		// prevent bit spills the other versions have,
		// allowing to use the unused part as a counter
		asm("uadd16 %[d], %[d], %[s]" : [d]"+r"(raw32[0]) : [s]"r"(rhs.raw32[0]));
		asm("uadd16 %[d], %[d], %[s]" : [d]"+r"(raw32[1]) : [s]"r"(rhs.raw32[1]));
#elif defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 4
		// avoid having to do carry that's not needed here
		raw32[0] += rhs.raw32[0];
		raw32[1] += rhs.raw32[1];
#else
		raw += rhs.raw;
#endif
		return *this;
	}

	inline void set_counter(int counter)
	{
#ifdef HAVE_ARMV6
		c.counter = counter;
#endif
	}
	inline void get_counter(int &counter)
	{
#ifdef HAVE_ARMV6
		counter = raw32[1];
#endif
	}
};

#ifndef NDEBUG

typedef struct {
        u32 v;
} le32_t;

typedef struct {
        u16 v;
} le16_t;

#define LExRead(v_) (v_.v)

#else

typedef u32 le32_t;
typedef u16 le16_t;
#define LExRead(v) (v)

#endif

static inline u32 le32_to_u32(le32_t le)
{
        return LE32TOH(LExRead(le));
}

static inline s32 le32_to_s32(le32_t le)
{
        return (int32_t) LE32TOH(LExRead(le));
}

static inline u32 le32_raw(le32_t le)
{
	return LExRead(le);
}

static inline le32_t u32_to_le32(u32 u)
{
	return (le32_t){ HTOLE32(u) };
}

static inline u16 le16_to_u16(le16_t le)
{
        return LE16TOH(LExRead(le));
}

static inline s16 le16_to_s16(le16_t le)
{
        return (int16_t) LE16TOH(LExRead(le));
}

static inline u16 le16_raw(le16_t le)
{
	return LExRead(le);
}

static inline le16_t u16_to_le16(u16 u)
{
	return (le16_t){ HTOLE16(u) };
}

union PtrUnion
{
	le32_t  *U4;
	le16_t  *U2;
	u8   *U1;
	void *ptr;
};

union GPUPacket
{
	le32_t U4[16];
	le16_t U2[32];
	u8  U1[64];
};

template<class T> static inline void SwapValues(T &x, T &y)
{
	T tmp(x);  x = y;  y = tmp;
}

template<typename T>
static inline T Min2 (const T a, const T b)
{
	return (a<b)?a:b;
}

template<typename T>
static inline T Min3 (const T a, const T b, const T c)
{
	return  Min2(Min2(a,b),c);
}

template<typename T>
static inline T Max2 (const T a, const T b)
{
	return  (a>b)?a:b;
}

template<typename T>
static inline T Max3 (const T a, const T b, const T c)
{
	return  Max2(Max2(a,b),c);
}


///////////////////////////////////////////////////////////////////////////////
//  GPU Raster Macros

// Convert 24bpp color parameter of GPU command to 16bpp (15bpp + mask bit)
#define	GPU_RGB16(rgb) ((((rgb)&0xF80000)>>9)|(((rgb)&0xF800)>>6)|(((rgb)&0xF8)>>3))

// Sign-extend 11-bit coordinate command param
#define GPU_EXPANDSIGN(x) (((s32)(x)<<(32-11))>>(32-11))

// Max difference between any two X or Y primitive coordinates
#define CHKMAX_X 1024
#define CHKMAX_Y 512

#define	FRAME_BUFFER_SIZE	(1024*512*2)
#define	FRAME_WIDTH			  1024
#define	FRAME_HEIGHT		  512
#define	FRAME_OFFSET(x,y)	(((y)<<10)+(x))
#define FRAME_BYTE_STRIDE     2048
#define FRAME_BYTES_PER_PIXEL 2

static inline s32 GPU_DIV(s32 rs, s32 rt)
{
	return rt ? (rs / rt) : (0);
}

// 'Unsafe' version of above that doesn't check for div-by-zero
#define GPU_FAST_DIV(rs, rt) ((signed)(rs) / (signed)(rt))

// warning: gpu_arm.S asm uses this struct, update the asm if you change this
struct gpu_unai_inner_t {
	le16_t* TBA;              // 00 Ptr to current texture in VRAM
	le16_t* CBA;              // 04 Ptr to current CLUT in VRAM

	// 22.10 Fixed-pt texture coords, mask, scanline advance
	// NOTE: U,V are no longer packed together into one u32, this proved to be
	//  too imprecise, leading to pixel dropouts.  Example: NFS3's skybox.
	u32 u, v;                 // 08 not fractional for sprites
	u32 mask_v00u;            // 10 (v_mask << 24) | (u_mask & 0xff)
	u32 unused;
	union {
	  struct {
	    s32 u_inc, v_inc;     // 18 poly uv increment, 22.10
	  };
	  struct {
	    s32 y0, y1;           // 18 sprite y range
	  };
	};

	// Color for flat-shaded, texture-blended prims
	u8  r5, g5, b5, pad5;     // 20 5-bit light for sprite asm
	union {
	  u32 bgr0888;            // 24 8-bit light for dithered prims
	  struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	    u8 pad8, b8, g8, r8;
#else
	    u8 r8, g8, b8, pad8;
#endif
	  };
	};

	// Color for Gouraud-shaded prims
	// Fixed-pt 8.8 rgb triplet
	//  layout:  ccccccccXXXXXXXX for c in [r, g, b]
	//           ^ bit 16
	gcol_t gCol;       // 28
	gcol_t gInc;       // 30 Increment along scanline for gCol

	// Color for flat-shaded, untextured prims
	u16 PixelData;     // 38 bgr555 color for untextured flat-shaded polys

	u8 unused2;

	u8 ilace_mask;          // Determines what lines to skip when rendering.
	                        //  Normally 0 when PS1 240 vertical res is in
	                        //  use and ilace_force is 0. When running in
	                        //  PS1 480 vertical res on a low-resolution
	                        //  device (320x240), will usually be set to 1
	                        //  so odd lines are not rendered. (Unless future
	                        //  full-screen scaling option is in use ..TODO)
};

struct gpu_unai_t {
	u32 GPU_GP1;
	GPUPacket PacketBuffer;
	le16_t *vram;

#ifdef USE_GPULIB
	le16_t *downscale_vram;
#endif
	////////////////////////////////////////////////////////////////////////////
	// Variables used only by older standalone version of gpu_unai (gpu.cpp)
#ifndef USE_GPULIB
	u32  GPU_GP0;
	u32  tex_window;       // Current texture window vals (set by GP0(E2h) cmd)
	s32  PacketCount;
	s32  PacketIndex;
	bool fb_dirty;         // Framebuffer is dirty (according to GPU)

	//  Display status
	//  NOTE: Standalone older gpu_unai didn't care about horiz display range
	u16  DisplayArea[6];   // [0] : Start of display area (in VRAM) X
	                       // [1] : Start of display area (in VRAM) Y
	                       // [2] : Display mode resolution HORIZONTAL
	                       // [3] : Display mode resolution VERTICAL
	                       // [4] : Vertical display range (on TV) START
	                       // [5] : Vertical display range (on TV) END

	////////////////////////////////////////////////////////////////////////////
	//  Dma Transfers info
	struct {
		s32  px,py;
		s32  x_end,y_end;
		le16_t* pvram;
		u32 *last_dma;     // Last dma pointer
		bool FrameToRead;  // Load image in progress
		bool FrameToWrite; // Store image in progress
	} dma;

	////////////////////////////////////////////////////////////////////////////
	//  Frameskip
	struct {
		int  skipCount;    // Frame skip (0,1,2,3...)
		bool isSkip;       // Skip frame (according to GPU)
		bool skipFrame;    // Skip this frame (according to frame skip)
		bool wasSkip;      // Skip frame old value (according to GPU)
		bool skipGPU;      // Skip GPU primitives
	} frameskip;
#endif
	// END of standalone gpu_unai variables
	////////////////////////////////////////////////////////////////////////////

	u32 TextureWindowCur;  // Current setting from last GP0(0xE2) cmd (raw form)
	u8  TextureWindow[4];  // [0] : Texture window offset X
	                       // [1] : Texture window offset Y
	                       // [2] : Texture window mask X
	                       // [3] : Texture window mask Y

	u16 DrawingArea[4];    // [0] : Drawing area top left X
	                       // [1] : Drawing area top left Y
	                       // [2] : Drawing area bottom right X
	                       // [3] : Drawing area bottom right Y

	s16 DrawingOffset[2];  // [0] : Drawing offset X (signed)
	                       // [1] : Drawing offset Y (signed)

	////////////////////////////////////////////////////////////////////////////
	//  Inner Loop parameters

	__attribute__((aligned(32)))
	gpu_unai_inner_t inn;

	// End of inner Loop parameters
	////////////////////////////////////////////////////////////////////////////

	u32 DitherLut32[4];     // shifted up by 4, packed into u32 as 4 values
	s16 DitherLut16[4][4];  // shifted up by 4 and s16 to simplify lookup asm

	bool prog_ilace_flag;   // Tracks successive frames for 'prog_ilace' option

	u8 BLEND_MODE;
	u8 TEXT_MODE;
	u8 Masking;

	u16 PixelMSB;

	gpu_unai_config_t config;

	u8  LightLUT[32*32];    // 5-bit lighting LUT (gpu_inner_light.h)
};

static __attribute__((aligned(32))) gpu_unai_t gpu_unai;

// Global config that frontend can alter.. Values are read in GPU_init().
// TODO: if frontend menu modifies a setting, add a function that can notify
// GPU plugin to use new setting.
gpu_unai_config_t gpu_unai_config_ext;

///////////////////////////////////////////////////////////////////////////////
// Internal inline funcs to get option status: (Allows flexibility)
static inline bool LightingEnabled()
{
	return gpu_unai.config.lighting;
}

static inline bool FastLightingEnabled()
{
	return gpu_unai.config.fast_lighting;
}

static inline bool BlendingEnabled()
{
	return gpu_unai.config.blending;
}

static inline bool DitheringEnabled()
{
	return gpu_unai.config.dithering;
}

static inline bool ForcedDitheringEnabled()
{
	return gpu_unai.config.force_dithering;
}

static inline bool ProgressiveInterlaceEnabled()
{
#ifdef USE_GPULIB
	// Using this old option greatly decreases quality of image. Disabled
	//  for now when using new gpulib, since it also adds more work in loops.
	return false;
#else
	return gpu_unai.config.prog_ilace;
#endif
}


// ---------------------------------------------------------------------------
// Stereo 3D mod ("dual pass"): every drawing command list is rasterized twice,
// once per eye. In 3D polygons each vertex is moved sideways by an amount that
// depends on its real depth (taken from the GTE), 2D things are drawn
// identically in both eyes. The right eye is drawn into its own frame buffer
// (same size and layout as VRAM); textures are always read from the main VRAM.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned short *gpu_unai_vram_r;   // right-eye buffer, 1024x512 (set by frontend)
extern int gpu_unai_stereo_on;            // frontend: stereo is wanted right now
extern int gpu_unai_stereo_resync;        // frontend: copy main VRAM to right buffer
extern int gpu_unai_stereo_strength;      // frontend: shift range in pixels (slider scaled)
extern int gpu_unai_stereo_vfar;          // depth scale of the current scene
extern int gpu_unai_stereo_rcpn;
extern int gpu_unai_eye;                  // 0 = normal, +1 = left eye pass, -1 = right eye pass
void gpu_unai_depth_record_rtps(const unsigned int *cp2d);
void gpu_unai_depth_record_rtpt(const unsigned int *cp2d);
void gpu_unai_stereo_scale_update(void);
extern unsigned gpu_unai_zrec, gpu_unai_zhit, gpu_unai_zmiss;
#ifdef __cplusplus
}
#endif

// destination of drawing: right eye pass draws into the right-eye buffer
#define GPU_DST_VRAM ((gpu_unai_eye < 0) ? (le16_t *)gpu_unai_vram_r : gpu_unai.vram)

int gpu_depth_lookup(u32 key);

// sideways shift (pixels) of a vertex with depth value v: positive for near
// things; the left eye adds it, the right eye subtracts it
static inline int gpu_stereo_half(int v)
{
	int n = ((v - gpu_unai_stereo_vfar) * gpu_unai_stereo_rcpn) >> 16;
	if (n < 0) n = 0; else if (n > 256) n = 256;
	int h = ((n - 128) * gpu_unai_stereo_strength * 8) >> 9;      // 1/8 pixel
	return h >= 0 ? (h + 4) >> 3 : -((-h + 4) >> 3);
}

// The left eye pass works out the shift of every polygon vertex and writes it
// here; the right eye pass reads the very same numbers back, so both eyes
// always agree and shared polygon edges stay closed.
#define GPU_VSHIFT_MAX 131072
extern signed char gpu_vshift_buf[GPU_VSHIFT_MAX];
extern int gpu_vshift_n, gpu_vshift_pos;
void gpu_depth_store(u32 key, int v);
extern int gpu_stereo_lastv;   // depth of the last 3D polygon drawn in this frame (1 = far)

// Small sprites and tiles (stars, particles, HUD) have no depth of their own:
// they take the depth of the last 3D polygon drawn before them. Same recording
// scheme as for polygon vertices, one number per primitive.
static inline int gpu_stereo_prim_shift(u32 w, u32 h)
{
	if (gpu_unai_eye > 0) {
		int half = (w <= 64 && h <= 64) ? gpu_stereo_half(gpu_stereo_lastv) : 0;
		if (gpu_vshift_n < GPU_VSHIFT_MAX)
			gpu_vshift_buf[gpu_vshift_n] = (signed char)half;
		gpu_vshift_n++;
		return half;
	} else if (gpu_unai_eye < 0) {
		int half = 0;
		if (gpu_vshift_pos < gpu_vshift_n && gpu_vshift_pos < GPU_VSHIFT_MAX)
			half = gpu_vshift_buf[gpu_vshift_pos];
		gpu_vshift_pos++;
		return -half;
	}
	return 0;
}

#endif // GPU_UNAI_H
