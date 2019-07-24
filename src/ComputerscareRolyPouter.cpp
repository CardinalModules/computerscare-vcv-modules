#include "Computerscare.hpp"

struct ComputerscareRolyPouter;

const int numKnobs = 16;

struct ComputerscareRolyPouter : Module {
	int counter = 0;
	int routing[numKnobs];
	int numOutputChannels = 16;
	ComputerscareSVGPanel* panelRef;
	enum ParamIds {
		KNOB,
		NUM_PARAMS = KNOB + numKnobs
	};
	enum InputIds {
		POLY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};


	ComputerscareRolyPouter()  {

		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int i = 0; i < numKnobs; i++) {
			configParam(KNOB + i, 1.f, 16.f, (i + 1), "output ch" + std::to_string(i + 1) + " = input ch");
			routing[i] = i;
		}

	}
	void process(const ProcessArgs &args) override {
		counter++;
		int inputChannels = inputs[POLY_INPUT].getChannels();
		int knobSetting;
		if (counter > 5012) {
			//printf("%f \n",random::uniform());
			counter = 0;
			for (int i = 0; i < numKnobs; i++) {
				routing[i] = (int)params[KNOB + i].getValue();
			}

		}
		outputs[POLY_OUTPUT].setChannels(numOutputChannels);
		for (int i = 0; i < numOutputChannels; i++) {
			knobSetting = params[KNOB+i].getValue();
			if(knobSetting > inputChannels) {
							outputs[POLY_OUTPUT].setVoltage(0,i);
			}
			else {
			outputs[POLY_OUTPUT].setVoltage(inputs[POLY_INPUT].getVoltage(knobSetting - 1), i);
			}
		}
	}

};
struct PouterSmallDisplay : SmallLetterDisplay
{
	ComputerscareRolyPouter *module;
	int ch;
	PouterSmallDisplay(int outputChannelNumber)
	{

		ch = outputChannelNumber;
		SmallLetterDisplay();
	};
	void draw(const DrawArgs &args)
	{
		//this->setNumDivisionsString();
		if (module)
		{


			std::string str = std::to_string(module->routing[ch]);
			value = str;



		}
		SmallLetterDisplay::draw(args);
	}

};

struct ComputerscareRolyPouterWidget : ModuleWidget {
	ComputerscareRolyPouterWidget(ComputerscareRolyPouter *module) {

		setModule(module);
		//setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComputerscareRolyPouterPanel.svg")));
		box.size = Vec(4 * 15, 380);
		{
			ComputerscareSVGPanel *panel = new ComputerscareSVGPanel();
			panel->box.size = box.size;
			panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComputerscareRolyPouterPanel.svg")));

			//module->panelRef = panel;

			addChild(panel);

		}
		float xx;
		float yy;
		for (int i = 0; i < numKnobs; i++) {
			xx = 1.4f + 24.3 * (i - i % 8) / 8;
			yy = 66 + 36.5 * (i % 8) + 14.3 * (i - i % 8) / 8;
			addLabeledKnob(std::to_string(i + 1), xx, yy, module, i, (i - i % 8) * 1.3 - 5, i<8 ? 4 : 0);
		}


		addInput(createInput<InPort>(Vec(1, 34), module, ComputerscareRolyPouter::POLY_INPUT));
		addOutput(createOutput<PointingUpPentagonPort>(Vec(32, 24), module, ComputerscareRolyPouter::POLY_OUTPUT));

	}
	void addLabeledKnob(std::string label, int x, int y, ComputerscareRolyPouter *module, int index, float labelDx, float labelDy) {

		pouterSmallDisplay = new PouterSmallDisplay(index);
		pouterSmallDisplay->box.size = Vec(20, 20);
		pouterSmallDisplay->box.pos = Vec(x-2.5 ,y+1.f);
		pouterSmallDisplay->fontSize = 26;
		pouterSmallDisplay->textAlign = 18;
		pouterSmallDisplay->textColor = COLOR_COMPUTERSCARE_LIGHT_GREEN;
		pouterSmallDisplay->breakRowWidth=20;
		pouterSmallDisplay->module = module;


		outputChannelLabel = new SmallLetterDisplay();
		outputChannelLabel->box.size = Vec(5, 5);
		outputChannelLabel->box.pos = Vec(x + labelDx, y - 12 + labelDy);
		outputChannelLabel->fontSize = 14;
		outputChannelLabel->textAlign = index < 8 ? 1 : 4;
		outputChannelLabel->breakRowWidth=15;

		outputChannelLabel->value = std::to_string(index + 1);

		addParam(createParam<MediumDotSnapKnob>(Vec(x, y), module, ComputerscareRolyPouter::KNOB + index));
		addChild(pouterSmallDisplay);
		addChild(outputChannelLabel);

	}
	PouterSmallDisplay* pouterSmallDisplay;
	SmallLetterDisplay* outputChannelLabel;
};


Model *modelComputerscareRolyPouter = createModel<ComputerscareRolyPouter, ComputerscareRolyPouterWidget>("computerscare-roly-pouter");