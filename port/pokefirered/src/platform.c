#include "gba/defines.h"
#include "gba/io_reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

#include "pfr/audio.h"
#include "pfr/core.h"

typedef struct PfrOptions
{
  bool headless;
  bool quit_on_title;
  bool quit_on_main_menu;
  uint32_t frame_limit;
  uint32_t auto_press_start_frame;
  bool auto_press_start;
  const char* save_path;
  bool boot_demo;
  bool boot_sandbox;
} PfrOptions;

static const float sPfrMasterVolume = 0.0f;

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

static bool
pfr_parse_options(int argc, char** argv, PfrOptions* options)
{
  int i;

  options->headless = false;
  options->quit_on_title = false;
  options->quit_on_main_menu = false;
  options->frame_limit = 0;
  options->auto_press_start_frame = 0;
  options->auto_press_start = false;
  options->save_path = NULL;
  options->boot_demo = false;
  options->boot_sandbox = false;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--headless") == 0) {
      options->headless = true;
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
      if (i + 1 >= argc ||
          !pfr_parse_u32(argv[++i], &options->auto_press_start_frame)) {
        fprintf(stderr, "invalid --auto-press-start-frame value\n");
        return false;
      }

      options->auto_press_start = true;
    } else if (strcmp(argv[i], "--save-path") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --save-path value\n");
        return false;
      }

      options->save_path = argv[++i];
    } else if (strcmp(argv[i], "--demo") == 0) {
      options->boot_demo = true;
    } else if (strcmp(argv[i], "--sandbox") == 0) {
      options->boot_sandbox = true;
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
pfr_update_audio(AudioStream* stream, PfrAudioState* audio_state)
{
  int16_t samples[512];

  while (IsAudioStreamProcessed(*stream)) {
    pfr_audio_generate(audio_state,
                       samples,
                       PFR_ARRAY_COUNT(samples),
                       gPfrRuntimeState.keys_held,
                       gPfrRuntimeState.frame_counter);
    UpdateAudioStream(*stream, samples, PFR_ARRAY_COUNT(samples));
  }
}

static uint16_t
pfr_keys_for_frame(const PfrOptions* options,
                   uint32_t frame_index,
                   uint16_t keys)
{
  if (options->auto_press_start &&
      frame_index == options->auto_press_start_frame) {
    keys |= START_BUTTON;
  }

  return keys;
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
  PfrAudioState audio_state;
  uint32_t frames = options->frame_limit == 0 ? 60U : options->frame_limit;
  uint32_t frame_index;
  int16_t samples[256];

  pfr_audio_reset(&audio_state);

  for (frame_index = 0; frame_index < frames; frame_index++) {
    pfr_core_set_keys(pfr_keys_for_frame(options, frame_index, 0));
    pfr_core_run_frame();
    pfr_audio_generate(&audio_state,
                       samples,
                       PFR_ARRAY_COUNT(samples),
                       gPfrRuntimeState.keys_held,
                       gPfrRuntimeState.frame_counter);

    if (options->quit_on_title && pfr_core_is_title_visible()) {
      break;
    }

    if (options->quit_on_main_menu && pfr_core_is_main_menu_visible()) {
      break;
    }

    if (pfr_core_should_quit()) {
      break;
    }
  }

  return 0;
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
  PfrAudioState audio_state;
  uint32_t frame_index = 0;

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
  InitWindow(DISPLAY_WIDTH * 4, DISPLAY_HEIGHT * 4, "pokefirered native PAL");
  SetWindowMinSize(DISPLAY_WIDTH * 2, DISPLAY_HEIGHT * 2);
  SetExitKey(0);

  texture = LoadTextureFromImage(image);
  SetTextureFilter(texture, TEXTURE_FILTER_POINT);

  InitAudioDevice();
  SetMasterVolume(sPfrMasterVolume);
  stream = LoadAudioStream(PFR_DEFAULT_AUDIO_SAMPLE_RATE, 16, 1);
  pfr_audio_reset(&audio_state);
  PlayAudioStream(stream);

  while (!WindowShouldClose()) {
    pfr_core_set_keys(
      pfr_keys_for_frame(options, frame_index, pfr_poll_keys()));
    pfr_core_run_frame();
    UpdateTexture(texture, pfr_core_framebuffer());
    pfr_update_audio(&stream, &audio_state);

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

    if (options->frame_limit != 0 && frame_index >= options->frame_limit) {
      break;
    }

    if (options->quit_on_title && pfr_core_is_title_visible()) {
      break;
    }

    if (options->quit_on_main_menu && pfr_core_is_main_menu_visible()) {
      break;
    }

    if (pfr_core_should_quit()) {
      break;
    }
  }

  UnloadAudioStream(stream);
  CloseAudioDevice();
  UnloadTexture(texture);
  CloseWindow();

  return 0;
}

int
pfr_platform_main(int argc, char** argv)
{
  PfrOptions options;

  if (!pfr_parse_options(argc, argv, &options)) {
    return 2;
  }

  if (options.boot_demo) {
    pfr_core_init(options.save_path, PFR_BOOT_DEMO);
  } else if (options.boot_sandbox) {
    pfr_core_init(options.save_path, PFR_BOOT_SANDBOX);
  } else {
    pfr_core_init(options.save_path, PFR_BOOT_FRONTEND);
  }

  if (options.headless) {
    pfr_run_headless(&options);
  } else {
    pfr_run_windowed(&options);
  }

  pfr_core_shutdown();
  return 0;
}
