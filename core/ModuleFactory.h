#pragma once
#include "../modules/Altitude.h"
#include "../modules/Dropoff.h"
#include "../modules/Ridgeline.h"
#include "../modules/Scree.h"
#include "../modules/Summit.h"
#include "../modules/Traverse.h"
#include <functional>

namespace ps
{
struct ModuleInfo { juce::String id, name; std::function<std::unique_ptr<Module>()> create; };

/** Every module Basecamp can load, in menu order. Add new ports here. */
inline const std::vector<ModuleInfo>& moduleRegistry()
{
    static const std::vector<ModuleInfo> r = {
        { "altitude", "Altitude", [] { return std::unique_ptr<Module> (new Altitude()); } },
        { "dropoff", "Dropoff", [] { return std::unique_ptr<Module> (new Dropoff()); } },
        { "ridgeline", "Ridgeline", [] { return std::unique_ptr<Module> (new Ridgeline()); } },
        { "scree", "Scree", [] { return std::unique_ptr<Module> (new Scree()); } },
        { "summit",  "Summit",  [] { return std::unique_ptr<Module> (new Summit()); } },
        { "traverse", "Traverse", [] { return std::unique_ptr<Module> (new Traverse()); } },
    };
    return r;
}
inline std::unique_ptr<Module> createModule (const juce::String& id)
{
    for (auto& m : moduleRegistry()) if (m.id == id) return m.create();
    return {};
}
} // namespace ps
