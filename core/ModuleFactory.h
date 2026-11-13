#pragma once
#include "../modules/Dropoff.h"
#include "../modules/Summit.h"
#include <functional>

namespace ps
{
struct ModuleInfo { juce::String id, name; std::function<std::unique_ptr<Module>()> create; };

/** Every module Basecamp can load, in menu order. Add new ports here. */
inline const std::vector<ModuleInfo>& moduleRegistry()
{
    static const std::vector<ModuleInfo> r = {
        { "dropoff", "Dropoff", [] { return std::unique_ptr<Module> (new Dropoff()); } },
        { "summit",  "Summit",  [] { return std::unique_ptr<Module> (new Summit()); } },
    };
    return r;
}
inline std::unique_ptr<Module> createModule (const juce::String& id)
{
    for (auto& m : moduleRegistry()) if (m.id == id) return m.create();
    return {};
}
} // namespace ps
