#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class SongTableEntry:
  song_id: int
  symbol: str
  ms: int
  me: int


@dataclass
class DirectSoundEntry:
  label: str
  path: str


@dataclass
class ProgrammableWaveEntry:
  label: str
  path: str


@dataclass
class VoiceEntry:
  kind: str
  type_value: int = 0
  key: int = 60
  length: int = 0
  pan_sweep: int = 0
  ref: str | None = None
  attack: int = 0
  decay: int = 0
  sustain: int = 15
  release: int = 0
  wav_value: int = 0
  subgroup: str | None = None
  keysplit: str | None = None


@dataclass
class KeySplitBlob:
  data: list[int]
  offsets: dict[str, int]


@dataclass
class TrackRelocation:
  offset: int
  target_offset: int


@dataclass
class PendingTrackRelocation:
  offset: int
  target: str


@dataclass
class ParsedTrack:
  label: str
  data: bytearray = field(default_factory=bytearray)
  labels: dict[str, int] = field(default_factory=dict)
  relocations: list[TrackRelocation] = field(default_factory=list)


@dataclass
class ParsedSong:
  entry: SongTableEntry
  priority: int
  reverb: int
  voicegroup: str
  track_count: int
  has_loop: bool
  tracks: list[ParsedTrack]


class AudioAssetTool:
  def __init__(self, repo_root: Path, asset_root: Path):
    self.repo_root = repo_root
    self.asset_root = asset_root
    self.env = self._parse_equ_file(self.repo_root / "sound" / "MPlayDef.s")
    self.voice_groups = self._parse_voice_groups(
      self.repo_root / "sound" / "voice_groups.inc")
    self.voice_group_order = list(self.voice_groups.keys())
    self.voice_group_offsets: dict[str, int] = {}
    total_voice_count = 0
    for name in self.voice_group_order:
      self.voice_group_offsets[name] = total_voice_count
      total_voice_count += len(self.voice_groups[name])
    self.keysplit_blob = self._parse_keysplit_tables(
      self.repo_root / "sound" / "keysplit_tables.inc")
    self.song_entries = self._parse_song_table(
      self.repo_root / "sound" / "song_table.inc")
    self.cry_tables = self._parse_cry_tables(
      self.repo_root / "sound" / "cry_tables.inc")
    self.direct_sound_entries = self._parse_direct_sound_data(
      self.repo_root / "sound" / "direct_sound_data.inc")
    self.direct_sound_by_label = {
      entry.label: entry for entry in self.direct_sound_entries
    }
    self.programmable_waves = self._parse_programmable_wave_data(
      self.repo_root / "sound" / "programmable_wave_data.inc")
    self.programmable_wave_by_label = {
      entry.label: entry for entry in self.programmable_waves
    }
    self._validate_catalog()

  def build(self, out_c: Path, manifest_out: Path | None) -> None:
    songs = [self._assemble_song(entry) for entry in self.song_entries]
    wav_samples = [
      self._load_wav_asset(entry.label, self.asset_root / entry.path)
      for entry in self.direct_sound_entries
    ]
    self._write_source(out_c, songs, wav_samples)
    if manifest_out is not None:
      self._write_manifest(manifest_out, songs)

  def _validate_catalog(self) -> None:
    for table_name, labels in self.cry_tables.items():
      for label in labels:
        if label not in self.direct_sound_by_label:
          raise RuntimeError(f"missing {table_name} cry asset: {label}")

    for group_name, entries in self.voice_groups.items():
      for entry in entries:
        if entry.kind == "directsound":
          if entry.ref not in self.direct_sound_by_label:
            raise RuntimeError(
              f"voicegroup {group_name} references missing wave {entry.ref}")
        elif entry.kind == "programmable_wave":
          if entry.ref not in self.programmable_wave_by_label:
            raise RuntimeError(
              f"voicegroup {group_name} references missing wave {entry.ref}")
        elif entry.kind in {"keysplit", "rhythm"}:
          if entry.subgroup not in self.voice_groups:
            raise RuntimeError(
              f"voicegroup {group_name} references missing subgroup "
              f"{entry.subgroup}")
          if entry.keysplit is not None and \
              entry.keysplit not in self.keysplit_blob.offsets:
            raise RuntimeError(
              f"voicegroup {group_name} references missing keysplit "
              f"{entry.keysplit}")

  def _strip_comment(self, raw_line: str) -> str:
    return raw_line.split("@", 1)[0].strip()

  def _parse_label(self, line: str) -> str | None:
    match = re.fullmatch(r"([A-Za-z0-9_]+)(::?)", line)
    if match is None:
      return None
    return match.group(1)

  def _resolve_alias(self, symbol: str, aliases: dict[str, str]) -> str:
    value = symbol
    seen: set[str] = set()

    while value in aliases:
      if value in seen:
        raise RuntimeError(f"alias cycle detected while resolving {symbol}")
      seen.add(value)
      value = aliases[value]

    return value

  def _parse_equ_file(self, path: Path) -> dict[str, int]:
    env: dict[str, int] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = self._strip_comment(raw_line)
      if not line.startswith(".equ"):
        continue
      name, expr = [part.strip() for part in line[len(".equ"):].split(",", 1)]
      env[name] = self._eval_expr(expr, env)
    return env

  def _parse_song_table(self, path: Path) -> list[SongTableEntry]:
    entries: list[SongTableEntry] = []

    for index, raw_line in enumerate(
        line for line in path.read_text(encoding="utf-8").splitlines()
        if self._strip_comment(line).startswith("song ")):
      line = self._strip_comment(raw_line)
      parts = [part.strip() for part in line[len("song "):].split(",")]
      entries.append(SongTableEntry(index, parts[0], int(parts[1]), int(parts[2])))

    return entries

  def _parse_cry_tables(self, path: Path) -> dict[str, list[str]]:
    tables = {"forward": [], "reverse": []}
    current = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = self._strip_comment(raw_line)
      if not line:
        continue
      if line == "gCryTable::":
        current = "forward"
        continue
      if line == "gCryTable_Reverse::":
        current = "reverse"
        continue
      if current is None:
        continue
      if line.startswith("cry_reverse "):
        tables[current].append(line.split(None, 1)[1].strip())
      elif line.startswith("cry "):
        tables[current].append(line.split(None, 1)[1].strip())

    return tables

  def _parse_direct_sound_data(self, path: Path) -> list[DirectSoundEntry]:
    entries: list[DirectSoundEntry] = []
    current_label = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = self._strip_comment(raw_line)
      if not line:
        continue
      label = self._parse_label(line)
      if label is not None:
        current_label = label
        continue
      if current_label is None or not line.startswith(".incbin"):
        continue
      match = re.fullmatch(r'\.incbin\s+"([^"]+)"', line)
      if match is None:
        continue
      entries.append(DirectSoundEntry(current_label, match.group(1)))
      current_label = None

    return entries

  def _parse_programmable_wave_data(self,
                                    path: Path) -> list[ProgrammableWaveEntry]:
    entries: list[ProgrammableWaveEntry] = []
    current_label = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = self._strip_comment(raw_line)
      if not line:
        continue
      label = self._parse_label(line)
      if label is not None:
        current_label = label
        continue
      if current_label is None or not line.startswith(".incbin"):
        continue
      match = re.fullmatch(r'\.incbin\s+"([^"]+)"', line)
      if match is None:
        continue
      entries.append(ProgrammableWaveEntry(current_label, match.group(1)))
      current_label = None

    return entries

  def _parse_keysplit_tables(self, path: Path) -> KeySplitBlob:
    blocks = []
    current = None
    current_pos = 0
    prefix_padding = 0

    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = self._strip_comment(raw_line)
      if line.startswith(".set "):
        match = re.fullmatch(r"\.set\s+([A-Za-z0-9_]+)\s*,\s*\.\s*-\s*(\d+)",
                             line)
        if match is None:
          continue
        current = {
          "name": match.group(1),
          "subtract": int(match.group(2)),
          "position": current_pos,
          "bytes": [],
        }
        blocks.append(current)
        prefix_padding = max(prefix_padding,
                             current["subtract"] - current["position"])
        continue
      if current is None or not line.startswith(".byte"):
        continue
      values = self._eval_tokens(line[len(".byte"):], {})
      current["bytes"].extend(values)
      current_pos += len(values)

    blob = [0] * max(prefix_padding, 0)
    offsets: dict[str, int] = {}
    required_size = len(blob)

    for block in blocks:
      label_offset = len(blob) - block["subtract"]
      if label_offset < 0:
        raise RuntimeError(f"negative keysplit offset for {block['name']}")
      offsets[block["name"]] = label_offset
      blob.extend(block["bytes"])
      required_size = max(required_size, label_offset + 128)

    if len(blob) < required_size:
      blob.extend([0] * (required_size - len(blob)))

    return KeySplitBlob(blob, offsets)

  def _parse_voice_groups(self, path: Path) -> dict[str, list[VoiceEntry]]:
    groups: dict[str, list[VoiceEntry]] = {}
    current = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = self._strip_comment(raw_line)
      if not line:
        continue
      label = self._parse_label(line)
      if label is not None:
        current = label
        groups[current] = []
        continue
      if current is None or not line.startswith("voice_"):
        continue
      name, args_text = line.split(None, 1)
      args = [arg.strip() for arg in args_text.split(",")]
      groups[current].append(self._parse_voice_entry(name, args))

    return groups

  def _parse_voice_entry(self, name: str, args: list[str]) -> VoiceEntry:
    if name in {"voice_directsound", "voice_directsound_alt",
                "voice_directsound_no_resample"}:
      if name == "voice_directsound_no_resample":
        type_value = 0x08
      elif name == "voice_directsound_alt":
        type_value = 0x10
      else:
        type_value = 0x00
      pan = int(args[1])
      return VoiceEntry("directsound",
                        type_value,
                        int(args[0]),
                        0,
                        0x80 | pan if pan != 0 else 0,
                        args[2],
                        int(args[3]),
                        int(args[4]),
                        int(args[5]),
                        int(args[6]))
    if name in {"voice_square_1", "voice_square_1_alt"}:
      pan = int(args[1])
      return VoiceEntry("square1",
                        0x09 if name.endswith("_alt") else 0x01,
                        int(args[0]),
                        0x80 | pan if pan != 0 else 0,
                        int(args[2]) & 0xFF,
                        None,
                        int(args[4]) & 0x7,
                        int(args[5]) & 0x7,
                        int(args[6]) & 0xF,
                        int(args[7]) & 0x7,
                        int(args[3]) & 0x3)
    if name in {"voice_square_2", "voice_square_2_alt"}:
      pan = int(args[1])
      return VoiceEntry("square2",
                        0x0A if name.endswith("_alt") else 0x02,
                        int(args[0]),
                        0x80 | pan if pan != 0 else 0,
                        0,
                        None,
                        int(args[3]) & 0x7,
                        int(args[4]) & 0x7,
                        int(args[5]) & 0xF,
                        int(args[6]) & 0x7,
                        int(args[2]) & 0x3)
    if name in {"voice_programmable_wave", "voice_programmable_wave_alt"}:
      pan = int(args[1])
      return VoiceEntry("programmable_wave",
                        0x0B if name.endswith("_alt") else 0x03,
                        int(args[0]),
                        0x80 | pan if pan != 0 else 0,
                        0,
                        args[2],
                        int(args[3]) & 0x7,
                        int(args[4]) & 0x7,
                        int(args[5]) & 0xF,
                        int(args[6]) & 0x7)
    if name in {"voice_noise", "voice_noise_alt"}:
      pan = int(args[1])
      return VoiceEntry("noise",
                        0x0C if name.endswith("_alt") else 0x04,
                        int(args[0]),
                        0x80 | pan if pan != 0 else 0,
                        0,
                        None,
                        int(args[3]) & 0x7,
                        int(args[4]) & 0x7,
                        int(args[5]) & 0xF,
                        int(args[6]) & 0x7,
                        int(args[2]) & 0x1)
    if name == "voice_keysplit":
      return VoiceEntry("keysplit", 0x40, 0, 0, 0, None, 0, 0, 0, 0, 0,
                        args[0], args[1])
    if name == "voice_keysplit_all":
      return VoiceEntry("rhythm", 0x80, 0, 0, 0, None, 0, 0, 0, 0, 0,
                        args[0])
    raise RuntimeError(f"unsupported voice macro {name}")

  def _assemble_song(self, entry: SongTableEntry) -> ParsedSong:
    path = self.asset_root / "sound" / f"{entry.symbol}.s"
    source_text = path.read_text(encoding="utf-8")
    env = dict(self.env)
    aliases: dict[str, str] = {}
    tracks: list[ParsedTrack] = []
    pending_relocations: dict[str, list[PendingTrackRelocation]] = {}
    current: ParsedTrack | None = None
    header_bytes: list[int] = []
    header_words: list[str] = []
    header_mode = False

    for raw_line in source_text.splitlines():
      line = self._strip_comment(raw_line)
      if not line:
        continue
      if line.startswith(".equ"):
        name, expr = [part.strip() for part in line[len(".equ"):].split(",", 1)]
        try:
          env[name] = self._eval_expr(expr, env)
        except Exception:
          aliases[name] = expr
        continue

      label = self._parse_label(line)
      if label is not None:
        if label == entry.symbol:
          header_mode = True
          current = None
          continue
        if re.fullmatch(rf"{re.escape(entry.symbol)}_(\d+)", label):
          current = ParsedTrack(label)
          current.labels[label] = 0
          tracks.append(current)
          pending_relocations[label] = []
          continue
        if current is not None:
          current.labels[label] = len(current.data)
        continue

      if header_mode:
        if line.startswith(".byte"):
          header_bytes.extend(self._eval_tokens(line[len(".byte"):], env))
        elif line.startswith(".word") or line.startswith(".4byte"):
          token = line.split(None, 1)[1].strip()
          header_words.append(self._resolve_alias(token, aliases))
        elif line.startswith(".end"):
          break
        continue

      if current is None:
        continue

      if line.startswith(".byte"):
        current.data.extend(self._eval_tokens(line[len(".byte"):], env))
      elif line.startswith(".word") or line.startswith(".4byte"):
        token = line.split(None, 1)[1].strip()
        pending_relocations[current.label].append(
          PendingTrackRelocation(len(current.data), token))
        current.data.extend(b"\x00\x00\x00\x00")

    track_by_label = {track.label: track for track in tracks}
    for track in tracks:
      for pending in pending_relocations[track.label]:
        target = self._resolve_alias(pending.target, aliases)
        if target not in track.labels:
          raise RuntimeError(f"unsupported cross-track relocation {target} "
                             f"in {entry.symbol}")
        track.relocations.append(
          TrackRelocation(pending.offset, track.labels[target]))

    if len(header_bytes) < 4 or not header_words:
      raise RuntimeError(f"invalid song header for {entry.symbol}")

    track_count = header_bytes[0]
    selected_tracks = []
    for label_name in header_words[1:1 + track_count]:
      if label_name not in track_by_label:
        raise RuntimeError(f"missing track {label_name} in {entry.symbol}")
      selected_tracks.append(track_by_label[label_name])

    voicegroup = self._resolve_alias(header_words[0], aliases)
    has_loop = any(0xB2 in track.data for track in selected_tracks)
    return ParsedSong(entry=entry,
                      priority=header_bytes[2],
                      reverb=header_bytes[3],
                      voicegroup=voicegroup,
                      track_count=track_count,
                      has_loop=has_loop,
                      tracks=selected_tracks)

  def _load_wav_asset(self, name: str, path: Path) -> dict:
    flags_word, pitch_word, loop_start, sample_count, data = \
      self._load_wav_bin(path)
    return {
      "name": name,
      "flags_word": flags_word,
      "pitch_word": pitch_word,
      "loop_start": loop_start,
      "sample_count": sample_count,
      "data": data,
    }

  def _load_wav_bin(self, path: Path) -> tuple[int, int, int, int, list[int]]:
    blob = path.read_bytes()
    if len(blob) < 16:
      raise RuntimeError(f"wav asset too small: {path}")

    flags_word = int.from_bytes(blob[0:4], "little")
    pitch_word = int.from_bytes(blob[4:8], "little")
    loop_start = int.from_bytes(blob[8:12], "little")
    sample_count = int.from_bytes(blob[12:16], "little")
    samples: list[int] = []
    for value in blob[16:]:
      if value > 127:
        value -= 256
      samples.append(value)

    return flags_word, pitch_word, loop_start, sample_count, samples

  def _load_programmable_wave(self, entry: ProgrammableWaveEntry) -> list[int]:
    return list((self.repo_root / entry.path).read_bytes())

  def _eval_tokens(self, text: str, env: dict[str, int]) -> list[int]:
    output = []
    for token in [part.strip() for part in text.split(",") if part.strip()]:
      output.append(self._eval_expr(token, env) & 0xFF)
    return output

  def _eval_expr(self, expr: str, env: dict[str, int]) -> int:
    return int(eval(expr.strip(), {"__builtins__": {}}, env))

  def _emit_int8_array(self, name: str, values: list[int]) -> list[str]:
    lines = [f"static const s8 {name}[] = {{"]
    for index in range(0, len(values), 24):
      row = ", ".join(str(value) for value in values[index:index + 24])
      lines.append(f"  {row},")
    lines.append("};")
    return lines

  def _emit_uint8_array(self,
                        name: str,
                        values: list[int],
                        storage: str = "static const") -> list[str]:
    lines = [f"{storage} u8 {name}[] = {{"]
    for index in range(0, len(values), 24):
      row = ", ".join(f"0x{value:02X}" for value in values[index:index + 24])
      lines.append(f"  {row},")
    lines.append("};")
    return lines

  def _fnv1a32(self, values: bytes) -> int:
    digest = 2166136261
    for value in values:
      digest ^= value
      digest = (digest * 16777619) & 0xFFFFFFFF
    return digest

  def _write_source(self,
                    out_c: Path,
                    songs: list[ParsedSong],
                    wav_samples: list[dict]) -> None:
    lines = ['#include "pfr/audio_assets.h"', "#include <stdint.h>", ""]

    for sample in wav_samples:
      flags_word = sample["flags_word"]
      pitch_word = sample["pitch_word"]
      data = sample["data"]
      sample_type = flags_word & 0xFFFF
      sample_status = (flags_word >> 16) & 0xFFFF
      loop_start = sample["loop_start"]
      sample_count = sample["sample_count"]
      lines.append(
        f"static const struct {{ u16 type; u16 status; u32 freq; "
        f"u32 loopStart; u32 size; s8 data[{len(data)}]; }} "
        f"sPfrWave_{sample['name']} = {{ {sample_type}, "
        f"0x{sample_status:04X}u, {pitch_word}u, {loop_start}u, "
        f"{sample_count}u, {{")
      for index in range(0, len(data), 24):
        row = ", ".join(str(value) for value in data[index:index + 24])
        lines.append(f"  {row},")
      lines += ["}};", ""]

    for entry in self.programmable_waves:
      data = self._load_programmable_wave(entry)
      lines += self._emit_uint8_array(f"sPfrPwaveData_{entry.label}", data)
      lines.append("")

    lines += self._emit_uint8_array("gPfrKeysplitBlob",
                                    self.keysplit_blob.data,
                                    "const")
    lines.append("")

    lines.append("static const struct ToneData sPfrVoiceData[] = {")
    for group in self.voice_group_order:
      lines.append(f"  /* {group} */")
      for entry in self.voice_groups[group]:
        if entry.kind == "directsound":
          wav_ref = f"(struct WaveData *)&sPfrWave_{entry.ref}"
          attack = entry.attack
          decay = entry.decay
          sustain = entry.sustain
          release = entry.release
        elif entry.kind == "programmable_wave":
          wav_ref = f"(struct WaveData *)sPfrPwaveData_{entry.ref}"
          attack = entry.attack
          decay = entry.decay
          sustain = entry.sustain
          release = entry.release
        elif entry.kind in {"square1", "square2", "noise"}:
          wav_ref = f"(struct WaveData *)(uintptr_t){entry.wav_value}"
          attack = entry.attack
          decay = entry.decay
          sustain = entry.sustain
          release = entry.release
        elif entry.kind == "keysplit":
          keysplit_offset = self.keysplit_blob.offsets[entry.keysplit]
          wav_ref = (
            f"(struct WaveData *)&sPfrVoiceData["
            f"{self.voice_group_offsets[entry.subgroup]}]")
          attack = keysplit_offset & 0xFF
          decay = (keysplit_offset >> 8) & 0xFF
          sustain = (keysplit_offset >> 16) & 0xFF
          release = (keysplit_offset >> 24) & 0xFF
        else:
          wav_ref = (
            f"(struct WaveData *)&sPfrVoiceData["
            f"{self.voice_group_offsets[entry.subgroup]}]")
          attack = 0
          decay = 0
          sustain = 0
          release = 0
        lines.append(
          f"  {{ {entry.type_value}, {entry.key}, {entry.length}, "
          f"{entry.pan_sweep}, {wav_ref}, {attack}, {decay}, {sustain}, "
          f"{release} }},")
    lines += ["};", ""]

    for song in songs:
      symbol = song.entry.symbol
      for index, track in enumerate(song.tracks):
        lines += self._emit_int8_array(f"sPfrTrackData_{symbol}_{index}",
                                       list(track.data))
        if track.relocations:
          lines.append(
            f"static const PfrAudioTrackRelocation "
            f"sPfrTrackRelocs_{symbol}_{index}[] = {{")
          for reloc in track.relocations:
            lines.append(
              f"  {{ {reloc.offset}u, {reloc.target_offset}u }},")
          lines += ["};", ""]

      if song.tracks:
        lines.append(
          f"static const PfrAudioTrackAsset sPfrTrackAssets_{symbol}[] = {{")
        for index, track in enumerate(song.tracks):
          reloc_ptr = (f"sPfrTrackRelocs_{symbol}_{index}"
                       if track.relocations else "NULL")
          lines.append(
            f"  {{ (const u8*)sPfrTrackData_{symbol}_{index}, "
            f"{len(track.data)}u, {reloc_ptr}, "
            f"{len(track.relocations)}u }},")
        lines += ["};", ""]

      part_capacity = max(len(song.tracks), 1)
      lines.append(
        f"static struct {{ u8 trackCount; u8 blockCount; u8 priority; "
        f"u8 reverb; struct ToneData* tone; u8* part[{part_capacity}]; }} "
        f"sPfrSongHeader_{symbol} = {{")
      lines.append(
        f"  {song.track_count}, 0, {song.priority}, {song.reverb}, "
        f"(struct ToneData *)&sPfrVoiceData["
        f"{self.voice_group_offsets[song.voicegroup]}], {{")
      if song.tracks:
        for index in range(len(song.tracks)):
          lines.append(f"    (u8*)sPfrTrackData_{symbol}_{index},")
      else:
        lines.append("    NULL,")
      lines += ["  }", "};", ""]

    lines.append("const struct Song gSongTable[] = {")
    for song in songs:
      lines.append(
        f"  {{ (struct SongHeader*)&sPfrSongHeader_{song.entry.symbol}, "
        f"{song.entry.ms}, {song.entry.me} }},")
    lines += ["};", ""]

    lines.append("const PfrAudioSongAsset gPfrAudioSongAssets[] = {")
    for song in songs:
      track_ptr = (f"sPfrTrackAssets_{song.entry.symbol}"
                   if song.tracks else "NULL")
      lines.append(f"  {{ {track_ptr} }},")
    lines.append("};")
    lines.append(f"const u32 gPfrAudioSongAssetCount = {len(songs)}u;")
    lines.append("")

    lines.append("const struct ToneData gCryTable[] = {")
    for wave in self.cry_tables["forward"]:
      lines.append(
        f"  {{ 0x20, 60, 0, 0, (struct WaveData*)&sPfrWave_{wave}, "
        f"0xFF, 0, 0xFF, 0 }},")
    lines.append("};")
    lines.append("")
    lines.append("const struct ToneData gCryTable_Reverse[] = {")
    for wave in self.cry_tables["reverse"]:
      lines.append(
        f"  {{ 0x30, 60, 0, 0, (struct WaveData*)&sPfrWave_{wave}, "
        f"0xFF, 0, 0xFF, 0 }},")
    lines.append("};")

    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_c.write_text("\n".join(lines) + "\n", encoding="utf-8")

  def _write_manifest(self, manifest_out: Path, songs: list[ParsedSong]) -> None:
    payload = {
      "song_count": len(songs),
      "direct_sound_count": len(self.direct_sound_entries),
      "programmable_wave_count": len(self.programmable_waves),
      "voice_group_count": len(self.voice_group_order),
      "songs": [{
        "id": song.entry.song_id,
        "symbol": song.entry.symbol,
        "ms": song.entry.ms,
        "me": song.entry.me,
        "source": f"sound/{song.entry.symbol}.s",
        "voicegroup": song.voicegroup,
        "track_count": song.track_count,
        "priority": song.priority,
        "reverb": song.reverb,
        "has_loop_goto": song.has_loop,
        "track_lengths": [len(track.data) for track in song.tracks],
        "track_relocation_counts": [
          len(track.relocations) for track in song.tracks
        ],
        "track_hashes": [
          f"{self._fnv1a32(bytes(track.data)):08x}" for track in song.tracks
        ],
      } for song in songs],
      "direct_sounds": [{
        "label": entry.label,
        "path": entry.path,
        "kind": "cry" if "/cries/" in entry.path.replace("\\", "/")
        else "sample",
      } for entry in self.direct_sound_entries],
      "programmable_waves": [{
        "label": entry.label,
        "path": entry.path,
      } for entry in self.programmable_waves],
      "voice_groups": [{
        "name": name,
        "offset": self.voice_group_offsets[name],
        "count": len(self.voice_groups[name]),
      } for name in self.voice_group_order],
      "keysplit_tables": [{
        "name": name,
        "offset": offset,
      } for name, offset in sorted(self.keysplit_blob.offsets.items())],
      "cry_tables": self.cry_tables,
    }

    manifest_out.parent.mkdir(parents=True, exist_ok=True)
    manifest_out.write_text(
      json.dumps(payload, indent=2, sort_keys=False) + "\n",
      encoding="utf-8")


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--repo-root", required=True)
  parser.add_argument("--asset-root", required=True)
  parser.add_argument("--out-c", required=True)
  parser.add_argument("--manifest-out")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  manifest_out = Path(args.manifest_out) if args.manifest_out else None
  AudioAssetTool(Path(args.repo_root), Path(args.asset_root)).build(
    Path(args.out_c), manifest_out)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
