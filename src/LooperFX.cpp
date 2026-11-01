// LooperFX.cpp: the My lil Looper FX expander. Placed left of a My lil Looper: per track FILTER, SPEED, MUTE, hold STUT and
// TAPE, and 8 scenes (mutes, speeds, filters, levels) recalled on the Looper's next downbeat. The panel only asks: the
// Looper runs the DSP (core/FxHead.h, core/DjFilter.h) and owns the timing (core/Scene.h). Messages: src/FxLink.hpp.
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout_fx.hpp"
#include "Pad.hpp"
#include "FxLink.hpp"
#include "TalkingFace.hpp"
#include <atomic>

static const int STUT_DIV[] = { 4, 8, 16, 32 };

struct LooperFX : Module
{
    static constexpr int N = 5, SCENES = 8;
    enum ParamId { ENUMS (FILTER_PARAM, N), ENUMS (SPEED_PARAM, N), ENUMS (MUTE_PARAM, N), ENUMS (STUT_PARAM, N), ENUMS (TAPE_PARAM, N),
                   ENUMS (SCENE_PARAM, SCENES), STUTLEN_PARAM, PARAMS_LEN };
    enum InputId { SCENE_INPUT, STUT_INPUT, TAPE_INPUT, FILTER_INPUT, INPUTS_LEN };
    enum OutputId { OUTPUTS_LEN };
    enum LightId { ENUMS (MUTE_LIGHT, N), ENUMS (SCENE_LIGHT, 2 * SCENES), LIGHTS_LEN };

    FxBack backMsg[2];                    // the right expander's double buffer: the Looper writes one, we read the other
    uint32_t lastBackSeq = 0, linkSeq = 0;
    std::atomic<bool> linked { false };
    FaceTap face;   // the mascot's mouth: the Looper's main out

    loop::Scene scenes[SCENES];
    int current = -1;                     // the scene in effect (-1 none)
    int requested = -1;                   // the scene asked for, waiting for the downbeat
    loop::Scene reqScene;
    uint32_t reqId = 0, ackId = 0;        // newest request; the one taken over into the switches
    int64_t partner = -1;                 // the Looper we last reconciled with (-1 none): see process()
    int barLen = 1;
    float level[N] = { 1.f, 1.f, 1.f, 1.f, 1.f };   // the Looper's LEVEL knobs, as last reported
    float mainOut = 0.f;
    float gFrom[N] = {}, gTo[N] = {};     // filter glide over one bar after a scene
    int gN = 0, gLen = 0;
    float held[SCENES] = {}, flash[SCENES] = {};
    bool savedNow[SCENES] = {};
    int lastCvScene = -1;
    float blinkTime = 0.f;
    dsp::ClockDivider lightDiv;

    LooperFX()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int i = 0; i < N; ++i)
        {
            const std::string t = "Track " + std::to_string (i + 1);
            configParam (FILTER_PARAM + i, -1.f, 1.f, 0.f, t + " filter (left: low-pass, right: high-pass, centre: off)");
            configSwitch (SPEED_PARAM + i, 0.f, 2.f, 1.f, t + " speed (on the next bar)", { "x0.5", "x1", "x2" });
            configSwitch (MUTE_PARAM + i, 0.f, 1.f, 0.f, t + " mute (on the next bar)", { "Playing", "Muted" });
            configButton (STUT_PARAM + i, t + " stutter (hold)");
            configButton (TAPE_PARAM + i, t + " tape stop (hold)");
        }
        for (int k = 0; k < SCENES; ++k) configButton (SCENE_PARAM + k, "Scene " + std::to_string (k + 1) + " (press: recall on the next bar; hold 2 s: save)");
        configSwitch (STUTLEN_PARAM, 0.f, 3.f, 2.f, "Stutter length", { "1/4 bar", "1/8 bar", "1/16 bar", "1/32 bar" });
        configInput (SCENE_INPUT, "Scene select (0-10 V: scenes 1-8)");
        configInput (STUT_INPUT, "Stutter gates (poly: channel n = track n)");
        configInput (TAPE_INPUT, "Tape stop gates (poly: channel n = track n)");
        configInput (FILTER_INPUT, "Filter CV (poly: channel n = track n, 5 V = half the knob)");
        rightExpander.producerMessage = &backMsg[0];
        rightExpander.consumerMessage = &backMsg[1];
        lightDiv.setDivision (64);
    }

    static bool gate (Input& in, int c) { return c < in.getChannels() && in.getVoltage (c) >= 1.f; }

    void recall (int k)
    {
        if (! scenes[k].saved || ! linked.load (std::memory_order_relaxed)) return;   // no Looper to play it: nothing queues
        reqScene = scenes[k];
        ++reqId;
        requested = k;
    }

    // False (nothing saved) while no Looper is linked: the levels would be the last ones it reported, not the mix.
    bool save (int k)
    {
        if (! linked.load (std::memory_order_relaxed)) return false;
        loop::Scene& s = scenes[k];
        s.saved = true;
        for (int i = 0; i < N; ++i)
        {
            s.t[i].mute = params[MUTE_PARAM + i].getValue() > 0.5f;
            s.t[i].speed = (int) std::round (params[SPEED_PARAM + i].getValue());
            s.t[i].filter = params[FILTER_PARAM + i].getValue();
            s.t[i].level = level[i];
        }
        // still waiting for the downbeat: a new request id, or the Looper keeps the old mix
        if (requested == k && reqId != ackId) { reqScene = s; ++reqId; }
        return true;
    }

    void process (const ProcessArgs& args) override
    {
        const FxBack& back = *(const FxBack*) rightExpander.consumerMessage;
        const bool looper = rightExpander.module && rightExpander.module->model == modelLooper;
        const bool fresh = looper && back.seq != lastBackSeq;
        lastBackSeq = back.seq;
        linked.store (fresh, std::memory_order_relaxed);
        if (! looper) partner = -1;
        if (fresh)
        {
            // A Looper new to us (or back after an unlink): its applied counter is its own, so start from it. Else a
            // stale scene would replay on its next downbeat, and switches taken over earlier would stay dead.
            if (rightExpander.moduleId != partner)
            {
                partner = rightExpander.moduleId;
                reqId = ackId = back.applied;
                requested = -1;   // current stays: it is display only, and the patch's scene is still the one in effect
                gN = gLen = 0;
            }
            barLen = std::max (1, back.barLen);
            for (int i = 0; i < N; ++i) level[i] = back.level[i];
            mainOut = back.mainOut;
            if (back.applied == reqId && reqId != ackId)   // our scene is in effect: take it over into the switches
            {
                for (int i = 0; i < N; ++i)
                {
                    params[SPEED_PARAM + i].setValue ((float) reqScene.t[i].speed);
                    params[MUTE_PARAM + i].setValue (reqScene.t[i].mute ? 1.f : 0.f);
                    gFrom[i] = params[FILTER_PARAM + i].getValue();
                    gTo[i] = reqScene.t[i].filter;
                }
                gN = 0;
                gLen = barLen;
                ackId = reqId;
                current = requested;
            }
        }
        if (gN < gLen)   // filters glide to the scene over the same bar as the Looper's levels
        {
            ++gN;
            for (int i = 0; i < N; ++i) params[FILTER_PARAM + i].setValue (gFrom[i] + (gTo[i] - gFrom[i]) * (float) gN / (float) gLen);
        }

        for (int k = 0; k < SCENES; ++k)   // press: recall; hold 2 s: save
        {
            if (params[SCENE_PARAM + k].getValue() > 0.f)
            {
                held[k] += args.sampleTime;
                if (held[k] >= 2.f && ! savedNow[k]) { savedNow[k] = true; if (save (k)) flash[k] = 0.6f; }
            }
            else
            {
                if (held[k] > 0.f && ! savedNow[k]) recall (k);
                held[k] = 0.f;
                savedNow[k] = false;
            }
        }
        if (inputs[SCENE_INPUT].isConnected())
        {
            const int k = clamp ((int) (inputs[SCENE_INPUT].getVoltage() / 1.25f), 0, SCENES - 1);
            if (k != lastCvScene) recall (k);
            lastCvScene = k;
        }
        else lastCvScene = -1;

        if (looper && rightExpander.module->leftExpander.producerMessage)
        {
            FxLink* m = (FxLink*) rightExpander.module->leftExpander.producerMessage;
            m->seq = ++linkSeq;
            Input& cv = inputs[FILTER_INPUT];
            for (int i = 0; i < N; ++i)
            {
                const float add = i < cv.getChannels() ? cv.getVoltage (i) / 5.f : 0.f;
                m->filter[i] = clamp (params[FILTER_PARAM + i].getValue() + add, -1.f, 1.f);
                m->speed[i] = (int) std::round (params[SPEED_PARAM + i].getValue());
                m->mute[i] = params[MUTE_PARAM + i].getValue() > 0.5f;
                m->stut[i] = params[STUT_PARAM + i].getValue() > 0.f || gate (inputs[STUT_INPUT], i);
                m->tape[i] = params[TAPE_PARAM + i].getValue() > 0.f || gate (inputs[TAPE_INPUT], i);
            }
            m->stutDiv = STUT_DIV[clamp ((int) std::round (params[STUTLEN_PARAM].getValue()), 0, 3)];
            m->reqId = reqId;
            m->ackId = ackId;
            m->req = reqScene;
            rightExpander.module->leftExpander.requestMessageFlip();
        }
        face.push (fresh ? mainOut : 0.f, args.sampleRate);

        if (lightDiv.process()) updateLights (args.sampleTime * lightDiv.getDivision());
    }

    void updateLights (float dt)
    {
        blinkTime = std::fmod (blinkTime + dt, 0.5f);
        const bool blink = blinkTime < 0.25f;
        for (int i = 0; i < N; ++i) lights[MUTE_LIGHT + i].setBrightness (params[MUTE_PARAM + i].getValue() > 0.5f ? 0.f : 1.f);   // lit = audible
        for (int k = 0; k < SCENES; ++k)
        {
            float lime = 0.f;
            if (linked.load (std::memory_order_relaxed) && k == requested && reqId != ackId) lime = blink ? 1.f : 0.f;   // waiting for the downbeat
            else if (k == current) lime = 1.f;
            else if (scenes[k].saved) lime = 0.15f;
            flash[k] = std::max (0.f, flash[k] - dt);
            lights[SCENE_LIGHT + 2 * k].setBrightness (flash[k] > 0.f ? 0.f : lime);
            lights[SCENE_LIGHT + 2 * k + 1].setBrightness (flash[k] > 0.f ? 1.f : 0.f);   // magenta: just saved
        }
    }

    // Initialize: every control back to default (Module::onReset) and the scenes emptied.
    void onReset (const ResetEvent& e) override
    {
        Module::onReset (e);
        for (loop::Scene& s : scenes) s = loop::Scene();
        current = requested = -1;
        gN = gLen = 0;
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* arr = json_array();
        for (const loop::Scene& s : scenes)
        {
            if (! s.saved) { json_array_append_new (arr, json_null()); continue; }
            json_t* tracks = json_array();
            for (const loop::SceneTrack& t : s.t)
            {
                json_t* o = json_object();
                json_object_set_new (o, "mute", json_boolean (t.mute));
                json_object_set_new (o, "speed", json_integer (t.speed));
                json_object_set_new (o, "filter", json_real (t.filter));
                json_object_set_new (o, "level", json_real (t.level));
                json_array_append_new (tracks, o);
            }
            json_array_append_new (arr, tracks);
        }
        json_object_set_new (root, "scenes", arr);
        json_object_set_new (root, "current", json_integer (current));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        json_t* arr = json_object_get (root, "scenes");
        for (int k = 0; k < SCENES; ++k)
        {
            loop::Scene& s = scenes[k];
            s = loop::Scene();
            json_t* tracks = json_array_get (arr, k);   // null for an empty scene or a short array
            if (json_array_size (tracks) != (size_t) N) continue;
            s.saved = true;
            for (int i = 0; i < N; ++i)
            {
                json_t* o = json_array_get (tracks, i);
                json_t* m = json_object_get (o, "mute");   // a field of the wrong type keeps its default
                json_t* sp = json_object_get (o, "speed");
                json_t* f = json_object_get (o, "filter");
                json_t* lv = json_object_get (o, "level");
                if (json_is_boolean (m)) s.t[i].mute = json_boolean_value (m);
                if (json_is_integer (sp)) s.t[i].speed = clamp ((int) json_integer_value (sp), 0, 2);
                if (json_is_number (f)) s.t[i].filter = clamp ((float) json_number_value (f), -1.f, 1.f);
                if (json_is_number (lv)) s.t[i].level = clamp ((float) json_number_value (lv), 0.f, 2.f);
            }
        }
        json_t* c = json_object_get (root, "current");
        current = json_is_integer (c) ? clamp ((int) json_integer_value (c), -1, SCENES - 1) : -1;
        if (current >= 0 && ! scenes[current].saved) current = -1;
        requested = -1;
    }
};

// Dims the face while no Looper sits on the right.
struct FxFaceDim : TransparentWidget
{
    LooperFX* module = nullptr;
    void draw (const DrawArgs& a) override
    {
        if (! module || module->linked.load (std::memory_order_relaxed)) return;
        nvgBeginPath (a.vg);
        nvgCircle (a.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2);
        nvgFillColor (a.vg, nvgRGBA (0x16, 0x16, 0x1a, 0x80));
        nvgFill (a.vg);
    }
};

struct LooperFXWidget : ModuleWidget
{
    LooperFXWidget (LooperFX* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/LooperFX.svg")));
        namespace L = layout::looper_fx;
        const Vec facePos = mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2]));
        const Vec faceSize = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2]));
        TalkingFace* f = createWidget<TalkingFace> (facePos);
        f->box.size = faceSize;
        if (module) f->tap = &module->face;
        addChild (f);
        FxFaceDim* dim = createWidget<FxFaceDim> (facePos);
        dim->box.size = faceSize;
        dim->module = module;
        addChild (dim);
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        static const float* const filters[5] = { L::FILTER0_PARAM, L::FILTER1_PARAM, L::FILTER2_PARAM, L::FILTER3_PARAM, L::FILTER4_PARAM };
        static const float* const speeds[5] = { L::SPEED0_PARAM, L::SPEED1_PARAM, L::SPEED2_PARAM, L::SPEED3_PARAM, L::SPEED4_PARAM };
        static const float* const mutes[5] = { L::MUTE0_PARAM, L::MUTE1_PARAM, L::MUTE2_PARAM, L::MUTE3_PARAM, L::MUTE4_PARAM };
        static const float* const stuts[5] = { L::STUT0_PARAM, L::STUT1_PARAM, L::STUT2_PARAM, L::STUT3_PARAM, L::STUT4_PARAM };
        static const float* const tapes[5] = { L::TAPE0_PARAM, L::TAPE1_PARAM, L::TAPE2_PARAM, L::TAPE3_PARAM, L::TAPE4_PARAM };
        for (int i = 0; i < LooperFX::N; ++i)
        {
            addParam (createParamCentered<Rogan1PSWhite> (at (filters[i]), module, LooperFX::FILTER_PARAM + i));
            addParam (createParamCentered<CKSSThree> (at (speeds[i]), module, LooperFX::SPEED_PARAM + i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (mutes[i]), module, LooperFX::MUTE_PARAM + i, LooperFX::MUTE_LIGHT + i));
            addParam (createParamCentered<VCVButton> (at (stuts[i]), module, LooperFX::STUT_PARAM + i));
            addParam (createParamCentered<VCVButton> (at (tapes[i]), module, LooperFX::TAPE_PARAM + i));
        }
        static const float* const pads[8] = { L::SCENE0_PARAM, L::SCENE1_PARAM, L::SCENE2_PARAM, L::SCENE3_PARAM,
                                              L::SCENE4_PARAM, L::SCENE5_PARAM, L::SCENE6_PARAM, L::SCENE7_PARAM };
        for (int k = 0; k < LooperFX::SCENES; ++k)   // a pad with its ring light behind it, PAD_RING mm bigger on every side
        {
            ParamWidget* p = createParamCentered<ScenePad> (at (pads[k]), module, LooperFX::SCENE_PARAM + k);
            PadRing* ring = createLightCentered<PadRing> (p->box.getCenter(), module, LooperFX::SCENE_LIGHT + 2 * k);
            ring->box.size = p->box.size.plus (mm2px (Vec (2 * L::PAD_RING, 2 * L::PAD_RING)));
            ring->box.pos = p->box.getCenter().minus (ring->box.size.div (2));
            addChild (ring);
            addParam (p);
        }
        addParam (createParamCentered<Rogan1PSWhite> (at (L::STUTLEN_PARAM), module, LooperFX::STUTLEN_PARAM));
        addInput (createInputCentered<PJ301MPort> (at (L::SCENE_INPUT), module, LooperFX::SCENE_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::STUT_INPUT), module, LooperFX::STUT_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::TAPE_INPUT), module, LooperFX::TAPE_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::FILTER_INPUT), module, LooperFX::FILTER_INPUT));
    }

    void appendContextMenu (Menu* menu) override
    {
        LooperFX* m = dynamic_cast<LooperFX*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createMenuLabel (m->linked.load() ? "Linked to the My lil Looper on the right" : "No My lil Looper on the right"));
    }
};

Model* modelLooperFX = createModel<LooperFX, LooperFXWidget> ("LooperFX");
