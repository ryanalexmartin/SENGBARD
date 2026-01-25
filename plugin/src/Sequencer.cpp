#include "plugin.hpp"

// Constants
static const int NUM_TRACKS = 3;
static const int NUM_STEPS = 8;
static const int NUM_SCENES = 8;

// Clock division ratios - musical note values (assuming clock = quarter note)
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
    int stepCount = 8;
    int divisionIndex = 2;  // Default 1/4
    Direction direction = DIR_FORWARD;
    float pitches[NUM_STEPS] = {0.f};
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
        RST_PARAM,
        // Track select buttons (radio-style, one active at a time)
        ENUMS(TRACK_SELECT_PARAMS, NUM_TRACKS),
        // Track controls (apply to selected track)
        STEPS_PARAM,
        DIV_PARAM,
        DIR_PARAM,
        // Step pitch encoders (8 shared, edit selected track)
        ENUMS(PITCH_PARAMS, NUM_STEPS),
        // Step gate buttons (24 total: 8 steps × 3 tracks in grid)
        ENUMS(GATE_PARAMS, NUM_TRACKS * NUM_STEPS),
        // Scene buttons
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
        CLOCK_OUTPUT,
        RESET_OUTPUT,
        TRACK1_PITCH_OUTPUT,
        TRACK1_GATE_OUTPUT,
        TRACK2_PITCH_OUTPUT,
        TRACK2_GATE_OUTPUT,
        TRACK3_PITCH_OUTPUT,
        TRACK3_GATE_OUTPUT,
        SCENE_CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        RUN_LIGHT,
        RST_LIGHT,
        // Track select indicator LEDs
        ENUMS(TRACK_SELECT_LIGHTS, NUM_TRACKS),
        // Gate LEDs (24 total: 8 steps × 3 tracks)
        ENUMS(GATE_LIGHTS, NUM_TRACKS * NUM_STEPS),
        // Step position LEDs (24 total: 8 steps × 3 tracks)
        ENUMS(STEP_LIGHTS, NUM_TRACKS * NUM_STEPS),
        // Scene LEDs (RGB)
        ENUMS(SCENE_LIGHTS, NUM_SCENES * 3),
        COPY_LIGHT,
        DELETE_LIGHT,
        LIGHTS_LEN
    };

    // Scene and track state
    SceneData scenes[NUM_SCENES];
    int currentScene = 0;
    int selectedTrack = 0;  // Which track the encoders control (0-2)
    int copySourceScene = -1;
    bool deleteMode = false;

    // Per-track playback state
    int currentStep[NUM_TRACKS] = {0, 0, 0};
    int pendulumDir[NUM_TRACKS] = {1, 1, 1};
    float clockPhase[NUM_TRACKS] = {0.f, 0.f, 0.f};

    // Triggers
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger sceneTriggers[NUM_SCENES];
    dsp::SchmittTrigger trackSelectTriggers[NUM_TRACKS];
    dsp::SchmittTrigger copyTrigger;
    dsp::SchmittTrigger deleteTrigger;
    dsp::SchmittTrigger gateTriggers[NUM_TRACKS * NUM_STEPS];
    dsp::SchmittTrigger runTrigger;
    dsp::SchmittTrigger rstButtonTrigger;

    // Gate pulse generators
    dsp::PulseGenerator gatePulse[NUM_TRACKS];
    dsp::PulseGenerator clockOutputPulse;
    dsp::PulseGenerator resetOutputPulse;

    // Internal clock state
    float internalClockPhase = 0.f;
    bool isRunning = true;

    // Clock period tracking
    float lastClockRiseTime = 0.f;
    float clockPeriod = 0.5f;
    float elapsedTime = 0.f;

    // Swing state
    float swingAccumulator[NUM_TRACKS] = {0.f, 0.f, 0.f};
    int stepParity[NUM_TRACKS] = {0, 0, 0};
    bool pendingSwingGate[NUM_TRACKS] = {false, false, false};
    int pendingSwingStep[NUM_TRACKS] = {0, 0, 0};
    float outputPitch[NUM_TRACKS] = {0.f, 0.f, 0.f};
    int outputStep[NUM_TRACKS] = {0, 0, 0};

    // Clock multiplication state
    float trackClockPhase[NUM_TRACKS] = {0.f, 0.f, 0.f};
    int trackSubStep[NUM_TRACKS] = {0, 0, 0};

    // Gate button state tracking
    bool gateButtonStates[NUM_TRACKS * NUM_STEPS] = {false};

    // Previous encoder values for change detection
    float prevEncoderValues[NUM_STEPS] = {0.f};

    Sequencer() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Clock controls
        configParam(BPM_PARAM, 30.f, 300.f, 120.f, "BPM");
        configButton(RUN_PARAM, "Run/Stop");
        configButton(RST_PARAM, "Reset");

        // Track select buttons
        for (int t = 0; t < NUM_TRACKS; t++) {
            configButton(TRACK_SELECT_PARAMS + t, string::f("Select Track %d", t + 1));
        }

        // Track controls (shared, apply to selected track)
        configParam(STEPS_PARAM, 1.f, 8.f, 8.f, "Steps");
        configSwitch(DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Division",
            {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"});
        configSwitch(DIR_PARAM, 0.f, 3.f, 0.f, "Direction",
            {"Forward", "Reverse", "Pendulum", "Random"});

        // Pitch encoders (8 shared)
        for (int s = 0; s < NUM_STEPS; s++) {
            configParam(PITCH_PARAMS + s, 0.f, 5.f, 0.f, string::f("Step %d Pitch", s + 1), " V");
        }

        // Gate buttons (24 total: 8 steps × 3 tracks)
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                configButton(GATE_PARAMS + t * NUM_STEPS + s,
                    string::f("Track %d Step %d Gate", t + 1, s + 1));
            }
        }

        // Scene buttons
        for (int s = 0; s < NUM_SCENES; s++) {
            configButton(SCENE_PARAMS + s, string::f("Scene %d", s + 1));
        }

        // Modifier buttons
        configButton(COPY_PARAM, "Copy Scene");
        configButton(DELETE_PARAM, "Delete Scene");

        // Groove controls
        configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", "%");
        configParam(PW_PARAM, 10.f, 90.f, 50.f, "Pulse Width", "%");

        // Inputs
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(SCENE_CV_INPUT, "Scene CV");

        // Outputs
        configOutput(CLOCK_OUTPUT, "Clock");
        configOutput(RESET_OUTPUT, "Reset");
        configOutput(TRACK1_PITCH_OUTPUT, "Track 1 Pitch");
        configOutput(TRACK1_GATE_OUTPUT, "Track 1 Gate");
        configOutput(TRACK2_PITCH_OUTPUT, "Track 2 Pitch");
        configOutput(TRACK2_GATE_OUTPUT, "Track 2 Gate");
        configOutput(TRACK3_PITCH_OUTPUT, "Track 3 Pitch");
        configOutput(TRACK3_GATE_OUTPUT, "Track 3 Gate");
        configOutput(SCENE_CV_OUTPUT, "Scene CV");

        // Initialize first scene
        scenes[0].isEmpty = false;
    }

    void onReset() override {
        for (int i = 0; i < NUM_SCENES; i++) {
            scenes[i] = SceneData();
        }
        scenes[0].isEmpty = false;
        currentScene = 0;
        selectedTrack = 0;
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
        isRunning = true;
        internalClockPhase = 0.f;
        elapsedTime = 0.f;
        lastClockRiseTime = 0.f;
        loadTrackToEncoders();
    }

    void loadTrackToEncoders() {
        // Load selected track's pitches into encoder params
        SceneData& scene = scenes[currentScene];
        for (int s = 0; s < NUM_STEPS; s++) {
            params[PITCH_PARAMS + s].setValue(scene.tracks[selectedTrack].pitches[s]);
            prevEncoderValues[s] = scene.tracks[selectedTrack].pitches[s];
        }
        // Load track controls
        params[STEPS_PARAM].setValue(scene.tracks[selectedTrack].stepCount);
        params[DIV_PARAM].setValue(scene.tracks[selectedTrack].divisionIndex);
        params[DIR_PARAM].setValue((float)scene.tracks[selectedTrack].direction);
    }

    void saveEncodersToTrack() {
        // Save encoder values to selected track's pitches
        SceneData& scene = scenes[currentScene];
        for (int s = 0; s < NUM_STEPS; s++) {
            scene.tracks[selectedTrack].pitches[s] = params[PITCH_PARAMS + s].getValue();
        }
        // Save track controls
        scene.tracks[selectedTrack].stepCount = (int)params[STEPS_PARAM].getValue();
        scene.tracks[selectedTrack].divisionIndex = (int)params[DIV_PARAM].getValue();
        scene.tracks[selectedTrack].direction = (Direction)(int)params[DIR_PARAM].getValue();
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

        // Handle track select buttons (radio-style)
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (trackSelectTriggers[t].process(params[TRACK_SELECT_PARAMS + t].getValue() > 0.f)) {
                if (t != selectedTrack) {
                    saveEncodersToTrack();  // Save current track
                    selectedTrack = t;
                    loadTrackToEncoders();  // Load new track
                }
            }
        }

        // Check for encoder changes and save to current track
        for (int s = 0; s < NUM_STEPS; s++) {
            float val = params[PITCH_PARAMS + s].getValue();
            if (val != prevEncoderValues[s]) {
                scene.tracks[selectedTrack].pitches[s] = val;
                prevEncoderValues[s] = val;
            }
        }

        // Save track control changes to current track
        scene.tracks[selectedTrack].stepCount = (int)params[STEPS_PARAM].getValue();
        scene.tracks[selectedTrack].divisionIndex = (int)params[DIV_PARAM].getValue();
        scene.tracks[selectedTrack].direction = (Direction)(int)params[DIR_PARAM].getValue();

        // Handle gate button toggles
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                bool pressed = params[GATE_PARAMS + idx].getValue() > 0.f;
                if (pressed && !gateButtonStates[idx]) {
                    scene.tracks[t].gates[s] = !scene.tracks[t].gates[s];
                }
                gateButtonStates[idx] = pressed;
            }
        }

        // Handle reset
        bool resetFromInput = resetTrigger.process(inputs[RESET_INPUT].getVoltage());
        bool resetFromButton = rstButtonTrigger.process(params[RST_PARAM].getValue() > 0.f);
        if (resetFromInput || resetFromButton) {
            for (int t = 0; t < NUM_TRACKS; t++) {
                currentStep[t] = 0;
                pendulumDir[t] = 1;
                clockPhase[t] = 0.f;
            }
            internalClockPhase = 0.f;
            resetOutputPulse.trigger(0.001f);
        }
        outputs[RESET_OUTPUT].setVoltage(resetOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

        // Handle scene CV input
        if (inputs[SCENE_CV_INPUT].isConnected()) {
            float sceneCV = inputs[SCENE_CV_INPUT].getVoltage();
            int newScene = clamp((int)sceneCV, 0, NUM_SCENES - 1);
            if (newScene != currentScene && !scenes[newScene].isEmpty) {
                saveEncodersToTrack();
                currentScene = newScene;
                loadTrackToEncoders();
            }
        }

        // Handle scene buttons
        for (int s = 0; s < NUM_SCENES; s++) {
            if (sceneTriggers[s].process(params[SCENE_PARAMS + s].getValue() > 0.f)) {
                if (copySourceScene >= 0) {
                    scenes[s] = scenes[copySourceScene];
                    scenes[s].isEmpty = false;
                    copySourceScene = -1;
                    saveEncodersToTrack();
                    currentScene = s;
                    loadTrackToEncoders();
                } else if (deleteMode && s != 0) {
                    scenes[s] = SceneData();
                    deleteMode = false;
                    if (currentScene == s) {
                        currentScene = 0;
                        loadTrackToEncoders();
                    }
                } else {
                    if (scenes[s].isEmpty) {
                        scenes[s] = scenes[currentScene];
                        scenes[s].isEmpty = false;
                    }
                    saveEncodersToTrack();
                    currentScene = s;
                    loadTrackToEncoders();
                }
            }
        }

        // Copy button
        if (copyTrigger.process(params[COPY_PARAM].getValue() > 0.f)) {
            deleteMode = false;
            copySourceScene = (copySourceScene < 0) ? currentScene : -1;
        }

        // Delete button
        if (deleteTrigger.process(params[DELETE_PARAM].getValue() > 0.f)) {
            copySourceScene = -1;
            deleteMode = !deleteMode;
        }

        // Run/stop button
        if (runTrigger.process(params[RUN_PARAM].getValue() > 0.f)) {
            isRunning = !isRunning;
        }

        // Track elapsed time
        elapsedTime += args.sampleTime;

        // Get groove params
        float swingAmount = params[SWING_PARAM].getValue() / 100.f;
        float pulseWidth = params[PW_PARAM].getValue() / 100.f;

        // Clock generation
        float bpm = params[BPM_PARAM].getValue();
        bool useInternalClock = !inputs[CLOCK_INPUT].isConnected();
        bool clockRising = false;

        float clockFreq = bpm / 60.f;
        clockPeriod = 1.f / clockFreq;

        if (isRunning) {
            if (useInternalClock) {
                internalClockPhase += clockFreq * args.sampleTime;
                if (internalClockPhase >= 1.f) {
                    internalClockPhase -= 1.f;
                    clockRising = true;
                    clockOutputPulse.trigger(0.001f);
                }
            } else {
                clockRising = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
                if (clockRising) {
                    float timeSinceLastClock = elapsedTime - lastClockRiseTime;
                    if (timeSinceLastClock > 0.01f && timeSinceLastClock < 4.f) {
                        clockPeriod = timeSinceLastClock;
                    }
                    lastClockRiseTime = elapsedTime;
                    clockOutputPulse.trigger(0.001f);
                }
            }
        }
        outputs[CLOCK_OUTPUT].setVoltage(clockOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

        // Process each track
        for (int t = 0; t < NUM_TRACKS; t++) {
            TrackData& trackData = scene.tracks[t];
            float division = DIVISIONS[trackData.divisionIndex];
            bool shouldAdvance = false;

            float stepDuration = clockPeriod * division;
            float gateDuration = stepDuration * pulseWidth;
            gateDuration = clamp(gateDuration, 0.001f, stepDuration * 0.95f);

            if (division >= 1.f) {
                if (clockRising) {
                    clockPhase[t] += 1.f / division;
                    if (clockPhase[t] >= 1.f) {
                        clockPhase[t] -= 1.f;
                        shouldAdvance = true;
                    }
                }
            } else {
                int stepsPerClock = (int)(1.f / division);
                if (clockRising) {
                    trackSubStep[t] = 0;
                    trackClockPhase[t] = 0.f;
                    shouldAdvance = true;
                } else if (isRunning && clockPeriod > 0.f) {
                    trackClockPhase[t] += args.sampleTime;
                    float stepInterval = clockPeriod / stepsPerClock;
                    int expectedSubStep = (int)(trackClockPhase[t] / stepInterval);
                    if (expectedSubStep >= stepsPerClock) {
                        expectedSubStep = stepsPerClock - 1;
                    }
                    if (expectedSubStep > trackSubStep[t]) {
                        trackSubStep[t] = expectedSubStep;
                        shouldAdvance = true;
                    }
                }
            }

            if (shouldAdvance) {
                advanceStep(t);
                stepParity[t] = (stepParity[t] + 1) % 2;

                float swingDelay = 0.f;
                if (stepParity[t] == 1 && swingAmount > 0.f) {
                    swingDelay = clockPeriod * (division >= 1.f ? division : 1.f) * swingAmount * 0.5f;
                }

                if (trackData.gates[currentStep[t]]) {
                    if (swingDelay > 0.001f) {
                        swingAccumulator[t] = swingDelay;
                        pendingSwingGate[t] = true;
                        pendingSwingStep[t] = currentStep[t];
                    } else {
                        gatePulse[t].trigger(gateDuration);
                        outputPitch[t] = trackData.pitches[currentStep[t]];
                        outputStep[t] = currentStep[t];
                    }
                } else {
                    if (swingDelay <= 0.001f) {
                        outputPitch[t] = trackData.pitches[currentStep[t]];
                        outputStep[t] = currentStep[t];
                    }
                }
            }

            if (pendingSwingGate[t] && swingAccumulator[t] > 0.f) {
                swingAccumulator[t] -= args.sampleTime;
                if (swingAccumulator[t] <= 0.f) {
                    swingAccumulator[t] = 0.f;
                    gatePulse[t].trigger(gateDuration);
                    outputPitch[t] = scene.tracks[t].pitches[pendingSwingStep[t]];
                    outputStep[t] = pendingSwingStep[t];
                    pendingSwingGate[t] = false;
                }
            }
        }

        // Outputs
        int pitchOutputs[NUM_TRACKS] = {TRACK1_PITCH_OUTPUT, TRACK2_PITCH_OUTPUT, TRACK3_PITCH_OUTPUT};
        int gateOutputs[NUM_TRACKS] = {TRACK1_GATE_OUTPUT, TRACK2_GATE_OUTPUT, TRACK3_GATE_OUTPUT};

        for (int t = 0; t < NUM_TRACKS; t++) {
            outputs[pitchOutputs[t]].setVoltage(outputPitch[t]);
            bool gateOn = isRunning ? gatePulse[t].process(args.sampleTime) : scene.tracks[t].gates[currentStep[t]];
            outputs[gateOutputs[t]].setVoltage(gateOn ? 10.f : 0.f);
        }
        outputs[SCENE_CV_OUTPUT].setVoltage((float)currentScene);

        // Update LEDs
        // Track select LEDs
        for (int t = 0; t < NUM_TRACKS; t++) {
            lights[TRACK_SELECT_LIGHTS + t].setBrightness(t == selectedTrack ? 1.f : 0.2f);
        }

        // Gate and step LEDs
        for (int t = 0; t < NUM_TRACKS; t++) {
            bool gateOutputHigh = gatePulse[t].remaining > 0.f;
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                lights[GATE_LIGHTS + idx].setBrightness(scene.tracks[t].gates[s] ? 1.f : 0.1f);
                if (outputStep[t] == s) {
                    lights[STEP_LIGHTS + idx].setBrightness(isRunning ? (gateOutputHigh ? 1.f : 0.3f) : 1.f);
                } else {
                    lights[STEP_LIGHTS + idx].setBrightness(0.f);
                }
            }
        }

        // Scene LEDs
        for (int s = 0; s < NUM_SCENES; s++) {
            bool isCurrent = (s == currentScene);
            bool isEmpty = scenes[s].isEmpty;
            bool isCopySource = (s == copySourceScene);
            lights[SCENE_LIGHTS + s * 3 + 0].setBrightness(isCopySource ? 1.f : 0.f);
            lights[SCENE_LIGHTS + s * 3 + 1].setBrightness(isCurrent ? 1.f : 0.f);
            lights[SCENE_LIGHTS + s * 3 + 2].setBrightness(!isEmpty ? 0.5f : 0.1f);
        }

        lights[COPY_LIGHT].setBrightness(copySourceScene >= 0 ? 1.f : 0.f);
        lights[DELETE_LIGHT].setBrightness(deleteMode ? 1.f : 0.f);
        lights[RUN_LIGHT].setBrightness(isRunning ? 1.f : 0.f);
        lights[RST_LIGHT].setBrightness(resetOutputPulse.remaining > 0.f ? 1.f : 0.f);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "currentScene", json_integer(currentScene));
        json_object_set_new(rootJ, "selectedTrack", json_integer(selectedTrack));
        json_object_set_new(rootJ, "isRunning", json_boolean(isRunning));

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
        json_t* currentSceneJ = json_object_get(rootJ, "currentScene");
        if (currentSceneJ) currentScene = json_integer_value(currentSceneJ);

        json_t* selectedTrackJ = json_object_get(rootJ, "selectedTrack");
        if (selectedTrackJ) selectedTrack = json_integer_value(selectedTrackJ);

        json_t* isRunningJ = json_object_get(rootJ, "isRunning");
        if (isRunningJ) isRunning = json_boolean_value(isRunningJ);

        json_t* scenesJ = json_object_get(rootJ, "scenes");
        if (scenesJ) {
            for (int i = 0; i < NUM_SCENES && i < (int)json_array_size(scenesJ); i++) {
                json_t* sceneJ = json_array_get(scenesJ, i);
                json_t* isEmptyJ = json_object_get(sceneJ, "isEmpty");
                if (isEmptyJ) scenes[i].isEmpty = json_boolean_value(isEmptyJ);

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
                            if (pitchesJ && s < (int)json_array_size(pitchesJ))
                                scenes[i].tracks[t].pitches[s] = json_real_value(json_array_get(pitchesJ, s));
                            if (gatesJ && s < (int)json_array_size(gatesJ))
                                scenes[i].tracks[t].gates[s] = json_boolean_value(json_array_get(gatesJ, s));
                        }
                    }
                }
            }
        }
        loadTrackToEncoders();
    }
};

// BPM display widget
struct BpmDisplay : Widget {
    Sequencer* module;

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.0);
        nvgFillColor(args.vg, nvgRGB(0, 0, 0));
        nvgFill(args.vg);

        if (module) {
            float bpm = module->params[Sequencer::BPM_PARAM].getValue();
            bool isInternal = !module->inputs[Sequencer::CLOCK_INPUT].isConnected();
            if (!isInternal && module->clockPeriod > 0.f) {
                bpm = 60.f / module->clockPeriod;
            }

            nvgFontSize(args.vg, 14);
            nvgFillColor(args.vg, nvgRGB(255, 200, 50));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2 - 3, string::f("%.0f", bpm).c_str(), NULL);

            nvgFontSize(args.vg, 8);
            nvgFillColor(args.vg, isInternal ? nvgRGB(0, 255, 100) : nvgRGB(100, 150, 255));
            nvgText(args.vg, box.size.x / 2, box.size.y / 2 + 8, isInternal ? "INT" : "EXT", NULL);
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

        // Screws (20 HP)
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // ========== LEFT COLUMN: CLOCK (x=10) ==========
        // Compact layout with 10mm spacing
        float leftX = 10;

        // BPM display (y=16-26)
        BpmDisplay* bpmDisplay = new BpmDisplay();
        bpmDisplay->box.pos = mm2px(Vec(3, 16));
        bpmDisplay->box.size = mm2px(Vec(14, 10));
        bpmDisplay->module = module;
        addChild(bpmDisplay);

        // BPM knob (y=32)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(leftX, 32)), module, Sequencer::BPM_PARAM));

        // RUN button + LED (y=42)
        addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(leftX, 42)), module, Sequencer::RUN_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(leftX, 42)), module, Sequencer::RUN_PARAM));

        // CLK IN (y=52)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(leftX, 52)), module, Sequencer::CLOCK_INPUT));

        // RST button + LED (y=62)
        addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(leftX, 62)), module, Sequencer::RST_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(leftX, 62)), module, Sequencer::RST_PARAM));

        // RST IN (y=72)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(leftX, 72)), module, Sequencer::RESET_INPUT));

        // SCV IN (y=82)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(leftX, 82)), module, Sequencer::SCENE_CV_INPUT));

        // ========== TOP ROW: TRACK SELECT + CONTROLS (y=14) ==========
        // Track select buttons: T1, T2, T3 (x=28, 38, 48)
        for (int t = 0; t < NUM_TRACKS; t++) {
            float x = 28 + t * 10;
            addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(x, 14)), module, Sequencer::TRACK_SELECT_LIGHTS + t));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, 14)), module, Sequencer::TRACK_SELECT_PARAMS + t));
        }

        // Track controls: STP, DIV, DIR (x=62, 72, 82)
        addParam(createParamCentered<Trimpot>(mm2px(Vec(62, 14)), module, Sequencer::STEPS_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(72, 14)), module, Sequencer::DIV_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(82, 14)), module, Sequencer::DIR_PARAM));

        // ========== STEP GRID (y=24 to y=87, 9mm spacing) ==========
        float stepStartY = 24;
        float stepSpacing = 9;

        for (int s = 0; s < NUM_STEPS; s++) {
            float y = stepStartY + s * stepSpacing;

            // Pitch encoder (x=28)
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(28, y)), module, Sequencer::PITCH_PARAMS + s));

            // Gate buttons for T1, T2, T3 (x=40, 50, 60)
            for (int t = 0; t < NUM_TRACKS; t++) {
                float x = 40 + t * 10;
                int idx = t * NUM_STEPS + s;
                addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(x, y)), module, Sequencer::GATE_LIGHTS + idx));
                addParam(createParamCentered<LEDButton>(mm2px(Vec(x, y)), module, Sequencer::GATE_PARAMS + idx));
            }

            // Step indicator LEDs for T1, T2, T3 (x=72, 77, 82)
            for (int t = 0; t < NUM_TRACKS; t++) {
                float x = 72 + t * 5;
                int idx = t * NUM_STEPS + s;
                addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x, y)), module, Sequencer::STEP_LIGHTS + idx));
            }
        }

        // ========== RIGHT COLUMN: OUTPUTS (x=93, 10mm spacing) ==========
        float outX = 93;
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 22)), module, Sequencer::TRACK1_PITCH_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 32)), module, Sequencer::TRACK1_GATE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 42)), module, Sequencer::TRACK2_PITCH_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 52)), module, Sequencer::TRACK2_GATE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 62)), module, Sequencer::TRACK3_PITCH_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 72)), module, Sequencer::TRACK3_GATE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 82)), module, Sequencer::CLOCK_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 92)), module, Sequencer::RESET_OUTPUT));

        // ========== BOTTOM SECTION (y=97-120) ==========
        // Groove: SWG, PW (x=26, 36, y=106)
        addParam(createParamCentered<Trimpot>(mm2px(Vec(26, 106)), module, Sequencer::SWING_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(36, 106)), module, Sequencer::PW_PARAM));

        // Scene buttons (2x4 grid, starting x=46, y=102/110)
        for (int s = 0; s < NUM_SCENES; s++) {
            int row = s / 4;
            int col = s % 4;
            float x = 46 + col * 7;
            float y = 102 + row * 8;
            addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(x, y)), module, Sequencer::SCENE_LIGHTS + s * 3));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, y)), module, Sequencer::SCENE_PARAMS + s));
        }

        // MOD: CPY, DEL (x=78, y=102/110)
        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(78, 102)), module, Sequencer::COPY_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(78, 102)), module, Sequencer::COPY_PARAM));
        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(78, 110)), module, Sequencer::DELETE_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(78, 110)), module, Sequencer::DELETE_PARAM));

        // SCV OUT (x=93, y=106)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outX, 106)), module, Sequencer::SCENE_CV_OUTPUT));
    }
};

Model* modelSequencer = createModel<Sequencer, SequencerWidget>("Sequencer");
