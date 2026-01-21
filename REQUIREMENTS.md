# SENGBARD — Requirements Specification

## Overview

**SENGBARD** is a Eurorack sequencer/arranger module designed for making complete songs using a modular approach. It provides 3 independent polyphonic tracks with 8 scenes for arrangement, enabling melody, countermelody, and rhythm from a single compact module.

**Design Philosophy:**
- Inspire creativity as the primary goal
- Beginner approachable, expert powerful
- No menu diving — what you see is what you get
- Immediate tactile control with visual feedback

---

## Hardware Platform

- **MCU:** STM32 (prototype on Blue Pill)
- **Power:** Standard Eurorack ±12V
- **Form Factor:** Eurorack module (HP TBD based on panel layout)

---

## Functional Sections

### 1. Clock Section

SENGBARD uses an external clock source. The clock section is utility-only.

| Component | Type | Description |
|-----------|------|-------------|
| CLK IN | Input jack | External clock input |
| RESET | Input jack | Reset all tracks to step 1 |

**Behavior:**
- Clock pulses advance the sequencer
- RESET returns all three tracks to step 1 immediately

---

### 2. Sequencer Section

Three independent tracks, each with 8 steps.

#### 2.1 Per-Track Controls (×3)

| Component | Type | Range/Options | Description |
|-----------|------|---------------|-------------|
| STEPS | Knob | 1-8 | Number of active steps for this track |
| DIV | Knob | TBD (e.g., ÷4, ÷2, ×1, ×2, ×4) | Clock division/multiplication |
| DIR | 4-position rotary switch | Fwd / Rev / Pend / Rand | Playback direction |
| PITCH CV | Output jack | 0-5V or 1V/oct | Pitch CV output |
| GATE | Output jack | 0-5V gate | Gate output |

**Direction Modes:**
1. **Forward** — Steps 1→2→3→...→N→1→...
2. **Reverse** — Steps N→(N-1)→...→1→N→...
3. **Pendulum** — Steps 1→2→...→N→(N-1)→...→1→...
4. **Random** — Random step selection

#### 2.2 Step Grid (×8 steps × 3 tracks = 24 total)

| Component | Type | Description |
|-----------|------|-------------|
| Pitch encoder | Rotary encoder | Sets pitch CV value for this step |
| Gate button | LED button | Toggles gate on/off for this step |

**Physical Layout:**
```
Track 1:  [E][E][E][E][E][E][E][E]   (8 encoders)
          [B][B][B][B][B][B][B][B]   (8 LED buttons)

Track 2:  [E][E][E][E][E][E][E][E]
          [B][B][B][B][B][B][B][B]

Track 3:  [E][E][E][E][E][E][E][E]
          [B][B][B][B][B][B][B][B]
```

**LED Button Behavior:**
- **Off** — Gate disabled for this step
- **On (solid)** — Gate enabled for this step
- **On (bright/pulsing)** — Current step during playback

**Track Independence:**
- Each track has independent step count (1-8)
- Each track has independent clock division
- Each track has independent direction mode
- Tracks can create polyrhythms against each other

---

### 3. Scene Section

8 scenes for song arrangement. Each scene stores the complete state of all 3 tracks.

#### 3.1 Scene Buttons

| Component | Type | Description |
|-----------|------|-------------|
| SCENE 1-8 | Multicolor LED rubber buttons (×8) | Select/trigger scenes |
| COPY | Button | Modifier for copy operation |
| DELETE | Button | Modifier for delete operation |

**Scene Button LED States:**
- **Off** — Empty scene (no data)
- **Solid color** — Scene has content
- **Pulsating brightness** — Currently playing scene

**Scene Button Interactions:**
| Action | Result |
|--------|--------|
| Tap empty scene | Creates new blank scene, switches to it |
| Tap scene with content | Switches to that scene, resets to step 1 |
| Tap currently playing scene | Restarts from step 1 (retrigger/drum hit) |
| Hold COPY + tap source + tap destination | Copies source scene to destination (overwrites) |
| Hold DELETE + tap scene | Clears that scene (becomes empty) |

#### 3.2 Scene I/O

| Component | Type | Description |
|-----------|------|-------------|
| SCENE CV IN | Input jack | CV control of scene selection (0-7V) |
| SCENE CV OUT | Output jack | Outputs current scene as CV (0-7V) |

**Scene CV Mapping:**
| Voltage | Scene |
|---------|-------|
| 0V | Scene 1 |
| 1V | Scene 2 |
| 2V | Scene 3 |
| 3V | Scene 4 |
| 4V | Scene 5 |
| 5V | Scene 6 |
| 6V | Scene 7 |
| 7V | Scene 8 |

**Scene Behavior:**
- Scene changes happen immediately (mid-step)
- Scene change resets all tracks to step 1
- Auto-save: scenes are always being saved, no manual save needed
- Scene data persists after power off (EEPROM/Flash storage)

---

### 4. Scene Data Structure

Each scene stores:

```
Scene {
  Track[3] {
    step_count: 1-8
    clock_division: enum
    direction: Fwd | Rev | Pend | Rand
    steps[8] {
      pitch: 0-4095 (12-bit DAC value)
      gate: bool
    }
  }
}
```

**Total scenes:** 8
**Storage requirement:** TBD based on final data structure

---

## Component Summary

| Component | Count |
|-----------|-------|
| **Inputs** | 3 |
| CLK IN | 1 |
| RESET | 1 |
| SCENE CV IN | 1 |
| **Outputs** | 7 |
| PITCH CV (per track) | 3 |
| GATE (per track) | 3 |
| SCENE CV OUT | 1 |
| **Encoders** | 24 |
| **LED buttons (gate)** | 24 |
| **LED buttons (scene)** | 8 |
| **Knobs** | 6 |
| STEPS (per track) | 3 |
| DIV (per track) | 3 |
| **4-position rotary switches** | 3 |
| DIR (per track) | 3 |
| **Buttons** | 2 |
| COPY | 1 |
| DELETE | 1 |

**Grand Total:**
- 3 input jacks
- 7 output jacks
- 24 encoders
- 32 LED buttons
- 6 knobs
- 3 rotary switches
- 2 buttons

---

## Panel Layout (Finalized)

```
┌─────────────────────────────────────────────────────────────────────┐
│  SENGBARD                                                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  TRACK 1  [STEPS][DIV][DIR]  [E][E][E][E][E][E][E][E]  ○ PITCH     │
│                              [B][B][B][B][B][B][B][B]  ○ GATE      │
│                                                                     │
│  TRACK 2  [STEPS][DIV][DIR]  [E][E][E][E][E][E][E][E]  ○ PITCH     │
│                              [B][B][B][B][B][B][B][B]  ○ GATE      │
│                                                                     │
│  TRACK 3  [STEPS][DIV][DIR]  [E][E][E][E][E][E][E][E]  ○ PITCH     │
│                              [B][B][B][B][B][B][B][B]  ○ GATE      │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ○ CLK IN      [COPY] [1][2][3][4]      ○ SCENE CV IN              │
│  ○ RESET       [DEL]  [5][6][7][8]      ○ SCENE CV OUT             │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**Layout Key:**
- `[E]` = Encoder (pitch per step)
- `[B]` = LED Button (gate toggle per step)
- `[STEPS]` = Knob (1-8 step count)
- `[DIV]` = Knob (clock division)
- `[DIR]` = 4-position rotary switch (Fwd/Rev/Pend/Rand)
- `[1-8]` = Scene LED buttons (2×4 grid)
- `[COPY]` / `[DEL]` = Modifier buttons
- `○` = 3.5mm jack

**Section Organization:**
- **Top area:** 3 track rows, each with controls on left, step grid in center, outputs on right
- **Bottom area:** Clock inputs on left, scene section in center, scene CV I/O on right

---

## VCV Rack Prototype

For workflow validation, build a VCV Rack module that simulates SENGBARD:

- Use knobs in place of encoders (VCV doesn't have native encoder widgets)
- Use lit buttons for gate toggles
- Use lit buttons for scene selection (2×4 grid)
- Implement all 3 tracks with independent step count, division, and direction
- Implement 8 scenes with auto-save behavior
- Scene CV in/out (0-7V mapping)

---

## Open Questions / TBD

1. **Panel width (HP)** — To be estimated based on component spacing
2. **Clock division range** — Exact ratios TBD (e.g., ÷8, ÷4, ÷2, ×1, ×2, ×4)
3. **Encoder type** — Standard rotary encoder with detents, or smooth?
4. **LED button style** — Specific part number for Launchpad-style buttons
5. **Voltage ranges:**
   - PITCH CV output range (0-5V? 0-8V? Configurable?)
   - Gate output voltage (5V? 10V?)
   - Scene CV input thresholds
6. **Encoder LED indicators** — Do encoders need LED rings to show pitch value?
7. **Multiplexer architecture** — How to scan 24 encoders + 32 buttons with STM32

---

## Future Considerations (Not in V1)

- Quantizer (scale selection)
- Slew/portamento
- Attenuverter
- Swing per track
- Probability per step
- MIDI in/out
- USB for preset backup

---

## Next Steps

1. Create panel layout mockup
2. Build VCV Rack prototype to validate UI/UX
3. Estimate HP width
4. Component sourcing research (Taiwan availability)
5. STM32 pin allocation and multiplexer design
