#include "plugin.hpp"
#include <cmath>
#include <algorithm>
using namespace rack;
using namespace rack::componentlibrary;

struct Phrasing : Module {
    enum ParamId {
        DENSITY_PARAM,
        GAP_JITTER_PARAM,
        DURATION_JITTER_PARAM,
        GUARANTEE_ONE_PARAM,
        LANE1_ACTIVE_PARAM,
        LANE2_ACTIVE_PARAM,
        LANE3_ACTIVE_PARAM,
        LANE4_ACTIVE_PARAM,
        WEIGHT1_PARAM,
        WEIGHT2_PARAM,
        WEIGHT3_PARAM,
        WEIGHT4_PARAM,
        LANEDUR1_PARAM,
        LANEDUR2_PARAM,
        LANEDUR3_PARAM,
        LANEDUR4_PARAM,
        FLOOR1_PARAM,
        FLOOR2_PARAM,
        FLOOR3_PARAM,
        FLOOR4_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        DENSITY_CV_INPUT,
        TRIG1_INPUT,
        TRIG2_INPUT,
        TRIG3_INPUT,
        TRIG4_INPUT,
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
        LANE1_LIGHT,
        LANE2_LIGHT,
        LANE3_LIGHT,
        LANE4_LIGHT,
        LIGHTS_LEN
    };

    float laneTarget[4] = {0.f, 0.f, 0.f, 0.f};
    float laneValue[4] = {0.f, 0.f, 0.f, 0.f};
    float laneOnTimer[4] = {0.f, 0.f, 0.f, 0.f};
    float gapTimer = 0.f;
    bool initialized = false;
    dsp::SchmittTrigger laneBtnTrig[4];
    dsp::SchmittTrigger trigIn[4];
    bool laneEnabled[4] = {true, true, true, true};
    bool laneEnabledInit = false;

    Phrasing() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(DENSITY_PARAM, 0.f, 1.f, 0.7f, "Density", "%", 0.f, 100.f);
        configParam(GAP_JITTER_PARAM, 0.f, 1.f, 0.25f, "Gap Jitter", "%", 0.f, 100.f);
        configParam(DURATION_JITTER_PARAM, 0.f, 1.f, 0.25f, "Duration Jitter", "%", 0.f, 100.f);
        configSwitch(GUARANTEE_ONE_PARAM, 0.f, 1.f, 0.f, "Guarantee one lane", {"Off", "On"});
        configInput(DENSITY_CV_INPUT, "Density CV");
        configInput(TRIG1_INPUT, "Trig 1");
        configInput(TRIG2_INPUT, "Trig 2");
        configInput(TRIG3_INPUT, "Trig 3");
        configInput(TRIG4_INPUT, "Trig 4");
        configButton(LANE1_ACTIVE_PARAM, "Lane 1 Enable");
        configButton(LANE2_ACTIVE_PARAM, "Lane 2 Enable");
        configButton(LANE3_ACTIVE_PARAM, "Lane 3 Enable");
        configButton(LANE4_ACTIVE_PARAM, "Lane 4 Enable");
        configParam(WEIGHT1_PARAM, 0.f, 1.f, 0.8f, "Weight I", "%", 0.f, 100.f);
        configParam(WEIGHT2_PARAM, 0.f, 1.f, 0.8f, "Weight II", "%", 0.f, 100.f);
        configParam(WEIGHT3_PARAM, 0.f, 1.f, 0.8f, "Weight III", "%", 0.f, 100.f);
        configParam(WEIGHT4_PARAM, 0.f, 1.f, 0.8f, "Weight IV", "%", 0.f, 100.f);
        configParam(LANEDUR1_PARAM, 0.f, 1.f, 0.5f, "Duration I");
        configParam(LANEDUR2_PARAM, 0.f, 1.f, 0.5f, "Duration II");
        configParam(LANEDUR3_PARAM, 0.f, 1.f, 0.5f, "Duration III");
        configParam(LANEDUR4_PARAM, 0.f, 1.f, 0.5f, "Duration IV");
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

    float knobToSeconds(float d) {
        const float minS = 5.0f;
        const float maxS = 90.0f;
        return minS * std::pow(maxS / minS, d);
    }

    float densityToGapSeconds(float density) {
        const float minGap = 5.0f;
        const float maxGap = 90.0f;
        const float x = clamp(density, 0.f, 1.f);
        return minGap * std::pow(maxGap / minGap, 1.f - x);
    }

    float jitterMul(float amt) {
        amt = clamp(amt, 0.f, 1.f);
        const float u = random::uniform();
        return (1.f - amt) + (2.f * amt) * u;
    }

    void process(const ProcessArgs& args) override {
        const float sr = args.sampleRate;
        const float dt = args.sampleTime;

        if (inputs[DENSITY_CV_INPUT].isConnected()) {
            const float cv = clamp(inputs[DENSITY_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
            params[DENSITY_PARAM].setValue(cv);
        }

        const float density = clamp(params[DENSITY_PARAM].getValue(), 0.f, 1.f);
        const float gapJitter = clamp(params[GAP_JITTER_PARAM].getValue(), 0.f, 1.f);
        const float durJitter = clamp(params[DURATION_JITTER_PARAM].getValue(), 0.f, 1.f);
        const bool guaranteeOne = params[GUARANTEE_ONE_PARAM].getValue() > 0.5f;

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

        if (!laneEnabledInit) {
            laneEnabledInit = true;
            for (int i = 0; i < 4; i++) {
                laneEnabled[i] = true;
                laneOnTimer[i] = 0.f;
                laneTarget[i] = 0.f;
                laneValue[i] = 0.f;
            }
        }

        for (int i = 0; i < 4; i++) {
            const float v = params[LANE1_ACTIVE_PARAM + i].getValue();
            if (laneBtnTrig[i].process(v)) {
                laneEnabled[i] = !laneEnabled[i];
                if (!laneEnabled[i]) {
                    laneOnTimer[i] = 0.f;
                    laneTarget[i] = 0.f;
                }
            }
        }

        const bool laneActive[4] = { laneEnabled[0], laneEnabled[1], laneEnabled[2], laneEnabled[3] };

        const float durJitAmt = 0.50f * durJitter;
        for (int i = 0; i < 4; i++) {
            if (!laneActive[i]) continue;
            if (inputs[TRIG1_INPUT + i].isConnected()) {
                if (trigIn[i].process(inputs[TRIG1_INPUT + i].getVoltage())) {
                    const float baseDur = knobToSeconds(laneDurKnob[i]);
                    const float durMul = jitterMul(durJitAmt);
                    laneOnTimer[i] = std::max(0.f, baseDur * durMul);
                    laneTarget[i] = 1.f;
                }
            }
        }

        if (!initialized) {
            initialized = true;
            const float baseGap0 = densityToGapSeconds(density);
            gapTimer = random::uniform() * baseGap0;
            for (int i = 0; i < 4; i++) {
                laneOnTimer[i] = 0.f;
                laneTarget[i] = 0.f;
                laneValue[i] = 0.f;
            }
        }

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

        const float gapJitAmt = 0.30f * gapJitter;
        const float baseGap = densityToGapSeconds(density);
        gapTimer -= dt;
        if (gapTimer <= 0.f) {
            for (int i = 0; i < 4; i++) {
                if (!laneActive[i]) continue;
                if (laneOnTimer[i] > 0.f) continue;
                const float w = laneWeight[i];
                if (w <= 0.f) continue;
                if (random::uniform() < w) {
                    const float baseDur = knobToSeconds(laneDurKnob[i]);
                    const float durMul = jitterMul(durJitAmt);
                    laneOnTimer[i] = std::max(0.f, baseDur * durMul);
                    laneTarget[i] = 1.f;
                }
            }
            const float gapMul = jitterMul(gapJitAmt);
            gapTimer = std::max(0.01f, baseGap * gapMul);
        }

        if (guaranteeOne) {
            bool anyActive = false;
            bool anyOn = false;
            for (int i = 0; i < 4; i++) {
                if (laneActive[i]) anyActive = true;
                if (laneActive[i] && laneOnTimer[i] > 0.f) anyOn = true;
            }
            if (anyActive && !anyOn) {
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

        auto onePoleCoeff = [&](float timeSec) {
            if (timeSec <= 0.f) return 0.f;
            return std::exp(-1.f / (timeSec * sr));
        };

        for (int i = 0; i < 4; i++) {
            const float laneBaseDur = knobToSeconds(laneDurKnob[i]);
            const float attackSec = clamp(laneBaseDur * 0.20f, 0.030f, 2.0f);
            const float releaseSec = clamp(laneBaseDur * 0.40f, 0.300f, 25.0f);
            const float aCoeff = onePoleCoeff(attackSec);
            const float rCoeff = onePoleCoeff(releaseSec);
            const float target = laneActive[i] ? laneTarget[i] : 0.f;
            const float cur = laneValue[i];
            const float coeff = (target > cur) ? aCoeff : rCoeff;
            laneValue[i] = target + (cur - target) * coeff;
            laneValue[i] = clamp(laneValue[i], 0.f, 1.f);
        }

        const float maxV = 5.f;
        for (int i = 0; i < 4; i++) {
            float v = 0.f;
            if (laneActive[i]) {
                const float f = clamp(laneFloor[i], 0.f, 1.f);
                const float shaped = f + (1.f - f) * laneValue[i];
                v = maxV * shaped;
            }
            outputs[OUT1_OUTPUT + i].setVoltage(v);
        }

        for (int i = 0; i < 4; i++) {
            const float base = laneActive[i] ? 0.15f : 0.f;
            const float activity = laneActive[i] ? laneValue[i] : 0.f;
            lights[LANE1_LIGHT + i].setBrightness(std::max(base, activity));
        }
    }
};

struct PhrasingWidget : ModuleWidget {
    PhrasingWidget(Phrasing* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Phrasing.svg")));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float densityX = 73.f;
        const float gapJitterX = 33.f;
        const float durJitterX = 113.f;
        const float guaranteeX = 143.f;
        const float globalY = 40.f;
        const float lane1X = 22.f;
        const float lane2X = 56.f;
        const float lane3X = 90.f;
        const float lane4X = 124.f;
        const float enY = 105.f;
        const float lightY = 122.f;
        const float weightY = 150.f;
        const float laneDurY = 190.f;
        const float floorY = 230.f;
        const float outY = 340.f;
        const float trigY = outY - 35.f;

        addParam(createParamCentered<RoundBlackKnob>(Vec(densityX, globalY), module, Phrasing::DENSITY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(gapJitterX, globalY), module, Phrasing::GAP_JITTER_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(durJitterX, globalY), module, Phrasing::DURATION_JITTER_PARAM));
        addParam(createParamCentered<CKSS>(Vec(guaranteeX, globalY), module, Phrasing::GUARANTEE_ONE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(densityX, globalY + 36.f), module, Phrasing::DENSITY_CV_INPUT));

        addParam(createParamCentered<TL1105>(Vec(lane1X, enY), module, Phrasing::LANE1_ACTIVE_PARAM));
        addParam(createParamCentered<TL1105>(Vec(lane2X, enY), module, Phrasing::LANE2_ACTIVE_PARAM));
        addParam(createParamCentered<TL1105>(Vec(lane3X, enY), module, Phrasing::LANE3_ACTIVE_PARAM));
        addParam(createParamCentered<TL1105>(Vec(lane4X, enY), module, Phrasing::LANE4_ACTIVE_PARAM));

        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane1X, lightY), module, Phrasing::LANE1_LIGHT));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane2X, lightY), module, Phrasing::LANE2_LIGHT));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane3X, lightY), module, Phrasing::LANE3_LIGHT));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(lane4X, lightY), module, Phrasing::LANE4_LIGHT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, weightY), module, Phrasing::WEIGHT1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, weightY), module, Phrasing::WEIGHT2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, weightY), module, Phrasing::WEIGHT3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, weightY), module, Phrasing::WEIGHT4_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, laneDurY), module, Phrasing::LANEDUR1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, laneDurY), module, Phrasing::LANEDUR2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, laneDurY), module, Phrasing::LANEDUR3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, laneDurY), module, Phrasing::LANEDUR4_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(Vec(lane1X, floorY), module, Phrasing::FLOOR1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane2X, floorY), module, Phrasing::FLOOR2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane3X, floorY), module, Phrasing::FLOOR3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(lane4X, floorY), module, Phrasing::FLOOR4_PARAM));

        addOutput(createOutputCentered<PJ301MPort>(Vec(lane1X, outY), module, Phrasing::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(lane2X, outY), module, Phrasing::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(lane3X, outY), module, Phrasing::OUT3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(lane4X, outY), module, Phrasing::OUT4_OUTPUT));

        addInput(createInputCentered<PJ301MPort>(Vec(lane1X, trigY), module, Phrasing::TRIG1_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(lane2X, trigY), module, Phrasing::TRIG2_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(lane3X, trigY), module, Phrasing::TRIG3_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(lane4X, trigY), module, Phrasing::TRIG4_INPUT));
    }
};

Model* modelPhrasing = createModel<Phrasing, PhrasingWidget>("phrasing");