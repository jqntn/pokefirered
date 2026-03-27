#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pfr/core.h"

#define BMP_HEADER_SIZE 54

static void
write_u16_le(FILE* file, uint16_t value)
{
  fputc((int)(value & 0xFF), file);
  fputc((int)((value >> 8) & 0xFF), file);
}

static void
write_u32_le(FILE* file, uint32_t value)
{
  fputc((int)(value & 0xFF), file);
  fputc((int)((value >> 8) & 0xFF), file);
  fputc((int)((value >> 16) & 0xFF), file);
  fputc((int)((value >> 24) & 0xFF), file);
}

static void
write_bmp(const char* path, const uint32_t* framebuffer)
{
  FILE* file = fopen(path, "wb");
  const int width = PFR_SCREEN_WIDTH;
  const int height = PFR_SCREEN_HEIGHT;
  const uint32_t pixel_bytes = (uint32_t)width * (uint32_t)height * 4U;
  unsigned char bgra[4];
  int y;
  int x;

  if (file == NULL) {
    fprintf(stderr, "failed to open %s\n", path);
    exit(EXIT_FAILURE);
  }

  fputc('B', file);
  fputc('M', file);
  write_u32_le(file, BMP_HEADER_SIZE + pixel_bytes);
  write_u16_le(file, 0);
  write_u16_le(file, 0);
  write_u32_le(file, BMP_HEADER_SIZE);

  write_u32_le(file, 40);
  write_u32_le(file, (uint32_t)width);
  write_u32_le(file, (uint32_t)(-height));
  write_u16_le(file, 1);
  write_u16_le(file, 32);
  write_u32_le(file, 0);
  write_u32_le(file, pixel_bytes);
  write_u32_le(file, 2835);
  write_u32_le(file, 2835);
  write_u32_le(file, 0);
  write_u32_le(file, 0);

  for (y = 0; y < height; y++) {
    const uint32_t* row = framebuffer + (size_t)y * (size_t)width;

    for (x = 0; x < width; x++) {
      uint32_t rgba = row[x];

      bgra[0] = (unsigned char)((rgba >> 16) & 0xFF);
      bgra[1] = (unsigned char)((rgba >> 8) & 0xFF);
      bgra[2] = (unsigned char)(rgba & 0xFF);
      bgra[3] = (unsigned char)((rgba >> 24) & 0xFF);
      fwrite(bgra, sizeof(bgra), 1, file);
    }
  }

  fclose(file);
}

int
main(int argc, char** argv)
{
  int frames;

  if (argc != 3) {
    fprintf(stderr, "usage: %s <frames> <output.bmp>\n", argv[0]);
    return EXIT_FAILURE;
  }

  frames = atoi(argv[1]);
  if (frames <= 0) {
    fprintf(stderr, "invalid frame count: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  pfr_core_init("pfr_title_capture.sav", PFR_BOOT_FRONTEND);

  while (frames-- > 0) {
    pfr_core_run_frame();
  }

  write_bmp(argv[2], pfr_core_framebuffer());
  pfr_core_shutdown();
  remove("pfr_title_capture.sav");
  return EXIT_SUCCESS;
}
