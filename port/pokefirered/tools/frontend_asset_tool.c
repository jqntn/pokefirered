#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

typedef struct PfrColorPalette
{
  Color* colors;
  int count;
} PfrColorPalette;

typedef struct PfrByteBuffer
{
  unsigned char* data;
  size_t size;
  size_t capacity;
} PfrByteBuffer;

static void
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
  return ((color.r >> 3) & 0x1F) |
         (((color.g >> 3) & 0x1F) << 5) |
         (((color.b >> 3) & 0x1F) << 10);
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

    palette.colors[index] =
      (Color){ (unsigned char)red, (unsigned char)green, (unsigned char)blue, 255 };
  }

  fclose(file);
  return palette;
}

static PfrColorPalette
pfr_load_palette_from_png(const char* path)
{
  Image image = LoadImage(path);
  Color* colors = NULL;
  int count = 0;
  PfrColorPalette palette = { 0 };

  if (image.data == NULL) {
    pfr_fail("failed to load png palette source", path);
  }

  colors = LoadImagePalette(image, 256, &count);
  UnloadImage(image);

  if (colors == NULL || count <= 0) {
    pfr_fail("failed to extract png palette", path);
  }

  palette.colors = colors;
  palette.count = count;
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
  return (PfrColorPalette){ 0 };
}

static void
pfr_free_palette(PfrColorPalette* palette)
{
  if (palette->colors != NULL) {
    UnloadImagePalette(palette->colors);
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

  palette =
    (palette_path != NULL) ? pfr_load_palette(palette_path)
                           : pfr_load_palette_from_png(input_path);
  pixels = LoadImageColors(image);

  if (pixels == NULL) {
    pfr_free_palette(&palette);
    UnloadImage(image);
    pfr_fail("failed to read image pixels", input_path);
  }

  if (bpp == 4 && palette.count > 16) {
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
            int index =
              pfr_find_palette_index(&palette,
                                     pixels[pixel_y * image.width + pixel_x]);

            if (index < 0 || index > 255) {
              free(output);
              UnloadImageColors(pixels);
              pfr_free_palette(&palette);
              UnloadImage(image);
              pfr_fail("pixel color is not present in palette", input_path);
            }

            output[offset++] = (unsigned char)index;
          }
        } else {
          for (col = 0; col < 8; col += 2) {
            int pixel_x = tile_x * 8 + col;
            int pixel_y = tile_y * 8 + row;
            int left =
              pfr_find_palette_index(&palette,
                                     pixels[pixel_y * image.width + pixel_x]);
            int right =
              pfr_find_palette_index(&palette,
                                     pixels[pixel_y * image.width + pixel_x + 1]);

            if (left < 0 || left > 15 || right < 0 || right > 15) {
              free(output);
              UnloadImageColors(pixels);
              pfr_free_palette(&palette);
              UnloadImage(image);
              pfr_fail("pixel color is not present in 4bpp palette", input_path);
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
  pfr_free_palette(&palette);
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
           data[candidate + (size_t)length] == data[position + (size_t)length]) {
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
        pfr_buffer_push(&output,
                        (unsigned char)(((length - 3) << 4) |
                                        ((back_offset >> 8) & 0x0F)));
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
