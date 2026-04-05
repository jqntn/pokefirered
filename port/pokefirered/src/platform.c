#include "gba/defines.h"
#include "gba/io_reg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

#include "pfr/audio.h"
#include "pfr/core.h"
#include "pfr/scripted_input.h"

#define PFR_GBA_FRAME_SECONDS (280896.0 / 16777216.0)
#define PFR_MAX_FRAME_PACING_DRIFT 4.0
#define PFR_HOST_AUDIO_SAMPLE_RATE 48000
#define PFR_HOST_AUDIO_STREAM_FRAMES 804
#define PFR_HOST_AUDIO_PRIME_SOURCE_FRAMES                                     \
  (((PFR_HOST_AUDIO_STREAM_FRAMES * 2 * PFR_DEFAULT_AUDIO_SAMPLE_RATE) +       \
    PFR_HOST_AUDIO_SAMPLE_RATE - 1) /                                          \
   PFR_HOST_AUDIO_SAMPLE_RATE)

typedef struct PfrOptions
{
  bool headless;
  bool quit_on_title;
  bool quit_on_main_menu;
  uint32_t frame_limit;
  PfrScriptedInput scripted_input;
  const char* save_path;
  PfrMode mode;
} PfrOptions;

static const float sPfrMasterVolume = 0.5f;

static bool
pfr_parse_u32(const char* text, uint32_t* value)
{
  char* end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);

  if (text[0] == '\0' || end == NULL || *end != '\0') {
    return false;
  }

  *value = (uint32_t)parsed;
  return true;
}

static void
pfr_print_usage(const char* program_name)
{
  fprintf(stderr,
          "usage: %s [--mode game|demo|sandbox] [--headless]\n"
          "       [--frames N] [--quit-on-title] [--quit-on-main-menu]\n"
          "       [--auto-press-start-frame N]... [--input-manifest PATH]\n"
          "       [--save-path PATH]\n",
          program_name);
}

static bool
pfr_parse_mode(const char* text, PfrMode* mode)
{
  if (strcmp(text, "game") == 0) {
    *mode = PFR_MODE_GAME;
    return true;
  }

  if (strcmp(text, "demo") == 0) {
    *mode = PFR_MODE_DEMO;
    return true;
  }

  if (strcmp(text, "sandbox") == 0) {
    *mode = PFR_MODE_SANDBOX;
    return true;
  }

  return false;
}

static bool
pfr_parse_options(int argc, char** argv, PfrOptions* options)
{
  int i;

  options->headless = false;
  options->quit_on_title = false;
  options->quit_on_main_menu = false;
  options->frame_limit = 0;
  pfr_scripted_input_init(&options->scripted_input);
  options->save_path = NULL;
  options->mode = PFR_MODE_GAME;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--headless") == 0) {
      options->headless = true;
    } else if (strcmp(argv[i], "--mode") == 0) {
      if (i + 1 >= argc || !pfr_parse_mode(argv[++i], &options->mode)) {
        fprintf(stderr, "invalid --mode value\n");
        return false;
      }
    } else if (strcmp(argv[i], "--quit-on-title") == 0) {
      options->quit_on_title = true;
    } else if (strcmp(argv[i], "--quit-on-main-menu") == 0) {
      options->quit_on_main_menu = true;
    } else if (strcmp(argv[i], "--frames") == 0) {
      if (i + 1 >= argc || !pfr_parse_u32(argv[++i], &options->frame_limit)) {
        fprintf(stderr, "invalid --frames value\n");
        return false;
      }
    } else if (strcmp(argv[i], "--auto-press-start-frame") == 0) {
      uint32_t frame = 0;

      if (i + 1 >= argc || !pfr_parse_u32(argv[++i], &frame)) {
        fprintf(stderr, "invalid --auto-press-start-frame value\n");
        return false;
      }

      if (!pfr_scripted_input_append(
            &options->scripted_input, frame, START_BUTTON, NULL, 0)) {
        fprintf(stderr, "failed to append scripted input\n");
        return false;
      }
    } else if (strcmp(argv[i], "--input-manifest") == 0) {
      char error[256];

      if (i + 1 >= argc) {
        fprintf(stderr, "missing --input-manifest value\n");
        return false;
      }

      if (!pfr_scripted_input_load_manifest(
            &options->scripted_input, argv[++i], error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return false;
      }
    } else if (strcmp(argv[i], "--save-path") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --save-path value\n");
        return false;
      }

      options->save_path = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      pfr_print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      return false;
    }
  }

  return true;
}

static uint16_t
pfr_poll_keys(void)
{
  uint16_t keys = 0;

  if (IsKeyDown(KEY_X)) {
    keys |= A_BUTTON;
  }

  if (IsKeyDown(KEY_C)) {
    keys |= B_BUTTON;
  }

  if (IsKeyDown(KEY_RIGHT_SHIFT)) {
    keys |= SELECT_BUTTON;
  }

  if (IsKeyDown(KEY_ENTER)) {
    keys |= START_BUTTON;
  }

  if (IsKeyDown(KEY_RIGHT)) {
    keys |= DPAD_RIGHT;
  }

  if (IsKeyDown(KEY_LEFT)) {
    keys |= DPAD_LEFT;
  }

  if (IsKeyDown(KEY_UP)) {
    keys |= DPAD_UP;
  }

  if (IsKeyDown(KEY_DOWN)) {
    keys |= DPAD_DOWN;
  }

  if (IsKeyDown(KEY_S)) {
    keys |= L_BUTTON;
  }

  if (IsKeyDown(KEY_D)) {
    keys |= R_BUTTON;
  }

  if (IsGamepadAvailable(0)) {
    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
      keys |= A_BUTTON;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
      keys |= B_BUTTON;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
      keys |= SELECT_BUTTON;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) {
      keys |= START_BUTTON;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
      keys |= DPAD_RIGHT;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
      keys |= DPAD_LEFT;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
      keys |= DPAD_UP;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
      keys |= DPAD_DOWN;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2)) {
      keys |= L_BUTTON;
    }

    if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) {
      keys |= R_BUTTON;
    }
  }

  return keys;
}

static void
pfr_update_audio(AudioStream* stream, bool* stream_started)
{
  int16_t samples[PFR_HOST_AUDIO_STREAM_FRAMES * PFR_AUDIO_CHANNEL_COUNT];

  if (!*stream_started &&
      pfr_audio_available_frames() < PFR_HOST_AUDIO_PRIME_SOURCE_FRAMES) {
    return;
  }

  while (IsAudioStreamProcessed(*stream)) {
    pfr_audio_drain_resampled_frames(samples,
                                     PFR_ARRAY_COUNT(samples) /
                                       PFR_AUDIO_CHANNEL_COUNT,
                                     PFR_HOST_AUDIO_SAMPLE_RATE);
    UpdateAudioStream(
      *stream, samples, PFR_ARRAY_COUNT(samples) / PFR_AUDIO_CHANNEL_COUNT);
  }

  if (!*stream_started) {
    PlayAudioStream(*stream);
    *stream_started = true;
  }
}

static uint16_t
pfr_keys_for_frame(const PfrOptions* options,
                   uint32_t frame_index,
                   uint16_t keys)
{
  return keys | pfr_scripted_input_keys_for_frame(&options->scripted_input,
                                                  frame_index);
}

static int
pfr_fail_requested_target(const char* target, uint32_t frame_count)
{
  fprintf(stderr,
          "did not reach %s within %u frames\n",
          target,
          (unsigned)frame_count);
  return 1;
}

static int
pfr_fail_runtime_quit(void)
{
  const char* status_line = pfr_core_status_line();

  if (status_line[0] != '\0') {
    fprintf(stderr, "%s\n", status_line);
  } else {
    fprintf(stderr,
            "runtime requested quit before reaching the requested state\n");
  }

  return 1;
}

static Rectangle
pfr_compute_target_rect(void)
{
  int window_width = GetScreenWidth();
  int window_height = GetScreenHeight();
  int scale_x = window_width / DISPLAY_WIDTH;
  int scale_y = window_height / DISPLAY_HEIGHT;
  int scale = scale_x < scale_y ? scale_x : scale_y;
  float width;
  float height;
  float x;
  float y;

  if (scale < 1) {
    scale = 1;
  }

  width = (float)(DISPLAY_WIDTH * scale);
  height = (float)(DISPLAY_HEIGHT * scale);
  x = ((float)window_width - width) * 0.5f;
  y = ((float)window_height - height) * 0.5f;

  return (Rectangle){ x, y, width, height };
}

static int
pfr_run_headless(const PfrOptions* options)
{
  uint32_t frames = options->frame_limit == 0 ? 60U : options->frame_limit;
  uint32_t frame_index;

  for (frame_index = 0; frame_index < frames; frame_index++) {
    pfr_core_set_keys(pfr_keys_for_frame(options, frame_index, 0));
    pfr_core_run_frame();

    if (options->quit_on_title && pfr_core_is_title_visible()) {
      return 0;
    }

    if (options->quit_on_main_menu && pfr_core_is_main_menu_visible()) {
      return 0;
    }

    if (pfr_core_should_quit()) {
      return pfr_fail_runtime_quit();
    }
  }

  if (options->quit_on_title) {
    return pfr_fail_requested_target("the title screen", frames);
  }

  if (options->quit_on_main_menu) {
    return pfr_fail_requested_target("the main menu", frames);
  }

  return 0;
}

static void
pfr_free_options(PfrOptions* options)
{
  pfr_scripted_input_free(&options->scripted_input);
}

static int
pfr_run_windowed(const PfrOptions* options)
{
  Image image = {
    .data = (void*)pfr_core_framebuffer(),
    .width = DISPLAY_WIDTH,
    .height = DISPLAY_HEIGHT,
    .mipmaps = 1,
    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
  };
  Texture2D texture;
  AudioStream stream;
  bool stream_started = false;
  uint32_t frame_index = 0;
  double next_frame_time;

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(DISPLAY_WIDTH * 4, DISPLAY_HEIGHT * 4, "pokefirered native PAL");
  SetWindowMinSize(DISPLAY_WIDTH * 2, DISPLAY_HEIGHT * 2);
  SetExitKey(0);

  texture = LoadTextureFromImage(image);
  SetTextureFilter(texture, TEXTURE_FILTER_POINT);

  InitAudioDevice();
  SetAudioStreamBufferSizeDefault(PFR_HOST_AUDIO_STREAM_FRAMES);
  SetMasterVolume(sPfrMasterVolume);
  stream =
    LoadAudioStream(PFR_HOST_AUDIO_SAMPLE_RATE, 16, PFR_AUDIO_CHANNEL_COUNT);
  pfr_audio_reset();
  next_frame_time = GetTime();

  while (!WindowShouldClose()) {
    double now = GetTime();

    if (now < next_frame_time) {
      WaitTime(next_frame_time - now);
    }

    pfr_core_set_keys(
      pfr_keys_for_frame(options, frame_index, pfr_poll_keys()));
    pfr_core_run_frame();
    UpdateTexture(texture, pfr_core_framebuffer());
    pfr_update_audio(&stream, &stream_started);

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(
      texture,
      (Rectangle){ 0.0f, 0.0f, (float)DISPLAY_WIDTH, (float)DISPLAY_HEIGHT },
      pfr_compute_target_rect(),
      (Vector2){ 0.0f, 0.0f },
      0.0f,
      WHITE);
    DrawText(pfr_core_status_line(), 16, 16, 20, RAYWHITE);
    EndDrawing();

    frame_index++;
    next_frame_time += PFR_GBA_FRAME_SECONDS;

    now = GetTime();
    if (now >
        next_frame_time + PFR_GBA_FRAME_SECONDS * PFR_MAX_FRAME_PACING_DRIFT) {
      next_frame_time = now;
    }

    if (options->frame_limit != 0 && frame_index >= options->frame_limit) {
      break;
    }

    if (options->quit_on_title && pfr_core_is_title_visible()) {
      return 0;
    }

    if (options->quit_on_main_menu && pfr_core_is_main_menu_visible()) {
      return 0;
    }

    if (pfr_core_should_quit()) {
      break;
    }
  }

  UnloadAudioStream(stream);
  CloseAudioDevice();
  UnloadTexture(texture);
  CloseWindow();

  if (pfr_core_should_quit()) {
    return pfr_fail_runtime_quit();
  }

  if (options->quit_on_title) {
    return pfr_fail_requested_target("the title screen", frame_index);
  }

  if (options->quit_on_main_menu) {
    return pfr_fail_requested_target("the main menu", frame_index);
  }

  return 0;
}

int
pfr_platform_main(int argc, char** argv)
{
  PfrOptions options;
  int result;

  if (!pfr_parse_options(argc, argv, &options)) {
    pfr_print_usage(argv[0]);
    return 2;
  }

  pfr_core_init(options.save_path, options.mode);
  pfr_audio_reset();

  if (options.headless) {
    result = pfr_run_headless(&options);
  } else {
    result = pfr_run_windowed(&options);
  }

  pfr_audio_shutdown();
  pfr_core_shutdown();
  pfr_free_options(&options);
  return result;
}
