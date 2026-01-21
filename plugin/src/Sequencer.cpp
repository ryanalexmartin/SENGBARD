#include "plugin.hpp"

// Constants
static const int NUM_TRACKS = 3;
static const int NUM_STEPS = 8;
static const int NUM_SCENES = 8;

// Clock division ratios
static const float DIVISIONS[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
static const int NUM_DIVISIONS = 6;

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

    // Track if we're in the middle of a gate (for sustain behavior)
    bool gateHigh[NUM_TRACKS] = {false, false, false};
    float lastClockTime[NUM_TRACKS] = {0.f, 0.f, 0.f};

    // Previous param values for change detection
    float prevPitchParams[NUM_TRACKS * NUM_STEPS] = {0.f};

    // Gate button state tracking (for toggle behavior)
    bool gateButtonStates[NUM_TRACKS * NUM_STEPS] = {false};

    Sequencer() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Configure internal clock controls
        configParam(BPM_PARAM, 30.f, 300.f, 120.f, "BPM");
        configButton(RUN_PARAM, "Run/Stop");

        // Configure track controls
        configParam(TRACK1_STEPS_PARAM, 1.f, 8.f, 8.f, "Track 1 Steps");
        configParam(TRACK1_DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Track 1 Division");
        configSwitch(TRACK1_DIR_PARAM, 0.f, 3.f, 0.f, "Track 1 Direction", {"Forward", "Reverse", "Pendulum", "Random"});

        configParam(TRACK2_STEPS_PARAM, 1.f, 8.f, 8.f, "Track 2 Steps");
        configParam(TRACK2_DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Track 2 Division");
        configSwitch(TRACK2_DIR_PARAM, 0.f, 3.f, 0.f, "Track 2 Direction", {"Forward", "Reverse", "Pendulum", "Random"});

        configParam(TRACK3_STEPS_PARAM, 1.f, 8.f, 8.f, "Track 3 Steps");
        configParam(TRACK3_DIV_PARAM, 0.f, NUM_DIVISIONS - 1, 2.f, "Track 3 Division");
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

        // Configure inputs
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(SCENE_CV_INPUT, "Scene CV");

        // Configure outputs
        configOutput(CLOCK_OUTPUT, "Clock");
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
        }
        // Reset internal clock state
        isRunning = true;
        internalClockPhase = 0.f;
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

        // Handle reset input
        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
            for (int t = 0; t < NUM_TRACKS; t++) {
                currentStep[t] = 0;
                pendulumDir[t] = 1;
                clockPhase[t] = 0.f;
            }
        }

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

        // Determine clock source and generate clock
        bool useInternalClock = !inputs[CLOCK_INPUT].isConnected();
        bool clockRising = false;

        if (isRunning) {
            if (useInternalClock) {
                // Generate internal clock
                float bpm = params[BPM_PARAM].getValue();
                float freq = bpm / 60.f;
                internalClockPhase += freq * args.sampleTime;
                if (internalClockPhase >= 1.f) {
                    internalClockPhase -= 1.f;
                    clockRising = true;
                    clockOutputPulse.trigger(0.001f);  // 1ms pulse
                }
            } else {
                // Use external clock
                clockRising = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
                if (clockRising) {
                    clockOutputPulse.trigger(0.001f);  // Pass through clock
                }
            }
        }

        // Output clock
        outputs[CLOCK_OUTPUT].setVoltage(clockOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

        // Process clock for each track
        for (int t = 0; t < NUM_TRACKS; t++) {
            TrackData& trackData = scene.tracks[t];
            float division = DIVISIONS[trackData.divisionIndex];

            // Handle clock
            if (clockRising) {
                clockPhase[t] += 1.f / division;
                if (clockPhase[t] >= 1.f) {
                    clockPhase[t] -= 1.f;
                    advanceStep(t);
                    // Trigger gate pulse if this step has gate enabled
                    if (trackData.gates[currentStep[t]]) {
                        gatePulse[t].trigger(0.01f);  // 10ms gate pulse
                    }
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

            // Output pitch CV for current step
            outputs[pitchOutputs[t]].setVoltage(trackData.pitches[currentStep[t]]);

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
            for (int s = 0; s < NUM_STEPS; s++) {
                int idx = t * NUM_STEPS + s;
                // Gate on/off indicator (yellow)
                lights[GATE_LIGHTS + idx].setBrightness(scene.tracks[t].gates[s] ? 1.f : 0.1f);
                // Current step indicator (green)
                lights[STEP_LIGHTS + idx].setBrightness(currentStep[t] == s ? 1.f : 0.f);
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

        // Draw BPM text
        std::string text = "120";
        if (module) {
            float bpm = module->params[Sequencer::BPM_PARAM].getValue();
            text = string::f("%.0f", bpm);
        }
        nvgFontSize(args.vg, 12);
        nvgFillColor(args.vg, nvgRGB(255, 200, 50));  // Amber color
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), NULL);
    }
};

struct SequencerWidget : ModuleWidget {
    SequencerWidget(Sequencer* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Sequencer.svg")));

        // Add screws (24 HP module)
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout constants for 24HP (121.92mm wide) panel
        const float trackHeight = 30.f;   // Spacing between track rows
        const float trackStartY = 26.f;   // First track Y position (center of track area)
        const float controlX = 9.f;       // X position for STEPS knob
        const float stepStartX = 38.f;    // X position for first step encoder
        const float stepSpacing = 8.f;    // Spacing between step encoders
        const float outputX = 112.f;      // X position for outputs

        // Track controls and step grid for each track
        for (int t = 0; t < NUM_TRACKS; t++) {
            float trackY = trackStartY + t * trackHeight;

            // STEPS knob
            int stepsParam = (t == 0) ? Sequencer::TRACK1_STEPS_PARAM :
                            (t == 1) ? Sequencer::TRACK2_STEPS_PARAM : Sequencer::TRACK3_STEPS_PARAM;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(controlX, trackY)), module, stepsParam));

            // DIV knob
            int divParam = (t == 0) ? Sequencer::TRACK1_DIV_PARAM :
                          (t == 1) ? Sequencer::TRACK2_DIV_PARAM : Sequencer::TRACK3_DIV_PARAM;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(controlX + 10, trackY)), module, divParam));

            // DIR switch (snap knob)
            int dirParam = (t == 0) ? Sequencer::TRACK1_DIR_PARAM :
                          (t == 1) ? Sequencer::TRACK2_DIR_PARAM : Sequencer::TRACK3_DIR_PARAM;
            addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(controlX + 20, trackY)), module, dirParam));

            // Step pitch encoders and gate buttons (8 steps)
            for (int s = 0; s < NUM_STEPS; s++) {
                float stepX = stepStartX + s * stepSpacing;
                int idx = t * NUM_STEPS + s;

                // Pitch encoder (top)
                addParam(createParamCentered<Trimpot>(mm2px(Vec(stepX, trackY - 5)), module, Sequencer::PITCH_PARAMS + idx));

                // Gate button with LED (bottom)
                addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(stepX, trackY + 5)), module, Sequencer::GATE_LIGHTS + idx));
                addParam(createParamCentered<LEDButton>(mm2px(Vec(stepX, trackY + 5)), module, Sequencer::GATE_PARAMS + idx));

                // Step position LED (green dot below gate)
                addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(stepX, trackY + 10)), module, Sequencer::STEP_LIGHTS + idx));
            }

            // Track outputs
            int pitchOut = (t == 0) ? Sequencer::TRACK1_PITCH_OUTPUT :
                          (t == 1) ? Sequencer::TRACK2_PITCH_OUTPUT : Sequencer::TRACK3_PITCH_OUTPUT;
            int gateOut = (t == 0) ? Sequencer::TRACK1_GATE_OUTPUT :
                         (t == 1) ? Sequencer::TRACK2_GATE_OUTPUT : Sequencer::TRACK3_GATE_OUTPUT;

            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outputX, trackY - 5)), module, pitchOut));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outputX, trackY + 5)), module, gateOut));
        }

        // Bottom section
        float bottomY = 115.f;

        // Internal clock controls (left side, compact vertical stack)
        // BPM display (small LCD-style)
        BpmDisplay* bpmDisplay = new BpmDisplay();
        bpmDisplay->box.pos = mm2px(Vec(3, 97));
        bpmDisplay->box.size = mm2px(Vec(12, 5));
        bpmDisplay->module = module;
        addChild(bpmDisplay);

        // BPM knob (to the right of display)
        addParam(createParamCentered<Trimpot>(mm2px(Vec(19, 99.5f)), module, Sequencer::BPM_PARAM));

        // Run button with LED
        addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(9, 107)), module, Sequencer::RUN_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(9, 107)), module, Sequencer::RUN_PARAM));

        // Clock I/O and Reset (bottom row, left side)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(6, bottomY)), module, Sequencer::CLOCK_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15, bottomY)), module, Sequencer::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24, bottomY)), module, Sequencer::RESET_INPUT));

        // Scene buttons (center, 2x4 grid)
        float sceneStartX = 35.f;
        float sceneSpacingX = 10.f;
        float sceneSpacingY = 8.f;
        for (int s = 0; s < NUM_SCENES; s++) {
            int row = s / 4;
            int col = s % 4;
            float x = sceneStartX + col * sceneSpacingX;
            float y = bottomY - 4 + row * sceneSpacingY;

            addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(x, y)), module, Sequencer::SCENE_LIGHTS + s * 3));
            addParam(createParamCentered<LEDButton>(mm2px(Vec(x, y)), module, Sequencer::SCENE_PARAMS + s));
        }

        // Copy/Delete buttons
        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(78, bottomY - 4)), module, Sequencer::COPY_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(78, bottomY - 4)), module, Sequencer::COPY_PARAM));

        addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(78, bottomY + 4)), module, Sequencer::DELETE_LIGHT));
        addParam(createParamCentered<LEDButton>(mm2px(Vec(78, bottomY + 4)), module, Sequencer::DELETE_PARAM));

        // Scene CV I/O (right side)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(100, bottomY)), module, Sequencer::SCENE_CV_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(112, bottomY)), module, Sequencer::SCENE_CV_OUTPUT));
    }
};

Model* modelSequencer = createModel<Sequencer, SequencerWidget>("Sequencer");
