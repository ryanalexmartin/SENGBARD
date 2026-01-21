# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SENGBARD is a VCV Rack plugin that implements a 3-track polyphonic sequencer with 8 scenes for song arrangement. This is a prototype/simulation of a planned Eurorack hardware module, built to validate the workflow and UI/UX before hardware implementation.

## Build Commands

```bash
# Build the plugin (from plugin/ directory)
cd plugin && make

# Build with verbose output
cd plugin && make V=1

# Clean build artifacts
cd plugin && make clean

# Install to VCV Rack plugins folder
cd plugin && make install

# Create distributable package
cd plugin && make dist
```

The build requires the VCV Rack SDK. By default, it expects the SDK at `../Rack-SDK` relative to the plugin directory.

## Architecture

### Directory Structure
- `plugin/` - VCV Rack plugin source code
- `plugin/src/` - C++ source files
- `plugin/res/` - SVG panel graphics
- `Rack-SDK/` - VCV Rack SDK (dependency)
- `REQUIREMENTS.md` - Full hardware specification document

### Module Structure
The plugin contains one module: **Sequencer** (`src/Sequencer.cpp`)

Key data structures:
- `TrackData` - Per-track state (step count, division, direction, 8 pitch/gate values)
- `SceneData` - Complete snapshot of all 3 tracks (8 scenes total)
- `Sequencer` - Main module with params, inputs, outputs, lights

### Sequencer Features
- 3 independent tracks, each with 8 steps
- Per-track controls: step count (1-8), clock division (÷4 to ×8), direction (forward/reverse/pendulum/random)
- 8 scenes for arrangement with copy/delete operations
- Scene CV in/out (0-7V maps to scenes 1-8)
- External clock input with reset

### VCV Rack Plugin Conventions
- Parameters defined in `ParamId` enum, use `ENUMS()` macro for arrays
- `process()` runs at audio rate - handle clock detection, state updates, and output generation
- `dataToJson()`/`dataFromJson()` for patch persistence
- Widget layout uses `mm2px()` for millimeter-to-pixel conversion
- Panel SVG stored in `res/Sequencer.svg`

## Key Implementation Details

- Clock division uses fractional accumulator pattern (`clockPhase[t] += 1.f / division`)
- Gate outputs use `PulseGenerator` for consistent 10ms pulses
- Scene changes load all track parameters via `loadSceneToParams()`
- Gate buttons use toggle behavior (tracked via `gateButtonStates[]`)
- Scene 0 cannot be deleted (always exists as fallback)
