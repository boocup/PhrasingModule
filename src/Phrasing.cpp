#include "plugin.hpp"

using namespace rack;
using namespace rack::componentlibrary;

// =======================
//   MODULE DEFINITION
// =======================

struct Phrasing : Module {
	enum ParamId {
		DENSITY_PARAM,

		RANGE_SWITCH_PARAM,   // 0=5V, 1=10V
		CURVE_SWITCH_PARAM,   // 0=Linear, 1=Shaped

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
		LIGHTS_LEN
	};

	Phrasing() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// Global density (0..1). "How much overall presence is allowed"
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.7f, "Density", "%", 0.f, 100.f);

		// Global switches (0/1)
		configSwitch(RANGE_SWITCH_PARAM, 0.f, 1.f, 1.f, "Range", { "5V", "10V" });
		configSwitch(CURVE_SWITCH_PARAM, 0.f, 1.f, 0.f, "Curve", { "Linear", "Shaped" });

		// Per-lane presence (0..1). "How much you hear it"
		configParam(PRESENCE1_PARAM, 0.f, 1.f, 0.8f, "Presence I", "%", 0.f, 100.f);
		configParam(PRESENCE2_PARAM, 0.f, 1.f, 0.8f, "Presence II", "%", 0.f, 100.f);
		configParam(PRESENCE3_PARAM, 0.f, 1.f, 0.8f, "Presence III", "%", 0.f, 100.f);
		configParam(PRESENCE4_PARAM, 0.f, 1.f, 0.8f, "Presence IV", "%", 0.f, 100.f);

		configOutput(OUT1_OUTPUT, "Presence CV I");
		configOutput(OUT2_OUTPUT, "Presence CV II");
		configOutput(OUT3_OUTPUT, "Presence CV III");
		configOutput(OUT4_OUTPUT, "Presence CV IV");
	}

	void process(const ProcessArgs& args) override {
		(void)args;

		const float density = clamp(params[DENSITY_PARAM].getValue(), 0.f, 1.f);

		// Read switches (0 or 1)
		const bool range10V = (params[RANGE_SWITCH_PARAM].getValue() > 0.5f);
		const bool shaped   = (params[CURVE_SWITCH_PARAM].getValue() > 0.5f);

		// Not used yet (placeholders for later behavior)
		(void)range10V;
		(void)shaped;

		const float p1 = clamp(params[PRESENCE1_PARAM].getValue(), 0.f, 1.f);
		const float p2 = clamp(params[PRESENCE2_PARAM].getValue(), 0.f, 1.f);
		const float p3 = clamp(params[PRESENCE3_PARAM].getValue(), 0.f, 1.f);
		const float p4 = clamp(params[PRESENCE4_PARAM].getValue(), 0.f, 1.f);

		// v1 behavior: fixed 0..10V (we'll switch this later using range10V)
		outputs[OUT1_OUTPUT].setVoltage(10.f * clamp(density * p1, 0.f, 1.f));
		outputs[OUT2_OUTPUT].setVoltage(10.f * clamp(density * p2, 0.f, 1.f));
		outputs[OUT3_OUTPUT].setVoltage(10.f * clamp(density * p3, 0.f, 1.f));
		outputs[OUT4_OUTPUT].setVoltage(10.f * clamp(density * p4, 0.f, 1.f));
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

		// SVG is authored in panel pixel coordinates — do NOT mm2px()

		// Global density knob centered under logo (SVG coords)
		const float globalX = 75.f;
		const float globalY = 58.f;

		// Two switches flanking density (SVG coords)
		const float leftSwitchX  = 45.f;
		const float rightSwitchX = 105.f;
		const float switchY      = 58.f;

		// Four lane centers (SVG coords)
		const float lane1X = 22.f;
		const float lane2X = 56.f;
		const float lane3X = 90.f;
		const float lane4X = 124.f;

		// PRESENCE knobs above OUT jacks
		const float presY = 120.f;  // knobs
		const float outY  = 170.f;  // outputs

		// Density
		addParam(createParamCentered<RoundBlackKnob>(Vec(globalX, globalY), module, Phrasing::DENSITY_PARAM));

		addParam(createParamCentered<TL1105>(Vec(leftSwitchX, switchY), module, Phrasing::RANGE_SWITCH_PARAM));
		addParam(createParamCentered<TL1105>(Vec(rightSwitchX, switchY), module, Phrasing::CURVE_SWITCH_PARAM));

		// Lane presence knobs (top)
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, presY), module, Phrasing::PRESENCE1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, presY), module, Phrasing::PRESENCE2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, presY), module, Phrasing::PRESENCE3_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, presY), module, Phrasing::PRESENCE4_PARAM));

		// Lane OUTs (below)
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane1X, outY), module, Phrasing::OUT1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane2X, outY), module, Phrasing::OUT2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane3X, outY), module, Phrasing::OUT3_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(lane4X, outY), module, Phrasing::OUT4_OUTPUT));
	}
};

// IMPORTANT: this string must match your plugin.json module slug
Model* modelPhrasing = createModel<Phrasing, PhrasingWidget>("phrasing");