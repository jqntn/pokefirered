#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/io_reg.h"
#include "pfr/audio.h"
#include "pfr/core.h"
#include "pfr/scripted_input.h"
#include "raylib.h"

#define PFR_AUDIO_FRAMES_PER_GBA_FRAME 224
#define PFR_DEFAULT_AUDIO_MANIFEST_NAME "audio_capture_manifest.txt"
#define PFR_DEFAULT_SAVE_PATH "pfr_startup_audio_capture.tmp.sav"
#define PFR_FILENAME_BUFFER_SIZE 256
#define PFR_LINE_BUFFER_SIZE 512
#define PFR_PATH_BUFFER_SIZE 1024

typedef struct PfrAudioCaptureTarget
{
  char* name;
  uint32_t start_frame;
  uint32_t frame_count;
  bool has_expected_hash;
  uint32_t expected_hash;
  uint32_t hash;
  int16_t* samples;
  size_t sample_count;
  size_t write_index;
} PfrAudioCaptureTarget;

typedef struct PfrAudioCaptureList
{
  PfrAudioCaptureTarget* items;
  size_t count;
  size_t capacity;
} PfrAudioCaptureList;

typedef struct PfrOptions
{
  PfrMode mode;
  const char* output_dir;
  const char* manifest_out;
  const char* save_path;
  PfrScriptedInput scripted_input;
  PfrAudioCaptureList captures;
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

static bool
pfr_is_path_separator(char ch)
{
  return ch == '/' || ch == '\\';
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
  unsigned long parsed = strtoul(text, &end, 16);

  if (text[0] == '\0' || end == NULL || *end != '\0' || parsed > 0xFFFFFFFFUL) {
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
pfr_free_captures(PfrAudioCaptureList* captures)
{
  size_t i;

  for (i = 0; i < captures->count; i++) {
    free(captures->items[i].name);
    free(captures->items[i].samples);
  }

  free(captures->items);
  captures->items = NULL;
  captures->count = 0;
  captures->capacity = 0;
}

static void
pfr_append_capture(PfrAudioCaptureList* captures,
                   const char* name,
                   uint32_t start_frame,
                   uint32_t frame_count,
                   bool has_expected_hash,
                   uint32_t expected_hash)
{
  if (captures->count == captures->capacity) {
    size_t new_capacity = captures->capacity == 0 ? 8 : captures->capacity * 2;
    PfrAudioCaptureTarget* new_items = (PfrAudioCaptureTarget*)realloc(
      captures->items, new_capacity * sizeof(PfrAudioCaptureTarget));

    if (new_items == NULL) {
      fprintf(stderr, "out of memory\n");
      exit(EXIT_FAILURE);
    }

    captures->items = new_items;
    captures->capacity = new_capacity;
  }

  captures->items[captures->count].name = pfr_strdup_text(name);
  captures->items[captures->count].start_frame = start_frame;
  captures->items[captures->count].frame_count = frame_count;
  captures->items[captures->count].has_expected_hash = has_expected_hash;
  captures->items[captures->count].expected_hash = expected_hash;
  captures->items[captures->count].hash = 2166136261u;
  captures->items[captures->count].samples = NULL;
  captures->items[captures->count].sample_count = 0;
  captures->items[captures->count].write_index = 0;
  captures->count++;
}

static void
pfr_load_capture_manifest(PfrAudioCaptureList* captures, const char* path)
{
  FILE* file = fopen(path, "r");
  char line_buffer[PFR_LINE_BUFFER_SIZE];
  unsigned long line_number = 0;

  if (file == NULL) {
    fprintf(stderr, "failed to open audio manifest: %s\n", path);
    exit(EXIT_FAILURE);
  }

  while (fgets(line_buffer, sizeof(line_buffer), file) != NULL) {
    char* line;
    char* first_sep;
    char* second_sep;
    char* third_sep;
    char* name_token;
    char* start_token;
    char* count_token;
    char* hash_token = NULL;
    uint32_t start_frame = 0;
    uint32_t frame_count = 0;
    uint32_t expected_hash = 0;
    bool has_expected_hash = false;

    line_number++;
    line = pfr_trim(line_buffer);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    first_sep = strchr(line, '|');
    second_sep = first_sep != NULL ? strchr(first_sep + 1, '|') : NULL;
    third_sep = second_sep != NULL ? strchr(second_sep + 1, '|') : NULL;
    if (first_sep == NULL || second_sep == NULL) {
      fprintf(
        stderr, "invalid audio manifest line %lu in %s\n", line_number, path);
      exit(EXIT_FAILURE);
    }

    *first_sep = '\0';
    *second_sep = '\0';
    if (third_sep != NULL) {
      *third_sep = '\0';
      hash_token = pfr_trim(third_sep + 1);
    }

    name_token = pfr_trim(line);
    start_token = pfr_trim(first_sep + 1);
    count_token = pfr_trim(second_sep + 1);

    if (name_token[0] == '\0' || !pfr_parse_u32(start_token, &start_frame) ||
        !pfr_parse_u32(count_token, &frame_count) || frame_count == 0) {
      fprintf(
        stderr, "invalid audio manifest line %lu in %s\n", line_number, path);
      exit(EXIT_FAILURE);
    }

    if (hash_token != NULL && hash_token[0] != '\0') {
      if (!pfr_parse_hex_u32(hash_token, &expected_hash)) {
        fprintf(stderr,
                "invalid audio hash on manifest line %lu in %s\n",
                line_number,
                path);
        exit(EXIT_FAILURE);
      }

      has_expected_hash = true;
    }

    pfr_append_capture(captures,
                       name_token,
                       start_frame,
                       frame_count,
                       has_expected_hash,
                       expected_hash);
  }

  fclose(file);
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
    output[write_index++] = 'a';
  }

  output[write_index] = '\0';
}

static void
pfr_build_audio_filename(const PfrAudioCaptureTarget* capture,
                         char* output,
                         size_t output_size)
{
  char safe_name[PFR_FILENAME_BUFFER_SIZE];

  pfr_sanitize_name(capture->name, safe_name, sizeof(safe_name));
  snprintf(output, output_size, "%06u_%s.wav", capture->start_frame, safe_name);
}

static void
pfr_write_wav(const char* path,
              const int16_t* samples,
              size_t frame_count,
              uint32_t sample_rate)
{
  FILE* file = fopen(path, "wb");
  uint32_t data_bytes =
    (uint32_t)(frame_count * PFR_AUDIO_CHANNEL_COUNT * sizeof(int16_t));
  size_t i;

  if (file == NULL) {
    fprintf(stderr, "failed to open %s\n", path);
    exit(EXIT_FAILURE);
  }

  fwrite("RIFF", 1, 4, file);
  write_u32_le(file, 36u + data_bytes);
  fwrite("WAVE", 1, 4, file);
  fwrite("fmt ", 1, 4, file);
  write_u32_le(file, 16);
  write_u16_le(file, 1);
  write_u16_le(file, PFR_AUDIO_CHANNEL_COUNT);
  write_u32_le(file, sample_rate);
  write_u32_le(file, sample_rate * PFR_AUDIO_CHANNEL_COUNT * sizeof(int16_t));
  write_u16_le(file, PFR_AUDIO_CHANNEL_COUNT * sizeof(int16_t));
  write_u16_le(file, 16);
  fwrite("data", 1, 4, file);
  write_u32_le(file, data_bytes);

  for (i = 0; i < frame_count * PFR_AUDIO_CHANNEL_COUNT; i++) {
    write_u16_le(file, (uint16_t)samples[i]);
  }

  fclose(file);
}

static void
pfr_hash_audio_samples(PfrAudioCaptureTarget* capture,
                       const int16_t* samples,
                       size_t sample_count)
{
  size_t i;

  for (i = 0; i < sample_count; i++) {
    uint16_t sample_bits = (uint16_t)samples[i];

    capture->hash ^= (uint8_t)(sample_bits & 0xFF);
    capture->hash *= 16777619u;
    capture->hash ^= (uint8_t)(sample_bits >> 8);
    capture->hash *= 16777619u;
  }
}

static void
pfr_prepare_capture_buffers(PfrAudioCaptureList* captures)
{
  size_t i;

  for (i = 0; i < captures->count; i++) {
    size_t sample_count = (size_t)captures->items[i].frame_count *
                          PFR_AUDIO_FRAMES_PER_GBA_FRAME *
                          PFR_AUDIO_CHANNEL_COUNT;

    captures->items[i].samples =
      (int16_t*)malloc(sample_count * sizeof(int16_t));
    if (captures->items[i].samples == NULL) {
      fprintf(stderr, "out of memory\n");
      exit(EXIT_FAILURE);
    }

    captures->items[i].sample_count = sample_count;
  }
}

static void
pfr_print_usage(const char* program_name)
{
  fprintf(stderr,
          "usage: %s --output-dir DIR --audio-manifest PATH\n"
          "       [--mode game|demo|sandbox] [--input-manifest PATH]\n"
          "       [--auto-press-start-frame N]... [--manifest-out PATH]\n"
          "       [--save-path PATH]\n"
          "audio manifest lines: name|start_frame|frame_count|expected_hash\n",
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
    } else if (strcmp(argv[i], "--audio-manifest") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --audio-manifest value\n");
        exit(EXIT_FAILURE);
      }

      pfr_load_capture_manifest(&options->captures, argv[++i]);
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
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      pfr_print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      pfr_print_usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  if (options->output_dir == NULL || options->captures.count == 0) {
    pfr_print_usage(argv[0]);
    exit(EXIT_FAILURE);
  }
}

static int
pfr_run_capture(PfrOptions* options, const char* manifest_out)
{
  FILE* manifest_file = NULL;
  const char* save_path =
    options->save_path != NULL ? options->save_path : PFR_DEFAULT_SAVE_PATH;
  bool remove_save_path = options->save_path == NULL;
  uint32_t max_frame = 0;
  uint32_t sample_rate = PFR_DEFAULT_AUDIO_SAMPLE_RATE;
  int16_t frame_audio[PFR_AUDIO_FRAMES_PER_GBA_FRAME * PFR_AUDIO_CHANNEL_COUNT];
  size_t i;
  uint32_t frame_index;
  int exit_code = EXIT_SUCCESS;

  for (i = 0; i < options->captures.count; i++) {
    uint32_t capture_end = options->captures.items[i].start_frame +
                           options->captures.items[i].frame_count - 1;
    if (capture_end > max_frame) {
      max_frame = capture_end;
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

  pfr_prepare_capture_buffers(&options->captures);
  pfr_core_init(save_path, options->mode);
  pfr_audio_reset();
  sample_rate = pfr_audio_source_sample_rate();

  for (frame_index = 0; frame_index <= max_frame; frame_index++) {
    uint16_t keys =
      pfr_scripted_input_keys_for_frame(&options->scripted_input, frame_index);

    pfr_core_set_keys(keys);
    pfr_core_run_frame();

    if (pfr_audio_drain_source_frames(frame_audio,
                                      PFR_AUDIO_FRAMES_PER_GBA_FRAME) !=
        PFR_AUDIO_FRAMES_PER_GBA_FRAME) {
      fprintf(
        stderr, "failed to drain source audio at frame %u\n", frame_index);
      exit_code = EXIT_FAILURE;
      break;
    }

    for (i = 0; i < options->captures.count; i++) {
      PfrAudioCaptureTarget* capture = &options->captures.items[i];
      uint32_t capture_end = capture->start_frame + capture->frame_count;

      if (frame_index < capture->start_frame || frame_index >= capture_end) {
        continue;
      }

      memcpy(&capture->samples[capture->write_index],
             frame_audio,
             sizeof(frame_audio));
      capture->write_index += PFR_ARRAY_COUNT(frame_audio);
      pfr_hash_audio_samples(
        capture, frame_audio, PFR_ARRAY_COUNT(frame_audio));
    }

    if (pfr_core_should_quit() && frame_index < max_frame) {
      const char* status_line = pfr_core_status_line();

      if (status_line[0] != '\0') {
        fprintf(stderr, "%s\n", status_line);
      } else {
        fprintf(stderr,
                "runtime requested quit before all audio captures completed\n");
      }

      exit_code = EXIT_FAILURE;
      break;
    }
  }

  if (exit_code == EXIT_SUCCESS) {
    for (i = 0; i < options->captures.count; i++) {
      const PfrAudioCaptureTarget* capture = &options->captures.items[i];
      char audio_filename[PFR_FILENAME_BUFFER_SIZE];
      char audio_path[PFR_PATH_BUFFER_SIZE];

      pfr_build_audio_filename(capture, audio_filename, sizeof(audio_filename));
      pfr_join_path(
        options->output_dir, audio_filename, audio_path, sizeof(audio_path));
      pfr_write_wav(audio_path,
                    capture->samples,
                    (size_t)capture->frame_count *
                      PFR_AUDIO_FRAMES_PER_GBA_FRAME,
                    sample_rate);
      fprintf(manifest_file,
              "%s|%u|%u|%u|%08x|%s\n",
              capture->name,
              capture->start_frame,
              capture->frame_count,
              sample_rate,
              capture->hash,
              audio_filename);

      if (capture->has_expected_hash &&
          capture->expected_hash != capture->hash) {
        fprintf(
          stderr,
          "audio hash mismatch for %s at frame %u: expected %08x got %08x\n",
          capture->name,
          capture->start_frame,
          capture->expected_hash,
          capture->hash);
        exit_code = EXIT_FAILURE;
      }
    }
  }

  pfr_audio_shutdown();
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
                  PFR_DEFAULT_AUDIO_MANIFEST_NAME,
                  manifest_out,
                  sizeof(manifest_out));
  }

  exit_code = pfr_run_capture(&options, manifest_out);
  pfr_free_captures(&options.captures);
  pfr_scripted_input_free(&options.scripted_input);
  return exit_code;
}
