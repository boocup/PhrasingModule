#include "plugin.hpp"
#include <cmath>
#include <algorithm>

using namespace rack;
using namespace rack::componentlibrary;

// =======================
//   MODULE DEFINITION
// =======================

struct Phrasing : Module {
	enum ParamId {
		DENSITY_PARAM,
		DURATION_PARAM,   // controls time between target changes AND fade times

		// per-lane enable (pushbutton -> toggled in code)
		LANE1_ACTIVE_PARAM,
		LANE2_ACTIVE_PARAM,
		LANE3_ACTIVE_PARAM,
		LANE4_ACTIVE_PARAM,

		PRESENCE1_PARAM,
		PRESENCE2_PARAM,
		PRESENCE3_PARAM,
		PRESENCE4_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		INPUTS_LEN
	};

	enum OutputId {
		OUT1_OUTPUT,
		OUT2_OUTPUT,
		OUT3_OUTPUT,
		OUT4_OUTPUT,
		OUTPUTS_LEN
	};

	enum LightId {
		LANE1_LIGHT,   // shows lane *enabled* (on/off)
		LANE2_LIGHT,
		LANE3_LIGHT,
		LANE4_LIGHT,
		LIGHTS_LEN
	};

	// --- State for probabilistic presence + smoothing ---
	float laneTarget[4]  = {0.f, 0.f, 0.f, 0.f}; // 0 or 1
	float laneValue[4]   = {0.f, 0.f, 0.f, 0.f}; // smoothed 0..1
	float laneTimer[4]   = {0.f, 0.f, 0.f, 0.f}; // seconds until next re-roll
	bool initialized = false;

	// --- Anti-streak: prevent "stuck high for minutes" ---
	float laneHighTime[4] = {0.f, 0.f, 0.f, 0.f}; // seconds current target has been high

	// --- Pushbutton-to-toggle latch state for lane enables ---
	dsp::SchmittTrigger laneBtnTrig[4];
	bool laneEnabled[4] = {true, true, true, true};
	bool laneEnabledInit = false;

	// Track OFF->ON transitions so re-enabled lanes wake up immediately
	bool prevLaneActive[4] = {true, true, true, true};

	Phrasing() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(DENSITY_PARAM, 0.f, 1.f, 0.7f, "Density", "%", 0.f, 100.f);
		configParam(DURATION_PARAM, 0.f, 1.f, 0.5f, "Duration");

		// These are momentary buttons in the UI (TL1105), but we toggle them in code.
		configButton(LANE1_ACTIVE_PARAM, "Lane 1 Enable");
		configButton(LANE2_ACTIVE_PARAM, "Lane 2 Enable");
		configButton(LANE3_ACTIVE_PARAM, "Lane 3 Enable");
		configButton(LANE4_ACTIVE_PARAM, "Lane 4 Enable");

		configParam(PRESENCE1_PARAM, 0.f, 1.f, 0.8f, "Presence I", "%", 0.f, 100.f);
		configParam(PRESENCE2_PARAM, 0.f, 1.f, 0.8f, "Presence II", "%", 0.f, 100.f);
		configParam(PRESENCE3_PARAM, 0.f, 1.f, 0.8f, "Presence III", "%", 0.f, 100.f);
		configParam(PRESENCE4_PARAM, 0.f, 1.f, 0.8f, "Presence IV", "%", 0.f, 100.f);

		configOutput(OUT1_OUTPUT, "Presence CV I");
		configOutput(OUT2_OUTPUT, "Presence CV II");
		configOutput(OUT3_OUTPUT, "Presence CV III");
		configOutput(OUT4_OUTPUT, "Presence CV IV");

		configLight(LANE1_LIGHT, "Lane 1 Enabled");
		configLight(LANE2_LIGHT, "Lane 2 Enabled");
		configLight(LANE3_LIGHT, "Lane 3 Enabled");
		configLight(LANE4_LIGHT, "Lane 4 Enabled");
	}

	// Map 0..1 -> seconds (exponential mapping)
	float durationToSeconds(float d) {
		const float minS = 0.5f;
		const float maxS = 60.0f;
		return minS * std::pow(maxS / minS, d);
	}

	void process(const ProcessArgs& args) override {
		const float sr = args.sampleRate;
		const float dt = args.sampleTime;

		const float density  = clamp(params[DENSITY_PARAM].getValue(), 0.f, 1.f);
		const float duration = clamp(params[DURATION_PARAM].getValue(), 0.f, 1.f);

		const float laneKnob[4] = {
			clamp(params[PRESENCE1_PARAM].getValue(), 0.f, 1.f),
			clamp(params[PRESENCE2_PARAM].getValue(), 0.f, 1.f),
			clamp(params[PRESENCE3_PARAM].getValue(), 0.f, 1.f),
			clamp(params[PRESENCE4_PARAM].getValue(), 0.f, 1.f)
		};

		// Initialize lane enables once (all enabled by default)
		if (!laneEnabledInit) {
			laneEnabledInit = true;
			for (int i = 0; i < 4; i++) {
				laneEnabled[i] = true;
				prevLaneActive[i] = true;
				laneHighTime[i] = 0.f;
			}
		}

		// Pushbutton edge -> toggle enabled state
		for (int i = 0; i < 4; i++) {
			const float v = params[LANE1_ACTIVE_PARAM + i].getValue();
			if (laneBtnTrig[i].process(v)) {
				laneEnabled[i] = !laneEnabled[i];
			}
		}

		const bool laneActive[4] = { laneEnabled[0], laneEnabled[1], laneEnabled[2], laneEnabled[3] };

		auto computeProb = [&](float p) {
			float prob = std::sqrt(density * p);
			return clamp(prob, 0.f, 1.f);
		};

		const float baseInterval = durationToSeconds(duration);

		// If a lane was OFF and is now ON, wake it up immediately
		for (int i = 0; i < 4; i++) {
			if (!prevLaneActive[i] && laneActive[i]) {
				laneTimer[i] = 0.f; // force reroll this sample
				laneHighTime[i] = 0.f;

				const float p = laneKnob[i];
				const float prob = computeProb(p);
				if (density > 0.95f && p > 0.95f) {
					laneTarget[i] = 1.f;
				} else {
					laneTarget[i] = (random::uniform() < prob) ? 1.f : 0.f;
				}
			}

			// If a lane was ON and is now OFF, clear its anti-streak timer
			if (prevLaneActive[i] && !laneActive[i]) {
				laneHighTime[i] = 0.f;
			}

			prevLaneActive[i] = laneActive[i];
		}

		// --- Initialize once ---
		if (!initialized) {
			initialized = true;

			for (int i = 0; i < 4; i++) {
				laneTimer[i] = random::uniform() * baseInterval;

				if (!laneActive[i]) {
					laneTarget[i] = 0.f;
					laneValue[i]  = 0.f;
					laneTimer[i]  = 0.f;
					laneHighTime[i] = 0.f;
					continue;
				}

				const float prob = computeProb(laneKnob[i]);
				laneTarget[i] = (random::uniform() < prob) ? 1.f : 0.f;
				laneValue[i]  = laneTarget[i];
				laneHighTime[i] = (laneTarget[i] > 0.5f) ? 0.f : 0.f;
			}

			// Ensure at least one active lane starts high
			int bestLane = -1;
			float bestScore = -1.f;
			bool anyActive = false;
			bool anyHigh = false;

			for (int i = 0; i < 4; i++) {
				if (!laneActive[i]) continue;
				anyActive = true;
				if (laneTarget[i] > 0.5f) anyHigh = true;

				const float score = computeProb(laneKnob[i]);
				if (score > bestScore) {
					bestScore = score;
					bestLane = i;
				}
			}

			if (anyActive && !anyHigh && bestLane >= 0) {
				laneTarget[bestLane] = 1.f;
				laneValue[bestLane]  = 1.f;
				laneHighTime[bestLane] = 0.f;
			}
		}

		// --- Track how long each lane has been HIGH (anti-streak) ---
		for (int i = 0; i < 4; i++) {
			if (laneActive[i] && laneTarget[i] > 0.5f) {
				laneHighTime[i] += dt;
			} else {
				laneHighTime[i] = 0.f;
			}
		}

		// If a lane has been high too long, force the next reroll to happen immediately
		// and bias it to drop. This prevents "stuck high for minutes".
		const float maxHighTime = baseInterval * 2.0f;  // tune: 2 phrases max
		for (int i = 0; i < 4; i++) {
			if (laneActive[i] && laneHighTime[i] > maxHighTime) {
				laneTimer[i] = 0.f; // force reroll now
			}
		}

		// --- Re-roll targets when timer expires ---
		bool anyRolledThisSample = false;

		for (int i = 0; i < 4; i++) {
			if (!laneActive[i]) {
				laneTarget[i] = 0.f;
				laneTimer[i]  = 0.f;
				continue;
			}

			laneTimer[i] -= dt;
			if (laneTimer[i] <= 0.f) {
				anyRolledThisSample = true;

				const float p = laneKnob[i];
				const float prob = computeProb(p);

				// If it has been high too long, force a drop
				if (laneHighTime[i] > maxHighTime) {
					laneTarget[i] = 0.f;
					laneHighTime[i] = 0.f;
				}
				// Otherwise normal behavior
				else if (density > 0.95f && p > 0.95f) {
					laneTarget[i] = 1.f;
				} else {
					laneTarget[i] = (random::uniform() < prob) ? 1.f : 0.f;
				}

				const float jitter = 0.85f + 0.30f * random::uniform();
				laneTimer[i] = baseInterval * jitter;
			}
		}

		// --- Guarantee: keep at least one target high among active lanes ---
		if (anyRolledThisSample) {
			bool anyActive = false;
			bool anyHigh = false;

			int bestLane = -1;
			float bestScore = -1.f;

			for (int i = 0; i < 4; i++) {
				if (!laneActive[i]) continue;

				anyActive = true;
				if (laneTarget[i] > 0.5f) anyHigh = true;

				const float score = computeProb(laneKnob[i]);
				if (score > bestScore) {
					bestScore = score;
					bestLane = i;
				}
			}

			if (anyActive && !anyHigh && bestLane >= 0) {
				laneTarget[bestLane] = 1.f;
				laneHighTime[bestLane] = 0.f;
			}
		}

		// ---- Slew toward targets ----
		const float attackSec  = clamp(baseInterval * 0.10f, 0.030f, 2.0f);
		const float releaseSec = clamp(baseInterval * 0.40f, 0.300f, 25.0f);

		auto onePoleCoeff = [&](float timeSec) {
			if (timeSec <= 0.f) return 0.f;
			return std::exp(-1.f / (timeSec * sr));
		};

		const float aCoeff = onePoleCoeff(attackSec);
		const float rCoeff = onePoleCoeff(releaseSec);

		for (int i = 0; i < 4; i++) {
			const float target = laneActive[i] ? laneTarget[i] : 0.f;
			const float cur = laneValue[i];
			const float coeff = (target > cur) ? aCoeff : rCoeff;
			laneValue[i] = target + (cur - target) * coeff;
			laneValue[i] = clamp(laneValue[i], 0.f, 1.f);
		}

		// Output 0..5V
		const float maxV = 5.f;
		outputs[OUT1_OUTPUT].setVoltage(maxV * laneValue[0]);
		outputs[OUT2_OUTPUT].setVoltage(maxV * laneValue[1]);
		outputs[OUT3_OUTPUT].setVoltage(maxV * laneValue[2]);
		outputs[OUT4_OUTPUT].setVoltage(maxV * laneValue[3]);

		// Lights show lane enabled (on/off)
		lights[LANE1_LIGHT].setBrightness(laneActive[0] ? 1.f : 0.f);
		lights[LANE2_LIGHT].setBrightness(laneActive[1] ? 1.f : 0.f);
		lights[LANE3_LIGHT].setBrightness(laneActive[2] ? 1.f : 0.f);
		lights[LANE4_LIGHT].setBrightness(laneActive[3] ? 1.f : 0.f);
	}
};

// =======================
//   WIDGET LAYOUT
// =======================

struct PhrasingWidget : ModuleWidget {
	PhrasingWidget(Phrasing* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Phrasing.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// SVG coords (no mm2px)

		// ---- GLOBAL CONTROLS TOP ----
		// Put Density and Duration up top, side-by-side
		const float densityX = 50.f;
		const float durationX = 100.f;
		const float globalY  = 58.f;

		// ---- LANES ----
		const float lane1X = 22.f;
		const float lane2X = 56.f;
		const float lane3X = 90.f;
		const float lane4X = 124.f;

		// Enable buttons row
		const float enY = 105.f;

		// Lights
		const float lightY = 122.f;

		// Presence knobs
		const float presY = 150.f;

		// Outputs
		const float outY = 195.f;

		// Global knobs
		addParam(createParamCentered<RoundBlackKnob>(Vec(densityX, globalY), module, Phrasing::DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(durationX, globalY), module, Phrasing::DURATION_PARAM));

		// Lane enable buttons
		addParam(createParamCentered<TL1105>(Vec(lane1X, enY), module, Phrasing::LANE1_ACTIVE_PARAM));
		addParam(createParamCentered<TL1105>(Vec(lane2X, enY), module, Phrasing::LANE2_ACTIVE_PARAM));
		addParam(createParamCentered<TL1105>(Vec(lane3X, enY), module, Phrasing::LANE3_ACTIVE_PARAM));
		addParam(createParamCentered<TL1105>(Vec(lane4X, enY), module, Phrasing::LANE4_ACTIVE_PARAM));

		// Bigger/brighter lane enabled lights
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane1X, lightY), module, Phrasing::LANE1_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane2X, lightY), module, Phrasing::LANE2_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane3X, lightY), module, Phrasing::LANE3_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane4X, lightY), module, Phrasing::LANE4_LIGHT));

		// Presence knobs
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, presY), module, Phrasing::PRESENCE1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, presY), module, Phrasing::PRESENCE2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, presY), module, Phrasing::PRESENCE3_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, presY), module, Phrasing::PRESENCE4_PARAM));

		// Outputs
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane1X, outY), module, Phrasing::OUT1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane2X, outY), module, Phrasing::OUT2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane3X, outY), module, Phrasing::OUT3_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane4X, outY), module, Phrasing::OUT4_OUTPUT));
	}
};

Model* modelPhrasing = createModel<Phrasing, PhrasingWidget>("phrasing");