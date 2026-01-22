#include "plugin.hpp"

// Constants
static const int NUM_TRACKS = 3;
static const int NUM_STEPS = 8;
static const int NUM_SCENES = 8;

// Clock division ratios - musical note values (assuming clock = quarter note)
// Values > 1 = slower (multiple clocks per step), < 1 = faster (multiple steps per clock)
static const float DIVISIONS[] = {
    4.0f,           // 1/1  (whole note) - 4 clocks per step
    2.0f,           // 1/2  (half note) - 2 clocks per step
    1.0f,           // 1/4  (quarter note) - 1 clock per step
    0.5f,           // 1/8  (eighth note) - 2 steps per clock
    1.0f / 3.0f,    // 1/8T (eighth triplet) - 3 steps per clock
    0.25f,          // 1/16 (sixteenth note) - 4 steps per clock
    1.0f / 6.0f,    // 1/16T (sixteenth triplet) - 6 steps per clock
    0.125f          // 1/32 (thirty-second note) - 8 steps per clock
};
static const int NUM_DIVISIONS = 8;

// Direction modes
enum Direction {
    DIR_FORWARD,
    DIR_REVERSE,
    DIR_PENDULUM,
    DIR_RANDOM
};

// Track data structure
struct TrackData {
    int stepCount = 8;           // 1-8 steps
    int divisionIndex = 2;       // Index into DIVISIONS array (default 1x)
    Direction direction = DIR_FORWARD;
    float pitches[NUM_STEPS] = {0.f};  // Pitch CV values (0-5V, semitones)
    bool gates[NUM_STEPS] = {true, true, true, true, true, true, true, true};
};

// Scene stores complete state of all tracks
struct SceneData {
    TrackData tracks[NUM_TRACKS];
    bool isEmpty = true;
};

struct Sequencer : Module {
    enum ParamId {
        // Internal clock controls
        BPM_PARAM,
        RUN_PARAM,
        RST_PARAM,  // Manual reset button
        // Track 1 controls
        TRACK1_STEPS_PARAM,
        TRACK1_DIV_PARAM,
        TRACK1_DIR_PARAM,
        // Track 2 controls
        TRACK2_STEPS_PARAM,
        TRACK2_DIV_PARAM,
        TRACK2_DIR_PARAM,
        // Track 3 controls
        TRACK3_STEPS_PARAM,
        TRACK3_DIV_PARAM,
        TRACK3_DIR_PARAM,
        // Step pitch encoders (24 total: 8 per track)
        ENUMS(PITCH_PARAMS, NUM_TRACKS * NUM_STEPS),
        // Step gate buttons (24 total: 8 per track)
        ENUMS(GATE_PARAMS, NUM_TRACKS * NUM_STEPS),
        // Scene buttons (8 total)
        ENUMS(SCENE_PARAMS, NUM_SCENES),
        // Modifier buttons
        COPY_PARAM,
        DELETE_PARAM,
        // Groove controls
        SWING_PARAM,
        PW_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        SCENE_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        // Clock output
        CLOCK_OUTPUT,
        // Reset output
        RESET_OUTPUT,
        // Track outputs
        TRACK1_PITCH_OUTPUT,
        TRACK1_GATE_OUTPUT,
        TRACK2_PITCH_OUTPUT,
        TRACK2_GATE_OUTPUT,
        TRACK3_PITCH_OUTPUT,
        TRACK3_GATE_OUTPUT,
        // Scene CV output
        SCENE_CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        // Run indicator
        RUN_LIGHT,
        // Reset indicator
        RST_LIGHT,
        // Step gate LEDs (24 total)
        ENUMS(GATE_LIGHTS, NUM_TRACKS * NUM_STEPS),
        // Current step indicator LEDs (24 total) - green for active step
        ENUMS(STEP_LIGHTS, NUM_TRACKS * NUM_STEPS),
        // Scene LEDs (8 total, RGB)
        ENUMS(SCENE_LIGHTS, NUM_SCENES * 3),
        // Copy/Delete indicator lights
        COPY_LIGHT,
        DELETE_LIGHT,
        LIGHTS_LEN
    };

    // Internal state
    SceneData scenes[NUM_SCENES];
    int currentScene = 0;
    int copySourceScene = -1;  // -1 means no copy in progress
    bool deleteMode = false;    // Toggle for delete mode

    // Per-track playback state
    int currentStep[NUM_TRACKS] = {0, 0, 0};
    int pendulumDir[NUM_TRACKS] = {1, 1, 1};  // 1 = forward, -1 = reverse
    float clockPhase[NUM_TRACKS] = {0.f, 0.f, 0.f};

    // Clock detection
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger sceneTriggers[NUM_SCENES];
    dsp::SchmittTrigger copyTrigger;
    dsp::SchmittTrigger deleteTrigger;
    dsp::SchmittTrigger gateTriggers[NUM_TRACKS * NUM_STEPS];

    // Gate pulse generators
    dsp::PulseGenerator gatePulse[NUM_TRACKS];

    // Internal clock state
    float internalClockPhase = 0.f;
    bool isRunning = true;
    dsp::PulseGenerator clockOutputPulse;
    dsp::SchmittTrigger runTrigger;

    // Reset button state
    dsp::SchmittTrigger rstButtonTrigger;
    dsp::PulseGenerator resetOutputPulse;

    // Track if we're in the middle of a gate (for sustain behavior)
    bool gateHigh[NUM_TRACKS] = {false, false, false};
    float lastClockTime[NUM_TRACKS] = {0.f, 0.f, 0.f};

    // Previous param values for change detection
    float prevPitchParams[NUM_TRACKS * NUM_STEPS] = {0.f};

    // Gate button state tracking (for toggle behavior)
    bool gateButtonStates[NUM_TRACKS * NUM_STEPS] = {false};

    // Clock period tracking for pulse width calculation
    float lastClockRiseTime = 0.f;
    float clockPeriod = 0.5f;  // Default to 120 BPM (0.5 sec period)
    float elapsedTime = 0.f;

    // Per-track swing state
    float swingAccumulator[NUM_TRACKS] = {0.f, 0.f, 0.f};
    int stepParity[NUM_TRACKS] = {0, 0, 0};  // Tracks odd/even for swing
    bool pendingSwingGate[NUM_TRACKS] = {false, false, false};  // Gate waiting for swing delay
    int pendingSwingStep[NUM_TRACKS] = {0, 0, 0};  // Which step is pending (for pitch lookup)
    float outputPitch[NUM_TRACKS] = {0.f, 0.f, 0.f};  // Current pitch being output (synced with gate)
    int outputStep[NUM_TRACKS] = {0, 0, 0};  // Current step for LED display (synced with gate)

    // Per-track clock multiplication state
    float trackClockPhase[NUM_TRACKS] = {0.f, 0.f, 0.f};  // Phase within current clock period for multiplication
    int trackSubStep[NUM_TRACKS] = {0, 0, 0};  // Which sub-step we're on (for 2x, 4x, 8x)

    // Debug: track actual values being used
    float debugBpm = 120.f;
    float debugFreq = 2.f;
    bool debugUsingInternal = true;
    int debugClockCount = 0;

    Sequencer() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Configure internal clock controls
        configParam(BPM_PARAM, 30.f, 300.f, 120.f, "BPM");
        configButton(RUN_PARAM, "Run/Stop");
        configButton(RST_PARAM, "Reset");

        // Configure track controls
        // Steps: 1-8
        configParam(TRACK1_STEPS_PARAM, 1.f, 8.f, 8.f, "Track 1 Steps");
        // Division: ÷4, ÷2, 1x, 2x, 4x, 8x (indices 0-5, default 2 = 1x)
        configSwitch(TRACK1_DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Track 1 Division", {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"});
        configSwitch(TRACK1_DIR_PARAM, 0.f, 3.f, 0.f, "Track 1 Direction", {"Forward", "Reverse", "Pendulum", "Random"});

        configParam(TRACK2_STEPS_PARAM, 1.f, 8.f, 8.f, "Track 2 Steps");
        configSwitch(TRACK2_DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Track 2 Division", {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"});
        configSwitch(TRACK2_DIR_PARAM, 0.f, 3.f, 0.f, "Track 2 Direction", {"Forward", "Reverse", "Pendulum", "Random"});

        configParam(TRACK3_STEPS_PARAM, 1.f, 8.f, 8.f, "Track 3 Steps");
        configSwitch(TRACK3_DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Track 3 Division", {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"});
        configSwitch(TRACK3_DIR_PARAM, 0.f, 3.f, 0.f, "Track 3 Direction", {"Forward", "Reverse", "Pendulum", "Random"});

        // Configure pitch encoders (0-5V range, semitone steps would be 1/12V per step)
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                configParam(PITCH_PARAMS + idx, 0.f, 5.f, 0.f,
                    string::f("Track %d Step %d Pitch", t + 1, s + 1), " V");
            }
        }

        // Configure gate buttons
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                configButton(GATE_PARAMS + idx, string::f("Track %d Step %d Gate", t + 1, s + 1));
            }
        }

        // Configure scene buttons
        for (int s = 0; s < NUM_SCENES; s++) {
            configButton(SCENE_PARAMS + s, string::f("Scene %d", s + 1));
        }

        // Configure modifier buttons
        configButton(COPY_PARAM, "Copy Scene");
        configButton(DELETE_PARAM, "Delete Scene");

        // Configure groove controls
        configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", "%");
        configParam(PW_PARAM, 10.f, 90.f, 50.f, "Pulse Width", "%");

        // Configure inputs
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(SCENE_CV_INPUT, "Scene CV");

        // Configure outputs
        configOutput(CLOCK_OUTPUT, "Clock");
        configOutput(RESET_OUTPUT, "Reset");
        configOutput(TRACK1_PITCH_OUTPUT, "Track 1 Pitch CV");
        configOutput(TRACK1_GATE_OUTPUT, "Track 1 Gate");
        configOutput(TRACK2_PITCH_OUTPUT, "Track 2 Pitch CV");
        configOutput(TRACK2_GATE_OUTPUT, "Track 2 Gate");
        configOutput(TRACK3_PITCH_OUTPUT, "Track 3 Pitch CV");
        configOutput(TRACK3_GATE_OUTPUT, "Track 3 Gate");
        configOutput(SCENE_CV_OUTPUT, "Scene CV");

        // Initialize first scene as active
        scenes[0].isEmpty = false;
    }

    void onReset() override {
        for (int i = 0; i < NUM_SCENES; i++) {
            scenes[i] = SceneData();
        }
        scenes[0].isEmpty = false;
        currentScene = 0;
        copySourceScene = -1;
        deleteMode = false;
        for (int t = 0; t < NUM_TRACKS; t++) {
            currentStep[t] = 0;
            pendulumDir[t] = 1;
            clockPhase[t] = 0.f;
            swingAccumulator[t] = 0.f;
            stepParity[t] = 0;
            pendingSwingGate[t] = false;
            pendingSwingStep[t] = 0;
            outputPitch[t] = 0.f;
            outputStep[t] = 0;
            trackClockPhase[t] = 0.f;
            trackSubStep[t] = 0;
        }
        // Reset internal clock state
        isRunning = true;
        internalClockPhase = 0.f;
        elapsedTime = 0.f;
        lastClockRiseTime = 0.f;
    }

    void advanceStep(int track) {
        SceneData& scene = scenes[currentScene];
        TrackData& trackData = scene.tracks[track];
        int steps = trackData.stepCount;

        switch (trackData.direction) {
            case DIR_FORWARD:
                currentStep[track] = (currentStep[track] + 1) % steps;
                break;
            case DIR_REVERSE:
                currentStep[track] = (currentStep[track] - 1 + steps) % steps;
                break;
            case DIR_PENDULUM:
                currentStep[track] += pendulumDir[track];
                if (currentStep[track] >= steps - 1) {
                    currentStep[track] = steps - 1;
                    pendulumDir[track] = -1;
                } else if (currentStep[track] <= 0) {
                    currentStep[track] = 0;
                    pendulumDir[track] = 1;
                }
                break;
            case DIR_RANDOM:
                currentStep[track] = random::u32() % steps;
                break;
        }
    }

    void process(const ProcessArgs& args) override {
        SceneData& scene = scenes[currentScene];

        // Read track control knobs and update scene data
        int stepsParams[NUM_TRACKS] = {TRACK1_STEPS_PARAM, TRACK2_STEPS_PARAM, TRACK3_STEPS_PARAM};
        int divParams[NUM_TRACKS] = {TRACK1_DIV_PARAM, TRACK2_DIV_PARAM, TRACK3_DIV_PARAM};
        int dirParams[NUM_TRACKS] = {TRACK1_DIR_PARAM, TRACK2_DIR_PARAM, TRACK3_DIR_PARAM};

        for (int t = 0; t < NUM_TRACKS; t++) {
            scene.tracks[t].stepCount = (int)params[stepsParams[t]].getValue();
            scene.tracks[t].divisionIndex = (int)params[divParams[t]].getValue();
            scene.tracks[t].direction = (Direction)(int)params[dirParams[t]].getValue();
        }

        // Read pitch encoders and update scene data
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                scene.tracks[t].pitches[s] = params[PITCH_PARAMS + idx].getValue();
            }
        }

        // Handle gate button toggles (proper toggle behavior)
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                bool pressed = params[GATE_PARAMS + idx].getValue() > 0.f;
                if (pressed && !gateButtonStates[idx]) {
                    // Button just pressed - toggle gate state
                    scene.tracks[t].gates[s] = !scene.tracks[t].gates[s];
                }
                gateButtonStates[idx] = pressed;
            }
        }

        // Handle reset (from input or button)
        bool resetFromInput = resetTrigger.process(inputs[RESET_INPUT].getVoltage());
        bool resetFromButton = rstButtonTrigger.process(params[RST_PARAM].getValue() > 0.f);
        if (resetFromInput || resetFromButton) {
            for (int t = 0; t < NUM_TRACKS; t++) {
                currentStep[t] = 0;
                pendulumDir[t] = 1;
                clockPhase[t] = 0.f;
            }
            internalClockPhase = 0.f;
            resetOutputPulse.trigger(0.001f);  // 1ms reset pulse
        }

        // Output reset pulse
        outputs[RESET_OUTPUT].setVoltage(resetOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

        // Handle scene CV input (0-7V maps to scenes 1-8)
        if (inputs[SCENE_CV_INPUT].isConnected()) {
            float sceneCV = inputs[SCENE_CV_INPUT].getVoltage();
            int newScene = clamp((int)sceneCV, 0, NUM_SCENES - 1);
            if (newScene != currentScene && !scenes[newScene].isEmpty) {
                currentScene = newScene;
                loadSceneToParams();
            }
        }

        // Handle scene button presses
        for (int s = 0; s < NUM_SCENES; s++) {
            if (sceneTriggers[s].process(params[SCENE_PARAMS + s].getValue() > 0.f)) {
                if (copySourceScene >= 0) {
                    // Copy mode active - paste to this scene
                    scenes[s] = scenes[copySourceScene];
                    scenes[s].isEmpty = false;
                    copySourceScene = -1;
                    currentScene = s;
                    loadSceneToParams();
                } else if (deleteMode && s != 0) {
                    // Delete mode active (can't delete scene 1)
                    scenes[s] = SceneData();
                    deleteMode = false;  // Exit delete mode after deleting
                    if (currentScene == s) {
                        currentScene = 0;
                        loadSceneToParams();
                    }
                } else {
                    // Normal scene selection - initialize empty scenes with current data
                    if (scenes[s].isEmpty) {
                        // Copy current scene to new scene
                        scenes[s] = scenes[currentScene];
                        scenes[s].isEmpty = false;
                    }
                    currentScene = s;
                    loadSceneToParams();
                }
            }
        }

        // Handle copy button (toggle)
        if (copyTrigger.process(params[COPY_PARAM].getValue() > 0.f)) {
            deleteMode = false;  // Cancel delete mode if active
            if (copySourceScene < 0) {
                copySourceScene = currentScene;
            } else {
                copySourceScene = -1;  // Cancel copy
            }
        }

        // Handle delete button (toggle)
        if (deleteTrigger.process(params[DELETE_PARAM].getValue() > 0.f)) {
            copySourceScene = -1;  // Cancel copy mode if active
            deleteMode = !deleteMode;
        }

        // Handle run/stop button (toggle)
        if (runTrigger.process(params[RUN_PARAM].getValue() > 0.f)) {
            isRunning = !isRunning;
        }

        // Track elapsed time
        elapsedTime += args.sampleTime;

        // Read BPM at the start - this value controls internal clock speed
        float bpm = params[BPM_PARAM].getValue();

        // Read groove params
        float swingAmount = params[SWING_PARAM].getValue() / 100.f;  // 0-1
        float pulseWidth = params[PW_PARAM].getValue() / 100.f;      // 0.1-0.9

        // Determine clock source and generate clock
        bool useInternalClock = !inputs[CLOCK_INPUT].isConnected();
        bool clockRising = false;

        // Always update clock period based on BPM (used for pulse width)
        float clockFreq = bpm / 60.f;
        clockPeriod = 1.f / clockFreq;

        // Debug: store values for display
        debugBpm = bpm;
        debugFreq = clockFreq;
        debugUsingInternal = useInternalClock;

        if (isRunning) {
            if (useInternalClock) {
                // Generate internal clock using BPM
                internalClockPhase += clockFreq * args.sampleTime;
                if (internalClockPhase >= 1.f) {
                    internalClockPhase -= 1.f;
                    clockRising = true;
                    clockOutputPulse.trigger(0.001f);  // 1ms pulse
                    debugClockCount++;  // Debug: count clock pulses
                }
            } else {
                // Use external clock
                clockRising = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
                if (clockRising) {
                    // Calculate clock period from external clock (overrides BPM-based period)
                    float timeSinceLastClock = elapsedTime - lastClockRiseTime;
                    if (timeSinceLastClock > 0.01f && timeSinceLastClock < 4.f) {
                        clockPeriod = timeSinceLastClock;
                    }
                    lastClockRiseTime = elapsedTime;
                    clockOutputPulse.trigger(0.001f);  // Pass through clock
                    debugClockCount++;  // Debug: count clock pulses
                }
            }
        }

        // Output clock
        outputs[CLOCK_OUTPUT].setVoltage(clockOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

        // Process clock for each track
        for (int t = 0; t < NUM_TRACKS; t++) {
            TrackData& trackData = scene.tracks[t];
            float division = DIVISIONS[trackData.divisionIndex];
            bool shouldAdvance = false;

            // Calculate step duration and gate duration based on division
            // Step duration = clockPeriod * division (works for both division and multiplication)
            float stepDuration = clockPeriod * division;
            float gateDuration = stepDuration * pulseWidth;
            gateDuration = clamp(gateDuration, 0.001f, stepDuration * 0.95f);

            if (division >= 1.f) {
                // DIVISION MODE (÷4, ÷2, 1x): accumulate clocks, advance after N clocks
                if (clockRising) {
                    clockPhase[t] += 1.f / division;
                    if (clockPhase[t] >= 1.f) {
                        clockPhase[t] -= 1.f;
                        shouldAdvance = true;
                    }
                }
            } else {
                // MULTIPLICATION MODE (2x, 4x, 8x): generate sub-steps between clocks
                // division < 1 means we need multiple steps per clock
                // e.g., division = 0.5 means 2 steps per clock (2x)
                int stepsPerClock = (int)(1.f / division);  // 2, 4, or 8

                if (clockRising) {
                    // Reset sub-step counter and phase on each clock
                    trackSubStep[t] = 0;
                    trackClockPhase[t] = 0.f;
                    shouldAdvance = true;  // First step happens on the clock
                } else if (isRunning && clockPeriod > 0.f) {
                    // Generate intermediate steps between clocks
                    trackClockPhase[t] += args.sampleTime;
                    float stepInterval = clockPeriod / stepsPerClock;
                    int expectedSubStep = (int)(trackClockPhase[t] / stepInterval);

                    // Clamp to avoid advancing beyond what we should before next clock
                    if (expectedSubStep >= stepsPerClock) {
                        expectedSubStep = stepsPerClock - 1;
                    }

                    // If we've reached a new sub-step, advance
                    if (expectedSubStep > trackSubStep[t]) {
                        trackSubStep[t] = expectedSubStep;
                        shouldAdvance = true;
                    }
                }
            }

            // Advance step and trigger gate if needed
            if (shouldAdvance) {
                advanceStep(t);
                stepParity[t] = (stepParity[t] + 1) % 2;

                // Apply swing: delay off-beat steps (steps 2, 4, 6, 8 when stepParity == 1)
                float swingDelay = 0.f;
                if (stepParity[t] == 1 && swingAmount > 0.f) {
                    // Swing delay is a fraction of the step duration
                    // At 100% swing, delay is 50% of the beat (maximum shuffle)
                    swingDelay = clockPeriod * (division >= 1.f ? division : 1.f) * swingAmount * 0.5f;
                }

                // Trigger gate pulse if this step has gate enabled
                if (trackData.gates[currentStep[t]]) {
                    if (swingDelay > 0.001f) {
                        // Swing: delay gate, pitch, AND LED update
                        swingAccumulator[t] = swingDelay;
                        pendingSwingGate[t] = true;
                        pendingSwingStep[t] = currentStep[t];  // Remember which step to play
                    } else {
                        // No swing: fire immediately, update pitch and LED now
                        gatePulse[t].trigger(gateDuration);
                        outputPitch[t] = trackData.pitches[currentStep[t]];
                        outputStep[t] = currentStep[t];
                    }
                } else {
                    // Gate disabled but still update pitch/LED for non-swung steps
                    if (swingDelay <= 0.001f) {
                        outputPitch[t] = trackData.pitches[currentStep[t]];
                        outputStep[t] = currentStep[t];
                    }
                }
            }

            // Process swing delay (outside shouldAdvance block - runs every sample)
            if (pendingSwingGate[t] && swingAccumulator[t] > 0.f) {
                swingAccumulator[t] -= args.sampleTime;
                if (swingAccumulator[t] <= 0.f) {
                    swingAccumulator[t] = 0.f;
                    // Fire the delayed gate AND update pitch/LED now
                    gatePulse[t].trigger(gateDuration);
                    outputPitch[t] = scene.tracks[t].pitches[pendingSwingStep[t]];
                    outputStep[t] = pendingSwingStep[t];
                    pendingSwingGate[t] = false;
                }
            }

            // If not running, just track gate state
            if (!isRunning) {
                gateHigh[t] = trackData.gates[currentStep[t]];
            }
        }

        // Update gate pulse generators and outputs
        int pitchOutputs[NUM_TRACKS] = {TRACK1_PITCH_OUTPUT, TRACK2_PITCH_OUTPUT, TRACK3_PITCH_OUTPUT};
        int gateOutputs[NUM_TRACKS] = {TRACK1_GATE_OUTPUT, TRACK2_GATE_OUTPUT, TRACK3_GATE_OUTPUT};

        for (int t = 0; t < NUM_TRACKS; t++) {
            TrackData& trackData = scene.tracks[t];

            // Output pitch CV (synced with gate - delayed by swing if active)
            outputs[pitchOutputs[t]].setVoltage(outputPitch[t]);

            // Output gate (use pulse generator if running, otherwise static)
            bool gateOn;
            if (isRunning) {
                gateOn = gatePulse[t].process(args.sampleTime);
            } else {
                gateOn = trackData.gates[currentStep[t]];
            }
            outputs[gateOutputs[t]].setVoltage(gateOn ? 10.f : 0.f);
        }

        // Output scene CV (0-7V)
        outputs[SCENE_CV_OUTPUT].setVoltage((float)currentScene);

        // Update gate LEDs
        for (int t = 0; t < NUM_TRACKS; t++) {
            // Check if gate output is currently high (respects pulse width)
            // Note: gatePulse[t].process() was already called above, so check remaining
            bool gateOutputHigh = gatePulse[t].remaining > 0.f;

            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                // Gate on/off indicator (yellow) - shows programmed gate state
                lights[GATE_LIGHTS + idx].setBrightness(scene.tracks[t].gates[s] ? 1.f : 0.1f);

                // Current step indicator (green) - uses outputStep which syncs with gate/pitch
                // This shows swing (LED moves when note plays) and pulse width (brightness)
                if (outputStep[t] == s) {
                    if (isRunning) {
                        // Show gate output state: bright when gate high, dim when gate low
                        lights[STEP_LIGHTS + idx].setBrightness(gateOutputHigh ? 1.f : 0.3f);
                    } else {
                        // Not running - just show position
                        lights[STEP_LIGHTS + idx].setBrightness(1.f);
                    }
                } else {
                    lights[STEP_LIGHTS + idx].setBrightness(0.f);
                }
            }
        }

        // Update scene LEDs (RGB: red=empty, green=current, blue=has data)
        for (int s = 0; s < NUM_SCENES; s++) {
            bool isCurrent = (s == currentScene);
            bool isEmpty = scenes[s].isEmpty;
            bool isCopySource = (s == copySourceScene);

            // Red: copy source indicator
            lights[SCENE_LIGHTS + s * 3 + 0].setBrightness(isCopySource ? 1.f : 0.f);
            // Green: current scene
            lights[SCENE_LIGHTS + s * 3 + 1].setBrightness(isCurrent ? 1.f : 0.f);
            // Blue: has data
            lights[SCENE_LIGHTS + s * 3 + 2].setBrightness(!isEmpty ? 0.5f : 0.1f);
        }

        // Copy/Delete indicator lights
        lights[COPY_LIGHT].setBrightness(copySourceScene >= 0 ? 1.f : 0.f);
        lights[DELETE_LIGHT].setBrightness(deleteMode ? 1.f : 0.f);

        // Run indicator light
        lights[RUN_LIGHT].setBrightness(isRunning ? 1.f : 0.f);

        // Reset indicator light (brief flash when reset occurs)
        lights[RST_LIGHT].setBrightness(resetOutputPulse.remaining > 0.f ? 1.f : 0.f);
    }

    void loadSceneToParams() {
        SceneData& scene = scenes[currentScene];

        // Load track controls
        params[TRACK1_STEPS_PARAM].setValue(scene.tracks[0].stepCount);
        params[TRACK1_DIV_PARAM].setValue(scene.tracks[0].divisionIndex);
        params[TRACK1_DIR_PARAM].setValue((float)scene.tracks[0].direction);

        params[TRACK2_STEPS_PARAM].setValue(scene.tracks[1].stepCount);
        params[TRACK2_DIV_PARAM].setValue(scene.tracks[1].divisionIndex);
        params[TRACK2_DIR_PARAM].setValue((float)scene.tracks[1].direction);

        params[TRACK3_STEPS_PARAM].setValue(scene.tracks[2].stepCount);
        params[TRACK3_DIV_PARAM].setValue(scene.tracks[2].divisionIndex);
        params[TRACK3_DIR_PARAM].setValue((float)scene.tracks[2].direction);

        // Load pitch values
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                params[PITCH_PARAMS + idx].setValue(scene.tracks[t].pitches[s]);
            }
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();

        // Save current scene
        json_object_set_new(rootJ, "currentScene", json_integer(currentScene));

        // Save running state
        json_object_set_new(rootJ, "isRunning", json_boolean(isRunning));

        // Save all scenes
        json_t* scenesJ = json_array();
        for (int i = 0; i < NUM_SCENES; i++) {
            json_t* sceneJ = json_object();
            json_object_set_new(sceneJ, "isEmpty", json_boolean(scenes[i].isEmpty));

            json_t* tracksJ = json_array();
            for (int t = 0; t < NUM_TRACKS; t++) {
                json_t* trackJ = json_object();
                json_object_set_new(trackJ, "stepCount", json_integer(scenes[i].tracks[t].stepCount));
                json_object_set_new(trackJ, "divisionIndex", json_integer(scenes[i].tracks[t].divisionIndex));
                json_object_set_new(trackJ, "direction", json_integer(scenes[i].tracks[t].direction));

                json_t* pitchesJ = json_array();
                json_t* gatesJ = json_array();
                for (int s = 0; s < NUM_STEPS; s++) {
                    json_array_append_new(pitchesJ, json_real(scenes[i].tracks[t].pitches[s]));
                    json_array_append_new(gatesJ, json_boolean(scenes[i].tracks[t].gates[s]));
                }
                json_object_set_new(trackJ, "pitches", pitchesJ);
                json_object_set_new(trackJ, "gates", gatesJ);

                json_array_append_new(tracksJ, trackJ);
            }
            json_object_set_new(sceneJ, "tracks", tracksJ);
            json_array_append_new(scenesJ, sceneJ);
        }
        json_object_set_new(rootJ, "scenes", scenesJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        // Load current scene
        json_t* currentSceneJ = json_object_get(rootJ, "currentScene");
        if (currentSceneJ) {
            currentScene = json_integer_value(currentSceneJ);
        }

        // Load running state
        json_t* isRunningJ = json_object_get(rootJ, "isRunning");
        if (isRunningJ) {
            isRunning = json_boolean_value(isRunningJ);
        }

        // Load all scenes
        json_t* scenesJ = json_object_get(rootJ, "scenes");
        if (scenesJ) {
            for (int i = 0; i < NUM_SCENES && i < (int)json_array_size(scenesJ); i++) {
                json_t* sceneJ = json_array_get(scenesJ, i);

                json_t* isEmptyJ = json_object_get(sceneJ, "isEmpty");
                if (isEmptyJ) {
                    scenes[i].isEmpty = json_boolean_value(isEmptyJ);
                }

                json_t* tracksJ = json_object_get(sceneJ, "tracks");
                if (tracksJ) {
                    for (int t = 0; t < NUM_TRACKS && t < (int)json_array_size(tracksJ); t++) {
                        json_t* trackJ = json_array_get(tracksJ, t);

                        json_t* stepCountJ = json_object_get(trackJ, "stepCount");
                        if (stepCountJ) scenes[i].tracks[t].stepCount = json_integer_value(stepCountJ);

                        json_t* divisionIndexJ = json_object_get(trackJ, "divisionIndex");
                        if (divisionIndexJ) scenes[i].tracks[t].divisionIndex = json_integer_value(divisionIndexJ);

                        json_t* directionJ = json_object_get(trackJ, "direction");
                        if (directionJ) scenes[i].tracks[t].direction = (Direction)json_integer_value(directionJ);

                        json_t* pitchesJ = json_object_get(trackJ, "pitches");
                        json_t* gatesJ = json_object_get(trackJ, "gates");

                        for (int s = 0; s < NUM_STEPS; s++) {
                            if (pitchesJ && s < (int)json_array_size(pitchesJ)) {
                                scenes[i].tracks[t].pitches[s] = json_real_value(json_array_get(pitchesJ, s));
                            }
                            if (gatesJ && s < (int)json_array_size(gatesJ)) {
                                scenes[i].tracks[t].gates[s] = json_boolean_value(json_array_get(gatesJ, s));
                            }
                        }
                    }
                }
            }
        }

        // Load scene data to params
        loadSceneToParams();
    }
};

// BPM display widget
struct BpmDisplay : Widget {
    Sequencer* module;

    void draw(const DrawArgs& args) override {
        // Draw background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.0);
        nvgFillColor(args.vg, nvgRGB(0, 0, 0));
        nvgFill(args.vg);

        if (module) {
            float displayBpm;
            std::string modeText;
            NVGcolor modeColor;

            if (module->debugUsingInternal) {
                // Internal clock - show BPM from knob
                displayBpm = module->params[Sequencer::BPM_PARAM].getValue();
                modeText = "INT";
                modeColor = nvgRGB(0, 255, 100);
            } else {
                // External clock - calculate BPM from clock period
                displayBpm = 60.f / module->clockPeriod;
                modeText = "EXT";
                modeColor = nvgRGB(100, 150, 255);
            }

            // Main BPM display (large)
            std::string bpmText = string::f("%.0f", displayBpm);
            nvgFontSize(args.vg, 14);
            nvgFillColor(args.vg, nvgRGB(255, 200, 50));  // Amber color
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2 - 4, bpmText.c_str(), NULL);

            // Mode indicator (small, at bottom)
            nvgFontSize(args.vg, 8);
            nvgFillColor(args.vg, modeColor);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
            nvgText(args.vg, box.size.x / 2, box.size.y - 1, modeText.c_str(), NULL);
        } else {
            nvgFontSize(args.vg, 14);
            nvgFillColor(args.vg, nvgRGB(255, 200, 50));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, "120", NULL);
        }
    }
};

struct SequencerWidget : ModuleWidget {
    SequencerWidget(Sequencer* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Sequencer.svg")));

        // Add screws (28 HP module)
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // ========== CLOCK COLUMN (left side, x=10 center) ==========
        // BPM display (x=3-17, y=20-28)
        BpmDisplay* bpmDisplay = new BpmDisplay();
        bpmDisplay->box.pos = mm2px(Vec(3, 20));
        bpmDisplay->box.size = mm2px(Vec(14, 8));
        bpmDisplay->module = module;
        addChild(bpmDisplay);

        // BPM knob (cx=10, cy=35)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(10, 35)), module, Sequencer::BPM_PARAM));

        // RUN button with LED (cx=10, cy=50)
        addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(10, 50)), module, Sequencer::RUN_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(10, 50)), module, Sequencer::RUN_PARAM));

        // CLK IN jack (cx=10, cy=66)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 66)), module, Sequencer::CLOCK_INPUT));

        // RST button with LED (cx=10, cy=84)
        addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(10, 84)), module, Sequencer::RST_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(10, 84)), module, Sequencer::RST_PARAM));

        // RST IN jack (cx=10, cy=100)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 100)), module, Sequencer::RESET_INPUT));

        // ========== TRACK 1 (y=11-38) ==========
        // Vertical track controls at x=26
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 22)), module, Sequencer::TRACK1_STEPS_PARAM));  // STP
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 28)), module, Sequencer::TRACK1_DIV_PARAM));    // DIV
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 34)), module, Sequencer::TRACK1_DIR_PARAM));    // DIR

        // Pitch knobs at y=22, spacing 9mm starting at x=42
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, 22)), module, Sequencer::PITCH_PARAMS + s));
        }
        // Gate buttons at y=31
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(x, 31)), module, Sequencer::GATE_LIGHTS + s));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, 31)), module, Sequencer::GATE_PARAMS + s));
        }
        // Step LEDs at y=36
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x, 36)), module, Sequencer::STEP_LIGHTS + s));
        }
        // Track 1 outputs (cx=127.18, cy=19 and 31)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.18, 19)), module, Sequencer::TRACK1_PITCH_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.18, 31)), module, Sequencer::TRACK1_GATE_OUTPUT));

        // ========== TRACK 2 (y=40-67) ==========
        // Vertical track controls at x=26
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 51)), module, Sequencer::TRACK2_STEPS_PARAM));  // STP
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 57)), module, Sequencer::TRACK2_DIV_PARAM));    // DIV
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 63)), module, Sequencer::TRACK2_DIR_PARAM));    // DIR

        // Pitch knobs at y=51
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, 51)), module, Sequencer::PITCH_PARAMS + NUM_STEPS + s));
        }
        // Gate buttons at y=60
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(x, 60)), module, Sequencer::GATE_LIGHTS + NUM_STEPS + s));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, 60)), module, Sequencer::GATE_PARAMS + NUM_STEPS + s));
        }
        // Step LEDs at y=65
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x, 65)), module, Sequencer::STEP_LIGHTS + NUM_STEPS + s));
        }
        // Track 2 outputs (cx=127.18, cy=48 and 60)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.18, 48)), module, Sequencer::TRACK2_PITCH_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.18, 60)), module, Sequencer::TRACK2_GATE_OUTPUT));

        // ========== TRACK 3 (y=69-96) ==========
        // Vertical track controls at x=26
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 80)), module, Sequencer::TRACK3_STEPS_PARAM));  // STP
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 86)), module, Sequencer::TRACK3_DIV_PARAM));    // DIV
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 92)), module, Sequencer::TRACK3_DIR_PARAM));    // DIR

        // Pitch knobs at y=80
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, 80)), module, Sequencer::PITCH_PARAMS + 2 * NUM_STEPS + s));
        }
        // Gate buttons at y=89
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(x, 89)), module, Sequencer::GATE_LIGHTS + 2 * NUM_STEPS + s));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, 89)), module, Sequencer::GATE_PARAMS + 2 * NUM_STEPS + s));
        }
        // Step LEDs at y=94
        for (int s = 0; s < NUM_STEPS; s++) {
            float x = 42 + s * 9;
            addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x, 94)), module, Sequencer::STEP_LIGHTS + 2 * NUM_STEPS + s));
        }
        // Track 3 outputs (cx=127.18, cy=77 and 89)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.18, 77)), module, Sequencer::TRACK3_PITCH_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.18, 89)), module, Sequencer::TRACK3_GATE_OUTPUT));

        // ========== BOTTOM SECTION (y=98-126) ==========
        // GROOVE section - SWG knob (cx=27, cy=112), PW knob (cx=38, cy=112)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(27, 112)), module, Sequencer::SWING_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(38, 112)), module, Sequencer::PW_PARAM));

        // MOD section - CPY button (cx=50, cy=110), DEL button (cx=50, cy=118)
        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(50, 110)), module, Sequencer::COPY_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(50, 110)), module, Sequencer::COPY_PARAM));

        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(50, 118)), module, Sequencer::DELETE_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(50, 118)), module, Sequencer::DELETE_PARAM));

        // SCV IN jack (cx=64, cy=114)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(64, 114)), module, Sequencer::SCENE_CV_INPUT));

        // SCENES section - 2x4 grid
        // Row 1 (cy=110): scenes 1-4 at cx=74, 82, 90, 98
        // Row 2 (cy=118): scenes 5-8 at cx=74, 82, 90, 98
        float sceneX[4] = {74, 82, 90, 98};
        for (int s = 0; s < NUM_SCENES; s++) {
            int row = s / 4;
            int col = s % 4;
            float x = sceneX[col];
            float y = (row == 0) ? 110 : 118;

            addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(x, y)), module, Sequencer::SCENE_LIGHTS + s * 3));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, y)), module, Sequencer::SCENE_PARAMS + s));
        }

        // OUTPUTS section - CLK OUT (cx=112, cy=111), RST OUT (cx=124, cy=111), SCV OUT (cx=134, cy=111)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(112, 111)), module, Sequencer::CLOCK_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(124, 111)), module, Sequencer::RESET_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(134, 111)), module, Sequencer::SCENE_CV_OUTPUT));
    }
};

Model* modelSequencer = createModel<Sequencer, SequencerWidget>("Sequencer");
