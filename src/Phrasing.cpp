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
		// ---- GLOBALS ----
		DENSITY_PARAM,          // controls base GAP time (higher = more frequent event attempts)
		GAP_JITTER_PARAM,       // adds randomness to gap time
		DURATION_JITTER_PARAM,  // adds randomness to lane ON duration
		GUARANTEE_ONE_PARAM,    // if ON, force at least one lane to be HIGH

		// per-lane enable (pushbutton -> toggled in code)
		LANE1_ACTIVE_PARAM,
		LANE2_ACTIVE_PARAM,
		LANE3_ACTIVE_PARAM,
		LANE4_ACTIVE_PARAM,

		// per-lane WEIGHT (bias/probability on each event attempt)
		WEIGHT1_PARAM,
		WEIGHT2_PARAM,
		WEIGHT3_PARAM,
		WEIGHT4_PARAM,

		// per-lane DURATION (how long lane stays high when it fires)
		LANEDUR1_PARAM,
		LANEDUR2_PARAM,
		LANEDUR3_PARAM,
		LANEDUR4_PARAM,

		// per-lane floor (minimum level)
		FLOOR1_PARAM,
		FLOOR2_PARAM,
		FLOOR3_PARAM,
		FLOOR4_PARAM,

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
		LANE1_LIGHT,   // shows lane enabled + activity
		LANE2_LIGHT,
		LANE3_LIGHT,
		LANE4_LIGHT,
		LIGHTS_LEN
	};

	// --- State ---
	float laneTarget[4]   = {0.f, 0.f, 0.f, 0.f}; // 0 or 1
	float laneValue[4]    = {0.f, 0.f, 0.f, 0.f}; // smoothed 0..1
	float laneOnTimer[4]  = {0.f, 0.f, 0.f, 0.f}; // seconds remaining HIGH
	float gapTimer        = 0.f;                  // seconds until next event attempt

	bool initialized = false;

	// --- Pushbutton-to-toggle latch state for lane enables ---
	dsp::SchmittTrigger laneBtnTrig[4];
	bool laneEnabled[4] = {true, true, true, true};
	bool laneEnabledInit = false;

	Phrasing() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// Globals
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.7f, "Density", "%", 0.f, 100.f);
		configParam(GAP_JITTER_PARAM, 0.f, 1.f, 0.25f, "Gap Jitter", "%", 0.f, 100.f);
		configParam(DURATION_JITTER_PARAM, 0.f, 1.f, 0.25f, "Duration Jitter", "%", 0.f, 100.f);

		// Optional “always one lane playing” rule (default OFF)
		configSwitch(GUARANTEE_ONE_PARAM, 0.f, 1.f, 0.f, "Guarantee one lane", {"Off", "On"});

		// Lane enable buttons (momentary in UI, toggled in code)
		configButton(LANE1_ACTIVE_PARAM, "Lane 1 Enable");
		configButton(LANE2_ACTIVE_PARAM, "Lane 2 Enable");
		configButton(LANE3_ACTIVE_PARAM, "Lane 3 Enable");
		configButton(LANE4_ACTIVE_PARAM, "Lane 4 Enable");

		// Per-lane weight (bias)
		configParam(WEIGHT1_PARAM, 0.f, 1.f, 0.8f, "Weight I", "%", 0.f, 100.f);
		configParam(WEIGHT2_PARAM, 0.f, 1.f, 0.8f, "Weight II", "%", 0.f, 100.f);
		configParam(WEIGHT3_PARAM, 0.f, 1.f, 0.8f, "Weight III", "%", 0.f, 100.f);
		configParam(WEIGHT4_PARAM, 0.f, 1.f, 0.8f, "Weight IV", "%", 0.f, 100.f);

		// Per-lane duration
		configParam(LANEDUR1_PARAM, 0.f, 1.f, 0.5f, "Duration I");
		configParam(LANEDUR2_PARAM, 0.f, 1.f, 0.5f, "Duration II");
		configParam(LANEDUR3_PARAM, 0.f, 1.f, 0.5f, "Duration III");
		configParam(LANEDUR4_PARAM, 0.f, 1.f, 0.5f, "Duration IV");

		// Per-lane floor
		configParam(FLOOR1_PARAM, 0.f, 1.f, 0.0f, "Floor I", "%", 0.f, 100.f);
		configParam(FLOOR2_PARAM, 0.f, 1.f, 0.0f, "Floor II", "%", 0.f, 100.f);
		configParam(FLOOR3_PARAM, 0.f, 1.f, 0.0f, "Floor III", "%", 0.f, 100.f);
		configParam(FLOOR4_PARAM, 0.f, 1.f, 0.0f, "Floor IV", "%", 0.f, 100.f);

		configOutput(OUT1_OUTPUT, "Lane CV I");
		configOutput(OUT2_OUTPUT, "Lane CV II");
		configOutput(OUT3_OUTPUT, "Lane CV III");
		configOutput(OUT4_OUTPUT, "Lane CV IV");

		configLight(LANE1_LIGHT, "Lane 1");
		configLight(LANE2_LIGHT, "Lane 2");
		configLight(LANE3_LIGHT, "Lane 3");
		configLight(LANE4_LIGHT, "Lane 4");
	}

	// Map 0..1 -> seconds (exponential mapping)
	/*
	•	Fully CCW → 5 seconds
	•	9 o’clock (~0.25) → ~9 seconds
	•	Noon (~0.5) → ~21 seconds
	•	3 o’clock (~0.75) → ~47 seconds
	•	Fully CW → 90 seconds
	*/
	float knobToSeconds(float d) {
		const float minS = 5.0f;
		const float maxS = 90.0f;
		return minS * std::pow(maxS / minS, d);
	}

	// Density controls *gap* between event attempts.
	// Higher density -> shorter gap (inverted exponential).
	float densityToGapSeconds(float density) {
		const float minGap = 5.0f;
		const float maxGap = 90.0f;
		const float x = clamp(density, 0.f, 1.f);
		return minGap * std::pow(maxGap / minGap, 1.f - x);
	}

	// returns multiplier in [1-amt, 1+amt]
	float jitterMul(float amt) {
		amt = clamp(amt, 0.f, 1.f);
		const float u = random::uniform(); // 0..1
		return (1.f - amt) + (2.f * amt) * u;
	}

	void process(const ProcessArgs& args) override {
		const float sr = args.sampleRate;
		const float dt = args.sampleTime;

		// Globals
		const float density      = clamp(params[DENSITY_PARAM].getValue(), 0.f, 1.f);
		const float gapJitter    = clamp(params[GAP_JITTER_PARAM].getValue(), 0.f, 1.f);
		const float durJitter    = clamp(params[DURATION_JITTER_PARAM].getValue(), 0.f, 1.f);
		const bool guaranteeOne  = params[GUARANTEE_ONE_PARAM].getValue() > 0.5f;

		// Per-lane params
		const float laneWeight[4] = {
			clamp(params[WEIGHT1_PARAM].getValue(), 0.f, 1.f),
			clamp(params[WEIGHT2_PARAM].getValue(), 0.f, 1.f),
			clamp(params[WEIGHT3_PARAM].getValue(), 0.f, 1.f),
			clamp(params[WEIGHT4_PARAM].getValue(), 0.f, 1.f)
		};

		const float laneDurKnob[4] = {
			clamp(params[LANEDUR1_PARAM].getValue(), 0.f, 1.f),
			clamp(params[LANEDUR2_PARAM].getValue(), 0.f, 1.f),
			clamp(params[LANEDUR3_PARAM].getValue(), 0.f, 1.f),
			clamp(params[LANEDUR4_PARAM].getValue(), 0.f, 1.f)
		};

		const float laneFloor[4] = {
			clamp(params[FLOOR1_PARAM].getValue(), 0.f, 1.f),
			clamp(params[FLOOR2_PARAM].getValue(), 0.f, 1.f),
			clamp(params[FLOOR3_PARAM].getValue(), 0.f, 1.f),
			clamp(params[FLOOR4_PARAM].getValue(), 0.f, 1.f)
		};

		// Initialize lane enables once (all enabled by default)
		if (!laneEnabledInit) {
			laneEnabledInit = true;
			for (int i = 0; i < 4; i++) {
				laneEnabled[i] = true;
				laneOnTimer[i] = 0.f;
				laneTarget[i]  = 0.f;
				laneValue[i]   = 0.f;
			}
		}

		// Pushbutton edge -> toggle enabled state
		for (int i = 0; i < 4; i++) {
			const float v = params[LANE1_ACTIVE_PARAM + i].getValue();
			if (laneBtnTrig[i].process(v)) {
				laneEnabled[i] = !laneEnabled[i];
				// If turning off, hard clear
				if (!laneEnabled[i]) {
					laneOnTimer[i] = 0.f;
					laneTarget[i]  = 0.f;
				}
			}
		}

		const bool laneActive[4] = { laneEnabled[0], laneEnabled[1], laneEnabled[2], laneEnabled[3] };

		// --- Init timers ---
		if (!initialized) {
			initialized = true;
			const float baseGap0 = densityToGapSeconds(density);
			gapTimer = random::uniform() * baseGap0; // stagger start
			for (int i = 0; i < 4; i++) {
				laneOnTimer[i] = 0.f;
				laneTarget[i]  = 0.f;
				laneValue[i]   = 0.f;
			}
		}

		// --- Tick lane ON timers -> targets ---
		for (int i = 0; i < 4; i++) {
			if (!laneActive[i]) {
				laneOnTimer[i] = 0.f;
				laneTarget[i] = 0.f;
				continue;
			}

			if (laneOnTimer[i] > 0.f) {
				laneOnTimer[i] -= dt;
				if (laneOnTimer[i] <= 0.f) {
					laneOnTimer[i] = 0.f;
					laneTarget[i] = 0.f;
				} else {
					laneTarget[i] = 1.f;
				}
			} else {
				laneTarget[i] = 0.f;
			}
		}

		// --- Gap timer: when it hits, attempt events on EACH lane (independent rolls) ---
		// Gap jitter range: up to ±30% at full CW
		const float gapJitAmt = 0.30f * gapJitter;

		// Duration jitter range: up to ±50% at full CW
		const float durJitAmt = 0.50f * durJitter;

		const float baseGap = densityToGapSeconds(density);

		gapTimer -= dt;
		if (gapTimer <= 0.f) {
			// For each active lane:
			// - if currently OFF, roll < weight -> turn ON for laneDuration * durationJitter
			// - if currently ON, do NOT retrigger (prevents "kept winning so it stayed high forever")
			for (int i = 0; i < 4; i++) {
				if (!laneActive[i]) continue;
				if (laneOnTimer[i] > 0.f) continue; // no retrigger while ON

				const float w = laneWeight[i];
				if (w <= 0.f) continue;

				if (random::uniform() < w) {
					const float baseDur = knobToSeconds(laneDurKnob[i]);
					const float durMul = jitterMul(durJitAmt);
					laneOnTimer[i] = std::max(0.f, baseDur * durMul);
					laneTarget[i] = 1.f;
				}
			}

			// reset gap timer
			const float gapMul = jitterMul(gapJitAmt);
			gapTimer = std::max(0.01f, baseGap * gapMul);
		}

		// --- Optional guarantee: if enabled and all active lanes are OFF, force one ON ---
		if (guaranteeOne) {
			bool anyActive = false;
			bool anyOn = false;
			for (int i = 0; i < 4; i++) {
				if (laneActive[i]) anyActive = true;
				if (laneActive[i] && laneOnTimer[i] > 0.f) anyOn = true;
			}

			if (anyActive && !anyOn) {
				// pick best by weight (or first active if all zero)
				int best = -1;
				float bestW = -1.f;

				float sumW = 0.f;
				for (int i = 0; i < 4; i++) if (laneActive[i]) sumW += laneWeight[i];

				if (sumW <= 1e-6f) {
					for (int i = 0; i < 4; i++) { if (laneActive[i]) { best = i; break; } }
				} else {
					for (int i = 0; i < 4; i++) {
						if (!laneActive[i]) continue;
						if (laneWeight[i] > bestW) { bestW = laneWeight[i]; best = i; }
					}
				}

				if (best >= 0) {
					const float baseDur = knobToSeconds(laneDurKnob[best]);
					const float durMul = jitterMul(durJitAmt);
					laneOnTimer[best] = std::max(0.f, baseDur * durMul);
					laneTarget[best] = 1.f;
				}
			}
		}

		// ---- Slew toward targets (per-lane based on that lane's Duration knob) ----
		auto onePoleCoeff = [&](float timeSec) {
			if (timeSec <= 0.f) return 0.f;
			return std::exp(-1.f / (timeSec * sr));
		};

		for (int i = 0; i < 4; i++) {
			const float laneBaseDur = knobToSeconds(laneDurKnob[i]);

			const float attackSec  = clamp(laneBaseDur * 0.10f, 0.030f, 2.0f);
			const float releaseSec = clamp(laneBaseDur * 0.40f, 0.300f, 25.0f);

			const float aCoeff = onePoleCoeff(attackSec);
			const float rCoeff = onePoleCoeff(releaseSec);

			const float target = laneActive[i] ? laneTarget[i] : 0.f;
			const float cur = laneValue[i];
			const float coeff = (target > cur) ? aCoeff : rCoeff;
			laneValue[i] = target + (cur - target) * coeff;
			laneValue[i] = clamp(laneValue[i], 0.f, 1.f);
		}

		// Output:
		// - If lane disabled: 0V
		// - If lane enabled: floor..5V (floor sets minimum, laneValue rides above it)
		const float maxV = 5.f;

		for (int i = 0; i < 4; i++) {
			float v = 0.f;
			if (laneActive[i]) {
				const float f = clamp(laneFloor[i], 0.f, 1.f);
				const float shaped = f + (1.f - f) * laneValue[i]; // 0..1 -> f..1
				v = maxV * shaped;
			}
			outputs[OUT1_OUTPUT + i].setVoltage(v);
		}

		// Lights:
		// - Disabled: off
		// - Enabled but quiet: dim glow
		// - Enabled and active: follows laneValue
		for (int i = 0; i < 4; i++) {
			const float base = laneActive[i] ? 0.15f : 0.f;
			const float activity = laneActive[i] ? laneValue[i] : 0.f;
			lights[LANE1_LIGHT + i].setBrightness(std::max(base, activity));
		}
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

		// ---- GLOBAL CONTROLS TOP ----
		// Density, Gap Jitter, Duration Jitter (left -> right)
		const float densityX    = 33.f;
		const float gapJitterX  = 73.f;
		const float durJitterX  = 113.f;
		const float guaranteeX  = 143.f;  // toggle at far right
		const float globalY     = 58.f;

		// ---- LANES ----
		const float lane1X = 22.f;
		const float lane2X = 56.f;
		const float lane3X = 90.f;
		const float lane4X = 124.f;

		// Enable buttons row
		const float enY = 105.f;

		// Lights
		const float lightY = 122.f;

		// Weight knobs
		const float weightY = 150.f;

		// Per-lane Duration knobs
		const float laneDurY = 190.f;

		// Floor knobs
		const float floorY = 230.f;

		// Outputs
		const float outY = 270.f;

		// Global knobs
		addParam(createParamCentered<RoundBlackKnob>(Vec(densityX, globalY), module, Phrasing::DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(gapJitterX, globalY), module, Phrasing::GAP_JITTER_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(durJitterX, globalY), module, Phrasing::DURATION_JITTER_PARAM));

		// Guarantee toggle
		addParam(createParamCentered<CKSS>(Vec(guaranteeX, globalY), module, Phrasing::GUARANTEE_ONE_PARAM));

		// Lane enable buttons
		addParam(createParamCentered<TL1105>(Vec(lane1X, enY), module, Phrasing::LANE1_ACTIVE_PARAM));
		addParam(createParamCentered<TL1105>(Vec(lane2X, enY), module, Phrasing::LANE2_ACTIVE_PARAM));
		addParam(createParamCentered<TL1105>(Vec(lane3X, enY), module, Phrasing::LANE3_ACTIVE_PARAM));
		addParam(createParamCentered<TL1105>(Vec(lane4X, enY), module, Phrasing::LANE4_ACTIVE_PARAM));

		// Lane lights
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane1X, lightY), module, Phrasing::LANE1_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane2X, lightY), module, Phrasing::LANE2_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane3X, lightY), module, Phrasing::LANE3_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane4X, lightY), module, Phrasing::LANE4_LIGHT));

		// Weight knobs
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, weightY), module, Phrasing::WEIGHT1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, weightY), module, Phrasing::WEIGHT2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, weightY), module, Phrasing::WEIGHT3_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, weightY), module, Phrasing::WEIGHT4_PARAM));

		// Per-lane Duration knobs
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, laneDurY), module, Phrasing::LANEDUR1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, laneDurY), module, Phrasing::LANEDUR2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, laneDurY), module, Phrasing::LANEDUR3_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, laneDurY), module, Phrasing::LANEDUR4_PARAM));

		// Floor knobs
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, floorY), module, Phrasing::FLOOR1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, floorY), module, Phrasing::FLOOR2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, floorY), module, Phrasing::FLOOR3_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, floorY), module, Phrasing::FLOOR4_PARAM));

		// Outputs
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane1X, outY), module, Phrasing::OUT1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane2X, outY), module, Phrasing::OUT2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane3X, outY), module, Phrasing::OUT3_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane4X, outY), module, Phrasing::OUT4_OUTPUT));
	}
};

Model* modelPhrasing = createModel<Phrasing, PhrasingWidget>("phrasing");