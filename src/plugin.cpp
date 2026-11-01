#include "plugin.hpp"
Plugin* pluginInstance;
void init (Plugin* p) { pluginInstance = p; p->addModel (modelLooper); p->addModel (modelLooperCV); p->addModel (modelLooperFX); }
