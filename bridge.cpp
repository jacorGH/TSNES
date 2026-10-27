// Touch-SNES (TSNES) bridge: wraps a libretro core in a small C API for JavaScript.
// The same file is built three times by .github/workflows/build.yml:
//   snes.js (Snes9x, with -DTS_SNES for the SNES video-hardware tools), gb.js (Gambatte), gba.js (VBA-Next).
#include <emscripten.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include "libretro.h"
#ifdef TS_SNES
#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#endif

static uint16_t vbuf[512 * 480];
static int vw = 256, vh = 224;
#define AMAX 16384
static int16_t abuf[AMAX * 2];
static int pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static int aframes = 0;
static uint32_t pad = 0, pad2 = 0;
static uint8_t regs[0x40];      // copy of $2100-$213F after each frame
static uint16_t scroll[8];      // BG1-4 horizontal/vertical scroll
static bool loaded = false;
static double rate = 32040.0, fps = 60.0988;

static void log_cb(enum retro_log_level, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

static bool env_cb(unsigned cmd, void *data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      pixfmt = *(enum retro_pixel_format *)data; return pixfmt <= RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/"; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
      ((struct retro_variable *)data)->value = NULL; return false;
    default: return false;
  }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
  if (!data) return;
  if (w > 512) w = 512;
  if (h > 480) h = 480;
  vw = w; vh = h;
  // Everything is handed to JavaScript as RGB565, whatever the core draws in.
  for (unsigned y = 0; y < h; y++) {
    const uint8_t *row = (const uint8_t *)data + y * pitch;
    uint16_t *out = vbuf + y * w;
    if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) memcpy(out, row, w * 2);
    else if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
      const uint32_t *p = (const uint32_t *)row;
      for (unsigned x = 0; x < w; x++) { uint32_t c = p[x]; out[x] = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F); }
    } else {
      const uint16_t *p = (const uint16_t *)row;
      for (unsigned x = 0; x < w; x++) { uint16_t c = p[x]; out[x] = ((c & 0x7C00) << 1) | ((c & 0x03E0) << 1) | ((c >> 4) & 0x20) | (c & 0x1F); }
    }
  }
}

static size_t audio_batch_cb(const int16_t *d, size_t frames) {
  size_t room = AMAX - aframes, n = frames < room ? frames : room;
  memcpy(abuf + aframes * 2, d, n * 4);
  aframes += n;
  return frames;
}
static void audio_cb(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch_cb(s, 1); }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned device, unsigned, unsigned id) {
  if (port > 1 || device != RETRO_DEVICE_JOYPAD) return 0;
  uint32_t m = port ? pad2 : pad;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
  return (m >> id) & 1;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void s9x_init() {
  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(poll_cb);
  retro_set_input_state(input_cb);
  retro_init();
}

EMSCRIPTEN_KEEPALIVE int s9x_load(const uint8_t *rom, int size) {
  if (loaded) { retro_unload_game(); loaded = false; }
  struct retro_game_info info = {"game.sfc", rom, (size_t)size, NULL};
  if (!retro_load_game(&info)) return 0;
  retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
  retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
  struct retro_system_av_info av;
  retro_get_system_av_info(&av);
  rate = av.timing.sample_rate; fps = av.timing.fps;
  loaded = true;
  return 1;
}

EMSCRIPTEN_KEEPALIVE void s9x_frame() {
  aframes = 0;
  retro_run();
#ifdef TS_SNES
  memcpy(regs, Memory.FillRAM + 0x2100, 0x40);
  for (int i = 0; i < 4; i++) { scroll[i * 2] = PPU.BG[i].HOffset; scroll[i * 2 + 1] = PPU.BG[i].VOffset; }
#endif
}

EMSCRIPTEN_KEEPALIVE void s9x_reset() { retro_reset(); }
EMSCRIPTEN_KEEPALIVE void s9x_set_input(uint32_t mask) { pad = mask; }
EMSCRIPTEN_KEEPALIVE void s9x_set_input2(uint32_t mask) { pad2 = mask; }
EMSCRIPTEN_KEEPALIVE uint16_t *s9x_video() { return vbuf; }
EMSCRIPTEN_KEEPALIVE int s9x_vw() { return vw; }
EMSCRIPTEN_KEEPALIVE int s9x_vh() { return vh; }
EMSCRIPTEN_KEEPALIVE int16_t *s9x_audio() { return abuf; }
EMSCRIPTEN_KEEPALIVE int s9x_audio_frames() { return aframes; }
EMSCRIPTEN_KEEPALIVE double s9x_rate() { return rate; }
EMSCRIPTEN_KEEPALIVE double s9x_fps() { return fps; }

EMSCRIPTEN_KEEPALIVE int s9x_state_size() { return (int)retro_serialize_size(); }
EMSCRIPTEN_KEEPALIVE int s9x_state_save(void *dst, int size) { return retro_serialize(dst, size); }
EMSCRIPTEN_KEEPALIVE int s9x_state_load(const void *src, int size) { return retro_unserialize(src, size); }
EMSCRIPTEN_KEEPALIVE void *s9x_sram() { return retro_get_memory_data(RETRO_MEMORY_SAVE_RAM); }
EMSCRIPTEN_KEEPALIVE int s9x_sram_size() { return (int)retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); }

// Which machine this build is: 1 = SNES, 2 = Game Boy / Color, 3 = Game Boy Advance.
EMSCRIPTEN_KEEPALIVE int s9x_system() {
#if defined(TS_SNES)
  return 1;
#elif defined(TS_GBA)
  return 3;
#else
  return 2;
#endif
}

// Work RAM for memory search, watches and lighting/season addresses (every system).
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_wram() {
#ifdef TS_SNES
  return Memory.RAM;
#else
  return (uint8_t *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
#endif
}
EMSCRIPTEN_KEEPALIVE int s9x_wram_size() {
#ifdef TS_SNES
  return 0x20000;
#else
  return (int)retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
#endif
}

#ifdef TS_SNES
// Raw SNES hardware state for the layer tools. All read-only from JS except via s9x_vram_write.
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_vram() { return Memory.VRAM; }
EMSCRIPTEN_KEEPALIVE uint16_t *s9x_cgram() { return PPU.CGDATA; }
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_oam() { return PPU.OAMData; }
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_regs() { return regs; }
EMSCRIPTEN_KEEPALIVE uint16_t *s9x_scroll() { return scroll; }

// Writes tile data through the real VRAM ports so Snes9x refreshes its tile cache,
// then restores the game's own VRAM port state so the game never notices.
EMSCRIPTEN_KEEPALIVE void s9x_vram_write(uint32_t waddr, const uint16_t *src, int words) {
  auto vma = PPU.VMA;
  uint8_t r15 = Memory.FillRAM[0x2115], r16 = Memory.FillRAM[0x2116], r17 = Memory.FillRAM[0x2117];
  bool8 block = Settings.BlockInvalidVRAMAccess;
  Settings.BlockInvalidVRAMAccess = FALSE;
  S9xSetPPU(0x80, 0x2115);
  S9xSetPPU(waddr & 0xff, 0x2116);
  S9xSetPPU((waddr >> 8) & 0x7f, 0x2117);
  for (int i = 0; i < words; i++) {
    S9xSetPPU(src[i] & 0xff, 0x2118);
    S9xSetPPU(src[i] >> 8, 0x2119);
  }
  PPU.VMA = vma;
  Memory.FillRAM[0x2115] = r15; Memory.FillRAM[0x2116] = r16; Memory.FillRAM[0x2117] = r17;
  Settings.BlockInvalidVRAMAccess = block;
}
#else
// Other systems have no SNES layer tools; these keep the JavaScript side simple.
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_vram() { return 0; }
EMSCRIPTEN_KEEPALIVE uint16_t *s9x_cgram() { return 0; }
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_oam() { return 0; }
EMSCRIPTEN_KEEPALIVE uint8_t *s9x_regs() { return regs; }
EMSCRIPTEN_KEEPALIVE uint16_t *s9x_scroll() { return scroll; }
EMSCRIPTEN_KEEPALIVE void s9x_vram_write(uint32_t, const uint16_t *, int) {}
#endif

EMSCRIPTEN_KEEPALIVE void s9x_cheat_reset() { retro_cheat_reset(); }
EMSCRIPTEN_KEEPALIVE void s9x_cheat_set(int index, int on, const char *code) { retro_cheat_set(index, on != 0, code); }

}
