// LooperSong.cpp: the My lil Looper Song expander. Placed left of a My lil Looper FX (which sits left of the Looper): SONG REC
// records which scene the FX applied on which bar, PLAY plays it back bar-exact by asking the FX for each scene one bar
// early (core/Song.h), LOOP repeats it. Messages through the FX: src/FxLink.hpp (SongBack in, SongLink out).
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout_song.hpp"
#include "Pad.hpp"
#include "FxLink.hpp"
#include "Song.h"
#include "TalkingFace.hpp"
#include <atomic>

struct LooperSong : Module
{
    enum ParamId { REC_PARAM, PLAY_PARAM, LOOP_PARAM, PARAMS_LEN };
    enum InputId { PLAY_INPUT, REC_INPUT, INPUTS_LEN };
    enum OutputId { END_OUTPUT, BAR_OUTPUT, OUTPUTS_LEN };
    enum LightId { ENUMS (REC_LIGHT, 2), ENUMS (PLAY_LIGHT, 2), LOOP_LIGHT, LIGHTS_LEN };

    SongBack backMsg[2];                  // the right expander's double buffer: the FX writes one, we read the other
    uint32_t lastBackSeq = 0, linkSeq = 0, reqSeq = 0;
    int reqScene = -1;
    loop::SongEngine engine;
    std::atomic<bool> linked { false };
    std::atomic<int> shownMode { 0 }, shownBar { 0 }, shownLen { 0 };   // for the readout (UI thread)
    dsp::BooleanTrigger recBtn, playBtn;
    dsp::SchmittTrigger recTrig, playTrig;
    dsp::PulseGenerator endPulse, barPulse;
    FaceTap face;   // the mascot's mouth: a pulse on every bar
    float mouth = 0.f;
    float blinkTime = 0.f;
    dsp::ClockDivider lightDiv;

    LooperSong()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configButton (REC_PARAM, "Song record (on the next bar; press again to stop on the next bar)");
        configButton (PLAY_PARAM, "Song play / stop (starts on the next bar)");
        configSwitch (LOOP_PARAM, 0.f, 1.f, 1.f, "Loop the song", { "Off: stop at the end", "On: start over" });
        configInput (PLAY_INPUT, "Play / stop trigger");
        configInput (REC_INPUT, "Record trigger");
        configOutput (END_OUTPUT, "End of song (trigger)");
        configOutput (BAR_OUTPUT, "Bar (trigger on every downbeat)");
        rightExpander.producerMessage = &backMsg[0];
        rightExpander.consumerMessage = &backMsg[1];
        lightDiv.setDivision (64);
    }

    void process (const ProcessArgs& args) override
    {
        const SongBack& back = *(const SongBack*) rightExpander.consumerMessage;
        const bool fx = rightExpander.module && rightExpander.module->model == modelLooperFX;
        const bool fresh = fx && back.seq != lastBackSeq;
        lastBackSeq = back.seq;
        const bool on = fresh && back.looper;
        linked.store (on, std::memory_order_relaxed);

        const bool rec = recBtn.process (params[REC_PARAM].getValue() > 0.f) | recTrig.process (inputs[REC_INPUT].getVoltage(), 0.1f, 1.f);
        const bool play = playBtn.process (params[PLAY_PARAM].getValue() > 0.f) | playTrig.process (inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f);
        const bool downbeat = on && back.downbeat;
        const loop::SongEngine::Out o = engine.step (on, downbeat, on ? back.applied : -1, on ? back.current : -1, rec, play,
                                                     params[LOOP_PARAM].getValue() > 0.5f);
        if (o.request >= 0) { ++reqSeq; reqScene = o.request; }
        if (o.end) endPulse.trigger (1e-3f);
        if (downbeat) { barPulse.trigger (1e-3f); mouth = 1.f; }

        if (fx && rightExpander.module->leftExpander.producerMessage)
        {
            SongLink* m = (SongLink*) rightExpander.module->leftExpander.producerMessage;
            m->seq = ++linkSeq;
            m->reqSeq = reqSeq;
            m->reqScene = reqScene;
            rightExpander.module->leftExpander.requestMessageFlip();
        }
        outputs[END_OUTPUT].setVoltage (endPulse.process (args.sampleTime) ? 10.f : 0.f);
        outputs[BAR_OUTPUT].setVoltage (barPulse.process (args.sampleTime) ? 10.f : 0.f);
        mouth *= 1.f - 8.f * args.sampleTime;   // the mouth opens on each bar and closes over about 1/8 s
        face.push (mouth, args.sampleRate);

        shownMode.store ((int) engine.mode, std::memory_order_relaxed);
        shownBar.store (engine.bar, std::memory_order_relaxed);
        shownLen.store (engine.song.len, std::memory_order_relaxed);
        if (lightDiv.process()) updateLights (args.sampleTime * lightDiv.getDivision());
    }

    void updateLights (float dt)
    {
        using E = loop::SongEngine;
        blinkTime = std::fmod (blinkTime + dt, 0.5f);
        const bool blink = blinkTime < 0.25f;
        const E::Mode m = engine.mode;
        lights[REC_LIGHT].setBrightness (0.f);
        lights[REC_LIGHT + 1].setBrightness (m == E::RECORDING ? 1.f : (m == E::REC_ARMED || m == E::REC_STOPPING) && blink ? 1.f : 0.f);
        lights[PLAY_LIGHT].setBrightness (m == E::PLAYING ? 1.f : m == E::PLAY_ARMED && blink ? 1.f : 0.f);
        lights[PLAY_LIGHT + 1].setBrightness (0.f);
        lights[LOOP_LIGHT].setBrightness (params[LOOP_PARAM].getValue());
    }

    // Initialize: the song is emptied (LOOP goes back to its default with the other params).
    void onReset (const ResetEvent& e) override
    {
        Module::onReset (e);
        engine = loop::SongEngine();
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* ev = json_array();
        for (int i = 0; i < engine.song.n; ++i)
        {
            json_t* pair = json_array();
            json_array_append_new (pair, json_integer (engine.song.bar[i]));
            json_array_append_new (pair, json_integer (engine.song.scene[i]));
            json_array_append_new (ev, pair);
        }
        json_object_set_new (root, "events", ev);
        json_object_set_new (root, "len", json_integer (engine.song.len));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        engine = loop::SongEngine();   // a reloaded song comes back stopped
        json_t* len = json_object_get (root, "len");
        if (! json_is_integer (len)) return;
        loop::SongData& s = engine.song;
        s.len = clamp ((int) json_integer_value (len), 0, loop::SONG_MAX_BARS);
        json_t* ev = json_object_get (root, "events");
        for (size_t i = 0; i < json_array_size (ev); ++i)
        {
            json_t* pair = json_array_get (ev, i);
            json_t *b = json_array_get (pair, 0), *k = json_array_get (pair, 1);
            if (! json_is_integer (b) || ! json_is_integer (k)) continue;
            const int bar = (int) json_integer_value (b), scene = (int) json_integer_value (k);
            if (bar < 0 || bar >= s.len || scene < 0 || scene > 7) continue;
            if (! s.put (bar, scene)) break;
        }
    }
};

// The bar readout: "12 / 32" playing, "REC 7" recording, "-- / 32" stopped with a song, "--" without.
struct SongReadout : TransparentWidget
{
    LooperSong* module = nullptr;
    void drawLayer (const DrawArgs& a, int layer) override
    {
        if (layer != 1) return;
        std::string t = "1 / 4";   // the library preview
        if (module)
        {
            using E = loop::SongEngine;
            const int m = module->shownMode.load(), bar = module->shownBar.load(), len = module->shownLen.load();
            if (m == E::RECORDING || m == E::REC_STOPPING) t = string::f ("REC %d", bar + 1);
            else if (m == E::REC_ARMED) t = "REC --";
            else if (m == E::PLAYING) t = string::f ("%d / %d", bar + 1, len);
            else t = len > 0 ? string::f ("-- / %d", len) : "--";
        }
        std::shared_ptr<window::Font> font = APP->window->loadFont (asset::system ("res/fonts/DejaVuSans.ttf"));
        if (! font) return;
        nvgFontFaceId (a.vg, font->handle);
        nvgFontSize (a.vg, box.size.y * 0.62f);
        nvgTextAlign (a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (a.vg, nvgRGB (0xc8, 0xff, 0x3a));
        nvgText (a.vg, box.size.x / 2, box.size.y / 2, t.c_str(), nullptr);
    }
};

// Dims the face while no FX with a Looper sits on the right.
struct SongFaceDim : TransparentWidget
{
    LooperSong* module = nullptr;
    void draw (const DrawArgs& a) override
    {
        if (! module || module->linked.load (std::memory_order_relaxed)) return;
        nvgBeginPath (a.vg);
        nvgCircle (a.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2);
        nvgFillColor (a.vg, nvgRGBA (0x16, 0x16, 0x1a, 0x80));
        nvgFill (a.vg);
    }
};

struct LooperSongWidget : ModuleWidget
{
    LooperSongWidget (LooperSong* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/LooperSong.svg")));
        namespace L = layout::looper_song;
        const Vec facePos = mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2]));
        const Vec faceSize = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2]));
        TalkingFace* f = createWidget<TalkingFace> (facePos);
        f->box.size = faceSize;
        if (module) f->tap = &module->face;
        addChild (f);
        SongFaceDim* dim = createWidget<SongFaceDim> (facePos);
        dim->box.size = faceSize;
        dim->module = module;
        addChild (dim);
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        SongReadout* r = createWidget<SongReadout> (mm2px (Vec (L::DISPLAY[0] - L::DISPLAY_SIZE[0] / 2, L::DISPLAY[1] - L::DISPLAY_SIZE[1] / 2)));
        r->box.size = mm2px (Vec (L::DISPLAY_SIZE[0], L::DISPLAY_SIZE[1]));
        r->module = module;
        addChild (r);
        auto pad = [&] (ParamWidget* p, int light) {   // a pad with its ring light behind it
            PadRing* ring = createLightCentered<PadRing> (p->box.getCenter(), module, light);
            ring->box.size = p->box.size.plus (mm2px (Vec (2 * L::PAD_RING, 2 * L::PAD_RING)));
            ring->box.pos = p->box.getCenter().minus (ring->box.size.div (2));
            addChild (ring);
            addParam (p);
        };
        pad (createParamCentered<Pad> (at (L::REC_PARAM), module, LooperSong::REC_PARAM), LooperSong::REC_LIGHT);
        pad (createParamCentered<Pad> (at (L::PLAY_PARAM), module, LooperSong::PLAY_PARAM), LooperSong::PLAY_LIGHT);
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (L::LOOP_PARAM), module, LooperSong::LOOP_PARAM, LooperSong::LOOP_LIGHT));
        addInput (createInputCentered<PJ301MPort> (at (L::PLAY_INPUT), module, LooperSong::PLAY_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::REC_INPUT), module, LooperSong::REC_INPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::END_OUTPUT), module, LooperSong::END_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::BAR_OUTPUT), module, LooperSong::BAR_OUTPUT));
    }

    void appendContextMenu (Menu* menu) override
    {
        LooperSong* m = dynamic_cast<LooperSong*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createMenuLabel (m->linked.load() ? "Linked: My lil Looper FX and My lil Looper on the right"
                                                         : "Needs My lil Looper FX on the right, with My lil Looper right of it"));
    }
};

Model* modelLooperSong = createModel<LooperSong, LooperSongWidget> ("LooperSong");
