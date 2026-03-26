#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

#if defined(_MSC_VER)
#define PFR_NORETURN __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#define PFR_NORETURN __attribute__((noreturn))
#else
#define PFR_NORETURN
#endif

typedef struct PfrColorPalette
{
  Color* colors;
  int count;
} PfrColorPalette;

typedef enum PfrPngIndexMode
{
  PFR_PNG_INDEX_NONE = 0,
  PFR_PNG_INDEX_GRAYSCALE,
  PFR_PNG_INDEX_PALETTE,
} PfrPngIndexMode;

typedef struct PfrPngIndexInfo
{
  PfrPngIndexMode mode;
  int width;
  int height;
  int bit_depth;
  int palette_count;
  unsigned char* indices;
  Color palette[256];
} PfrPngIndexInfo;

typedef struct PfrByteBuffer
{
  unsigned char* data;
  size_t size;
  size_t capacity;
} PfrByteBuffer;

static PFR_NORETURN void
pfr_fail(const char* message, const char* detail)
{
  if (detail != NULL) {
    fprintf(stderr, "%s: %s\n", message, detail);
  } else {
    fprintf(stderr, "%s\n", message);
  }

  exit(EXIT_FAILURE);
}

static int
pfr_ascii_casecmp(const char* lhs, const char* rhs)
{
  while (*lhs != '\0' && *rhs != '\0') {
    int lhs_char = tolower((unsigned char)*lhs);
    int rhs_char = tolower((unsigned char)*rhs);

    if (lhs_char != rhs_char) {
      return lhs_char - rhs_char;
    }

    lhs++;
    rhs++;
  }

  return (unsigned char)*lhs - (unsigned char)*rhs;
}

static const char*
pfr_extension(const char* path)
{
  const char* dot = strrchr(path, '.');

  if (dot == NULL) {
    return "";
  }

  return dot + 1;
}

static int
pfr_rgb_to_555(Color color)
{
  return ((color.r >> 3) & 0x1F) | (((color.g >> 3) & 0x1F) << 5) |
         (((color.b >> 3) & 0x1F) << 10);
}

static uint32_t
pfr_read_be_u32(const unsigned char* data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void
pfr_buffer_reserve(PfrByteBuffer* buffer, size_t extra)
{
  size_t required = buffer->size + extra;

  if (required <= buffer->capacity) {
    return;
  }

  if (buffer->capacity == 0) {
    buffer->capacity = 256;
  }

  while (buffer->capacity < required) {
    buffer->capacity *= 2;
  }

  buffer->data = realloc(buffer->data, buffer->capacity);
  if (buffer->data == NULL) {
    pfr_fail("failed to allocate output buffer", NULL);
  }
}

static void
pfr_buffer_push(PfrByteBuffer* buffer, unsigned char value)
{
  pfr_buffer_reserve(buffer, 1);
  buffer->data[buffer->size++] = value;
}

static void
pfr_buffer_append(PfrByteBuffer* buffer, const unsigned char* data, size_t size)
{
  pfr_buffer_reserve(buffer, size);
  memcpy(buffer->data + buffer->size, data, size);
  buffer->size += size;
}

static void
pfr_buffer_free(PfrByteBuffer* buffer)
{
  free(buffer->data);
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
}

static void
pfr_write_file(const char* path, const unsigned char* data, size_t size)
{
  FILE* file = fopen(path, "wb");

  if (file == NULL) {
    pfr_fail("failed to open output file", path);
  }

  if (size > 0 && fwrite(data, size, 1, file) != 1) {
    fclose(file);
    pfr_fail("failed to write output file", path);
  }

  fclose(file);
}

static unsigned char*
pfr_read_file(const char* path, size_t* size)
{
  FILE* file = fopen(path, "rb");
  long length;
  unsigned char* data;

  if (file == NULL) {
    pfr_fail("failed to open input file", path);
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    pfr_fail("failed to seek input file", path);
  }

  length = ftell(file);
  if (length < 0) {
    fclose(file);
    pfr_fail("failed to measure input file", path);
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    pfr_fail("failed to rewind input file", path);
  }

  data = malloc((size_t)length);
  if (length > 0 && data == NULL) {
    fclose(file);
    pfr_fail("failed to allocate input file buffer", path);
  }

  if (length > 0 && fread(data, (size_t)length, 1, file) != 1) {
    free(data);
    fclose(file);
    pfr_fail("failed to read input file", path);
  }

  fclose(file);
  *size = (size_t)length;
  return data;
}

static bool
pfr_try_load_png_index_info(const char* path, PfrPngIndexInfo* info)
{
  static const unsigned char sPngSignature[8] = { 0x89, 'P',  'N',  'G',
                                                  '\r', '\n', 0x1A, '\n' };
  size_t size = 0;
  unsigned char* data = pfr_read_file(path, &size);
  size_t offset = sizeof(sPngSignature);
  PfrByteBuffer idat = { 0 };
  unsigned char* decompressed = NULL;
  unsigned char* previous_row = NULL;
  unsigned char* current_row = NULL;
  bool has_ihdr = false;
  bool success = false;

  memset(info, 0, sizeof(*info));

  if (size < sizeof(sPngSignature) ||
      memcmp(data, sPngSignature, sizeof(sPngSignature)) != 0) {
    goto done;
  }

  while (offset + 12U <= size) {
    uint32_t chunk_size = pfr_read_be_u32(data + offset);
    const unsigned char* chunk_type = data + offset + 4U;
    const unsigned char* chunk_data = data + offset + 8U;

    if ((size_t)chunk_size > size - offset - 12U) {
      goto done;
    }

    if (memcmp(chunk_type, "IHDR", 4) == 0) {
      unsigned char color_type;
      unsigned char bit_depth;
      unsigned char interlace_method;

      if (chunk_size < 13U) {
        goto done;
      }

      info->width = (int)pfr_read_be_u32(chunk_data + 0);
      info->height = (int)pfr_read_be_u32(chunk_data + 4);
      bit_depth = chunk_data[8];
      color_type = chunk_data[9];
      interlace_method = chunk_data[12];

      if (bit_depth != 1U && bit_depth != 2U && bit_depth != 4U &&
          bit_depth != 8U) {
        goto done;
      }

      if (info->width <= 0 || info->height <= 0 || interlace_method != 0U) {
        goto done;
      }

      info->bit_depth = (int)bit_depth;
      if (color_type == 0U) {
        info->mode = PFR_PNG_INDEX_GRAYSCALE;
      } else if (color_type == 3U) {
        info->mode = PFR_PNG_INDEX_PALETTE;
      } else {
        goto done;
      }

      has_ihdr = true;
    } else if (memcmp(chunk_type, "PLTE", 4) == 0 &&
               info->mode == PFR_PNG_INDEX_PALETTE) {
      uint32_t index;

      if ((chunk_size % 3U) != 0 || chunk_size / 3U > 256U) {
        goto done;
      }

      info->palette_count = (int)(chunk_size / 3U);
      for (index = 0; index < chunk_size / 3U; index++) {
        info->palette[index] = (Color){ chunk_data[index * 3U + 0U],
                                        chunk_data[index * 3U + 1U],
                                        chunk_data[index * 3U + 2U],
                                        255 };
      }
    } else if (memcmp(chunk_type, "IDAT", 4) == 0) {
      pfr_buffer_append(&idat, chunk_data, chunk_size);
    } else if (memcmp(chunk_type, "IEND", 4) == 0) {
      break;
    }

    offset += (size_t)chunk_size + 12U;
  }

  if (!has_ihdr) {
    goto done;
  }

  if (info->mode == PFR_PNG_INDEX_GRAYSCALE) {
    success = true;
  } else if (info->mode == PFR_PNG_INDEX_PALETTE && info->palette_count > 0) {
    success = true;
  }

  if (success) {
    int packed_row_size = (info->width * info->bit_depth + 7) / 8;
    int expected_size = info->height * (packed_row_size + 1);
    int decompressed_size = 0;
    int y;

    if (idat.size < 6U) {
      success = false;
      goto done;
    }

    decompressed =
      DecompressData(idat.data + 2, (int)idat.size - 6, &decompressed_size);
    if (decompressed == NULL || decompressed_size != expected_size) {
      success = false;
      goto done;
    }

    info->indices = malloc((size_t)info->width * (size_t)info->height *
                           sizeof(unsigned char));
    previous_row = calloc((size_t)packed_row_size, sizeof(unsigned char));
    current_row = malloc((size_t)packed_row_size);
    if (info->indices == NULL || previous_row == NULL || current_row == NULL) {
      success = false;
      goto done;
    }

    for (y = 0; y < info->height; y++) {
      const unsigned char* src =
        decompressed + (size_t)y * (size_t)(packed_row_size + 1);
      unsigned char filter = src[0];
      int x;

      memcpy(current_row, src + 1, (size_t)packed_row_size);

      switch (filter) {
        case 0:
          break;
        case 1:
          for (x = 0; x < packed_row_size; x++) {
            unsigned char left = (x > 0) ? current_row[x - 1] : 0;
            current_row[x] = (unsigned char)(current_row[x] + left);
          }
          break;
        case 2:
          for (x = 0; x < packed_row_size; x++) {
            current_row[x] = (unsigned char)(current_row[x] + previous_row[x]);
          }
          break;
        case 3:
          for (x = 0; x < packed_row_size; x++) {
            unsigned char left = (x > 0) ? current_row[x - 1] : 0;
            unsigned char up = previous_row[x];
            current_row[x] =
              (unsigned char)(current_row[x] + ((left + up) / 2));
          }
          break;
        case 4:
          for (x = 0; x < packed_row_size; x++) {
            int left = (x > 0) ? current_row[x - 1] : 0;
            int up = previous_row[x];
            int up_left = (x > 0) ? previous_row[x - 1] : 0;
            int predictor = left + up - up_left;
            int left_delta = predictor - left;
            int up_delta = predictor - up;
            int up_left_delta = predictor - up_left;

            if (left_delta < 0) {
              left_delta = -left_delta;
            }

            if (up_delta < 0) {
              up_delta = -up_delta;
            }

            if (up_left_delta < 0) {
              up_left_delta = -up_left_delta;
            }

            if (left_delta <= up_delta && left_delta <= up_left_delta) {
              predictor = left;
            } else if (up_delta <= up_left_delta) {
              predictor = up;
            } else {
              predictor = up_left;
            }

            current_row[x] = (unsigned char)(current_row[x] + predictor);
          }
          break;
        default:
          success = false;
          goto done;
      }

      for (x = 0; x < info->width; x++) {
        unsigned char value;

        if (info->bit_depth == 8) {
          value = current_row[x];
        } else if (info->bit_depth == 4) {
          unsigned char packed = current_row[x / 2];
          value =
            (unsigned char)(((x & 1) == 0) ? (packed >> 4) : (packed & 0x0F));
        } else if (info->bit_depth == 2) {
          unsigned char packed = current_row[x / 4];
          value = (unsigned char)((packed >> (6 - (x & 3) * 2)) & 0x03);
        } else {
          unsigned char packed = current_row[x / 8];
          value = (unsigned char)((packed >> (7 - (x & 7))) & 0x01);
        }

        info->indices[(size_t)y * (size_t)info->width + (size_t)x] = value;
      }

      memcpy(previous_row, current_row, (size_t)packed_row_size);
    }
  }

done:
  if (!success && info->indices != NULL) {
    free(info->indices);
    info->indices = NULL;
  }

  free(current_row);
  free(previous_row);
  if (decompressed != NULL) {
    MemFree(decompressed);
  }
  pfr_buffer_free(&idat);
  free(data);
  return success;
}

static PfrColorPalette
pfr_load_palette_from_jasc(const char* path)
{
  FILE* file = fopen(path, "r");
  char line[256];
  int count;
  int index;
  PfrColorPalette palette = { 0 };

  if (file == NULL) {
    pfr_fail("failed to open palette file", path);
  }

  if (fgets(line, sizeof(line), file) == NULL ||
      strncmp(line, "JASC-PAL", 8) != 0) {
    fclose(file);
    pfr_fail("invalid JASC palette header", path);
  }

  if (fgets(line, sizeof(line), file) == NULL) {
    fclose(file);
    pfr_fail("invalid JASC palette version", path);
  }

  if (fgets(line, sizeof(line), file) == NULL) {
    fclose(file);
    pfr_fail("invalid JASC palette color count", path);
  }

  count = atoi(line);
  if (count <= 0 || count > 256) {
    fclose(file);
    pfr_fail("unsupported JASC palette color count", path);
  }

  palette.colors = calloc((size_t)count, sizeof(Color));
  if (palette.colors == NULL) {
    fclose(file);
    pfr_fail("failed to allocate palette", path);
  }

  palette.count = count;

  for (index = 0; index < count; index++) {
    int red = 0;
    int green = 0;
    int blue = 0;

    if (fgets(line, sizeof(line), file) == NULL ||
        sscanf(line, "%d %d %d", &red, &green, &blue) != 3) {
      fclose(file);
      free(palette.colors);
      pfr_fail("invalid JASC palette entry", path);
    }

    palette.colors[index] = (Color){
      (unsigned char)red, (unsigned char)green, (unsigned char)blue, 255
    };
  }

  fclose(file);
  return palette;
}

static PfrColorPalette
pfr_copy_palette(const Color* colors, int count, const char* path)
{
  PfrColorPalette palette = { 0 };

  if (colors == NULL || count <= 0 || count > 256) {
    pfr_fail("invalid palette data", path);
  }

  palette.colors = calloc((size_t)count, sizeof(Color));
  if (palette.colors == NULL) {
    pfr_fail("failed to allocate palette", path);
  }

  memcpy(palette.colors, colors, (size_t)count * sizeof(Color));
  palette.count = count;
  return palette;
}

static PfrColorPalette
pfr_load_palette_from_png(const char* path)
{
  PfrPngIndexInfo png_index_info = { 0 };
  Image image = { 0 };
  Color* colors = NULL;
  int count = 0;
  PfrColorPalette palette = { 0 };

  if (pfr_try_load_png_index_info(path, &png_index_info) &&
      png_index_info.mode == PFR_PNG_INDEX_PALETTE &&
      png_index_info.palette_count > 0) {
    palette = pfr_copy_palette(
      png_index_info.palette, png_index_info.palette_count, path);
    free(png_index_info.indices);
    return palette;
  }

  free(png_index_info.indices);
  image = LoadImage(path);

  if (image.data == NULL) {
    pfr_fail("failed to load png palette source", path);
  }

  colors = LoadImagePalette(image, 256, &count);
  UnloadImage(image);

  if (colors == NULL || count <= 0) {
    pfr_fail("failed to extract png palette", path);
  }

  palette = pfr_copy_palette(colors, count, path);
  UnloadImagePalette(colors);
  return palette;
}

static PfrColorPalette
pfr_load_palette(const char* path)
{
  if (pfr_ascii_casecmp(pfr_extension(path), "pal") == 0) {
    return pfr_load_palette_from_jasc(path);
  }

  if (pfr_ascii_casecmp(pfr_extension(path), "png") == 0) {
    return pfr_load_palette_from_png(path);
  }

  pfr_fail("unsupported palette input", path);
}

static void
pfr_free_palette(PfrColorPalette* palette)
{
  if (palette->colors != NULL) {
    free(palette->colors);
    palette->colors = NULL;
  }

  palette->count = 0;
}

static int
pfr_find_palette_index(const PfrColorPalette* palette, Color color)
{
  int index;
  int best_index = 0;
  int best_delta = 0x7FFFFFFF;
  int target_luma;

  if (color.a == 0) {
    return 0;
  }

  for (index = 0; index < palette->count; index++) {
    const Color entry = palette->colors[index];

    if (entry.r == color.r && entry.g == color.g && entry.b == color.b) {
      return index;
    }
  }

  target_luma = color.r * 3 + color.g * 6 + color.b;

  for (index = 0; index < palette->count; index++) {
    const Color entry = palette->colors[index];
    int entry_luma = entry.r * 3 + entry.g * 6 + entry.b;
    int delta = entry_luma - target_luma;

    if (delta < 0) {
      delta = -delta;
    }

    if (delta < best_delta) {
      best_delta = delta;
      best_index = index;
    }
  }

  return best_index;
}

static void
pfr_generate_tile_data(const char* input_path,
                       const char* output_path,
                       int bpp,
                       const char* palette_path)
{
  Image image = LoadImage(input_path);
  Color* pixels = NULL;
  PfrColorPalette palette = { 0 };
  PfrPngIndexInfo png_index_info = { 0 };
  bool use_png_indices = false;
  unsigned char* output = NULL;
  size_t output_size;
  int tiles_x;
  int tiles_y;
  int tile_x;
  int tile_y;
  int row;
  int col;
  size_t offset = 0;

  if (image.data == NULL) {
    pfr_fail("failed to load image", input_path);
  }

  if ((image.width % 8) != 0 || (image.height % 8) != 0) {
    UnloadImage(image);
    pfr_fail("image dimensions must be multiples of 8", input_path);
  }

  if (pfr_ascii_casecmp(pfr_extension(input_path), "png") == 0 &&
      pfr_try_load_png_index_info(input_path, &png_index_info)) {
    use_png_indices = true;
  } else {
    palette = (palette_path != NULL) ? pfr_load_palette(palette_path)
                                     : pfr_load_palette_from_png(input_path);
  }
  pixels = LoadImageColors(image);

  if (pixels == NULL) {
    if (!use_png_indices) {
      pfr_free_palette(&palette);
    }
    UnloadImage(image);
    pfr_fail("failed to read image pixels", input_path);
  }

  if (!use_png_indices && bpp == 4 && palette.count > 16) {
    pfr_fail("4bpp image uses more than 16 colors", input_path);
  }

  output_size = (size_t)image.width * (size_t)image.height / (size_t)(8 / bpp);
  output = malloc(output_size);
  if (output == NULL) {
    UnloadImageColors(pixels);
    pfr_free_palette(&palette);
    UnloadImage(image);
    pfr_fail("failed to allocate tile output", input_path);
  }

  tiles_x = image.width / 8;
  tiles_y = image.height / 8;

  for (tile_y = 0; tile_y < tiles_y; tile_y++) {
    for (tile_x = 0; tile_x < tiles_x; tile_x++) {
      for (row = 0; row < 8; row++) {
        if (bpp == 8) {
          for (col = 0; col < 8; col++) {
            int pixel_x = tile_x * 8 + col;
            int pixel_y = tile_y * 8 + row;
            int index;

            if (use_png_indices) {
              index =
                png_index_info
                  .indices[(size_t)pixel_y * (size_t)png_index_info.width +
                           (size_t)pixel_x];
              if (png_index_info.mode == PFR_PNG_INDEX_GRAYSCALE) {
                index = 0xFF - index;
              }
            } else {
              index = pfr_find_palette_index(
                &palette, pixels[pixel_y * image.width + pixel_x]);
            }

            if (index < 0 || index > 255) {
              free(output);
              UnloadImageColors(pixels);
              if (!use_png_indices) {
                pfr_free_palette(&palette);
              }
              UnloadImage(image);
              pfr_fail("pixel color is not present in palette", input_path);
            }

            output[offset++] = (unsigned char)index;
          }
        } else {
          for (col = 0; col < 8; col += 2) {
            int pixel_x = tile_x * 8 + col;
            int pixel_y = tile_y * 8 + row;
            int left;
            int right;

            if (use_png_indices) {
              int max_index = (1 << bpp) - 1;

              left = png_index_info
                       .indices[(size_t)pixel_y * (size_t)png_index_info.width +
                                (size_t)pixel_x];
              right =
                png_index_info
                  .indices[(size_t)pixel_y * (size_t)png_index_info.width +
                           (size_t)(pixel_x + 1)];
              left &= max_index;
              right &= max_index;
              if (png_index_info.mode == PFR_PNG_INDEX_GRAYSCALE) {
                left = max_index - left;
                right = max_index - right;
              }
            } else {
              left = pfr_find_palette_index(
                &palette, pixels[pixel_y * image.width + pixel_x]);
              right = pfr_find_palette_index(
                &palette, pixels[pixel_y * image.width + pixel_x + 1]);
            }

            if (left < 0 || left > 15 || right < 0 || right > 15) {
              free(output);
              UnloadImageColors(pixels);
              if (!use_png_indices) {
                pfr_free_palette(&palette);
              }
              UnloadImage(image);
              pfr_fail("pixel color is not present in 4bpp palette",
                       input_path);
            }

            output[offset++] = (unsigned char)(left | (right << 4));
          }
        }
      }
    }
  }

  pfr_write_file(output_path, output, output_size);
  free(output);
  UnloadImageColors(pixels);
  free(png_index_info.indices);
  if (!use_png_indices) {
    pfr_free_palette(&palette);
  }
  UnloadImage(image);
}

static void
pfr_generate_gbapal(const char* input_path, const char* output_path)
{
  PfrColorPalette palette = pfr_load_palette(input_path);
  unsigned char* output = NULL;
  int index;

  output = malloc((size_t)palette.count * 2U);
  if (output == NULL) {
    pfr_free_palette(&palette);
    pfr_fail("failed to allocate gbapal buffer", input_path);
  }

  for (index = 0; index < palette.count; index++) {
    int color = pfr_rgb_to_555(palette.colors[index]);

    output[index * 2] = (unsigned char)(color & 0xFF);
    output[index * 2 + 1] = (unsigned char)((color >> 8) & 0xFF);
  }

  pfr_write_file(output_path, output, (size_t)palette.count * 2U);
  free(output);
  pfr_free_palette(&palette);
}

static int
pfr_find_match(const unsigned char* data,
               size_t size,
               size_t position,
               int* out_length)
{
  size_t window_start = (position > 0x1000U) ? position - 0x1000U : 0;
  int best_length = 0;
  int best_offset = 0;
  size_t candidate;

  for (candidate = window_start; candidate < position; candidate++) {
    int length = 0;

    while (length < 18 && position + (size_t)length < size &&
           data[candidate + (size_t)length] ==
             data[position + (size_t)length]) {
      length++;
    }

    if (length >= 3 && length > best_length) {
      best_length = length;
      best_offset = (int)(position - candidate - 1);

      if (best_length == 18) {
        break;
      }
    }
  }

  *out_length = best_length;
  return best_offset;
}

static void
pfr_generate_lz(const char* input_path, const char* output_path)
{
  size_t input_size = 0;
  unsigned char* input = pfr_read_file(input_path, &input_size);
  PfrByteBuffer output = { 0 };
  size_t position = 0;

  pfr_buffer_push(&output, 0x10);
  pfr_buffer_push(&output, (unsigned char)(input_size & 0xFF));
  pfr_buffer_push(&output, (unsigned char)((input_size >> 8) & 0xFF));
  pfr_buffer_push(&output, (unsigned char)((input_size >> 16) & 0xFF));

  while (position < input_size) {
    size_t flag_offset = output.size;
    unsigned char flags = 0;
    int bit;

    pfr_buffer_push(&output, 0);

    for (bit = 0; bit < 8 && position < input_size; bit++) {
      int length = 0;
      int back_offset = pfr_find_match(input, input_size, position, &length);

      if (length >= 3) {
        flags |= (unsigned char)(1U << (7 - bit));
        pfr_buffer_push(
          &output,
          (unsigned char)(((length - 3) << 4) | ((back_offset >> 8) & 0x0F)));
        pfr_buffer_push(&output, (unsigned char)(back_offset & 0xFF));
        position += (size_t)length;
      } else {
        pfr_buffer_push(&output, input[position]);
        position++;
      }
    }

    output.data[flag_offset] = flags;
  }

  while ((output.size & 3U) != 0) {
    pfr_buffer_push(&output, 0);
  }

  pfr_write_file(output_path, output.data, output.size);
  pfr_buffer_free(&output);
  free(input);
}

static void
pfr_print_usage(const char* program)
{
  fprintf(stderr,
          "usage:\n"
          "  %s tile4 <input.png> <output> [palette.pal|palette.png]\n"
          "  %s tile8 <input.png> <output> [palette.pal|palette.png]\n"
          "  %s gbapal <input.pal|input.png> <output>\n"
          "  %s lz <input> <output>\n",
          program,
          program,
          program,
          program);
}

int
main(int argc, char** argv)
{
  if (argc < 4) {
    pfr_print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "tile4") == 0) {
    pfr_generate_tile_data(argv[2], argv[3], 4, argc >= 5 ? argv[4] : NULL);
    return EXIT_SUCCESS;
  }

  if (strcmp(argv[1], "tile8") == 0) {
    pfr_generate_tile_data(argv[2], argv[3], 8, argc >= 5 ? argv[4] : NULL);
    return EXIT_SUCCESS;
  }

  if (strcmp(argv[1], "gbapal") == 0) {
    pfr_generate_gbapal(argv[2], argv[3]);
    return EXIT_SUCCESS;
  }

  if (strcmp(argv[1], "lz") == 0) {
    pfr_generate_lz(argv[2], argv[3]);
    return EXIT_SUCCESS;
  }

  pfr_print_usage(argv[0]);
  return EXIT_FAILURE;
}
