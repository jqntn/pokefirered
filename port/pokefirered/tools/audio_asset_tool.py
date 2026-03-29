#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import re
import subprocess
import wave
from array import array
from dataclasses import dataclass
from pathlib import Path

OUTPUT_SAMPLE_RATE = 13379
TICKS_PER_BEAT = 24
TARGET_PEAK = 112
RHYTHM_BASE_KEY = 36
DEFAULT_PAN_CENTER = 64

SONG_SPECS = [
  {
    "id": 321,
    "symbol": "mus_game_freak",
    "macro": "MUS_GAME_FREAK",
    "source": "sound/songs/midi/mus_game_freak.mid",
    "generated": True,
    "loop": False,
    "seconds": 18.0,
  },
  {
    "id": 277,
    "symbol": "mus_intro_fight",
    "macro": "MUS_INTRO_FIGHT",
    "source": "sound/songs/midi/mus_intro_fight.mid",
    "generated": True,
    "loop": False,
    "seconds": 45.0,
  },
  {
    "id": 278,
    "symbol": "mus_title",
    "macro": "MUS_TITLE",
    "source": "sound/songs/midi/mus_title.mid",
    "generated": True,
    "loop": True,
    "seconds": 120.0,
  },
  {
    "id": 5,
    "symbol": "se_select",
    "macro": "SE_SELECT",
    "source": "sound/songs/midi/se_select.mid",
    "generated": True,
    "loop": False,
    "seconds": 2.0,
  },
]

CRY_SPECS = [
  {
    "wave_name": "gPfrCryWaveCharizard",
    "species_index": 5,
    "source": "sound/direct_sound_samples/cries/charizard.wav",
  },
  {
    "wave_name": "gPfrCryWaveNidorino",
    "species_index": 32,
    "source": "sound/direct_sound_samples/cries/nidorino.wav",
  },
]

COMMAND_TOKENS = {
  "KEYSH",
  "TEMPO",
  "VOICE",
  "VOL",
  "PAN",
  "BEND",
  "BENDR",
  "LFOS",
  "MOD",
  "XCMD",
}

NOTE_NAMES = {
  "Cn": 0,
  "Cs": 1,
  "Dn": 2,
  "Ds": 3,
  "En": 4,
  "Fn": 5,
  "Fs": 6,
  "Gn": 7,
  "Gs": 8,
  "An": 9,
  "As": 10,
  "Bn": 11,
}


@dataclass
class VoiceEntry:
  kind: str
  base_key: int = 60
  pan: int = 0
  ref: str | None = None
  attack: int = 0
  decay: int = 0
  sustain: int = 15
  release: int = 0
  duty: int = 2
  period: int = 0
  subgroup: str | None = None
  keysplit: str | None = None


@dataclass
class NoteState:
  kind: str
  gain: float
  pan: int
  release_frames_total: int
  release_frames_left: int
  source_pos: float = 0.0
  source_step: float = 1.0
  source_data: list[float] | None = None
  phase: float = 0.0
  wave_cycle: list[float] | None = None
  held_sample: float = 0.0
  released: bool = False
  tied: bool = False
  remaining_ticks: int | None = None


@dataclass
class TrackProgram:
  instructions: list[dict]
  labels: dict[str, int]


@dataclass
class TrackRuntime:
  program: TrackProgram
  pc: int = 0
  wait: int = 0
  finished: bool = False
  last_command: str | None = None
  last_note_duration: int = 0
  last_pitch: int | None = None
  last_velocity: int = 100
  key_shift: int = 0
  voice: int = 0
  volume: int = 127
  pan: int = 0
  bend: int = 0
  bend_range: int = 2
  lfo_speed: int = 0
  mod_depth: int = 0
  active_note: NoteState | None = None
  tempo_override: int | None = None

  def __post_init__(self) -> None:
    self.stack: list[int] = []


class AudioAssetTool:
  def __init__(self, repo_root: Path, stage_dir: Path, mid2agb: Path):
    self.repo_root = repo_root
    self.stage_dir = stage_dir
    self.mid2agb = mid2agb
    self.sample_cache: dict[str, tuple[int, list[float]]] = {}
    self.voice_groups = self._parse_voice_groups(
      self.repo_root / "sound" / "voice_groups.inc")
    self.keysplit_tables = self._parse_keysplit_tables(
      self.repo_root / "sound" / "keysplit_tables.inc")
    self.song_players = self._parse_song_table(
      self.repo_root / "sound" / "song_table.inc")

  def build(self, out_c: Path, out_h: Path) -> None:
    song_assets = []
    cry_assets = []

    self.stage_dir.mkdir(parents=True, exist_ok=True)

    for spec in SONG_SPECS:
      asm_path = self._prepare_song_asm(spec)
      song_assets.append((spec, self._render_song(spec, asm_path)))

    for spec in CRY_SPECS:
      cry_assets.append((spec, self._render_cry(spec)))

    self._write_header(out_h)
    self._write_source(out_c, song_assets, cry_assets)

  def _prepare_song_asm(self, spec: dict) -> Path:
    source_path = self.repo_root / spec["source"]
    output_path = self.stage_dir / f"{spec['symbol']}.s"

    if not spec["generated"]:
      return source_path

    subprocess.run(
      [str(self.mid2agb), str(source_path), str(output_path)],
      check=True,
      cwd=self.repo_root,
    )
    return output_path

  def _render_cry(self, spec: dict) -> list[int]:
    _, samples = self._load_wav(self.repo_root / spec["source"])
    peak = max(abs(sample) for sample in samples) if samples else 1.0
    scale = TARGET_PEAK / peak if peak > 0.0 else 1.0
    output = []
    for sample in samples:
      value = int(round(sample * scale))
      value = max(-127, min(127, value))
      output.extend((value, value))
    return output

  def _parse_song_table(self, path: Path) -> dict[str, int]:
    mapping = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
      line = raw_line.split("@", 1)[0].strip()
      if not line.startswith("song "):
        continue
      parts = [part.strip() for part in line[len("song "):].split(",")]
      mapping[parts[0]] = int(parts[1])
    return mapping

  def _parse_keysplit_tables(self, path: Path) -> dict[str, dict[int, int]]:
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
      note = int(match.group(1))
      value = int(line[len(".byte"):].strip().split()[0])
      tables[current][note] = value
    return tables

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
      kind = "directsound_no_resample" if name.endswith("no_resample") else "directsound"
      return VoiceEntry(kind, int(args[0]), int(args[1]), args[2], int(args[3]),
                        int(args[4]), int(args[5]), int(args[6]))
    if name in {"voice_square_1", "voice_square_1_alt"}:
      return VoiceEntry("square1", int(args[0]), int(args[1]), None, int(args[4]),
                        int(args[5]), int(args[6]), int(args[7]), int(args[3]))
    if name in {"voice_square_2", "voice_square_2_alt"}:
      return VoiceEntry("square2", int(args[0]), int(args[1]), None, int(args[3]),
                        int(args[4]), int(args[5]), int(args[6]), int(args[2]))
    if name in {"voice_programmable_wave", "voice_programmable_wave_alt"}:
      return VoiceEntry("programmable_wave", int(args[0]), int(args[1]), args[2],
                        int(args[3]), int(args[4]), int(args[5]), int(args[6]))
    if name in {"voice_noise", "voice_noise_alt"}:
      return VoiceEntry("noise", int(args[0]), int(args[1]), None, int(args[3]),
                        int(args[4]), int(args[5]), int(args[6]), 2, int(args[2]))
    if name == "voice_keysplit":
      return VoiceEntry("keysplit", subgroup=args[0], keysplit=args[1])
    if name == "voice_keysplit_all":
      return VoiceEntry("rhythm", subgroup=args[0])
    raise RuntimeError(f"unsupported voice macro {name}")

  def _render_song(self, spec: dict, asm_path: Path) -> list[int]:
    tracks, env = self._parse_song_assembly(spec["symbol"], asm_path)
    runtimes = [TrackRuntime(program=track) for track in tracks]
    mix = array("f", [0.0]) * int(spec["seconds"] * OUTPUT_SAMPLE_RATE * 2)
    tempo_cmd = 75
    tempo_fraction = 0.0
    max_frames = len(mix) // 2
    write_frame = 0

    while write_frame < max_frames:
      any_active = False
      for runtime in runtimes:
        if runtime.finished or runtime.wait != 0:
          continue
        self._run_track_until_wait(runtime, env, spec["symbol"])

      for runtime in runtimes:
        if runtime.tempo_override is not None:
          tempo_cmd = runtime.tempo_override

      tick_frames_float = (
        OUTPUT_SAMPLE_RATE * 60.0 / ((tempo_cmd * 2.0) * TICKS_PER_BEAT))
      tempo_fraction += tick_frames_float
      tick_frames = int(tempo_fraction)
      tempo_fraction -= tick_frames
      if tick_frames <= 0:
        tick_frames = 1
      if write_frame + tick_frames > max_frames:
        tick_frames = max_frames - write_frame

      for runtime in runtimes:
        if runtime.active_note is not None:
          self._render_note(runtime.active_note, mix, write_frame, tick_frames)
          any_active = True

      write_frame += tick_frames

      for runtime in runtimes:
        if runtime.wait > 0:
          runtime.wait -= 1
        if runtime.active_note is not None and runtime.active_note.remaining_ticks is not None:
          runtime.active_note.remaining_ticks -= 1
          if runtime.active_note.remaining_ticks <= 0 and not runtime.active_note.released:
            self._release_note(runtime.active_note)
        if runtime.active_note is not None and runtime.active_note.released and runtime.active_note.release_frames_left <= 0:
          runtime.active_note = None
        if not runtime.finished or runtime.wait > 0 or runtime.active_note is not None:
          any_active = True

      if not any_active and not spec["loop"]:
        break

    return self._normalize_mix(mix[:write_frame * 2])

  def _parse_song_assembly(self,
                           song_symbol: str,
                           path: Path) -> tuple[list[TrackProgram], dict[str, int]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    env: dict[str, int | str] = {"mxv": 127, "c_v": 64}
    tracks: dict[int, TrackProgram] = {}
    current_track: TrackProgram | None = None
    pending_target = None
    header_seen = False

    for raw_line in lines:
      line = raw_line.split("@", 1)[0].rstrip()
      if not line:
        continue
      stripped = line.strip()

      if stripped.startswith(".equ"):
        name, expr = [part.strip() for part in stripped[len(".equ"):].split(",", 1)]
        try:
          env[name] = self._eval_expr(expr, env)
        except (NameError, SyntaxError, TypeError):
          env[name] = expr
        continue

      if stripped.endswith(":"):
        label = stripped[:-1]
        if label == song_symbol:
          header_seen = True
          current_track = None
          continue

        match = re.fullmatch(rf"{re.escape(song_symbol)}_(\d+)", label)
        if match:
          track_id = int(match.group(1))
          current_track = TrackProgram([], {label: 0})
          tracks[track_id] = current_track
          pending_target = None
          continue

        if current_track is not None:
          current_track.labels[label] = len(current_track.instructions)
        continue

      if header_seen or current_track is None:
        continue

      if stripped.startswith(".byte"):
        tokens = [token.strip() for token in stripped[len(".byte"):].split(",")]
        tokens = [token for token in tokens if token]
        if not tokens:
          continue
        inst = self._parse_track_tokens(tokens)
        current_track.instructions.append(inst)
        pending_target = inst if inst["op"] in {"goto", "patt"} else None
        continue

      if stripped.startswith(".word"):
        if pending_target is None:
          raise RuntimeError(f"unexpected .word in {path}")
        pending_target["target"] = stripped[len(".word"):].strip()
        pending_target = None

    return [tracks[index] for index in sorted(tracks)], env

  def _parse_track_tokens(self, tokens: list[str]) -> dict:
    first = tokens[0]
    if re.fullmatch(r"W\d+", first):
      return {"op": "wait", "value": int(first[1:]), "token": first}
    if first in {"GOTO", "PATT"}:
      return {"op": first.lower(), "target": None}
    if first == "PEND":
      return {"op": "pend"}
    if first == "FINE":
      return {"op": "fine"}
    if first == "EOT":
      return {"op": "eot"}
    if re.fullmatch(r"N\d+", first) or first == "TIE":
      return {"op": "note", "note_cmd": first, "args": tokens[1:]}
    if first == "XCMD":
      return {"op": "xcmd", "name": tokens[1], "args": tokens[2:]}
    if first.startswith("x"):
      return {"op": "xcmd", "name": first, "args": tokens[1:]}
    if first in COMMAND_TOKENS:
      return {"op": "cmd", "name": first, "args": tokens[1:]}
    if self._looks_like_note(first) or first.startswith("v"):
      return {"op": "note", "note_cmd": "", "args": tokens}
    return {"op": "cmd", "name": "", "args": tokens}

  def _run_track_until_wait(self,
                            runtime: TrackRuntime,
                            env: dict[str, int],
                            song_symbol: str) -> None:
    while not runtime.finished and runtime.wait == 0:
      if runtime.pc >= len(runtime.program.instructions):
        runtime.finished = True
        return
      inst = runtime.program.instructions[runtime.pc]
      runtime.pc += 1
      op = inst["op"]

      if op == "wait":
        runtime.wait = inst["value"]
        runtime.last_command = inst["token"]
      elif op == "goto":
        runtime.pc = runtime.program.labels[inst["target"]]
        runtime.last_command = "GOTO"
      elif op == "patt":
        runtime.stack.append(runtime.pc)
        runtime.pc = runtime.program.labels[inst["target"]]
        runtime.last_command = "PATT"
      elif op == "pend":
        runtime.pc = runtime.stack.pop() if runtime.stack else runtime.pc
        runtime.last_command = "PEND"
      elif op == "fine":
        runtime.finished = True
        runtime.last_command = "FINE"
      elif op == "cmd":
        self._apply_track_command(runtime, inst, env)
      elif op == "note":
        self._start_track_note(runtime, inst, song_symbol)
      elif op == "eot":
        if runtime.active_note is not None:
          self._release_note(runtime.active_note)
        runtime.last_command = "EOT"
      elif op == "xcmd":
        runtime.last_command = f"XCMD:{inst['name']}"

  def _apply_track_command(self,
                           runtime: TrackRuntime,
                           inst: dict,
                           env: dict[str, int]) -> None:
    name = inst["name"] or runtime.last_command or ""
    value = self._eval_expr(inst["args"][0], env) if inst["args"] else 0
    runtime.last_command = name

    if name == "KEYSH":
      runtime.key_shift = value
    elif name == "TEMPO":
      runtime.tempo_override = value
    elif name == "VOICE":
      runtime.voice = value
    elif name == "VOL":
      runtime.volume = value
    elif name == "PAN":
      runtime.pan = value - DEFAULT_PAN_CENTER
    elif name == "BEND":
      runtime.bend = value - DEFAULT_PAN_CENTER
    elif name == "BENDR":
      runtime.bend_range = value
    elif name == "LFOS":
      runtime.lfo_speed = value
    elif name == "MOD":
      runtime.mod_depth = value

  def _start_track_note(self, runtime: TrackRuntime, inst: dict, song_symbol: str) -> None:
    note_cmd = inst["note_cmd"] or runtime.last_command or ""
    args = list(inst["args"])
    pitch_token = None
    velocity_token = None

    if args and not args[0].startswith("v"):
      pitch_token = args.pop(0)
    if args:
      velocity_token = args.pop(0)

    if note_cmd.startswith("N"):
      runtime.last_note_duration = int(note_cmd[1:])
      duration = runtime.last_note_duration
    elif note_cmd == "TIE":
      duration = None
    else:
      duration = runtime.last_note_duration

    if pitch_token is not None:
      runtime.last_pitch = self._note_token_to_midi(pitch_token)
    if runtime.last_pitch is None:
      raise RuntimeError(f"track note missing pitch in {song_symbol}")
    if velocity_token is not None:
      runtime.last_velocity = int(velocity_token[1:])

    if runtime.active_note is not None and not runtime.active_note.released:
      self._release_note(runtime.active_note)

    voice = self._resolve_voice("voicegroup000", runtime.voice, runtime.last_pitch)
    runtime.active_note = self._make_note_state(runtime,
                                                voice,
                                                runtime.last_pitch,
                                                runtime.last_velocity)
    runtime.active_note.remaining_ticks = duration
    runtime.active_note.tied = note_cmd == "TIE"
    runtime.last_command = note_cmd

  def _make_note_state(self,
                       runtime: TrackRuntime,
                       voice: VoiceEntry,
                       pitch: int,
                       velocity: int) -> NoteState:
    gain = (runtime.volume / 127.0) * (velocity / 127.0) * 0.65
    pan = max(-64, min(63, runtime.pan + voice.pan))
    release_frames = max(16, voice.release * 16)

    if voice.kind.startswith("directsound"):
      sample_rate, sample_data = self._load_wav(self._wave_name_to_path(voice.ref))
      note = NoteState(voice.kind,
                       gain,
                       pan,
                       release_frames,
                       release_frames,
                       source_data=sample_data)
      if voice.kind == "directsound_no_resample":
        note.source_step = sample_rate / float(OUTPUT_SAMPLE_RATE)
      else:
        semitone_delta = pitch + runtime.key_shift - voice.base_key
        bend = (runtime.bend / 64.0) * runtime.bend_range
        ratio = math.pow(2.0, (semitone_delta + bend) / 12.0)
        note.source_step = ratio * sample_rate / float(OUTPUT_SAMPLE_RATE)
      return note

    if voice.kind in {"square1", "square2"}:
      return NoteState(voice.kind,
                       gain * 0.45,
                       pan,
                       release_frames,
                       release_frames,
                       source_step=self._note_frequency(pitch + runtime.key_shift,
                                                        runtime.bend,
                                                        runtime.bend_range) /
                       OUTPUT_SAMPLE_RATE,
                       wave_cycle=[float(voice.duty)])

    if voice.kind == "programmable_wave":
      wave_cycle = self._load_programmable_wave(voice.ref)
      return NoteState(voice.kind,
                       gain * 0.45,
                       pan,
                       release_frames,
                       release_frames,
                       source_step=self._note_frequency(pitch + runtime.key_shift,
                                                        runtime.bend,
                                                        runtime.bend_range) *
                       len(wave_cycle) / OUTPUT_SAMPLE_RATE,
                       wave_cycle=wave_cycle)

    if voice.kind == "noise":
      return NoteState(voice.kind,
                       gain * 0.40,
                       pan,
                       release_frames,
                       release_frames,
                       source_step=self._note_frequency(pitch + runtime.key_shift,
                                                        runtime.bend,
                                                        runtime.bend_range) /
                       OUTPUT_SAMPLE_RATE)

    raise RuntimeError(f"unsupported voice kind {voice.kind}")

  def _resolve_voice(self, group_name: str, voice_index: int, pitch: int) -> VoiceEntry:
    entries = self.voice_groups[group_name]
    entry = entries[min(max(voice_index, 0), len(entries) - 1)]

    while entry.kind in {"keysplit", "rhythm"}:
      subgroup_entries = self.voice_groups[entry.subgroup]
      if entry.kind == "keysplit":
        table = self.keysplit_tables[entry.keysplit]
        if pitch in table:
          sub_index = table[pitch]
        else:
          nearest_note = min(table, key=lambda note: abs(note - pitch))
          sub_index = table[nearest_note]
      else:
        sub_index = max(0, min(len(subgroup_entries) - 1, pitch - RHYTHM_BASE_KEY))
      entry = subgroup_entries[sub_index]

    return entry

  def _release_note(self, note: NoteState) -> None:
    note.released = True
    note.tied = False

  def _render_note(self,
                   note: NoteState,
                   mix: array,
                   frame_offset: int,
                   frame_count: int) -> None:
    left_gain, right_gain = self._pan_gains(note.pan, note.gain)

    if note.kind.startswith("directsound"):
      data = note.source_data or []
      sample_count = len(data)
      for i in range(frame_count):
        if sample_count == 0:
          break
        index = int(note.source_pos)
        if index >= sample_count:
          if note.kind == "directsound_no_resample" or note.released:
            note.release_frames_left = 0
            break
          note.source_pos = math.fmod(note.source_pos, sample_count)
          index = int(note.source_pos)
        sample = self._apply_release(data[index], note)
        note.source_pos += note.source_step
        self._mix_sample(mix, frame_offset + i, sample, left_gain, right_gain)
      return

    if note.kind in {"square1", "square2"}:
      duty = [0.125, 0.25, 0.5, 0.75][int(note.wave_cycle[0]) & 3]
      for i in range(frame_count):
        note.phase += note.source_step
        note.phase -= math.floor(note.phase)
        sample = 0.85 if note.phase < duty else -0.85
        sample = self._apply_release(sample, note)
        self._mix_sample(mix, frame_offset + i, sample, left_gain, right_gain)
      return

    if note.kind == "programmable_wave":
      cycle = note.wave_cycle or [0.0]
      count = len(cycle)
      for i in range(frame_count):
        sample = cycle[int(note.phase) % count]
        note.phase += note.source_step
        sample = self._apply_release(sample, note)
        self._mix_sample(mix, frame_offset + i, sample, left_gain, right_gain)
      return

    if note.kind == "noise":
      for i in range(frame_count):
        note.phase += note.source_step
        if note.phase >= 1.0:
          note.phase -= math.floor(note.phase)
          value = math.sin((frame_offset + i) * 12.9898) * 43758.5453
          value -= math.floor(value)
          note.held_sample = value * 2.0 - 1.0
        sample = self._apply_release(note.held_sample, note)
        self._mix_sample(mix, frame_offset + i, sample, left_gain, right_gain)

  def _apply_release(self, sample: float, note: NoteState) -> float:
    if not note.released:
      return sample
    if note.release_frames_left <= 0:
      return 0.0
    scale = note.release_frames_left / float(note.release_frames_total)
    note.release_frames_left -= 1
    return sample * scale

  def _mix_sample(self,
                  mix: array,
                  frame_index: int,
                  sample: float,
                  left_gain: float,
                  right_gain: float) -> None:
    base = frame_index * 2
    mix[base] += sample * left_gain
    mix[base + 1] += sample * right_gain

  def _normalize_mix(self, mix: array) -> list[int]:
    peak = 0.0
    for sample in mix:
      peak = max(peak, abs(sample))
    scale = TARGET_PEAK / peak if peak > 0.0 else 1.0
    output = []
    for sample in mix:
      value = int(round(sample * scale))
      output.append(max(-127, min(127, value)))
    return output

  def _load_wav(self, path: Path) -> tuple[int, list[float]]:
    cache_key = str(path)
    if cache_key in self.sample_cache:
      return self.sample_cache[cache_key]

    with wave.open(str(path), "rb") as handle:
      channels = handle.getnchannels()
      sample_width = handle.getsampwidth()
      sample_rate = handle.getframerate()
      frames = handle.readframes(handle.getnframes())

    if sample_width != 1:
      raise RuntimeError(f"unsupported sample width for {path}")

    output = []
    for index in range(0, len(frames), channels):
      accum = 0.0
      for channel in range(channels):
        accum += (frames[index + channel] - 128) / 128.0
      output.append(accum / channels)

    self.sample_cache[cache_key] = (sample_rate, output)
    return self.sample_cache[cache_key]

  def _load_programmable_wave(self, name: str | None) -> list[float]:
    if name is None:
      return [0.0]
    sample_number = int(name.split("_")[-1])
    path = self.repo_root / "sound" / "programmable_wave_samples" / f"{sample_number:02d}.pcm"
    data = path.read_bytes()
    output = []
    for byte in data:
      output.append(((byte >> 4) - 7.5) / 7.5)
      output.append(((byte & 0x0F) - 7.5) / 7.5)
    return output

  def _wave_name_to_path(self, name: str | None) -> Path:
    if name is None:
      raise RuntimeError("missing sample name")
    suffix = name.removeprefix("DirectSoundWaveData_")
    return self.repo_root / "sound" / "direct_sound_samples" / f"{suffix}.wav"

  def _pan_gains(self, pan: int, gain: float) -> tuple[float, float]:
    clamped = max(-64, min(63, pan))
    right = ((clamped + 64) / 127.0) * gain
    left = ((63 - clamped) / 127.0) * gain
    return left, right

  def _note_frequency(self, midi_note: int, bend: int, bend_range: int) -> float:
    semitone = midi_note + (bend / 64.0) * bend_range
    return 440.0 * math.pow(2.0, (semitone - 69.0) / 12.0)

  def _looks_like_note(self, token: str) -> bool:
    return re.fullmatch(r"[A-G][sn](M?\d+)", token) is not None

  def _note_token_to_midi(self, token: str) -> int:
    match = re.fullmatch(r"([A-G][sn])(M?\d+)", token)
    if match is None:
      raise RuntimeError(f"unsupported note token {token}")
    octave_text = match.group(2)
    octave = -int(octave_text[1:]) if octave_text.startswith("M") else int(octave_text)
    return (octave + 2) * 12 + NOTE_NAMES[match.group(1)]

  def _eval_expr(self, expr: str, env: dict[str, int | str]) -> int:
    text = expr.strip()
    if text in env and isinstance(env[text], int):
      return env[text]
    numeric_env = {
      name: value for name, value in env.items() if isinstance(value, int)
    }
    return int(eval(text, {"__builtins__": {}}, numeric_env))

  def _write_header(self, out_h: Path) -> None:
    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_h.write_text(
      "#ifndef PFR_GENERATED_AUDIO_ASSETS_NATIVE_H\n"
      "#define PFR_GENERATED_AUDIO_ASSETS_NATIVE_H\n\n"
      '#include "pfr/audio_assets.h"\n\n'
      "#endif\n",
      encoding="utf-8",
    )

  def _write_source(self,
                    out_c: Path,
                    song_assets: list[tuple[dict, list[int]]],
                    cry_assets: list[tuple[dict, list[int]]]) -> None:
    lines = [
      '#include "constants/songs.h"',
      '#include "pfr/audio_assets.h"',
      "",
    ]

    for spec, clip in song_assets:
      lines.extend(self._emit_sample_array(f"sPfrSongSamples_{spec['symbol']}", clip))
      lines.append("")

    for spec, clip in cry_assets:
      lines.extend(self._emit_sample_array(f"sPfrCrySamples_{spec['wave_name']}", clip))
      lines.append("")

    for spec, clip in cry_assets:
      lines.extend([
        f"static const struct WaveData {spec['wave_name']} = {{",
        f"  0, 0, {OUTPUT_SAMPLE_RATE}u, 0, {len(clip) // 2}u, {{ 0 }}",
        "};",
        "",
      ])

    lines.append("const PfrAudioClipAsset gPfrAudioSongAssets[] = {")
    for spec, clip in song_assets:
      player_id = self.song_players[spec["symbol"]]
      loop_flag = "TRUE" if spec["loop"] else "FALSE"
      lines.append(
        f"  {{ {spec['macro']}, {player_id}, {loop_flag}, {len(clip) // 2}u, "
        f"sPfrSongSamples_{spec['symbol']} }},")
    lines.append("};")
    lines.append(f"const u32 gPfrAudioSongAssetCount = {len(song_assets)}u;")
    lines.append("")

    lines.append("const PfrCryAsset gPfrAudioCryAssets[] = {")
    for spec, clip in cry_assets:
      lines.append(
        f"  {{ &{spec['wave_name']}, {len(clip) // 2}u, "
        f"sPfrCrySamples_{spec['wave_name']} }},")
    lines.append("};")
    lines.append(f"const u32 gPfrAudioCryAssetCount = {len(cry_assets)}u;")
    lines.append("")

    lines.extend(self._emit_cry_tables(cry_assets))
    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_c.write_text("\n".join(lines) + "\n", encoding="utf-8")

  def _emit_sample_array(self, name: str, samples: list[int]) -> list[str]:
    lines = [f"static const s8 {name}[] = {{"]
    row = []
    for value in samples:
      row.append(str(value))
      if len(row) == 24:
        lines.append("  " + ", ".join(row) + ",")
        row = []
    if row:
      lines.append("  " + ", ".join(row) + ",")
    lines.append("};")
    return lines

  def _emit_cry_tables(self, cry_assets: list[tuple[dict, list[int]]]) -> list[str]:
    wave_by_index = {spec["species_index"]: spec["wave_name"] for spec, _ in cry_assets}
    dummy_wave = cry_assets[0][0]["wave_name"]
    lines = ["const struct ToneData gCryTable[] = {"]
    reverse = ["const struct ToneData gCryTable_Reverse[] = {"]

    for index in range(128 * 4):
      wave_name = wave_by_index.get(index, dummy_wave)
      lines.append(
        f"  {{ 0x20, 60, 0, 0, (struct WaveData *)&{wave_name}, 0xFF, 0, 0xFF, 0 }},")
      reverse.append(
        f"  {{ 0x30, 60, 0, 0, (struct WaveData *)&{wave_name}, 0xFF, 0, 0xFF, 0 }},")

    lines.append("};")
    reverse.append("};")
    return lines + [""] + reverse


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--repo-root", required=True)
  parser.add_argument("--stage-dir", required=True)
  parser.add_argument("--mid2agb", required=True)
  parser.add_argument("--out-c", required=True)
  parser.add_argument("--out-h", required=True)
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  tool = AudioAssetTool(Path(args.repo_root),
                        Path(args.stage_dir),
                        Path(args.mid2agb))
  tool.build(Path(args.out_c), Path(args.out_h))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
