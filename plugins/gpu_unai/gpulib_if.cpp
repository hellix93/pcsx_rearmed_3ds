/***************************************************************************
*   Copyright (C) 2010 PCSX4ALL Team                                      *
*   Copyright (C) 2010 Unai                                               *
*   Copyright (C) 2011 notaz                                              *
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arm_features.h"
#include "../gpulib/gpu.h"
#include "old/if.h"

//#include "port.h"
#include "gpu_unai.h"

// GPU fixed point math
#include "gpu_fixedpoint.h"

// Inner loop driver instantiation file
#include "gpu_inner.h"

// GPU internal image drawing functions
#include "gpu_raster_image.h"

// GPU internal line drawing functions
#include "gpu_raster_line.h"

// GPU internal polygon drawing functions
#include "gpu_raster_polygon.h"

// GPU internal sprite drawing functions
#include "gpu_raster_sprite.h"

// GPU command buffer execution/store
#include "gpu_command.h"
#include "../../libpcsxcore/gte.h"

/////////////////////////////////////////////////////////////////////////////

#ifndef GPU_UNAI_NO_OLD
#define IS_OLD_RENDERER() gpu_unai.config.old_renderer
#else
#define IS_OLD_RENDERER() false
#endif

unsigned short *gpu_unai_depth;
int gpu_unai_depth_on;
unsigned gpu_unai_depth_cur = 1;
int gpu_unai_depth_disp_x, gpu_unai_depth_disp_y;
unsigned gpu_unai_depth_disp_n = 1;
u16 gpu_depth_tmp[GPU_DEPTH_TMP_PIXELS];

static inline u16 gpu_get_poly_gte_depth(u32 cmd, const le32_t *list)
{
  if (cmd < 0x20 || cmd > 0x3F)
    return gpu_unai_depth_cur;

  int is_quad = (cmd & 0x08) ? 4 : 3;
  int stride = 1 + ((cmd & 0x04) ? 1 : 0) + ((cmd & 0x10) ? 1 : 0);
  const le32_t *ptr = &list[1];

  u32 sum = 0, count = 0;
  for (int i = 0; i < is_quad; i++, ptr += stride) {
    u32 coords = le32_to_u32(*ptr);
    s16 x = (s16)GPU_EXPANDSIGN(coords);
    s16 y = (s16)GPU_EXPANDSIGN(coords >> 16);
    u16 d = gte_zcache_lookup(x, y);
    if (d > 0) {
      sum += d;
      count++;
    }
  }
  if (count > 0) {
    u16 avg_d = (u16)(sum / count);
    gpu_unai_depth_cur = avg_d;
    return avg_d;
  }
  return gpu_unai_depth_cur;
}

int renderer_init(void)
{
  if (!gpu_unai_depth)
    gpu_unai_depth = (unsigned short *)calloc(1024 * 512, sizeof(unsigned short));
  gpu_unai_depth_cur = 1;
  memset((void*)&gpu_unai, 0, sizeof(gpu_unai));
  gpu_unai.vram = (le16_t *)gpu.vram;

  gpu_unai.TextureWindow[0] = 0;
  gpu_unai.TextureWindow[1] = 0;
  gpu_unai.TextureWindow[2] = 255;
  gpu_unai.TextureWindow[3] = 255;
  gpu_unai.inn.mask_v00u =
    ((u32)gpu_unai.TextureWindow[3] << 24) | gpu_unai.TextureWindow[2];

  gpu_unai.config = gpu_unai_config_ext;
  gpu_unai.inn.ilace_mask = gpu_unai.config.ilace_force;

#if defined(GPU_UNAI_USE_INT_DIV_MULTINV) || (!defined(GPU_UNAI_NO_OLD) && !defined(GPU_UNAI_USE_FLOATMATH))
  for(int i=1;i<=(1<<TABLE_BITS);++i)
  {
    double v = 1.0 / double(i);
#ifdef GPU_TABLE_10_BITS
    v *= double(0xffffffff>>1);
#else
    v *= double(0x80000000);
#endif
    s_invTable[i-1]=s32(v);
  }
#endif

  SetupLightLUT();
  SetupDitheringConstants();

  return 0;
}

void renderer_finish(void)
{
  free(gpu_unai_depth);
  gpu_unai_depth = NULL;
}

void renderer_notify_screen_change(const struct psx_gpu_screen *screen)
{
  if (gpu_unai_depth_on && gpu_unai_depth
      && (screen->src_x != gpu_unai_depth_disp_x
          || screen->src_y != gpu_unai_depth_disp_y))
  {
    gpu_unai_depth_disp_n = gpu_unai_depth_cur > 1 ? gpu_unai_depth_cur - 1 : 1;
    gpu_depth_clear_rect(gpu_unai_depth_disp_x, gpu_unai_depth_disp_y,
        screen->hres > 0 ? screen->hres : 320,
        screen->vres > 0 ? screen->vres : 240);
    gpu_unai_depth_disp_x = screen->src_x;
    gpu_unai_depth_disp_y = screen->src_y;
    gpu_unai_depth_cur = 1;
    memset(gte_z_cache, 0, sizeof(gte_z_cache));
  }

  gpu_unai.inn.ilace_mask = gpu_unai.config.ilace_force;

  if (gpu.state.downscale_enable)
  {
    gpu_unai.inn.ilace_mask |= !!(gpu.status & PSX_GPU_STATUS_INTERLACE);
  }
}

#ifdef USE_GPULIB
static void gpuGP0Cmd_0xEx(gpu_unai_t &gpu_unai, u32 cmd_word)
{
  u8 num = (cmd_word >> 24) & 7;
  switch (num) {
    case 1: {
      u32 cur_texpage = gpu_unai.GPU_GP1 & 0x7FF;
      u32 new_texpage = cmd_word & 0x7FF;
      if (cur_texpage != new_texpage) {
        gpu_unai.GPU_GP1 = (gpu_unai.GPU_GP1 & ~0x7FF) | new_texpage;
        gpuSetTexture(gpu_unai.GPU_GP1);
      }
    } break;

    case 2: {
      if (cmd_word != gpu_unai.TextureWindowCur) {
        static const u8 TextureMask[32] = {
          255, 7, 15, 7, 31, 7, 15, 7, 63, 7, 15, 7, 31, 7, 15, 7,
          127, 7, 15, 7, 31, 7, 15, 7, 63, 7, 15, 7, 31, 7, 15, 7
        };
        gpu_unai.TextureWindowCur = cmd_word;
        gpu_unai.TextureWindow[0] = ((cmd_word >> 10) & 0x1F) << 3;
        gpu_unai.TextureWindow[1] = ((cmd_word >> 15) & 0x1F) << 3;
        gpu_unai.TextureWindow[2] = TextureMask[(cmd_word >> 0) & 0x1F];
        gpu_unai.TextureWindow[3] = TextureMask[(cmd_word >> 5) & 0x1F];
        gpu_unai.TextureWindow[0] &= ~gpu_unai.TextureWindow[2];
        gpu_unai.TextureWindow[1] &= ~gpu_unai.TextureWindow[3];

        gpu_unai.inn.mask_v00u =
          ((u32)gpu_unai.TextureWindow[3] << 24) | gpu_unai.TextureWindow[2];

        gpuSetTexture(gpu_unai.GPU_GP1);
      }
    } break;

    case 3: {
      gpu_unai.DrawingArea[0] = cmd_word         & 0x3FF;
      gpu_unai.DrawingArea[1] = (cmd_word >> 10) & 0x3FF;
    } break;

    case 4: {
      gpu_unai.DrawingArea[2] = (cmd_word         & 0x3FF) + 1;
      gpu_unai.DrawingArea[3] = ((cmd_word >> 10) & 0x3FF) + 1;
    } break;

    case 5: {
      gpu_unai.DrawingOffset[0] = GPU_EXPANDSIGN(cmd_word);
      gpu_unai.DrawingOffset[1] = GPU_EXPANDSIGN(cmd_word >> 11);
    } break;

    case 6: {
      gpu_unai.Masking  = (cmd_word & 0x2) <<  1;
      gpu_unai.PixelMSB = (cmd_word & 0x1) <<  8;
    } break;
  }
}
#endif

#include "../gpulib/gpu_timing.h"

static inline bool need_lighting(u32 rgb_raw)
{
  return (rgb_raw & HTOLE32(0xF8F8F8)) != HTOLE32(0x808080);
}

static inline void textured_sprite(int &cpu_cycles_sum, int &cpu_cycles)
{
  u32 PRIM = le32_to_u32(gpu_unai.PacketBuffer.U4[0]) >> 24;
  gpuSetCLUT(le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
  u32 driver_idx = Blending_Mode | gpu_unai.TEXT_MODE | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>1);
  s32 w = 0, h = 0;

  if (need_lighting(le32_raw(gpu_unai.PacketBuffer.U4[0])))
    driver_idx |= Lighting;
  PS driver = gpuSpriteDrivers[driver_idx];
  PtrUnion packet = { .ptr = (void*)&gpu_unai.PacketBuffer };
  gpuDrawS(packet, driver, &w, &h);
  gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
}

extern const unsigned char cmd_lengths[256];

int renderer_do_cmd_list(u32 *list_, int list_len, uint32_t *ex_regs,
 int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
  int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
  u32 cmd = 0, len, i;
  le32_t *list = (le32_t *)list_;
  le32_t *list_start = list;
  le32_t *list_end = list + list_len;

  if (IS_OLD_RENDERER()) {
    return oldunai_do_cmd_list(list_, list_len, ex_regs,
             cycles_sum_out, cycles_last, last_cmd);
  }

  for (; list < list_end; list += 1 + len)
  {
    cmd = le32_to_u32(*list) >> 24;
    len = cmd_lengths[cmd];
    if (list + 1 + len > list_end) {
      cmd = -1;
      break;
    }

    if (gpu_unai_depth_on)
      gpu_unai_depth_cur = gpu_get_poly_gte_depth(cmd, list);

    #define PRIM cmd
    gpu_unai.PacketBuffer.U4[0] = list[0];
    for (i = 1; i <= len; i++)
      gpu_unai.PacketBuffer.U4[i] = list[i];

    PtrUnion packet = { .ptr = (void*)&gpu_unai.PacketBuffer };

    switch (cmd)
    {
      case 0x02:
        gpuClearImage(packet);
        gput_sum(cpu_cycles_sum, cpu_cycles,
           gput_fill(le16_to_s16(packet.U2[4]) & 0x3ff, le16_to_s16(packet.U2[5]) & 0x1ff));
        break;

      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23: {
        PP driver = gpuPolySpanDrivers[
          Blending_Mode |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB
        ];
        gpuDrawPolyF(packet, driver, false);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base());
      } break;

      case 0x24:
      case 0x25:
      case 0x26:
      case 0x27: {
        gpuSetCLUT   (le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
        gpuSetTexture(le32_to_u32(gpu_unai.PacketBuffer.U4[4]) >> 16);

        u32 driver_idx =
          Dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB;

        if (!FastLightingEnabled()) {
          driver_idx |= Lighting;
        } else {
          if (!((gpu_unai.PacketBuffer.U1[0]>0x5F) && (gpu_unai.PacketBuffer.U1[1]>0x5F) && (gpu_unai.PacketBuffer.U1[2]>0x5F)))
            driver_idx |= Lighting;
        }

        PP driver = gpuPolySpanDrivers[driver_idx];
        gpuDrawPolyFT(packet, driver, false);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_t());
      } break;

      case 0x28:
      case 0x29:
      case 0x2A:
      case 0x2B: {
        PP driver = gpuPolySpanDrivers[
          Blending_Mode |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB
        ];
        gpuDrawPolyF(packet, driver, true);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base());
      } break;

      case 0x2C:
      case 0x2D:
      case 0x2E:
      case 0x2F: {
        u32 dithering = Dithering;
        u32 simplified_count;
        gpuSetTexture(le32_to_u32(gpu_unai.PacketBuffer.U4[4]) >> 16);
        if (!dithering &&
            (simplified_count = prim_try_simplify_quad_t(gpu_unai.PacketBuffer.U4,
              gpu_unai.PacketBuffer.U4)))
        {
          for (i = 0;; ) {
            textured_sprite(cpu_cycles_sum, cpu_cycles);
            if (++i >= simplified_count)
              break;
            memcpy(&gpu_unai.PacketBuffer.U4[0], &gpu_unai.PacketBuffer.U4[i * 4], 16);
          }
          break;
        }
        gpuSetCLUT(le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);

        u32 driver_idx =
          dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gpu_unai.PixelMSB;

        if (!FastLightingEnabled()) {
          driver_idx |= Lighting;
        } else {
          if (!((gpu_unai.PacketBuffer.U1[0]>0x5F) && (gpu_unai.PacketBuffer.U1[1]>0x5F) && (gpu_unai.PacketBuffer.U1[2]>0x5F)))
            driver_idx |= Lighting;
        }

        PP driver = gpuPolySpanDrivers[driver_idx];
        gpuDrawPolyFT(packet, driver, true);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base_t());
      } break;

      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33: {
        u32 dithering = Dithering;
        u8 gouraud = 129;
        if (!dithering) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 3; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 2]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0)
            gouraud = 0;
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode |
          gpu_unai.Masking | Blending | gouraud | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyG(packet, driver, false);
        else
          gpuDrawPolyF(packet, driver, false, POLYTYPE_G);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_g());
      } break;

      case 0x34:
      case 0x35:
      case 0x36:
      case 0x37: {
        gpuSetCLUT    (le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
        gpuSetTexture (le32_to_u32(gpu_unai.PacketBuffer.U4[5]) >> 16);
        u32 dithering = Dithering;
        u8 lighting = Lighting;
        u8 gouraud = lighting ? (1<<7) : 0;
        if (lighting) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 3; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 3]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0) {
            gouraud = 0;
            if (!dithering && !need_lighting(rgb0))
              lighting = 0;
          }
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gouraud | lighting | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyGT(packet, driver, false);
        else
          gpuDrawPolyFT(packet, driver, false, POLYTYPE_GT);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_gt());
      } break;

      case 0x38:
      case 0x39:
      case 0x3A:
      case 0x3B: {
        u32 dithering = Dithering;
        u8 gouraud = 129;
        if (!dithering) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 4; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 2]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0)
            gouraud = 0;
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode |
          gpu_unai.Masking | Blending | gouraud | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyG(packet, driver, true);
        else
          gpuDrawPolyF(packet, driver, true, POLYTYPE_G);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base_g());
      } break;

      case 0x3C:
      case 0x3D:
      case 0x3E:
      case 0x3F: {
        u32 dithering = Dithering;
        u32 simplified_count;
        gpuSetTexture(le32_to_u32(gpu_unai.PacketBuffer.U4[5]) >> 16);
        if (!dithering &&
             (simplified_count = prim_try_simplify_quad_gt(gpu_unai.PacketBuffer.U4,
              gpu_unai.PacketBuffer.U4)))
        {
          for (i = 0;; ) {
            textured_sprite(cpu_cycles_sum, cpu_cycles);
            if (++i >= simplified_count)
              break;
            memcpy(&gpu_unai.PacketBuffer.U4[0], &gpu_unai.PacketBuffer.U4[i * 4], 16);
          }
          break;
        }
        gpuSetCLUT(le32_to_u32(gpu_unai.PacketBuffer.U4[2]) >> 16);
        u8 lighting = Lighting;
        u8 gouraud = lighting ? (1<<7) : 0;
        if (lighting) {
          u32 xor_ = 0, rgb0 = le32_raw(gpu_unai.PacketBuffer.U4[0]);
          for (i = 1; i < 4; i++)
            xor_ |= rgb0 ^ le32_raw(gpu_unai.PacketBuffer.U4[i * 3]);
          if ((xor_ & HTOLE32(0xf8f8f8)) == 0) {
            gouraud = 0;
            if (!dithering && !need_lighting(rgb0))
              lighting = 0;
          }
        }
        PP driver = gpuPolySpanDrivers[
          dithering |
          Blending_Mode | gpu_unai.TEXT_MODE |
          gpu_unai.Masking | Blending | gouraud | lighting | gpu_unai.PixelMSB
        ];
        if (gouraud)
          gpuDrawPolyGT(packet, driver, true);
        else
          gpuDrawPolyFT(packet, driver, true, POLYTYPE_GT);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_quad_base_gt());
      } break;

      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43: {
        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineF(packet, driver);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
      } break;

      case 0x48 ... 0x4F: {
        u32 num_vertexes = 1;
        le32_t *list_position = &list[2];

        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineF(packet, driver);

        while(1)
        {
          gpu_unai.PacketBuffer.U4[1] = gpu_unai.PacketBuffer.U4[2];
          gpu_unai.PacketBuffer.U4[2] = *list_position++;
          gpuDrawLineF(packet, driver);
          gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));

          num_vertexes++;
          if(list_position >= list_end) {
            cmd = -1;
            goto breakloop;
          }
          if((le32_raw(*list_position) & HTOLE32(0xf000f000)) == HTOLE32(0x50005000))
            break;
        }

        len += (num_vertexes - 2);
      } break;

      case 0x50:
      case 0x51:
      case 0x52:
      case 0x53: {
        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        driver_idx |= (1 << 5);
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineG(packet, driver);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
      } break;

      case 0x58 ... 0x5F: {
        u32 num_vertexes = 1;
        le32_t *list_position = &list[2];

        u32 driver_idx = (Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1;
        driver_idx |= (1 << 5);
        PSD driver = gpuPixelSpanDrivers[driver_idx];
        gpuDrawLineG(packet, driver);

        while(1)
        {
          gpu_unai.PacketBuffer.U4[0] = gpu_unai.PacketBuffer.U4[2];
          gpu_unai.PacketBuffer.U4[1] = gpu_unai.PacketBuffer.U4[3];
          gpu_unai.PacketBuffer.U4[2] = *list_position++;
          gpu_unai.PacketBuffer.U4[3] = *list_position++;
          gpuDrawLineG(packet, driver);
          gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));

          num_vertexes++;
          if(list_position >= list_end) {
            cmd = -1;
            goto breakloop;
          }
          if((le32_raw(*list_position) & HTOLE32(0xf000f000)) == HTOLE32(0x50005000))
            break;
        }

        len += (num_vertexes - 2) * 2;
      } break;

      case 0x60:
      case 0x61:
      case 0x62:
      case 0x63: {
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
      } break;

      case 0x64:
      case 0x65:
      case 0x66:
      case 0x67:
        textured_sprite(cpu_cycles_sum, cpu_cycles);
        break;

      case 0x68:
      case 0x69:
      case 0x6A:
      case 0x6B: {
        gpu_unai.PacketBuffer.U4[2] = u32_to_le32(0x00010001);
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(1, 1));
      } break;

      case 0x70:
      case 0x71:
      case 0x72:
      case 0x73: {
        gpu_unai.PacketBuffer.U4[2] = u32_to_le32(0x00080008);
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
      } break;

      case 0x74:
      case 0x75:
      case 0x76:
      case 0x77: {
        gpu_unai.PacketBuffer.U4[3] = u32_to_le32(0x00080008);
        textured_sprite(cpu_cycles_sum, cpu_cycles);
      } break;

      case 0x78:
      case 0x79:
      case 0x7A:
      case 0x7B: {
        gpu_unai.PacketBuffer.U4[2] = u32_to_le32(0x00100010);
        PT driver = gpuTileDrivers[(Blending_Mode | gpu_unai.Masking | Blending | (gpu_unai.PixelMSB>>3)) >> 1];
        s32 w = 0, h = 0;
        gpuDrawT(packet, driver, &w, &h);
        gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
      } break;

      case 0x7C:
      case 0x7D:
      case 0x7E:
      case 0x7F: {
        gpu_unai.PacketBuffer.U4[3] = u32_to_le32(0x00100010);
        textured_sprite(cpu_cycles_sum, cpu_cycles);
      } break;

#ifdef TEST
      case 0x80:
        gpuMoveImage(packet);
        break;

      case 0xA0:
      {
        u32 load_width = list[2] & 0xffff;
        u32 load_height = list[2] >> 16;
        u32 load_size = load_width * load_height;

        len += load_size / 2;
      } break;

      case 0xC0:
        break;
#else
      case 0x1F:
      case 0x80 ... 0x9F:
      case 0xA0 ... 0xBF:
      case 0xC0 ... 0xDF:
        goto breakloop;
#endif
      case 0xE1 ... 0xE6: {
        u32 cmd_word = le32_to_u32(gpu_unai.PacketBuffer.U4[0]);
        ex_regs[(cmd_word >> 24) & 7] = cmd_word;
        gpuGP0Cmd_0xEx(gpu_unai, cmd_word);
      } break;
    }
  }

breakloop:
  ex_regs[1] &= ~0x1ff;
  ex_regs[1] |= gpu_unai.GPU_GP1 & 0x1ff;

  *cycles_sum_out += cpu_cycles_sum;
  *cycles_last = cpu_cycles;
  *last_cmd = cmd;
  return list - list_start;
}

void renderer_sync_ecmds(u32 *ecmds_)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  uint32_t i, ecmds[8];
  for (i = 1; i <= 6; i++)
    ecmds[i] = HTOLE32(ecmds_[i]);
#else
  uint32_t *ecmds = ecmds_;
#endif
  if (!IS_OLD_RENDERER()) {
    int dummy;
    renderer_do_cmd_list(&ecmds[1], 6, ecmds, &dummy, &dummy, &dummy);
  }
  else
    oldunai_renderer_sync_ecmds(ecmds);
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
}

void renderer_flush_queues(void)
{
}

void renderer_set_interlace(int enable, int is_odd)
{
  renderer_notify_screen_change(&gpu.screen);
}

#include "../../frontend/plugin_lib.h"
void renderer_set_config(const struct rearmed_cbs *cbs)
{
  gpu_unai.vram = (le16_t *)gpu.vram;
  gpu_unai.config.old_renderer  = cbs->gpu_unai.old_renderer;
  gpu_unai.config.ilace_force   = cbs->gpu_unai.ilace_force;
  gpu_unai.config.lighting      = cbs->gpu_unai.lighting;
  gpu_unai.config.fast_lighting = cbs->gpu_unai.fast_lighting;
  gpu_unai.config.blending      = cbs->gpu_unai.blending;
  gpu_unai.config.dithering     = cbs->dithering != 0;
  gpu_unai.config.force_dithering = cbs->dithering >> 1;

  renderer_notify_screen_change(&gpu.screen);
  oldunai_renderer_set_config(cbs);
}
