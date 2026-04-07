#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

EXPECTED_SONG_COUNT = 347
EXPECTED_DIRECT_SOUND_COUNT = 477
EXPECTED_PROGRAMMABLE_WAVE_COUNT = 11


def strip_comment(raw_line: str) -> str:
  return raw_line.split("@", 1)[0].strip()


def parse_label(line: str) -> str | None:
  match = re.fullmatch(r"([A-Za-z0-9_]+)(::?)", line)
  if match is None:
    return None
  return match.group(1)


def eval_expr(expr: str, env: dict[str, int]) -> int:
  return int(eval(expr.strip(), {"__builtins__": {}}, env))


def eval_tokens(text: str, env: dict[str, int]) -> list[int]:
  return [eval_expr(token.strip(), env) & 0xFF
          for token in text.split(",") if token.strip()]


def resolve_alias(symbol: str, aliases: dict[str, str]) -> str:
  value = symbol
  seen: set[str] = set()

  while value in aliases:
    if value in seen:
      raise AssertionError(f"alias cycle detected for {symbol}")
    seen.add(value)
    value = aliases[value]

  return value


def fnv1a32(blob: bytes) -> str:
  digest = 2166136261
  for value in blob:
    digest ^= value
    digest = (digest * 16777619) & 0xFFFFFFFF
  return f"{digest:08x}"


def parse_equ_file(path: Path) -> dict[str, int]:
  env: dict[str, int] = {}
  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
    if not line.startswith(".equ"):
      continue
    name, expr = [part.strip() for part in line[len(".equ"):].split(",", 1)]
    env[name] = eval_expr(expr, env)
  return env


def parse_song_table(path: Path) -> list[dict]:
  songs = []
  song_id = 0
  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
    if not line.startswith("song "):
      continue
    parts = [part.strip() for part in line[len("song "):].split(",")]
    songs.append({
      "id": song_id,
      "symbol": parts[0],
      "ms": int(parts[1]),
      "me": int(parts[2]),
    })
    song_id += 1
  return songs


def parse_incbin_entries(path: Path) -> list[dict]:
  entries = []
  current_label = None

  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
    if not line:
      continue
    label = parse_label(line)
    if label is not None:
      current_label = label
      continue
    if current_label is None or not line.startswith(".incbin"):
      continue
    match = re.fullmatch(r'\.incbin\s+"([^"]+)"', line)
    if match is None:
      continue
    entries.append({"label": current_label, "path": match.group(1)})
    current_label = None

  return entries


def parse_cry_tables(path: Path) -> dict[str, list[str]]:
  tables = {"forward": [], "reverse": []}
  current = None

  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
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


def parse_voice_group_counts(path: Path) -> list[dict]:
  groups = []
  current = None

  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
    if not line:
      continue
    label = parse_label(line)
    if label is not None:
      current = {"name": label, "count": 0}
      groups.append(current)
      continue
    if current is not None and line.startswith("voice_"):
      current["count"] += 1

  return groups


def parse_keysplit_offsets(path: Path) -> dict[str, int]:
  blocks = []
  current = None
  current_pos = 0
  prefix_padding = 0

  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
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
    values = eval_tokens(line[len(".byte"):], {})
    current["bytes"].extend(values)
    current_pos += len(values)

  blob = [0] * max(prefix_padding, 0)
  offsets: dict[str, int] = {}

  for block in blocks:
    label_offset = len(blob) - block["subtract"]
    if label_offset < 0:
      raise AssertionError(f"negative keysplit offset for {block['name']}")
    offsets[block["name"]] = label_offset
    blob.extend(block["bytes"])

  return offsets


def parse_midi_cfg(path: Path) -> dict[str, str]:
  entries = {}
  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = raw_line.strip()
    if not line:
      continue
    name, _, args = line.partition(":")
    entries[name.strip()] = args.strip()
  return entries


def parse_song_asm(path: Path, symbol: str, base_env: dict[str, int]) -> dict:
  env = dict(base_env)
  aliases: dict[str, str] = {}
  tracks = []
  track_by_label = {}
  pending_relocs: dict[str, list[dict]] = {}
  current = None
  header_bytes: list[int] = []
  header_words: list[str] = []
  header_mode = False

  for raw_line in path.read_text(encoding="utf-8").splitlines():
    line = strip_comment(raw_line)
    if not line:
      continue
    if line.startswith(".equ"):
      name, expr = [part.strip() for part in line[len(".equ"):].split(",", 1)]
      try:
        env[name] = eval_expr(expr, env)
      except Exception:
        aliases[name] = expr
      continue

    label = parse_label(line)
    if label is not None:
      if label == symbol:
        header_mode = True
        current = None
        continue
      if re.fullmatch(rf"{re.escape(symbol)}_(\d+)", label):
        current = {"label": label, "data": bytearray(), "labels": {label: 0}}
        tracks.append(current)
        track_by_label[label] = current
        pending_relocs[label] = []
        continue
      if current is not None:
        current["labels"][label] = len(current["data"])
      continue

    if header_mode:
      if line.startswith(".byte"):
        header_bytes.extend(eval_tokens(line[len(".byte"):], env))
      elif line.startswith(".word") or line.startswith(".4byte"):
        token = line.split(None, 1)[1].strip()
        header_words.append(resolve_alias(token, aliases))
      elif line.startswith(".end"):
        break
      continue

    if current is None:
      continue

    if line.startswith(".byte"):
      current["data"].extend(eval_tokens(line[len(".byte"):], env))
    elif line.startswith(".word") or line.startswith(".4byte"):
      token = line.split(None, 1)[1].strip()
      pending_relocs[current["label"]].append({
        "offset": len(current["data"]),
        "target": token,
      })
      current["data"].extend(b"\x00\x00\x00\x00")

  track_count = header_bytes[0]
  selected_tracks = []
  relocation_counts = []
  track_hashes = []
  track_lengths = []

  for label_name in header_words[1:1 + track_count]:
    track = track_by_label[label_name]
    relocation_count = 0
    for reloc in pending_relocs[track["label"]]:
      target = resolve_alias(reloc["target"], aliases)
      if target not in track["labels"]:
        raise AssertionError(
          f"unsupported cross-track relocation {target} in {symbol}")
      relocation_count += 1
    selected_tracks.append(track)
    relocation_counts.append(relocation_count)
    track_lengths.append(len(track["data"]))
    track_hashes.append(fnv1a32(bytes(track["data"])))

  return {
    "voicegroup": resolve_alias(header_words[0], aliases),
    "track_count": track_count,
    "priority": header_bytes[2],
    "reverb": header_bytes[3],
    "track_lengths": track_lengths,
    "track_relocation_counts": relocation_counts,
    "track_hashes": track_hashes,
  }


def read_wav_header(path: Path) -> tuple[int, int]:
  blob = path.read_bytes()
  if len(blob) < 16:
    raise AssertionError(f"wav asset too small: {path}")
  flags_word = int.from_bytes(blob[0:4], "little")
  loop_start = int.from_bytes(blob[8:12], "little")
  return flags_word, loop_start


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--repo-root", required=True)
  parser.add_argument("--asset-root", required=True)
  parser.add_argument("--manifest", required=True)
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  repo_root = Path(args.repo_root)
  asset_root = Path(args.asset_root)
  manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))

  env = parse_equ_file(repo_root / "sound" / "MPlayDef.s")
  song_table = parse_song_table(repo_root / "sound" / "song_table.inc")
  direct_sounds = parse_incbin_entries(repo_root / "sound" / "direct_sound_data.inc")
  programmable_waves = parse_incbin_entries(
    repo_root / "sound" / "programmable_wave_data.inc")
  cry_tables = parse_cry_tables(repo_root / "sound" / "cry_tables.inc")
  voice_groups = parse_voice_group_counts(repo_root / "sound" / "voice_groups.inc")
  keysplit_offsets = parse_keysplit_offsets(
    repo_root / "sound" / "keysplit_tables.inc")
  midi_cfg = parse_midi_cfg(repo_root / "sound" / "songs" / "midi" / "midi.cfg")

  assert len(song_table) == EXPECTED_SONG_COUNT
  assert len(direct_sounds) == EXPECTED_DIRECT_SOUND_COUNT
  assert len(programmable_waves) == EXPECTED_PROGRAMMABLE_WAVE_COUNT

  assert manifest["song_count"] == len(song_table)
  assert manifest["direct_sound_count"] == len(direct_sounds)
  assert manifest["programmable_wave_count"] == len(programmable_waves)

  for manifest_song, song_entry in zip(manifest["songs"], song_table):
    assert manifest_song["id"] == song_entry["id"]
    assert manifest_song["symbol"] == song_entry["symbol"]
    assert manifest_song["ms"] == song_entry["ms"]
    assert manifest_song["me"] == song_entry["me"]

    midi_name = f"{song_entry['symbol']}.mid"
    midi_path = repo_root / "sound" / "songs" / "midi" / midi_name
    asm_path = asset_root / "sound" / f"{song_entry['symbol']}.s"

    assert midi_path.is_file(), f"missing midi source: {midi_path}"
    assert midi_name in midi_cfg, f"missing midi.cfg entry: {midi_name}"
    assert asm_path.is_file(), f"missing generated asm: {asm_path}"

    parsed_song = parse_song_asm(asm_path, song_entry["symbol"], env)
    assert manifest_song["voicegroup"] == parsed_song["voicegroup"]
    assert manifest_song["track_count"] == parsed_song["track_count"]
    assert manifest_song["priority"] == parsed_song["priority"]
    assert manifest_song["reverb"] == parsed_song["reverb"]
    assert manifest_song["track_lengths"] == parsed_song["track_lengths"]
    assert manifest_song["track_relocation_counts"] == \
      parsed_song["track_relocation_counts"]
    assert manifest_song["track_hashes"] == parsed_song["track_hashes"]

  for manifest_entry, original_entry in zip(manifest["direct_sounds"], direct_sounds):
    assert manifest_entry["label"] == original_entry["label"]
    assert manifest_entry["path"] == original_entry["path"]
    bin_path = asset_root / original_entry["path"]
    assert bin_path.is_file(), f"missing generated sample: {bin_path}"
    if manifest_entry["kind"] == "cry":
      flags_word, loop_start = read_wav_header(bin_path)
      assert (flags_word & 0xFFFF) == 1, f"unexpected cry type: {bin_path}"
      assert loop_start == 0, f"unexpected cry loop start: {bin_path}"

  for manifest_entry, original_entry in zip(manifest["programmable_waves"],
                                            programmable_waves):
    assert manifest_entry["label"] == original_entry["label"]
    assert manifest_entry["path"] == original_entry["path"]
    assert (repo_root / original_entry["path"]).is_file(), \
      f"missing programmable wave source: {original_entry['path']}"

  assert manifest["cry_tables"] == cry_tables
  assert len(manifest["voice_groups"]) == len(voice_groups)
  for manifest_group, original_group in zip(manifest["voice_groups"], voice_groups):
    assert manifest_group["name"] == original_group["name"]
    assert manifest_group["count"] == original_group["count"]

  manifest_keysplit = {
    entry["name"]: entry["offset"] for entry in manifest["keysplit_tables"]
  }
  assert manifest_keysplit == keysplit_offsets

  print("audio artifact parity: ok")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
