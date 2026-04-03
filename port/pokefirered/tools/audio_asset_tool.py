#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

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


class AudioAssetTool:
  def __init__(self, repo_root: Path, asset_root: Path):
    self.repo_root = repo_root
    self.asset_root = asset_root
    self.env = self._parse_equ_file(self.repo_root / "sound" / "MPlayDef.s")
    self.voice_groups = self._parse_voice_groups(
      self.repo_root / "sound" / "voice_groups.inc")
    self.keysplit_tables = self._parse_keysplit_tables(
      self.repo_root / "sound" / "keysplit_tables.inc")
    self.song_players = self._parse_song_table(
      self.repo_root / "sound" / "song_table.inc")
    self.cry_tables = self._parse_cry_tables(
      self.repo_root / "sound" / "cry_tables.inc")

  def build(self, out_c: Path, asset_args: list[str]) -> None:
    song_specs = []
    for arg in asset_args:
      stem = Path(arg).stem
      if stem in self.song_players:
        song_specs.append(self._parse_song_arg(arg))
    songs = [self._assemble_song(spec) for spec in song_specs]
    used_groups, used_tables, used_samples, used_pwaves = self._collect_voice_deps(songs)
    wav_samples = []
    for name in used_samples:
      wav_samples.append(self._load_wav_asset(name, self._wave_name_to_path(name)))
    cry_waves, cry_tables = self._collect_cry_assets()
    for cry in cry_waves.values():
      wav_samples.append(self._load_wav_asset(cry["wave_name"], cry["path"]))
    self._write_source(
      out_c, songs, wav_samples, used_groups, used_tables, used_pwaves,
      cry_tables)

  def _parse_song_arg(self, arg: str) -> dict:
    symbol = Path(arg).stem
    song_id, ms, me = self.song_players[symbol]
    return {"id": song_id, "symbol": symbol, "source": arg,
            "player_ids": (ms, me)}

  def _parse_equ_file(self, path: Path) -> dict[str, int]:
    env: dict[str, int] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
      if not line.startswith(".equ"):
        continue
      name, expr = [part.strip() for part in line[len(".equ"):].split(",", 1)]
      env[name] = self._eval_expr(expr, env)
    return env

  def _parse_song_table(self, path: Path) -> dict[str, tuple[int, int, int]]:
    mapping: dict[str, tuple[int, int, int]] = {}
    index = 0
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
      if not line.startswith("song "):
        continue
      parts = [part.strip() for part in line[len("song "):].split(",")]
      mapping[parts[0]] = (index, int(parts[1]), int(parts[2]))
      index += 1
    return mapping

  def _parse_cry_tables(self, path: Path) -> dict[str, list[str]]:
    tables = {"forward": [], "reverse": []}
    current = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
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

  def _normalize_cry_name(self, name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())

  def _cry_wave_symbol(self, stem: str) -> str:
    return "gPfrCryWave_" + re.sub(r"[^0-9A-Za-z_]", "_", stem)

  def _collect_cry_assets(self) -> tuple[dict[str, dict], dict[str, list[str]]]:
    cry_dir = self.asset_root / "sound" / "direct_sound_samples" / "cries"
    by_name: dict[str, dict] = {}
    tables: dict[str, list[str]] = {"forward": [], "reverse": []}

    for path in sorted(cry_dir.glob("*.s")):
      stem = path.stem
      key = self._normalize_cry_name(stem)
      if key in by_name:
        raise RuntimeError(f"duplicate cry asset for {stem}")
      by_name[key] = {
        "path": path,
        "wave_name": self._cry_wave_symbol(stem),
      }

    for table_name, labels in self.cry_tables.items():
      for label in labels:
        key = self._normalize_cry_name(label.removeprefix("Cry_"))
        if key not in by_name:
          raise RuntimeError(f"missing cry asset for {label}")
        tables[table_name].append(by_name[key]["wave_name"])

    return by_name, tables

  def _parse_keysplit_tables(self, path: Path) -> dict[str, list[int]]:
    tables: dict[str, dict[int, int]] = {}
    current = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.strip()
      if line.startswith(".set "):
        current = line.split(",", 1)[0].split()[1]
        tables[current] = {}
        continue
      if current is None or not line.startswith(".byte"):
        continue
      match = re.search(r"@\s*(\d+)", raw_line)
      if match is None:
        continue
      value_text = line[len(".byte"):].split("@", 1)[0].strip()
      tables[current][int(match.group(1))] = int(value_text)

    output: dict[str, list[int]] = {}
    for name, table in tables.items():
      values = []
      for note in range(128):
        if note in table:
          values.append(table[note])
        else:
          nearest = min(table, key=lambda candidate: abs(candidate - note))
          values.append(table[nearest])
      output[name] = values
    return output

  def _parse_voice_groups(self, path: Path) -> dict[str, list[VoiceEntry]]:
    groups: dict[str, list[VoiceEntry]] = {}
    current = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
      if not line:
        continue
      if line.endswith("::"):
        current = line[:-2]
        groups[current] = []
        continue
      if current is None or not line.startswith("voice_"):
        continue
      name, args_text = line.split(None, 1)
      args = [arg.strip() for arg in args_text.split(",")]
      groups[current].append(self._parse_voice_entry(name, args))
    return groups

  def _parse_voice_entry(self, name: str, args: list[str]) -> VoiceEntry:
    if name in {"voice_directsound", "voice_directsound_alt", "voice_directsound_no_resample"}:
      if name == "voice_directsound_no_resample":
        type_value = 0x08
      elif name == "voice_directsound_alt":
        type_value = 0x10
      else:
        type_value = 0x00
      pan = int(args[1])
      return VoiceEntry("directsound", type_value, int(args[0]), 0,
                        0x80 | pan if pan != 0 else 0, args[2], int(args[3]),
                        int(args[4]), int(args[5]), int(args[6]))
    if name in {"voice_square_1", "voice_square_1_alt"}:
      length = int(args[1])
      return VoiceEntry("square1", 0x09 if name.endswith("_alt") else 0x01,
                        int(args[0]), 0x80 | length if length != 0 else 0,
                        int(args[2]), None, int(args[4]), int(args[5]),
                        int(args[6]), int(args[7]), int(args[3]))
    if name in {"voice_square_2", "voice_square_2_alt"}:
      length = int(args[1])
      return VoiceEntry("square2", 0x0A if name.endswith("_alt") else 0x02,
                        int(args[0]), 0x80 | length if length != 0 else 0, 0,
                        None, int(args[3]), int(args[4]), int(args[5]),
                        int(args[6]), int(args[2]))
    if name in {"voice_programmable_wave", "voice_programmable_wave_alt"}:
      length = int(args[1])
      return VoiceEntry("programmable_wave", 0x0B if name.endswith("_alt") else 0x03,
                        int(args[0]), 0x80 | length if length != 0 else 0, 0,
                        args[2], int(args[3]), int(args[4]), int(args[5]),
                        int(args[6]))
    if name in {"voice_noise", "voice_noise_alt"}:
      length = int(args[1])
      return VoiceEntry("noise", 0x0C if name.endswith("_alt") else 0x04,
                        int(args[0]), 0x80 | length if length != 0 else 0, 0,
                        None, int(args[3]), int(args[4]), int(args[5]),
                        int(args[6]), int(args[2]))
    if name == "voice_keysplit":
      return VoiceEntry("keysplit", 0x40, 0, 0, 0, None, 0, 0, 0, 0, 0,
                        args[0], args[1])
    if name == "voice_keysplit_all":
      return VoiceEntry("rhythm", 0x80, 0, 0, 0, None, 0, 0, 0, 0, 0,
                        args[0])
    raise RuntimeError(f"unsupported voice macro {name}")

  def _assemble_song(self, spec: dict) -> dict:
    path = self.asset_root / spec["source"]
    env = dict(self.env)
    aliases: dict[str, str] = {}
    tracks = []
    current = None
    header_bytes: list[int] = []
    header_words: list[str] = []
    header_mode = False

    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
      if not line:
        continue
      if line.startswith(".equ"):
        name, expr = [part.strip() for part in line[len(".equ"):].split(",", 1)]
        try:
          env[name] = self._eval_expr(expr, env)
        except Exception:
          aliases[name] = expr
        continue
      if line.endswith(":"):
        label = line[:-1]
        if label == spec["symbol"]:
          header_mode = True
          current = None
          continue
        match = re.fullmatch(rf"{re.escape(spec['symbol'])}_(\d+)", label)
        if match:
          current = {"label": label, "bytes": bytearray(), "labels": {label: 0}, "relocs": []}
          tracks.append(current)
          continue
        if current is not None:
          current["labels"][label] = len(current["bytes"])
        continue
      if header_mode:
        if line.startswith(".byte"):
          header_bytes.extend(self._eval_tokens(line[len(".byte"):], env))
        elif line.startswith(".word"):
          header_words.append(line[len(".word"):].strip())
        elif line.startswith(".end"):
          break
        continue
      if current is None:
        continue
      if line.startswith(".byte"):
        current["bytes"].extend(self._eval_tokens(line[len(".byte"):], env))
      elif line.startswith(".word"):
        current["relocs"].append({"offset": len(current["bytes"]), "target": line[len(".word"):].strip()})
        current["bytes"].extend(b"\x00\x00\x00\x00")

    label_map = {track["label"]: track for track in tracks}
    for track in tracks:
      for reloc in track["relocs"]:
        target = reloc["target"]
        if target not in track["labels"]:
          raise RuntimeError(f"unsupported cross-track relocation {target} in {spec['symbol']}")
        reloc["target_offset"] = track["labels"][target]

    track_count = header_bytes[0]
    selected_tracks = [label_map[word] for word in header_words[1:1 + track_count]]
    has_goto = any(0xb2 in track["bytes"] for track in selected_tracks)
    return {
      "spec": spec,
      "priority": header_bytes[2],
      "reverb": header_bytes[3],
      "voicegroup": aliases.get(header_words[0], header_words[0]),
      "tracks": selected_tracks,
      "player_ids": spec["player_ids"],
      "loop": has_goto,
    }

  def _collect_voice_deps(self, songs: list[dict]) -> tuple[list[str], list[str], list[str], list[str]]:
    groups = []
    seen = set()
    tables = set()
    samples = set()
    pwaves = set()

    def visit(group: str) -> None:
      if group in seen:
        return
      seen.add(group)
      for entry in self.voice_groups[group]:
        if entry.kind in {"keysplit", "rhythm"}:
          visit(entry.subgroup)
          if entry.keysplit is not None:
            tables.add(entry.keysplit)
        elif entry.kind.startswith("directsound"):
          samples.add(entry.ref)
        elif entry.kind == "programmable_wave":
          pwaves.add(entry.ref)
      groups.append(group)

    for song in songs:
      visit(song["voicegroup"])

    return groups, sorted(tables), sorted(samples), sorted(pwaves)

  def _load_wav_asset(self, name: str, path: Path) -> dict:
    format_type, sample_rate, loop_start, sample_count, loop_enabled, data = \
      self._load_wav_asm(path)
    return {
      "name": name,
      "type": format_type,
      "rate": sample_rate,
      "loop_start": loop_start,
      "sample_count": sample_count,
      "loop_enabled": loop_enabled,
      "data": data,
    }

  def _load_wav_asm(self, path: Path) -> tuple[int, int, int, int, bool, list[int]]:
    header_bytes: list[int] = []
    words: list[int] = []
    samples: list[int] = []
    header_done = False
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
      if not line.startswith(".byte") and not line.startswith(".word"):
        continue
      if line.startswith(".word"):
        for token in line[len(".word"):].split(","):
          token = token.strip()
          if token:
            words.append(int(token, 0))
        continue
      if not header_done:
        header_done = True
        for token in line[len(".byte"):].split(","):
          token = token.strip()
          if token:
            header_bytes.append(int(token, 0))
        continue
      for token in line[len(".byte"):].split(","):
        token = token.strip()
        if token:
          value = int(token, 0)
          if value > 127:
            value -= 256
          samples.append(value)
    format_type = header_bytes[0] if header_bytes else 0
    sample_rate = words[0] // 1024 if words else 0
    loop_start = words[1] if len(words) > 1 else 0
    sample_count = words[2] if len(words) > 2 else len(samples)
    loop_enabled = len(header_bytes) > 3 and (header_bytes[3] & 0x40) != 0
    return format_type, sample_rate, loop_start, sample_count, loop_enabled, samples

  def _load_programmable_wave(self, name: str) -> list[int]:
    sample_number = int(name.split("_")[-1])
    path = self.repo_root / "sound" / "programmable_wave_samples" / f"{sample_number:02d}.pcm"
    output = []
    for byte in path.read_bytes():
      output.append(((byte >> 4) - 8) * 16)
      output.append(((byte & 0x0F) - 8) * 16)
    return output

  def _wave_name_to_path(self, name: str) -> Path:
    suffix = name.removeprefix("DirectSoundWaveData_")
    return self.asset_root / "sound" / "direct_sound_samples" / f"{suffix}.s"

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

  def _write_source(self, out_c: Path, songs: list[dict], wav_samples: list[dict],
                    groups: list[str], tables: list[str], pwaves: list[str],
                    cry_tables: dict[str, list[str]]) -> None:
    lines = ['#include "constants/songs.h"', '#include "pfr/audio_assets.h"',
             "#include <stdint.h>", ""]
    for sample in wav_samples:
      name, rate, data = sample["name"], sample["rate"], sample["data"]
      sample_type = sample["type"]
      loop_start = sample["loop_start"]
      sample_count = sample["sample_count"]
      loop_enabled = sample["loop_enabled"]
      status = "0xC000u" if loop_enabled else "0"
      lines += [f"static const struct {{ u16 type; u16 status; u32 freq; u32 loopStart; u32 size; s8 data[{len(data)}]; }} sPfrWave_{name} = {{ {sample_type}, {status}, {rate}u, {loop_start}u, {sample_count}u, {{"]
      for index in range(0, len(data), 24):
        row = ", ".join(str(value) for value in data[index:index + 24])
        lines.append(f"  {row},")
      lines += ["}};", ""]
    for pname in pwaves:
      data = self._load_programmable_wave(pname)
      lines += self._emit_int8_array(f"sPfrPwaveData_{pname}", data)
      lines += [""]
    for table in tables:
      values = self.keysplit_tables[table]
      lines += [f"static const u8 sPfrKeysplit_{table}[128] = {{",
                "  " + ", ".join(str(value) for value in values) + ",",
                "};", ""]
    used_group_names = set(groups)
    ordered_groups = [name for name in self.voice_groups.keys() if name in used_group_names]
    group_offsets: dict[str, int] = {}
    total_voice_count = 0
    for group in ordered_groups:
      group_offsets[group] = total_voice_count
      total_voice_count += len(self.voice_groups[group])
    lines.append("static const PfrAudioVoice sPfrVoiceData[] = {")
    for group in ordered_groups:
      lines.append(f"  /* {group} */")
      for entry in self.voice_groups[group]:
        if entry.kind in {"directsound", "directsound_no_resample"}:
          wav_ref = f"(const void *)&sPfrWave_{entry.ref}"
          subgroup = "NULL"
          keysplit = "NULL"
          subgroup_count = 0
        elif entry.kind == "programmable_wave":
          wav_ref = f"(const void *)sPfrPwaveData_{entry.ref}"
          subgroup = "NULL"
          keysplit = "NULL"
          subgroup_count = 0
        elif entry.kind == "square1":
          wav_ref = f"(const void *)(uintptr_t){entry.wav_value}"
          subgroup = "NULL"
          keysplit = "NULL"
          subgroup_count = 0
        elif entry.kind == "square2":
          wav_ref = f"(const void *)(uintptr_t){entry.wav_value}"
          subgroup = "NULL"
          keysplit = "NULL"
          subgroup_count = 0
        elif entry.kind == "noise":
          wav_ref = f"(const void *)(uintptr_t){entry.wav_value}"
          subgroup = "NULL"
          keysplit = "NULL"
          subgroup_count = 0
        elif entry.kind == "keysplit":
          wav_ref = f"(const void *)&sPfrVoiceData[{group_offsets[entry.subgroup]}]"
          subgroup = f"&sPfrVoiceData[{group_offsets[entry.subgroup]}]"
          keysplit = f"sPfrKeysplit_{entry.keysplit}"
          subgroup_count = total_voice_count - group_offsets[entry.subgroup]
        else:
          wav_ref = f"(const void *)&sPfrVoiceData[{group_offsets[entry.subgroup]}]"
          subgroup = f"&sPfrVoiceData[{group_offsets[entry.subgroup]}]"
          keysplit = "NULL"
          subgroup_count = total_voice_count - group_offsets[entry.subgroup]
        lines.append(
          f"  {{ {entry.type_value}, {entry.key}, {entry.length}, {entry.pan_sweep}, "
          f"{wav_ref}, {entry.attack}, {entry.decay}, {entry.sustain}, "
          f"{entry.release}, {subgroup}, {keysplit}, {subgroup_count} }},")
    lines += ["};", ""]
    max_song_id = max(song["spec"]["id"] for song in songs)
    for song in songs:
      symbol = song["spec"]["symbol"]
      for index, track in enumerate(song["tracks"]):
        lines += self._emit_int8_array(f"sPfrTrackData_{symbol}_{index}", list(track["bytes"]))
        if track["relocs"]:
          lines.append(f"static const PfrAudioTrackRelocation sPfrTrackRelocs_{symbol}_{index}[] = {{")
          for reloc in track["relocs"]:
            lines.append(f"  {{ {reloc['offset']}u, {reloc['target_offset']}u }},")
          lines += ["};", ""]
      lines.append(f"static const PfrAudioTrackAsset sPfrTrackAssets_{symbol}[] = {{")
      for index, track in enumerate(song["tracks"]):
        reloc_ptr = f"sPfrTrackRelocs_{symbol}_{index}" if track["relocs"] else "NULL"
        lines.append(f"  {{ (const u8*)sPfrTrackData_{symbol}_{index}, {len(track['bytes'])}u, {reloc_ptr}, {len(track['relocs'])}u }},")
      lines += ["};", ""]
      lines.append(f"static struct {{ u8 trackCount; u8 blockCount; u8 priority; u8 reverb; struct ToneData* tone; u8* part[{len(song['tracks'])}]; }} sPfrSongHeader_{symbol} = {{")
      lines.append(f"  {len(song['tracks'])}, 0, {song['priority']}, {song['reverb']}, NULL, {{")
      for index in range(len(song["tracks"])):
        lines.append(f"    (u8*)sPfrTrackData_{symbol}_{index},")
      lines += ["  }", "};", ""]
    lines.append("const struct Song gSongTable[] = {")
    for song_id in range(max_song_id + 1):
      match = next((song for song in songs if song["spec"]["id"] == song_id), None)
      if match is None:
        lines.append("  { NULL, 0, 0 },")
      else:
        ms, me = match["player_ids"]
        lines.append(f"  {{ (struct SongHeader*)&sPfrSongHeader_{match['spec']['symbol']}, {ms}, {me} }},")
    lines += ["};", ""]
    lines.append("const PfrAudioSongAsset gPfrAudioSongAssets[] = {")
    for song in songs:
      spec = song["spec"]
      ms, _ = song["player_ids"]
      loop_flag = "TRUE" if song["loop"] else "FALSE"
      voice_offset = group_offsets[song["voicegroup"]]
      voice_count = total_voice_count - voice_offset
      lines.append(f"  {{ {spec['symbol'].upper()}, {ms}, {loop_flag}, {len(song['tracks'])}, {song['priority']}, {song['reverb']}, (const struct SongHeader*)&sPfrSongHeader_{spec['symbol']}, &sPfrVoiceData[{voice_offset}], {voice_count}, sPfrTrackAssets_{spec['symbol']} }},")
    lines.append("};")
    lines.append(f"const u32 gPfrAudioSongAssetCount = {len(songs)}u;")
    lines.append("")
    lines.append("const struct ToneData gCryTable[] = {")
    reverse = ["const struct ToneData gCryTable_Reverse[] = {"]
    for wave in cry_tables["forward"]:
      lines.append(f"  {{ 0x20, 60, 0, 0, (struct WaveData*)&sPfrWave_{wave}, 0xFF, 0, 0xFF, 0 }},")
    for wave in cry_tables["reverse"]:
      reverse.append(f"  {{ 0x30, 60, 0, 0, (struct WaveData*)&sPfrWave_{wave}, 0xFF, 0, 0xFF, 0 }},")
    lines.append("};")
    reverse.append("};")
    lines += [""] + reverse
    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_c.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--repo-root", required=True)
  parser.add_argument("--asset-root", required=True)
  parser.add_argument("--out-c", required=True)
  parser.add_argument("--asset", action="append", default=[])
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  AudioAssetTool(Path(args.repo_root), Path(args.asset_root)).build(
    Path(args.out_c), args.asset)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
