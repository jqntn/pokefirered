# Native Audio Audit

This document records the original audio build contract and the matching native
CMake behavior.

## Original Build Truth

| Original source | Original behavior | Native mapping |
| --- | --- | --- |
| [`audio_rules.mk`](/c:/Users/jqntn/pokefirered/audio_rules.mk) | Existing `sound/songs/midi/*.mid` files are converted to `sound/songs/midi/*.s` through `mid2agb`, with per-file flags read from `sound/songs/midi/midi.cfg`. | [`generated_sources.cmake`](/c:/Users/jqntn/pokefirered/port/pokefirered/cmake/generated_sources.cmake) now enumerates every real `.mid` source and runs [`run_mid2agb_from_cfg.cmake`](/c:/Users/jqntn/pokefirered/port/pokefirered/cmake/run_mid2agb_from_cfg.cmake) for each one. |
| [`audio_rules.mk`](/c:/Users/jqntn/pokefirered/audio_rules.mk) | Non-cry WAVs use `wav2agb -b`. Cry WAVs use `wav2agb -b -c -l 1 --no-pad`. | [`native_asset_helpers.cmake`](/c:/Users/jqntn/pokefirered/port/pokefirered/cmake/native_asset_helpers.cmake) always invokes `wav2agb -b`; [`generated_sources.cmake`](/c:/Users/jqntn/pokefirered/port/pokefirered/cmake/generated_sources.cmake) adds the cry-specific `-c -l 1 --no-pad` flags for `sound/direct_sound_samples/cries/*.wav`. |
| [`data/sound_data.s`](/c:/Users/jqntn/pokefirered/data/sound_data.s) | Audio rodata include order is `voice_groups`, `keysplit_tables`, `programmable_wave_data`, `music_player_table`, `song_table`, `direct_sound_data`. | [`audio_asset_tool.py`](/c:/Users/jqntn/pokefirered/port/pokefirered/tools/audio_asset_tool.py) imports the same source files and emits the native catalog in that same semantic order. |
| [`sound/song_table.inc`](/c:/Users/jqntn/pokefirered/sound/song_table.inc) | `gSongTable` order is the authoritative song-id order. | The generated native `gSongTable` and `gPfrAudioSongAssets` are emitted one-to-one in original `song_table.inc` order. |
| [`sound/music_player_table.inc`](/c:/Users/jqntn/pokefirered/sound/music_player_table.inc) | `gMPlayTable` defines the original player topology: BGM `(10,0)`, SE1 `(3,1)`, SE2 `(9,1)`, SE3 `(1,0)`. | [`m4a_1_host.c`](/c:/Users/jqntn/pokefirered/port/pokefirered/src/m4a_1_host.c) keeps those exact values. [`integration.c`](/c:/Users/jqntn/pokefirered/port/pokefirered/tests/integration.c) asserts representative routing via `gSongTable`. |
| [`sound/direct_sound_data.inc`](/c:/Users/jqntn/pokefirered/sound/direct_sound_data.inc) | Full direct-sound sample list and order, including cries, are part of the asset contract. | The native importer consumes every `.bin` listed in `direct_sound_data.inc` and emits a full 477-entry wave catalog. |
| [`sound/programmable_wave_data.inc`](/c:/Users/jqntn/pokefirered/sound/programmable_wave_data.inc) | Full programmable-wave include list and order are part of the asset contract. | The native importer consumes all 11 `.pcm` programmable waves and emits them in original order. |
| [`sound/voice_groups.inc`](/c:/Users/jqntn/pokefirered/sound/voice_groups.inc) and [`sound/keysplit_tables.inc`](/c:/Users/jqntn/pokefirered/sound/keysplit_tables.inc) | Voice layout, keysplit offsets, subgroup routing, and sample references are original data. | The native importer emits the full voice catalog and full keysplit blob from those originals, without dependency pruning. |
| [`sound/cry_tables.inc`](/c:/Users/jqntn/pokefirered/sound/cry_tables.inc) | Forward and reverse cry tables reference the direct-sound cry assets by original label order. | The native importer emits both cry tables directly from `cry_tables.inc`, referencing the imported cry wave data. |
| [`ld_script.ld`](/c:/Users/jqntn/pokefirered/ld_script.ld#L618) | `song_data` explicitly links the song objects in original song-id order. | CMake now generates every song `.s`; the importer rebuilds `gSongTable` in that same order and tests enforce the mapping. |

## Tool Applicability

| Tool | Status | Reason |
| --- | --- | --- |
| `mid2agb` | Required | This is the original MIDI compiler and remains the only source for generated song assembly. |
| `wav2agb` | Required | This is the original WAV converter and remains the only source for generated sample bins. |
| Native Python importer | Required | Host-only bridge that consumes original generated artifacts and emits C data for the desktop runtime. It does not replace original conversion tools. |
| Non-audio asset tools | Not in the audio pipeline | They stay out of the audio graph unless the audio build specifically depends on them. |

## Native Acceptance Gates

1. Every song in [`sound/song_table.inc`](/c:/Users/jqntn/pokefirered/sound/song_table.inc) has a generated `assetroot/sound/<symbol>.s`.
2. Every sample in [`sound/direct_sound_data.inc`](/c:/Users/jqntn/pokefirered/sound/direct_sound_data.inc) has a generated `assetroot/<path>.bin`, with cry bins preserving the cry conversion class.
3. The generated native manifest matches original song order, player assignment, header fields, track byte hashes, relocation counts, voicegroup selection, programmable-wave order, and cry-table order.
4. Runtime smoke and startup capture tests still pass against the rebuilt full catalog.
