# Pokémon FireRed Desktop Port

Platform abstraction layer for Pokémon FireRed that bridges decompiled game code to a native desktop runtime with hardware-accurate behavior.

## What This Fork Is

This fork is focused on building a native desktop runtime around the decompiled FireRed codebase instead of treating the repository only as a ROM decompilation project.

The goal is to keep original game logic, data, and flow intact while replacing the GBA-facing platform pieces with a host implementation for:

- rendering
- input
- audio
- save storage
- timing
- runtime boot flow
- automated testing

In practice, this means the native layer acts as a PAL between the original game code and a desktop executable, with fidelity to GBA behavior as the bar for correctness.

## Current Scope

The native work lives under `port/pokefirered`.

Current desktop runtime work includes:

- booting through the original startup sequence
- native runtime modes for `game`, `demo`, and `sandbox`
- headless execution for automated checks
- renderer work aimed at hardware-accurate visual behavior
- smoke and integration coverage for PAL/runtime behavior

## Building

The native build uses CMake. Presets are defined in `port/pokefirered/CMakePresets.json`.

### Windows

```powershell
cd port/pokefirered
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

### Linux

```bash
cd port/pokefirered
cmake --preset linux-clang-debug
cmake --build out/build/linux-clang-debug
```

Other available presets include `x64-release`, `linux-gcc-debug`, and `linux-gcc-release`.

## Running

From the native build directory, run:

```text
pokefirered [--mode game|demo|sandbox] [--headless]
            [--frames N] [--quit-on-title] [--quit-on-main-menu]
            [--auto-press-start-frame N]... [--save-path PATH]
```

Examples:

```powershell
.\pokefirered.exe --mode game
.\pokefirered.exe --mode game --headless --frames 1600 --quit-on-title
```

## Controls

These controls apply to the current desktop build.

### Keyboard

- Arrow keys: D-Pad
- `X`: A
- `C`: B
- `Enter`: Start
- `Right Shift`: Select
- `S`: L
- `D`: R

### Gamepad

- D-Pad: D-Pad
- South / bottom face button: A
- East / right face button: B
- Start / Menu: Start
- Back / Select: Select
- Left trigger 2: L
- Right trigger 2: R

## Testing

Example native test flow:

```powershell
cd port/pokefirered
cmake --build out/build/x64-debug --target test
ctest --test-dir out/build/x64-debug --output-on-failure
```

The repository also includes a `format-native` CMake target for applying the native port formatting rules.

## Repository Notes

- `port/pokefirered` contains the native PAL/runtime implementation.
- The root codebase still provides the decompiled game code and data that the PAL is adapting.
