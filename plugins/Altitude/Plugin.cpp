#include "../../core/SinglePlugin.h"
#include "../../modules/Altitude.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ps::SinglePluginProcessor (std::make_unique<ps::Altitude>()); }
