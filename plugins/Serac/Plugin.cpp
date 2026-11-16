#include "../../core/SinglePlugin.h"
#include "../../modules/Serac.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ps::SinglePluginProcessor (std::make_unique<ps::Serac>()); }
