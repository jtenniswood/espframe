#pragma once

#include <cstdint>

struct WarmToneLuts {
  uint8_t r[32];
  uint8_t g[64];
  uint8_t b[32];
};

inline void build_warm_tone_luts(float last_w, float new_w, WarmToneLuts &luts) {
  // Build lookup tables that first undo the previous tint, then apply the new
  // tint. This prevents repeated warm-tone updates from permanently drifting
  // the image colors.
  float r_undo = (last_w > 0.005f) ? 1.0f / (1.0f + last_w * 0.06f) : 1.0f;
  float g_undo = (last_w > 0.005f) ? 1.0f / (1.0f - last_w * 0.07f) : 1.0f;
  float b_undo = (last_w > 0.005f) ? 1.0f / (1.0f - last_w * 0.28f) : 1.0f;
  float r_apply = 1.0f + new_w * 0.06f;
  float g_apply = 1.0f - new_w * 0.07f;
  float b_apply = 1.0f - new_w * 0.28f;

  for (int i = 0; i < 32; i++) {
    int v = (i << 3) | (i >> 2);
    int nv = (int)(v * r_undo * r_apply);
    luts.r[i] = (uint8_t)((nv > 255 ? 255 : (nv < 0 ? 0 : nv)) >> 3);
  }
  for (int i = 0; i < 64; i++) {
    int v = (i << 2) | (i >> 4);
    int nv = (int)(v * g_undo * g_apply);
    luts.g[i] = (uint8_t)((nv > 255 ? 255 : (nv < 0 ? 0 : nv)) >> 2);
  }
  for (int i = 0; i < 32; i++) {
    int v = (i << 3) | (i >> 2);
    int nv = (int)(v * b_undo * b_apply);
    luts.b[i] = (uint8_t)((nv > 255 ? 255 : (nv < 0 ? 0 : nv)) >> 3);
  }
}
