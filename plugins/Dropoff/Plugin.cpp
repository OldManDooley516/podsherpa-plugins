#include "../../core/SinglePlugin.h"
#include "../../modules/Dropoff.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ps::SinglePluginProcessor (std::make_unique<ps::Dropoff>()); }
