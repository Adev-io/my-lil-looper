// Looper.cpp: the Rack module. Five tracks (core/Track.h) synced by core/Sync.h, audio in core/BlockPool.h blocks that a
// worker thread keeps topped up. Loops are saved as WAVs in the patch storage folder (core/Wav.h).
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Pad.hpp"
#include "BlockPool.h"
#include "Sync.h"
#include "Track.h"
#include "Wav.h"
#include "TalkingFace.hpp"
#include <atomic>
#include <chrono>
#include <thread>

static const int PER_BAR[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 96 };
static const int MAX_MINUTES[] = { 1, 2, 5, 10 };

struct Looper : Module
{
    static constexpr int N = 5;
    enum ParamId { ENUMS (REC_PARAM, N), ENUMS (STOP_PARAM, N), ENUMS (UNDO_PARAM, N), ENUMS (REV_PARAM, N), ENUMS (ONESHOT_PARAM, N),
                   ENUMS (LEVEL_PARAM, N), ALL_PARAM, PARAMS_LEN };
    enum InputId { ENUMS (TRACK_INPUT, N), IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, RESET_INPUT, REC_INPUT, STOP_INPUT, CLEAR_INPUT, LEVEL_INPUT, INPUTS_LEN };
    enum OutputId { ENUMS (TRACK_OUTPUT, N), OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { ENUMS (PAD_LIGHT, 2 * N), ENUMS (UNDO_LIGHT, 2 * N), ENUMS (REV_LIGHT, N), ENUMS (ONESHOT_LIGHT, N), ENUMS (ALL_LIGHT, 2), LIGHTS_LEN };

    loop::BlockPool pool;
    loop::Sync sync;
    loop::Track tracks[N];
    FaceTap face;   // the mascot's mouth: the main out
    unsigned savedVersion[N] = {};

    // context menu (UI thread writes, process reads)
    bool dubAfterRec = false;
    bool monitor = false;
    int perBar = 4;
    int maxMinutes = 5;

    dsp::BooleanTrigger recBtn[N], stopBtn[N], undoBtn[N], allBtn;
    dsp::SchmittTrigger recTrig[N], stopTrig[N], clearTrig[N], clockTrig, resetTrig;
    float stopHeld[N] = {};
    bool stopCleared[N] = {};
    dsp::ClockDivider lightDiv;
    float blinkTime = 0.f;
    float appliedRate = 0.f;
    int appliedMinutes = 0;
    // The rate the loop frames (tracks and master) are counted in. Set when that frame space is (re)defined: at load and
    // while the looper is empty. A later engine rate change keeps it: the frames keep their meaning, the pitch shifts.
    float loopRate = 48000.f;
    double savedMaster = 0, savedRate = 0;   // the patch's master and loopRate, from dataFromJson for onAdd

    std::atomic<bool> working { true };
    std::thread worker;

    Looper()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int i = 0; i < N; ++i)
        {
            const std::string t = "Track " + std::to_string (i + 1);
            configButton (REC_PARAM + i, t + " record / play / overdub");
            configButton (STOP_PARAM + i, t + " stop (hold 2 s: clear)");
            configButton (UNDO_PARAM + i, t + " undo / redo the last overdub");
            configSwitch (REV_PARAM + i, 0.f, 1.f, 0.f, t + " reverse", { "Off", "On" });
            configSwitch (ONESHOT_PARAM + i, 0.f, 1.f, 0.f, t + " one-shot", { "Off", "On" });
            configParam (LEVEL_PARAM + i, 0.f, 2.f, 1.f, t + " level", " dB", -10.f, 20.f);
            configInput (TRACK_INPUT + i, t + " input (poly: 1 = L, 2 = R; overrides IN L/R)");
            configOutput (TRACK_OUTPUT + i, t + " output (poly: 1 = L, 2 = R)");
        }
        configButton (ALL_PARAM, "All start / stop");
        configInput (IN_L_INPUT, "Left");
        configInput (IN_R_INPUT, "Right (normalled to left)");
        configInput (CLOCK_INPUT, "Clock (record and play snap to its bars)");
        configInput (RESET_INPUT, "Reset to the downbeat");
        configInput (REC_INPUT, "Record / play triggers (poly: channel n = track n)");
        configInput (STOP_INPUT, "Stop triggers (poly: channel n = track n)");
        configInput (CLEAR_INPUT, "Clear triggers (poly: channel n = track n)");
        configInput (LEVEL_INPUT, "Level CV (poly: channel n = track n, 10 V = x1)");
        configOutput (OUT_L_OUTPUT, "Left");
        configOutput (OUT_R_OUTPUT, "Right");
        configBypass (IN_L_INPUT, OUT_L_OUTPUT);
        configBypass (IN_R_INPUT, OUT_R_OUTPUT);
        lightDiv.setDivision (64);
        pool.refill();
        worker = std::thread ([this] {
            while (working.load())
            {
                pool.refill();
                std::this_thread::sleep_for (std::chrono::milliseconds (20));
            }
        });
    }

    ~Looper()
    {
        working = false;
        worker.join();
        for (loop::Track& t : tracks) t.freeAll (pool);
        pool.drain();
    }

    static float trig (Input& in, int c) { return c < in.getChannels() ? in.getVoltage (c) : 0.f; }

    void process (const ProcessArgs& args) override
    {
        if (args.sampleRate != appliedRate || maxMinutes != appliedMinutes)
        {
            appliedRate = args.sampleRate;
            appliedMinutes = maxMinutes;
            for (loop::Track& t : tracks) t.setSampleRate (appliedRate, 60.f * appliedMinutes);
        }
        sync.perBar = perBar;

        bool anyRunning = false;
        for (loop::Track& t : tracks) anyRunning = anyRunning || t.running();
        if (allBtn.process (params[ALL_PARAM].getValue() > 0.f))
            for (loop::Track& t : tracks) { if (anyRunning) t.stopAll(); else t.armPlay(); }

        for (int i = 0; i < N; ++i)
        {
            loop::Track& t = tracks[i];
            t.dubAfterRec = dubAfterRec;
            t.reverse = params[REV_PARAM + i].getValue() > 0.5f;
            t.oneShot = params[ONESHOT_PARAM + i].getValue() > 0.5f;
            const bool rec = recBtn[i].process (params[REC_PARAM + i].getValue() > 0.f);
            if (recTrig[i].process (trig (inputs[REC_INPUT], i), 0.1f, 1.f) || rec) t.pressRec (pool);
            const bool held = params[STOP_PARAM + i].getValue() > 0.f;
            if (stopTrig[i].process (trig (inputs[STOP_INPUT], i), 0.1f, 1.f) || stopBtn[i].process (held)) t.pressStop();
            stopHeld[i] = held ? stopHeld[i] + args.sampleTime : 0.f;
            if (! held) stopCleared[i] = false;
            if ((stopHeld[i] >= 2.f && ! stopCleared[i]) || clearTrig[i].process (trig (inputs[CLEAR_INPUT], i), 0.1f, 1.f))
            {
                t.requestClear (pool);
                stopCleared[i] = held;
            }
            if (undoBtn[i].process (params[UNDO_PARAM + i].getValue() > 0.f)) t.pressUndo();
        }

        const bool reset = resetTrig.process (inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
        const bool edge = clockTrig.process (inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
        const bool beat = sync.tick (inputs[CLOCK_INPUT].isConnected(), edge, reset);
        if (reset) for (loop::Track& t : tracks) t.rewind();

        const float mainL = inputs[IN_L_INPUT].getVoltage();
        const float mainR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : mainL;
        float sumL = monitor ? mainL : 0.f, sumR = monitor ? mainR : 0.f;
        bool allEmpty = true;
        for (int i = 0; i < N; ++i)
        {
            loop::Track& t = tracks[i];
            float inL = mainL, inR = mainR;
            Input& own = inputs[TRACK_INPUT + i];
            if (own.isConnected())
            {
                inL = own.getVoltage (0);
                inR = own.getChannels() > 1 ? own.getVoltage (1) : inL;
            }
            float l, r;
            t.process (inL, inR, beat, sync.synced(), pool, l, r);
            if (t.justClosed && ! sync.synced()) sync.setMaster (t.len);
            allEmpty = allEmpty && t.state == loop::Track::EMPTY && t.pending == loop::Track::NONE;
            float g = params[LEVEL_PARAM + i].getValue();
            if (i < inputs[LEVEL_INPUT].getChannels()) g *= clamp (inputs[LEVEL_INPUT].getVoltage (i) / 10.f, 0.f, 1.f);
            l *= g;
            r *= g;
            Output& out = outputs[TRACK_OUTPUT + i];
            out.setChannels (2);
            out.setVoltage (l, 0);
            out.setVoltage (r, 1);
            sumL += l;
            sumR += r;
        }
        if (allEmpty && sync.masterLen > 0) sync.clear();
        if (allEmpty && loopRate != args.sampleRate) loopRate = args.sampleRate;
        outputs[OUT_L_OUTPUT].setVoltage (sumL);
        outputs[OUT_R_OUTPUT].setVoltage (sumR);
        face.push (clamp ((sumL + sumR) / 10.f, -1.f, 1.f), args.sampleRate);

        if (lightDiv.process()) updateLights (args.sampleTime * lightDiv.getDivision(), anyRunning);
    }

    void updateLights (float dt, bool anyRunning)
    {
        blinkTime = std::fmod (blinkTime + dt, 1.f);
        const bool blink = blinkTime < 0.25f || (blinkTime >= 0.5f && blinkTime < 0.75f);
        const float pulse = 0.6f + 0.4f * std::sin (2.f * M_PI * 2.f * blinkTime);
        for (int i = 0; i < N; ++i)
        {
            const loop::Track& t = tracks[i];
            float lime = 0.f, magenta = 0.f;
            if (t.pending != loop::Track::NONE) magenta = blink ? 1.f : 0.f;
            else if (t.state == loop::Track::RECORDING) magenta = 1.f;
            else if (t.state == loop::Track::OVERDUBBING) magenta = pulse;
            else if (t.state == loop::Track::PLAYING) lime = 1.f;
            else if (t.state == loop::Track::STOPPED) lime = 0.2f;
            lights[PAD_LIGHT + 2 * i].setBrightness (lime);
            lights[PAD_LIGHT + 2 * i + 1].setBrightness (magenta);
            lights[UNDO_LIGHT + 2 * i].setBrightness (t.hasUndo() && t.isUndone() ? 1.f : 0.f);       // lime: redo ready
            lights[UNDO_LIGHT + 2 * i + 1].setBrightness (t.hasUndo() && ! t.isUndone() ? 1.f : 0.f); // magenta: undo ready
            lights[REV_LIGHT + i].setBrightness (params[REV_PARAM + i].getValue());
            lights[ONESHOT_LIGHT + i].setBrightness (params[ONESHOT_PARAM + i].getValue());
        }
        lights[ALL_LIGHT].setBrightness (anyRunning ? 1.f : 0.f);
        lights[ALL_LIGHT + 1].setBrightness (0.f);
    }

    // Initialize: the engine does not run process() meanwhile, so the tracks can be emptied from this thread.
    void onReset (const ResetEvent& e) override
    {
        Module::onReset (e);
        for (loop::Track& t : tracks) t.freeAll (pool);
        sync.clear();
        dubAfterRec = monitor = false;
        perBar = 4;
        maxMinutes = 5;
    }

    std::string trackPath (const std::string& dir, int i) { return system::join (dir, string::f ("track%d.wav", i + 1)); }

    // Patch save: write each changed track. The pool's freeLock keeps returned blocks alive while their pointers are read.
    void onSave (const SaveEvent&) override
    {
        const std::string dir = createPatchStorageDirectory();
        const int rate = (int) std::lround (loopRate);
        std::lock_guard<std::mutex> hold (pool.freeLock);
        for (int i = 0; i < N; ++i)
        {
            loop::Track& t = tracks[i];
            const unsigned v = t.version;   // ponytail: plain reads of audio-thread fields; a save mid-overdub can catch a half-written block
            if (v == savedVersion[i]) continue;
            const std::string path = trackPath (dir, i);
            const int len = t.len, nb = t.nBlocks;
            if (t.state == loop::Track::RECORDING || t.state == loop::Track::EMPTY || len == 0)
            {
                system::remove (path);
                if (t.state != loop::Track::RECORDING) savedVersion[i] = v;
                continue;
            }
            std::vector<loop::Block*> list (t.blocks, t.blocks + nb);
            const std::string tmp = path + ".tmp";   // the old file stays until the new one is whole
            wav::Writer w;
            if (! w.open (tmp, rate)) { WARN ("Looper: cannot write %s", tmp.c_str()); continue; }
            bool whole = true;
            for (int b = 0; b * loop::BLOCK < len && whole; ++b)
            {
                whole = b < nb && list[b];
                if (whole) w.write (list[b]->l, list[b]->r, std::min (loop::BLOCK, len - b * loop::BLOCK));
            }
            if (w.close() && whole && system::rename (tmp, path)) savedVersion[i] = v;
            else
            {
                system::remove (tmp);
                WARN ("Looper: track %d not saved", i + 1);
            }
        }
    }

    // Patch load (after dataFromJson: Rack's Engine::fromJson runs module->fromJson, then addModule -> onAdd; the engine
    // does not run process() meanwhile): read the saved tracks, each from its own rate to the engine's.
    void onAdd (const AddEvent&) override
    {
        const std::string dir = getPatchStorageDirectory();
        const int rate = (int) std::lround (APP->engine->getSampleRate());
        const int64_t cap = (int64_t) loop::MAX_BLOCKS * loop::BLOCK;
        loopRate = (float) rate;
        const double oldMaster = savedMaster, oldRate = savedRate;
        savedMaster = savedRate = 0;
        const int64_t master = oldMaster > 0 ? (int64_t) std::min ((double) cap, std::round (oldMaster * rate / oldRate)) : 0;
        if (master > 0) sync.setMaster ((int) master);   // no tracks load: process() clears it again
        for (int i = 0; i < N; ++i)
        {
            const std::string path = trackPath (dir, i);
            if (! system::exists (path)) continue;
            std::vector<loop::Block*> list;
            try
            {
                std::vector<float> l, r;
                int fileRate = 0;
                if (! wav::read (path, l, r, fileRate, cap, rate)) { WARN ("Looper: cannot read %s", path.c_str()); continue; }
                // A loop saved as k masters comes back as exactly k masters, so it stays on the downbeats.
                const int64_t len = (int64_t) l.size(), k = oldMaster > 0 ? std::llround (len / oldMaster) : 0;
                const bool snap = master > 0 && fileRate == oldRate && k >= 1 && std::abs (len - k * oldMaster) <= 1 && k * master <= cap;
                const size_t want = snap ? (size_t) (k * master) : (size_t) std::max (1LL, std::llround ((double) len * rate / fileRate));
                l = wav::resample (l, want);
                r = wav::resample (r, want);
                const int n = (int) std::min ((int64_t) l.size(), cap);
                if ((int64_t) l.size() >= cap) WARN ("Looper: track %d cut to the %d-block maximum", i + 1, loop::MAX_BLOCKS);
                list.reserve ((n + loop::BLOCK - 1) / loop::BLOCK);   // push_back cannot throw with a block in hand
                for (int at = 0; at < n; at += loop::BLOCK)
                {
                    loop::Block* b = pool.make();
                    if (! b) { WARN ("Looper: out of memory, track %d cut to %d frames", i + 1, at); break; }
                    list.push_back (b);
                    const int m = std::min (loop::BLOCK, n - at);
                    for (int j = 0; j < m; ++j)   // adopted audio skips Track::write(): clean it here
                    {
                        b->l[j] = loop::clean (l[at + j]);
                        b->r[j] = loop::clean (r[at + j]);
                    }
                }
                tracks[i].adopt (list.data(), (int) list.size(), n, pool);
                savedVersion[i] = tracks[i].version;
            }
            catch (const std::exception& e)   // one escaping onAdd takes Rack down
            {
                WARN ("Looper: cannot load track %d: %s", i + 1, e.what());
                for (loop::Block* b : list) pool.give (b);
            }
        }
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_object_set_new (root, "dubAfterRec", json_boolean (dubAfterRec));
        json_object_set_new (root, "monitor", json_boolean (monitor));
        json_object_set_new (root, "perBar", json_integer (perBar));
        json_object_set_new (root, "maxMinutes", json_integer (maxMinutes));
        json_object_set_new (root, "masterLen", json_integer (sync.masterLen));
        json_object_set_new (root, "sampleRate", json_integer (std::lround (loopRate)));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        if (json_t* j = json_object_get (root, "dubAfterRec")) dubAfterRec = json_boolean_value (j);
        if (json_t* j = json_object_get (root, "monitor")) monitor = json_boolean_value (j);
        if (json_t* j = json_object_get (root, "perBar")) perBar = std::max (1, (int) json_integer_value (j));
        if (json_t* j = json_object_get (root, "maxMinutes")) maxMinutes = clamp ((int) json_integer_value (j), 1, 10);
        bool empty = true;   // a preset or paste onto a module with loops keeps its master
        for (loop::Track& t : tracks) empty = empty && t.state == loop::Track::EMPTY;
        json_t* m = json_object_get (root, "masterLen");
        json_t* s = json_object_get (root, "sampleRate");
        if (empty && m && s && json_integer_value (m) > 0 && json_integer_value (s) > 0)
        {
            savedMaster = (double) json_integer_value (m);
            savedRate = (double) json_integer_value (s);
        }
    }
};

struct LooperWidget : ModuleWidget
{
    LooperWidget (Looper* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Looper.svg")));
        namespace L = layout::looper;
        {
            TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2])));
            f->box.size = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2]));
            if (module) f->tap = &module->face;
            addChild (f);
        }
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        // A pad with its ring light behind it, PAD_RING mm bigger on every side.
        auto pad = [&] (ParamWidget* p, int light) {
            PadRing* ring = createLightCentered<PadRing> (p->box.getCenter(), module, light);
            ring->box.size = p->box.size.plus (mm2px (Vec (2 * L::PAD_RING, 2 * L::PAD_RING)));
            ring->box.pos = p->box.getCenter().minus (ring->box.size.div (2));
            addChild (ring);
            addParam (p);
        };

        static const float* const recs[5] = { L::REC0_PARAM, L::REC1_PARAM, L::REC2_PARAM, L::REC3_PARAM, L::REC4_PARAM };
        static const float* const stops[5] = { L::STOP0_PARAM, L::STOP1_PARAM, L::STOP2_PARAM, L::STOP3_PARAM, L::STOP4_PARAM };
        static const float* const undos[5] = { L::UNDO0_PARAM, L::UNDO1_PARAM, L::UNDO2_PARAM, L::UNDO3_PARAM, L::UNDO4_PARAM };
        static const float* const revs[5] = { L::REV0_PARAM, L::REV1_PARAM, L::REV2_PARAM, L::REV3_PARAM, L::REV4_PARAM };
        static const float* const shots[5] = { L::ONESHOT0_PARAM, L::ONESHOT1_PARAM, L::ONESHOT2_PARAM, L::ONESHOT3_PARAM, L::ONESHOT4_PARAM };
        static const float* const levels[5] = { L::LEVEL0_PARAM, L::LEVEL1_PARAM, L::LEVEL2_PARAM, L::LEVEL3_PARAM, L::LEVEL4_PARAM };
        static const float* const ins[5] = { L::TRACK0_INPUT, L::TRACK1_INPUT, L::TRACK2_INPUT, L::TRACK3_INPUT, L::TRACK4_INPUT };
        static const float* const outs[5] = { L::TRACK0_OUTPUT, L::TRACK1_OUTPUT, L::TRACK2_OUTPUT, L::TRACK3_OUTPUT, L::TRACK4_OUTPUT };
        for (int i = 0; i < Looper::N; ++i)
        {
            pad (createParamCentered<Pad> (at (recs[i]), module, Looper::REC_PARAM + i), Looper::PAD_LIGHT + 2 * i);
            addParam (createParamCentered<PadLow> (at (stops[i]), module, Looper::STOP_PARAM + i));
            addParam (createLightParamCentered<VCVLightButton<MediumSimpleLight<LimeMagentaLight>>> (at (undos[i]), module, Looper::UNDO_PARAM + i, Looper::UNDO_LIGHT + 2 * i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (revs[i]), module, Looper::REV_PARAM + i, Looper::REV_LIGHT + i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (shots[i]), module, Looper::ONESHOT_PARAM + i, Looper::ONESHOT_LIGHT + i));
            addParam (createParamCentered<Rogan1PSWhite> (at (levels[i]), module, Looper::LEVEL_PARAM + i));
            addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, Looper::TRACK_INPUT + i));
            addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, Looper::TRACK_OUTPUT + i));
        }
        pad (createParamCentered<Pad> (at (L::ALL_PARAM), module, Looper::ALL_PARAM), Looper::ALL_LIGHT);
        addInput (createInputCentered<PJ301MPort> (at (L::IN_L_INPUT), module, Looper::IN_L_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::IN_R_INPUT), module, Looper::IN_R_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::CLOCK_INPUT), module, Looper::CLOCK_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::RESET_INPUT), module, Looper::RESET_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::REC_INPUT), module, Looper::REC_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::STOP_INPUT), module, Looper::STOP_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::CLEAR_INPUT), module, Looper::CLEAR_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::LEVEL_INPUT), module, Looper::LEVEL_INPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::OUT_L_OUTPUT), module, Looper::OUT_L_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::OUT_R_OUTPUT), module, Looper::OUT_R_OUTPUT));
    }

    void appendContextMenu (Menu* menu) override
    {
        Looper* m = dynamic_cast<Looper*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createBoolPtrMenuItem ("Record then overdub (REC > DUB > PLAY)", "", &m->dubAfterRec));
        menu->addChild (createBoolPtrMenuItem ("Input monitor (IN L/R to the main out)", "", &m->monitor));
        std::vector<std::string> bars, mins;
        for (int p : PER_BAR) bars.push_back (std::to_string (p));
        for (int p : MAX_MINUTES) mins.push_back (std::to_string (p) + " min");
        menu->addChild (createIndexSubmenuItem ("Clock pulses per bar", bars,
            [=] { for (size_t i = 0; i < bars.size(); ++i) if (PER_BAR[i] == m->perBar) return i; return (size_t) 3; },
            [=] (size_t i) { m->perBar = PER_BAR[i]; }));
        menu->addChild (createIndexSubmenuItem ("Max length per track", mins,
            [=] { for (size_t i = 0; i < mins.size(); ++i) if (MAX_MINUTES[i] == m->maxMinutes) return i; return (size_t) 2; },
            [=] (size_t i) { m->maxMinutes = MAX_MINUTES[i]; }));
        menu->addChild (createMenuLabel (string::f ("Memory in use: %ld MB", m->pool.allocated.load() / 2)));
    }
};

Model* modelLooper = createModel<Looper, LooperWidget> ("Looper");
