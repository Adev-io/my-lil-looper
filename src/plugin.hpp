// plugin.hpp: the plugin's model and the house light colours.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <rack.hpp>
using namespace rack;
extern Plugin* pluginInstance;
extern Model* modelLooper;

// House light colours, the same as the panel (tools/panel.py), My lil Acid Bass and My lil Toolbox.
struct LimeLight : GrayModuleLightWidget { LimeLight() { addBaseColor (nvgRGB (0xc8, 0xff, 0x3a)); } };
// Two-state lights: channel 0 lime, channel 1 magenta.
struct LimeMagentaLight : GrayModuleLightWidget
{
    LimeMagentaLight()
    {
        addBaseColor (nvgRGB (0xc8, 0xff, 0x3a));
        addBaseColor (nvgRGB (0xff, 0x3d, 0x9a));
    }
};
