#ifndef PFR_RENDERER_H
#define PFR_RENDERER_H

#include <stdint.h>

void
pfr_renderer_init(void);
void
pfr_renderer_shutdown(void);
void
pfr_renderer_begin_frame_capture(void);
void
pfr_renderer_capture_scanline(int scanline);
void
pfr_renderer_render_frame(void);
const uint32_t*
pfr_renderer_framebuffer(void);
uint32_t
pfr_renderer_checksum(void);

#endif
