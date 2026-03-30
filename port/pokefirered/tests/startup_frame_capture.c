#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/io_reg.h"
#include "pfr/core.h"
#include "pfr/scripted_input.h"
#include "raylib.h"

#define BMP_HEADER_SIZE 54
#define PFR_DEFAULT_MANIFEST_NAME "capture_manifest.txt"
#define PFR_DEFAULT_SAVE_PATH "pfr_startup_frame_capture.tmp.sav"
#define PFR_FILENAME_BUFFER_SIZE 256
#define PFR_LINE_BUFFER_SIZE 512
#define PFR_PATH_BUFFER_SIZE 1024

typedef struct PfrCaptureTarget
{
  char* name;
  uint32_t frame;
  bool has_expected_checksum;
  uint32_t expected_checksum;
} PfrCaptureTarget;

typedef struct PfrCaptureList
{
  PfrCaptureTarget* items;
  size_t count;
  size_t capacity;
} PfrCaptureList;

typedef struct PfrOptions
{
  PfrMode mode;
  const char* output_dir;
  const char* manifest_out;
  const char* save_path;
  PfrScriptedInput scripted_input;
  PfrCaptureList captures;
} PfrOptions;

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

static bool
pfr_is_path_separator(char ch)
{
  return ch == '/' || ch == '\\';
}

static bool
pfr_parse_u32(const char* text, uint32_t* value)
{
  char* end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);

  if (text[0] == '\0' || end == NULL || *end != '\0' || parsed > 0xFFFFFFFFUL) {
    return false;
  }

  *value = (uint32_t)parsed;
  return true;
}

static bool
pfr_parse_hex_u32(const char* text, uint32_t* value)
{
  char* end = NULL;
  unsigned long parsed;

  if (text[0] == '\0') {
    return false;
  }

  parsed = strtoul(text, &end, 16);
  if (end == NULL || *end != '\0' || parsed > 0xFFFFFFFFUL) {
    return false;
  }

  *value = (uint32_t)parsed;
  return true;
}

static char*
pfr_trim(char* text)
{
  char* end;

  while (*text != '\0' && isspace((unsigned char)*text)) {
    text++;
  }

  end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) {
    end--;
  }

  *end = '\0';
  return text;
}

static char*
pfr_strdup_text(const char* text)
{
  size_t length = strlen(text) + 1;
  char* copy = (char*)malloc(length);

  if (copy == NULL) {
    fprintf(stderr, "out of memory\n");
    exit(EXIT_FAILURE);
  }

  memcpy(copy, text, length);
  return copy;
}

static void
pfr_free_captures(PfrCaptureList* captures)
{
  size_t i;

  for (i = 0; i < captures->count; i++) {
    free(captures->items[i].name);
  }

  free(captures->items);
  captures->items = NULL;
  captures->count = 0;
  captures->capacity = 0;
}

static void
pfr_append_capture(PfrCaptureList* captures,
                   const char* name,
                   uint32_t frame,
                   bool has_expected_checksum,
                   uint32_t expected_checksum)
{
  size_t i;

  for (i = 0; i < captures->count; i++) {
    if (strcmp(captures->items[i].name, name) == 0) {
      fprintf(stderr, "duplicate capture name: %s\n", name);
      exit(EXIT_FAILURE);
    }
  }

  if (captures->count == captures->capacity) {
    size_t new_capacity = captures->capacity == 0 ? 8 : captures->capacity * 2;
    PfrCaptureTarget* new_items = (PfrCaptureTarget*)realloc(
      captures->items, new_capacity * sizeof(PfrCaptureTarget));

    if (new_items == NULL) {
      fprintf(stderr, "out of memory\n");
      exit(EXIT_FAILURE);
    }

    captures->items = new_items;
    captures->capacity = new_capacity;
  }

  captures->items[captures->count].name = pfr_strdup_text(name);
  captures->items[captures->count].frame = frame;
  captures->items[captures->count].has_expected_checksum =
    has_expected_checksum;
  captures->items[captures->count].expected_checksum = expected_checksum;
  captures->count++;
}

static void
pfr_append_generated_capture(PfrCaptureList* captures, uint32_t frame)
{
  char generated_name[32];

  snprintf(generated_name, sizeof(generated_name), "frame_%06u", frame);
  pfr_append_capture(captures, generated_name, frame, false, 0);
}

static void
pfr_load_capture_manifest(PfrCaptureList* captures, const char* path)
{
  FILE* file = fopen(path, "r");
  char line_buffer[PFR_LINE_BUFFER_SIZE];
  unsigned long line_number = 0;

  if (file == NULL) {
    fprintf(stderr, "failed to open manifest: %s\n", path);
    exit(EXIT_FAILURE);
  }

  while (fgets(line_buffer, sizeof(line_buffer), file) != NULL) {
    char* line;
    char* first_sep;
    char* second_sep;
    char* third_sep;
    char* name_token;
    char* frame_token;
    char* checksum_token = NULL;
    uint32_t frame = 0;
    uint32_t expected_checksum = 0;
    bool has_expected_checksum = false;

    line_number++;
    line = pfr_trim(line_buffer);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    first_sep = strchr(line, '|');
    if (first_sep == NULL) {
      fprintf(stderr, "invalid manifest line %lu in %s\n", line_number, path);
      exit(EXIT_FAILURE);
    }

    *first_sep = '\0';
    second_sep = strchr(first_sep + 1, '|');
    if (second_sep != NULL) {
      *second_sep = '\0';
      third_sep = strchr(second_sep + 1, '|');
      if (third_sep != NULL) {
        *third_sep = '\0';
      }
      checksum_token = pfr_trim(second_sep + 1);
    }

    name_token = pfr_trim(line);
    frame_token = pfr_trim(first_sep + 1);

    if (name_token[0] == '\0' || !pfr_parse_u32(frame_token, &frame)) {
      fprintf(stderr, "invalid manifest line %lu in %s\n", line_number, path);
      exit(EXIT_FAILURE);
    }

    if (checksum_token != NULL && checksum_token[0] != '\0') {
      if (!pfr_parse_hex_u32(checksum_token, &expected_checksum)) {
        fprintf(stderr,
                "invalid checksum on manifest line %lu in %s\n",
                line_number,
                path);
        exit(EXIT_FAILURE);
      }

      has_expected_checksum = true;
    }

    pfr_append_capture(
      captures, name_token, frame, has_expected_checksum, expected_checksum);
  }

  fclose(file);
}

static bool
pfr_make_directories(const char* path)
{
  if (path == NULL || path[0] == '\0') {
    return false;
  }

  return DirectoryExists(path) || MakeDirectory(path) == 0;
}

static void
pfr_join_path(const char* lhs,
              const char* rhs,
              char* output,
              size_t output_size)
{
  size_t lhs_length = strlen(lhs);
  bool needs_separator =
    lhs_length > 0 && !pfr_is_path_separator(lhs[lhs_length - 1]);

  snprintf(output, output_size, needs_separator ? "%s/%s" : "%s%s", lhs, rhs);
}

static void
pfr_sanitize_name(const char* name, char* output, size_t output_size)
{
  size_t write_index = 0;

  while (*name != '\0' && write_index + 1 < output_size) {
    unsigned char ch = (unsigned char)*name++;

    if (isalnum(ch) || ch == '-' || ch == '_') {
      output[write_index++] = (char)ch;
    } else {
      output[write_index++] = '_';
    }
  }

  if (write_index == 0 && output_size > 1) {
    output[write_index++] = 'f';
  }

  output[write_index] = '\0';
}

static void
pfr_build_image_filename(const PfrCaptureTarget* capture,
                         char* output,
                         size_t output_size)
{
  char safe_name[PFR_FILENAME_BUFFER_SIZE];

  pfr_sanitize_name(capture->name, safe_name, sizeof(safe_name));
  snprintf(output, output_size, "%06u_%s.bmp", capture->frame, safe_name);
}

static uint16_t
pfr_keys_for_frame(const PfrOptions* options, uint32_t frame_index)
{
  return pfr_scripted_input_keys_for_frame(&options->scripted_input,
                                           frame_index);
}

static void
pfr_print_usage(const char* program_name)
{
  fprintf(stderr,
          "usage: %s --output-dir DIR [--mode game|demo|sandbox]\n"
          "       [--frame N]... [--frame-manifest PATH]\n"
          "       [--auto-press-start-frame N]... [--input-manifest PATH]\n"
          "       [--manifest-out PATH]\n"
          "       [--save-path PATH]\n"
          "manifest lines: name|frame|expected_checksum\n",
          program_name);
}

static PfrMode
pfr_parse_mode(const char* value)
{
  if (strcmp(value, "game") == 0) {
    return PFR_MODE_GAME;
  }

  if (strcmp(value, "demo") == 0) {
    return PFR_MODE_DEMO;
  }

  if (strcmp(value, "sandbox") == 0) {
    return PFR_MODE_SANDBOX;
  }

  fprintf(stderr, "invalid --mode value: %s\n", value);
  exit(EXIT_FAILURE);
}

static void
pfr_parse_options(int argc, char** argv, PfrOptions* options)
{
  int i;

  memset(options, 0, sizeof(*options));
  options->mode = PFR_MODE_GAME;
  pfr_scripted_input_init(&options->scripted_input);

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mode") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --mode value\n");
        exit(EXIT_FAILURE);
      }

      options->mode = pfr_parse_mode(argv[++i]);
    } else if (strcmp(argv[i], "--output-dir") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --output-dir value\n");
        exit(EXIT_FAILURE);
      }

      options->output_dir = argv[++i];
    } else if (strcmp(argv[i], "--manifest-out") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --manifest-out value\n");
        exit(EXIT_FAILURE);
      }

      options->manifest_out = argv[++i];
    } else if (strcmp(argv[i], "--save-path") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --save-path value\n");
        exit(EXIT_FAILURE);
      }

      options->save_path = argv[++i];
    } else if (strcmp(argv[i], "--frame") == 0) {
      uint32_t frame = 0;

      if (i + 1 >= argc || !pfr_parse_u32(argv[++i], &frame)) {
        fprintf(stderr, "invalid --frame value\n");
        exit(EXIT_FAILURE);
      }

      pfr_append_generated_capture(&options->captures, frame);
    } else if (strcmp(argv[i], "--frame-manifest") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --frame-manifest value\n");
        exit(EXIT_FAILURE);
      }

      pfr_load_capture_manifest(&options->captures, argv[++i]);
    } else if (strcmp(argv[i], "--auto-press-start-frame") == 0) {
      char error[256];
      uint32_t frame = 0;

      if (i + 1 >= argc || !pfr_parse_u32(argv[++i], &frame)) {
        fprintf(stderr, "invalid --auto-press-start-frame value\n");
        exit(EXIT_FAILURE);
      }

      if (!pfr_scripted_input_append(&options->scripted_input,
                                     frame,
                                     START_BUTTON,
                                     error,
                                     sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "--input-manifest") == 0) {
      char error[256];

      if (i + 1 >= argc) {
        fprintf(stderr, "missing --input-manifest value\n");
        exit(EXIT_FAILURE);
      }

      if (!pfr_scripted_input_load_manifest(
            &options->scripted_input, argv[++i], error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        exit(EXIT_FAILURE);
      }
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      pfr_print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      pfr_print_usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  if (options->output_dir == NULL) {
    fprintf(stderr, "missing --output-dir\n");
    pfr_print_usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  if (options->captures.count == 0) {
    fprintf(stderr, "no frames requested\n");
    pfr_print_usage(argv[0]);
    exit(EXIT_FAILURE);
  }
}

static int
pfr_run_capture(const PfrOptions* options, const char* manifest_out)
{
  FILE* manifest_file = NULL;
  const char* save_path =
    options->save_path != NULL ? options->save_path : PFR_DEFAULT_SAVE_PATH;
  uint32_t max_frame = 0;
  bool remove_save_path = options->save_path == NULL;
  size_t i;
  uint32_t frame_index;
  int exit_code = EXIT_SUCCESS;

  for (i = 0; i < options->captures.count; i++) {
    if (options->captures.items[i].frame > max_frame) {
      max_frame = options->captures.items[i].frame;
    }
  }

  if (!pfr_make_directories(options->output_dir)) {
    fprintf(
      stderr, "failed to create output directory: %s\n", options->output_dir);
    return EXIT_FAILURE;
  }

  manifest_file = fopen(manifest_out, "w");
  if (manifest_file == NULL) {
    fprintf(stderr, "failed to open manifest output: %s\n", manifest_out);
    return EXIT_FAILURE;
  }

  pfr_core_init(save_path, options->mode);

  for (frame_index = 0; frame_index <= max_frame; frame_index++) {
    pfr_core_set_keys(pfr_keys_for_frame(options, frame_index));
    pfr_core_run_frame();

    for (i = 0; i < options->captures.count; i++) {
      char image_filename[PFR_FILENAME_BUFFER_SIZE];
      char image_path[PFR_PATH_BUFFER_SIZE];
      uint32_t checksum;

      if (options->captures.items[i].frame != frame_index) {
        continue;
      }

      pfr_build_image_filename(
        &options->captures.items[i], image_filename, sizeof(image_filename));
      pfr_join_path(
        options->output_dir, image_filename, image_path, sizeof(image_path));

      write_bmp(image_path, pfr_core_framebuffer());
      checksum = pfr_core_frame_checksum();
      fprintf(manifest_file,
              "%s|%u|%08x|%s\n",
              options->captures.items[i].name,
              options->captures.items[i].frame,
              checksum,
              image_filename);

      if (options->captures.items[i].has_expected_checksum &&
          options->captures.items[i].expected_checksum != checksum) {
        fprintf(
          stderr,
          "checksum mismatch for %s at frame %u: expected %08x got %08x\n",
          options->captures.items[i].name,
          options->captures.items[i].frame,
          options->captures.items[i].expected_checksum,
          checksum);
        exit_code = EXIT_FAILURE;
      }
    }

    if (pfr_core_should_quit() && frame_index < max_frame) {
      const char* status_line = pfr_core_status_line();

      if (status_line[0] != '\0') {
        fprintf(stderr, "%s\n", status_line);
      } else {
        fprintf(stderr,
                "runtime requested quit before all captures completed\n");
      }

      exit_code = EXIT_FAILURE;
      break;
    }
  }

  pfr_core_shutdown();
  fclose(manifest_file);

  if (remove_save_path) {
    remove(save_path);
  }

  return exit_code;
}

int
main(int argc, char** argv)
{
  PfrOptions options;
  char manifest_out[PFR_PATH_BUFFER_SIZE];
  int exit_code;

  pfr_parse_options(argc, argv, &options);

  if (options.manifest_out != NULL) {
    snprintf(manifest_out, sizeof(manifest_out), "%s", options.manifest_out);
  } else {
    pfr_join_path(options.output_dir,
                  PFR_DEFAULT_MANIFEST_NAME,
                  manifest_out,
                  sizeof(manifest_out));
  }

  exit_code = pfr_run_capture(&options, manifest_out);
  pfr_free_captures(&options.captures);
  pfr_scripted_input_free(&options.scripted_input);
  return exit_code;
}
