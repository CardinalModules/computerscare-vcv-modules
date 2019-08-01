#include <string.h>
#include "plugin.hpp"
#include "Computerscare.hpp"


static const int BUFFER_SIZE = 512;


struct FolyPace : Module {
	enum ParamIds {
		X_SCALE_PARAM,
		X_POS_PARAM,
		Y_SCALE_PARAM,
		Y_POS_PARAM,
		TIME_PARAM,
		LISSAJOUS_PARAM,
		TRIG_PARAM,
		EXTERNAL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		X_INPUT,
		Y_INPUT,
		TRIG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		PLOT_LIGHT,
		LISSAJOUS_LIGHT,
		INTERNAL_LIGHT,
		EXTERNAL_LIGHT,
		NUM_LIGHTS
	};

	float bufferX[16][BUFFER_SIZE] = {};
	float bufferY[16][BUFFER_SIZE] = {};
	int channelsX = 0;
	int channelsY = 0;
	int bufferIndex = 0;
	int frameIndex = 0;

	dsp::BooleanTrigger sumTrigger;
	dsp::BooleanTrigger extTrigger;
	bool lissajous = false;
	bool external = false;
	dsp::SchmittTrigger triggers[16];

	FolyPace() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(X_SCALE_PARAM, -2.f, 8.f, 0.f, "X scale", " V/div", 1 / 2.f, 5);
		configParam(X_POS_PARAM, -10.f, 10.f, 0.f, "X position", " V");
		configParam(Y_SCALE_PARAM, -2.f, 8.f, 0.f, "Y scale", " V/div", 1 / 2.f, 5);
		configParam(Y_POS_PARAM, -10.f, 10.f, 0.f, "Y position", " V");
		const float timeBase = (float) BUFFER_SIZE / 6;
		configParam(TIME_PARAM, 6.f, 16.f, 14.f, "Time", " ms/div", 1 / 2.f, 1000 * timeBase);
		configParam(LISSAJOUS_PARAM, 0.f, 1.f, 0.f);
		configParam(TRIG_PARAM, -10.f, 10.f, 0.f, "Trigger position", " V");
		configParam(EXTERNAL_PARAM, 0.f, 1.f, 0.f);
	}

	void onReset() override {
		lissajous = false;
		external = false;
		std::memset(bufferX, 0, sizeof(bufferX));
		std::memset(bufferY, 0, sizeof(bufferY));
	}

	void process(const ProcessArgs &args) override {
		// Modes
		if (sumTrigger.process(params[LISSAJOUS_PARAM].getValue() > 0.f)) {
			lissajous = !lissajous;
		}
		lights[PLOT_LIGHT].setBrightness(!lissajous);
		lights[LISSAJOUS_LIGHT].setBrightness(lissajous);

		if (extTrigger.process(params[EXTERNAL_PARAM].getValue() > 0.f)) {
			external = !external;
		}
		lights[INTERNAL_LIGHT].setBrightness(!external);
		lights[EXTERNAL_LIGHT].setBrightness(external);

		// Compute time
		float deltaTime = std::pow(2.f, -params[TIME_PARAM].getValue());
		int frameCount = (int) std::ceil(deltaTime * args.sampleRate);

		// Set channels
		int channelsX = inputs[X_INPUT].getChannels();
		if (channelsX != this->channelsX) {
			std::memset(bufferX, 0, sizeof(bufferX));
			this->channelsX = channelsX;
		}

		int channelsY = inputs[Y_INPUT].getChannels();
		if (channelsY != this->channelsY) {
			std::memset(bufferY, 0, sizeof(bufferY));
			this->channelsY = channelsY;
		}

		// Add frame to buffer
		if (bufferIndex < BUFFER_SIZE) {
			if (++frameIndex > frameCount) {
				frameIndex = 0;
				for (int c = 0; c < channelsX; c++) {
					bufferX[c][bufferIndex] = inputs[X_INPUT].getVoltage(c);
				}
				for (int c = 0; c < channelsY; c++) {
					bufferY[c][bufferIndex] = inputs[Y_INPUT].getVoltage(c);
				}
				bufferIndex++;
			}
		}

		// Don't wait for trigger if still filling buffer
		if (bufferIndex < BUFFER_SIZE) {
			return;
		}

		// Trigger immediately if external but nothing plugged in, or in Lissajous mode
		if (true) {
			trigger();
			return;
		}

		frameIndex++;

		// Reset if triggered
		float trigThreshold = params[TRIG_PARAM].getValue();
		Input &trigInput = external ? inputs[TRIG_INPUT] : inputs[X_INPUT];

		// This may be 0
		int trigChannels = trigInput.getChannels();
		for (int c = 0; c < trigChannels; c++) {
			float trigVoltage = trigInput.getVoltage(c);
			if (triggers[c].process(rescale(trigVoltage, trigThreshold, trigThreshold + 0.001f, 0.f, 1.f))) {
				trigger();
				return;
			}
		}

		// Reset if we've been waiting for `holdTime`
		const float holdTime = 0.2f;
		if (frameIndex * args.sampleTime >= holdTime) {
			trigger();
			return;
		}
	}

	void trigger() {
		for (int c = 0; c < 16; c++) {
			triggers[c].reset();
		}
		bufferIndex = 0;
		frameIndex = 0;
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lissajous", json_integer((int) lissajous));
		json_object_set_new(rootJ, "external", json_integer((int) external));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *sumJ = json_object_get(rootJ, "lissajous");
		if (sumJ)
			lissajous = json_integer_value(sumJ);

		json_t *extJ = json_object_get(rootJ, "external");
		if (extJ)
			external = json_integer_value(extJ);
	}
};


struct FolyPaceDisplay : TransparentWidget {
	FolyPace *module;
	int statsFrame = 0;
	std::shared_ptr<Font> font;

	struct Stats {
		float vpp = 0.f;
		float vmin = 0.f;
		float vmax = 0.f;

		void calculate(float *buffer, int channels) {
			vmax = -INFINITY;
			vmin = INFINITY;
			for (int i = 0; i < BUFFER_SIZE * channels; i++) {
				float v = buffer[i];
				vmax = std::fmax(vmax, v);
				vmin = std::fmin(vmin, v);
			}
			vpp = vmax - vmin;
		}
	};

	Stats statsX, statsY;

	FolyPaceDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Oswald-Regular.ttf"));
	}

	void drawWaveform(const DrawArgs &args, float *bufferX, float offsetX, float gainX, float *bufferY, float offsetY, float gainY) {
		assert(bufferY);
		nvgSave(args.vg);
		Rect b = Rect(Vec(0, 15), box.size.minus(Vec(0, 15 * 2)));
		nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
		nvgBeginPath(args.vg);
		for (int i = 0; i < BUFFER_SIZE; i++) {
			Vec v;
			if (bufferX)
				v.x = (bufferX[i] + offsetX) * gainX / 2.f + 0.5f;
			else
				v.x = (float) i / (BUFFER_SIZE - 1);
			v.y = (bufferY[i] + offsetY) * gainY / 2.f + 0.5f;
			Vec p;
			p.x = rescale(v.x, 0.f, 1.f, b.pos.x, b.pos.x + b.size.x);
			p.y = rescale(v.y, 0.f, 1.f, b.pos.y + b.size.y, b.pos.y);
			if (i == 0)
				nvgMoveTo(args.vg, p.x, p.y);
			else
				nvgLineTo(args.vg, p.x, p.y);
		}
		nvgLineCap(args.vg, NVG_ROUND);
		nvgMiterLimit(args.vg, 2.f);
		nvgStrokeWidth(args.vg, 1.5f);
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
		nvgStroke(args.vg);
		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
	}
	void drawFace(const DrawArgs &args, float A, float B, float C, float D, float E, float F, float G, float H, float I, float J, float K, float L, float M, float N, float O, float P) {



		float sf = 1 + 0.3 * sin(B - C); //scaleFactor
		float ox = 67.5 + sf * 20.33 * sin(D - C / 2);
		float oy = 180 + sf * 30.33 * sin(G - F);

		float h = 0.4 + 0.3 * sin(A / 2) + 0.3 * sin(K / 3);	//face hue
		float s = 0.5 + 0.32 * sin(B / 3 - 33.21 - D / 2);	//face saturation
		float l = 0.5 + 0.45 * sin(C / 2);	//face lightness
		float fx = ox ;	//face y
		float fy = oy;	//face x


		float frx = sf * (70 + 40 * sin(A - B / 2));	// face x radius
		float fry = sf * (150 + 80 * sin(F / 2.2)); //face y radius
		float fr = 0.04 * sin(H - M) + 0.02 * sin(H / 3 + 2.2) + 0.02 * sin(L + P + 8.222); //face rotation

		float mpx = ox - 3 * sin(G + I + A);
		float mpy = oy + 20 + sf * sin(G - I);

		float msx = mpx - 30 * sf + 3 * sin(G);
		float msy = mpy + 5 * sin(L + I + P);


		NVGcolor eyecolor = nvgHSLA(0.5, 0.9, 0.5, 0xff);
		float mex = mpx + 30 * sf + 3 * sin(G + K / 2);
		float mey = mpy + 5 * (1 + sf) * sin(G + 992.2);


		float m1x = mpx - 15 * sf + 3 * sin(A / 3 - M);
		float m1y = mpy - 6 * sf * sin(M) + 14 * sf * cos(E - C + A);

		float m2x = mpx + 15 * sf - 10 * sf * sin(-N) + 14 * sf * sin(E);
		float m2y = mpy - 1 * sf * sin(G - A / 2);

		float epx = ox;
		float epy = oy - 10 * (2 + sf + sin(I - J / 2));

		float es = sf * 10 + 4 + 2 * sin(K - G + H);
		float erlx = 10 + 2 * sin(A) + 5 * sin(-L);
		float erly = 10 + 3 * sin(O - P) - 2 * sin(G);
		float errx = 10 + 3 * sin(M) + 4 * sin(M - 2 - 882.2);
		float erry = 10 + 2 * sin(J) + 4 * sin(J - erly / 20);


		//assert(bufferY);
		nvgSave(args.vg);
		Rect b = Rect(Vec(0, 0), box.size);

		nvgFillColor(args.vg, nvgHSLA(h, s, l, 0xff));
		nvgScissor(args.vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
		nvgBeginPath(args.vg);


		nvgLineCap(args.vg, NVG_ROUND);
		nvgMiterLimit(args.vg, 2.f);
		nvgStrokeWidth(args.vg, 4.5f);


		nvgRotate(args.vg, fr);
		nvgEllipse(args.vg, fx, fy, frx, fry);
		nvgClosePath(args.vg);
		nvgFill(args.vg);
		nvgStroke(args.vg);

		nvgFillColor(args.vg, eyecolor);
		nvgStrokeWidth(args.vg, 1.5f);
		nvgBeginPath(args.vg);

		nvgEllipse(args.vg, epx - es, epy, erlx, erly);
		nvgEllipse(args.vg, epx + es, epy, errx, erry);

		nvgClosePath(args.vg);
		nvgFill(args.vg);
		nvgStroke(args.vg);

		nvgFillColor(args.vg, nvgRGB(0, 0, 0));
		nvgBeginPath(args.vg);

		nvgEllipse(args.vg, epx - es, epy, 2, 2);
		nvgEllipse(args.vg, epx + es, epy, 2, 2);

		nvgClosePath(args.vg);
		nvgFill(args.vg);



		nvgFill(args.vg);
		nvgStroke(args.vg);
		nvgClosePath(args.vg);


		float mouthX = ox;
		float mouthY = oy + 10*(sf+sin(E));
		float mouthWidth = sf*30.f*(1.2+sin(C));
		float mouthOpen = 10*(1+sin(B));
		float mouthSmile = sin(D)*1.3;
		float mouthSkew = sin(L)-sin(H);
		float mouthThickness = 3.4f;
		NVGcolor mouthLipColor=nvgHSLA(0.5f,0.2,0.5,0xff);
		
		drawMouth(args,mouthX,mouthY,mouthWidth,mouthOpen,mouthSmile,mouthSkew,mouthThickness,mouthLipColor); 



		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
	}
	void drawMouth(const DrawArgs &args, float x, float y,float width, float open, float smile, float skew, float thickness,NVGcolor lipColor) {
		nvgBeginPath(args.vg);
		nvgStrokeWidth(args.vg, thickness);
		//nvgStrokeWidth(args.vg, 4.5f);
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
		nvgMoveTo(args.vg, x-width/2,y-10.f*smile);

		//top
		nvgBezierTo(args.vg, x-width/4, y-open*smile, x+width/4, y-open*smile, x+width/2, y-10.f*smile);

		//bottom
		nvgBezierTo(args.vg, x+width/4,y+smile*open, x-width/4, y+smile*open, x-width/2,y-10.f*smile);


		nvgStroke(args.vg);
		nvgClosePath(args.vg);
	}
	void drawEyes(const DrawArgs &args,float x,float y, float spacing, float rx, float ry, float open, float irisRad, float pupilRad, float gazeDir, float gazeStrength, NVGcolor irisColor, NVGcolor pupilColor) {
		float leftX = x - spacing/2;
		float rightX = x + spacing/2;
		float pupilOffsetX = gazeStrength*cos(gazeDir);
		float pupilOffsetY = gazeStrength*sin(gazeDir);
		float leftPupilX = leftX + pupilOffsetX;
		float leftPupilY = y + pupilOffsetY;
		float rightPupilX = rightX + pupilOffsetX;
		float rightPupilY = y + pupilOffsetY;

		nvgBeginPath(args.vg);
		nvgStrokeWidth(args.vg,1.f);
		//nvgStrokeWidth(args.vg, 4.5f);

		//whites of eyes
		nvgFillColor(args.vg, nvgRGB(250,250,250));
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
		
		nvgEllipse(args.vg, leftX, y, rx, ry);
		nvgEllipse(args.vg, rightX, y, rx, ry);

		nvgFill(args.vg);
		nvgClosePath(args.vg);

		//iris
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, irisColor);
		nvgGlobalCompositeOperation(args.vg, NVG_ATOP);
		
		nvgCircle(args.vg, leftPupilX, y, irisRad);
		nvgCircle(args.vg, rightPupilX, y, irisRad);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

//pupil
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, pupilColor);
		nvgGlobalCompositeOperation(args.vg, NVG_ATOP);
		
		nvgCircle(args.vg, leftPupilX, y, pupilRad);
		nvgCircle(args.vg, rightPupilX, y, pupilRad);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgGlobalCompositeOperation(args.vg,NVG_SOURCE_OVER);
//eyebrows

	}
	void drawNose(const DrawArgs &args,float x, float y, float height, float width, float thickness, NVGcolor color) {

	}


	void draw(const DrawArgs &args) override {
		if (!module) {
			drawFace(args, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10, random::uniform() * 10);
		}
		else {

			float gainX = std::pow(2.f, std::round(module->params[FolyPace::X_SCALE_PARAM].getValue())) / 10.f;
			float gainY = std::pow(2.f, std::round(module->params[FolyPace::Y_SCALE_PARAM].getValue())) / 10.f;
			float offsetX = module->params[FolyPace::X_POS_PARAM].getValue();
			float offsetY = module->params[FolyPace::Y_POS_PARAM].getValue();

			int lissajousChannels = std::max(module->channelsX, module->channelsY);
			drawFace(args, module->bufferX[0][0], module->bufferX[1][0], module->bufferX[2][0], module->bufferX[3][0], module->bufferX[4][0], module->bufferX[5][0], module->bufferX[6][0], module->bufferX[7][0], module->bufferX[8][0], module->bufferX[9][0], module->bufferX[10][0], module->bufferX[11][0], module->bufferX[12][0], module->bufferX[13][0], module->bufferX[14][0], module->bufferX[15][0]);

		}
	}
};


struct FolyPaceWidget : ModuleWidget {
	FolyPaceWidget(FolyPace *module) {
		setModule(module);

		box.size = Vec(9 * 15, 380);
		{
			ComputerscareSVGPanel *panel = new ComputerscareSVGPanel();
			panel->box.size = box.size;
			panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComputerscareFolyPacePanel.svg")));
			addChild(panel);

		}

		{
			FolyPaceDisplay *display = new FolyPaceDisplay();
			display->module = module;
			display->box.pos = Vec(0, 0);
			display->box.size = Vec(box.size.x, box.size.y);
			addChild(display);
		}

		addInput(createInput<PJ301MPort>(Vec(17, 319), module, FolyPace::X_INPUT));
		addInput(createInput<PJ301MPort>(Vec(63, 319), module, FolyPace::Y_INPUT));
		addInput(createInput<PJ301MPort>(Vec(154, 319), module, FolyPace::TRIG_INPUT));

	}
};


Model *modelComputerscareFolyPace = createModel<FolyPace, FolyPaceWidget>("computerscare-foly-pace");