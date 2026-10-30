// LooperCV.cpp: the My lil Looper CV expander. Placed right of a My lil Looper, lane n records its poly CV IN while track
// n records and plays it back while track n plays (core/CvLane.h), following the play head the Looper sends each frame.
// Lanes are saved as cvN.wav in the patch storage folder (core/Wav.h).
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout_cv.hpp"
#include "BlockPool.h"
#include "CvLane.h"
#include "Wav.h"
#include "TalkingFace.hpp"
#include <atomic>
#include <chrono>
#include <thread>

struct LooperCV : Module
{
    static constexpr int N = 5;
    enum ParamId { PARAMS_LEN };
    enum InputId { ENUMS (CV_INPUT, N), INPUTS_LEN };
    enum OutputId { ENUMS (CV_OUTPUT, N), OUTPUTS_LEN };
    enum LightId { ENUMS (LANE_LIGHT, 2 * N), LIGHTS_LEN };

    loop::BlockPool pool;
    loop::CvLane lanes[N];
    loop::CvLink msg[2];              // the left expander's double buffer: the Looper writes one, this module reads the other
    uint32_t lastSeq = 0;
    std::atomic<bool> linked { false };   // a Looper on the left sent a fresh message this frame (the face dims without one)
    uint8_t mode[N] = {};             // this frame's message per lane, for the lights
    FaceTap face;   // the mascot's mouth: the lane outputs
    unsigned savedVersion[N] = {};
    dsp::ClockDivider lightDiv;
    struct Saved { bool has = false; uint32_t take = 0; int decim = 1, trackLen = 0; } saved[N];   // dataFromJson -> onAdd

    std::atomic<bool> working { true };
    std::thread worker;

    LooperCV()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int i = 0; i < N; ++i)
        {
            const std::string n = std::to_string (i + 1);
            configInput (CV_INPUT + i, "Lane " + n + " CV (poly, recorded while track " + n + " records)");
            configOutput (CV_OUTPUT + i, "Lane " + n + " CV (track " + n + "'s take; 0 V when it is stopped)");
        }
        leftExpander.producerMessage = &msg[0];
        leftExpander.consumerMessage = &msg[1];
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

    ~LooperCV()
    {
        working = false;
        worker.join();
        for (loop::CvLane& l : lanes) l.clear (pool);
        pool.drain();
    }

    void process (const ProcessArgs& args) override
    {
        const loop::CvLink& link = *(const loop::CvLink*) leftExpander.consumerMessage;
        const bool fresh = leftExpander.module && leftExpander.module->model == modelLooper && link.seq != lastSeq;
        lastSeq = link.seq;
        linked.store (fresh, std::memory_order_relaxed);
        float sum = 0.f;
        for (int i = 0; i < N; ++i)
        {
            Input& in = inputs[CV_INPUT + i];
            float out[loop::CV_MAX_CH];
            const loop::CvMsg m = link.lane[i];
            const int n = lanes[i].step (m, fresh, in.getVoltages(), in.getChannels(), args.sampleRate, pool, out);
            Output& o = outputs[CV_OUTPUT + i];
            o.setChannels (n);
            o.writeVoltages (out);
            mode[i] = fresh && lanes[i].nch > 0 ? m.mode : (uint8_t) loop::CvMsg::IDLE;
            sum += out[0];
        }
        face.push (clamp (sum / 10.f, -1.f, 1.f), args.sampleRate);

        if (lightDiv.process())
            for (int i = 0; i < N; ++i)
            {
                lights[LANE_LIGHT + 2 * i].setBrightness (mode[i] == loop::CvMsg::PLAY ? 1.f : 0.f);      // lime: playing
                lights[LANE_LIGHT + 2 * i + 1].setBrightness (mode[i] == loop::CvMsg::REC ? 1.f : 0.f);   // magenta: recording
            }
    }

    // Initialize: the engine does not run process() meanwhile.
    void onReset (const ResetEvent& e) override
    {
        Module::onReset (e);
        for (loop::CvLane& l : lanes) l.clear (pool);
    }

    std::string lanePath (const std::string& dir, int i) { return system::join (dir, string::f ("cv%d.wav", i + 1)); }

    // Patch save: write each changed lane. The pool's freeLock keeps returned blocks alive while their pointers are read.
    void onSave (const SaveEvent&) override
    {
        const std::string dir = createPatchStorageDirectory();
        std::lock_guard<std::mutex> hold (pool.freeLock);
        for (int i = 0; i < N; ++i)
        {
            system::remove (lanePath (dir, i) + ".tmp");   // left by a crash mid-save
            loop::CvLane& l = lanes[i];
            const unsigned v = l.version;   // ponytail: plain reads of audio-thread fields, as the Looper's save
            if (v == savedVersion[i]) continue;
            const std::string path = lanePath (dir, i);
            const int nch = l.nch, stored = l.stored, nb = l.nBlocks, decim = l.D;
            if (mode[i] == loop::CvMsg::REC || nch == 0 || stored == 0)   // a take still recording is not saved, as its track
            {
                system::remove (path);
                if (mode[i] != loop::CvMsg::REC) savedVersion[i] = v;
                continue;
            }
            std::vector<const loop::Block*> list (l.blocks, l.blocks + nb);
            bool whole = (int64_t) nb * loop::CV_FLOATS >= (int64_t) stored * nch;
            for (const loop::Block* b : list) whole = whole && b;
            const std::string tmp = path + ".tmp";
            wav::Writer w;
            const int rate = std::max (1000, (int) std::lround (APP->engine->getSampleRate() / decim));   // nominal: load uses D from the patch
            if (! whole || ! w.open (tmp, rate, nch)) { system::remove (path); WARN ("LooperCV: cannot write %s", tmp.c_str()); continue; }   // a stale file would load under the new take id
            std::vector<float> buf;
            buf.reserve (1024 * nch);
            for (int s = 0; s < stored; s += 1024)
            {
                const int k = std::min (1024, stored - s);
                buf.clear();
                for (int64_t f = (int64_t) s * nch; f < (int64_t) (s + k) * nch; ++f) buf.push_back (loop::CvLane::floatAt (list.data(), f));
                w.writeFrames (buf.data(), k);
            }
            if (w.close() && system::rename (tmp, path)) savedVersion[i] = v;
            else
            {
                system::remove (tmp);
                system::remove (path);   // a stale file would load under the new take id
                WARN ("LooperCV: lane %d not saved", i + 1);
            }
        }
    }

    // Patch load (after dataFromJson; the engine does not run process() meanwhile): read the saved lanes. Each waits for
    // the Looper to report its take (CvLane::adopt).
    void onAdd (const AddEvent&) override
    {
        const std::string dir = getPatchStorageDirectory();
        for (int i = 0; i < N; ++i)
        {
            const Saved s = saved[i];
            saved[i] = Saved();
            const std::string path = lanePath (dir, i);
            if (! s.has || ! system::exists (path)) continue;
            std::vector<loop::Block*> list;
            try
            {
                std::vector<float> x;
                int ch = 0, fileRate = 0;
                if (! wav::readN (path, x, ch, fileRate, (int64_t) loop::CV_MAX_BLOCKS * loop::CV_FLOATS / 16))
                {
                    WARN ("LooperCV: cannot read %s", path.c_str());
                    continue;
                }
                const int samples = (int) (x.size() / ch);
                list.reserve ((x.size() + loop::CV_FLOATS - 1) / loop::CV_FLOATS);
                for (size_t at = 0; at < x.size(); at += loop::CV_FLOATS)
                {
                    loop::Block* b = pool.make();
                    if (! b) { WARN ("LooperCV: out of memory, lane %d cut", i + 1); break; }
                    list.push_back (b);
                    const size_t m = std::min ((size_t) loop::CV_FLOATS, x.size() - at);
                    for (size_t j = 0; j < m; ++j)   // adopted CV skips CvLane::write(): clean it here
                        (j < (size_t) loop::BLOCK ? b->l[j] : b->r[j - loop::BLOCK]) = loop::clean (x[at + j]);
                }
                lanes[i].adopt (list.data(), (int) list.size(), samples, ch, s.decim, s.trackLen, s.take, pool);
                savedVersion[i] = lanes[i].version;
            }
            catch (const std::exception& e)   // one escaping onAdd takes Rack down
            {
                WARN ("LooperCV: cannot load lane %d: %s", i + 1, e.what());
                for (loop::Block* b : list) pool.give (b);
            }
        }
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* arr = json_array();
        for (const loop::CvLane& l : lanes)
        {
            if (l.nch == 0 || l.stored == 0) { json_array_append_new (arr, json_null()); continue; }
            json_t* o = json_object();
            json_object_set_new (o, "take", json_integer (l.take));
            json_object_set_new (o, "decim", json_integer (l.D));
            json_object_set_new (o, "trackLen", json_integer (l.trackLen));
            json_array_append_new (arr, o);
        }
        json_object_set_new (root, "lanes", arr);
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        json_t* arr = json_object_get (root, "lanes");
        for (int i = 0; i < N; ++i)
        {
            json_t* o = arr ? json_array_get (arr, i) : nullptr;
            json_t *t = json_object_get (o, "take"), *d = json_object_get (o, "decim"), *n = json_object_get (o, "trackLen");
            saved[i] = Saved();
            if (! t || ! d || ! n) continue;
            saved[i].has = true;
            saved[i].take = (uint32_t) json_integer_value (t);
            saved[i].decim = clamp ((int) json_integer_value (d), 1, 1024);
            saved[i].trackLen = std::max (0, (int) json_integer_value (n));
        }
    }
};

// Dims the face while no Looper sits on the left.
struct FaceDim : TransparentWidget
{
    LooperCV* module = nullptr;
    void draw (const DrawArgs& a) override
    {
        if (! module || module->linked.load (std::memory_order_relaxed)) return;
        nvgBeginPath (a.vg);
        nvgCircle (a.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2);
        nvgFillColor (a.vg, nvgRGBA (0x16, 0x16, 0x1a, 0x80));
        nvgFill (a.vg);
    }
};

struct LooperCVWidget : ModuleWidget
{
    LooperCVWidget (LooperCV* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/LooperCV.svg")));
        namespace L = layout::looper_cv;
        const Vec facePos = mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2]));
        const Vec faceSize = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2]));
        TalkingFace* f = createWidget<TalkingFace> (facePos);
        f->box.size = faceSize;
        if (module) f->tap = &module->face;
        addChild (f);
        FaceDim* dim = createWidget<FaceDim> (facePos);
        dim->box.size = faceSize;
        dim->module = module;
        addChild (dim);
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        static const float* const ins[5] = { L::CV0_INPUT, L::CV1_INPUT, L::CV2_INPUT, L::CV3_INPUT, L::CV4_INPUT };
        static const float* const outs[5] = { L::CV0_OUTPUT, L::CV1_OUTPUT, L::CV2_OUTPUT, L::CV3_OUTPUT, L::CV4_OUTPUT };
        static const float* const leds[5] = { L::LANE0_LIGHT, L::LANE1_LIGHT, L::LANE2_LIGHT, L::LANE3_LIGHT, L::LANE4_LIGHT };
        for (int i = 0; i < LooperCV::N; ++i)
        {
            addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, LooperCV::CV_INPUT + i));
            addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, LooperCV::CV_OUTPUT + i));
            addChild (createLightCentered<MediumLight<LimeMagentaLight>> (at (leds[i]), module, LooperCV::LANE_LIGHT + 2 * i));
        }
    }

    void appendContextMenu (Menu* menu) override
    {
        LooperCV* m = dynamic_cast<LooperCV*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createMenuLabel (m->linked.load() ? "Linked to the My lil Looper on the left" : "No My lil Looper on the left"));
        menu->addChild (createMenuLabel (string::f ("Memory in use: %ld MB", m->pool.allocated.load() / 2)));
    }
};

Model* modelLooperCV = createModel<LooperCV, LooperCVWidget> ("LooperCV");
