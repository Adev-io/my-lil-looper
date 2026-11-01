// Pad.hpp: the big square pads and the Looper FX scene pads (momentary), the ring light drawn around them and the count-in lamps. Art from tools/panel.py.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include "plugin.hpp"

struct Pad : app::SvgSwitch
{
    Pad()
    {
        momentary = true;
        addFrame (Svg::load (asset::plugin (pluginInstance, "res/Pad_0.svg")));
        addFrame (Svg::load (asset::plugin (pluginInstance, "res/Pad_1.svg")));
    }
};

struct PadLow : app::SvgSwitch
{
    PadLow()
    {
        momentary = true;
        addFrame (Svg::load (asset::plugin (pluginInstance, "res/PadLow_0.svg")));
        addFrame (Svg::load (asset::plugin (pluginInstance, "res/PadLow_1.svg")));
    }
};

// The Looper FX scene pads (momentary, smaller).
struct ScenePad : app::SvgSwitch
{
    ScenePad()
    {
        momentary = true;
        addFrame (Svg::load (asset::plugin (pluginInstance, "res/ScenePad_0.svg")));
        addFrame (Svg::load (asset::plugin (pluginInstance, "res/ScenePad_1.svg")));
    }
};

// A rounded-rectangle ring in the light's colour (channel 0 lime, 1 magenta), dim ink outline when off.
struct PadRing : LimeMagentaLight
{
    void ring (const DrawArgs& a, NVGcolor c, float w)
    {
        nvgBeginPath (a.vg);
        nvgRoundedRect (a.vg, w / 2, w / 2, box.size.x - w, box.size.y - w, mm2px (1.8f));
        nvgStrokeColor (a.vg, c);
        nvgStrokeWidth (a.vg, w);
        nvgStroke (a.vg);
    }
    void drawBackground (const DrawArgs& a) override { ring (a, nvgRGB (0x2b, 0x2b, 0x33), mm2px (0.6f)); }
    void drawLight (const DrawArgs& a) override { if (color.a > 0.f) ring (a, color, mm2px (0.6f)); }
    void drawHalo (const DrawArgs&) override {}
};

// A big round count-in lamp: a dark disc when off, filled with the light colour (channel 0 lime, 1 magenta) when lit.
struct CountLamp : LimeMagentaLight
{
    CountLamp() { box.size = mm2px (Vec (8.f, 8.f)); }
    void disc (const DrawArgs& a, NVGcolor c)
    {
        nvgBeginPath (a.vg);
        nvgCircle (a.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2 - mm2px (0.3f));
        nvgFillColor (a.vg, c);
        nvgFill (a.vg);
    }
    void drawBackground (const DrawArgs& a) override { disc (a, nvgRGB (0x2b, 0x2b, 0x33)); }
    void drawLight (const DrawArgs& a) override { if (color.a > 0.f) disc (a, color); }
    void drawHalo (const DrawArgs&) override {}
};
