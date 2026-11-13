#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ps
{
/** Describes one module parameter. IDs are stable: never rename one once shipped. */
struct ParamSpec
{
    enum class Type { Float, Choice, Bool };

    juce::String id, name, unit;
    Type type = Type::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    bool logScale = false;
    int decimals = 1;
    juce::StringArray choices;

    static ParamSpec number (juce::String id, juce::String name, juce::String unit,
                             float min, float max, float def, bool logScale = false, int decimals = 1)
    {
        ParamSpec s; s.id = id; s.name = name; s.unit = unit; s.min = min; s.max = max; s.def = def;
        s.logScale = logScale; s.decimals = decimals; return s;
    }
    static ParamSpec choice (juce::String id, juce::String name, juce::StringArray choices, int defIndex)
    {
        ParamSpec s; s.id = id; s.name = name; s.type = Type::Choice; s.choices = choices;
        s.min = 0.0f; s.max = (float) juce::jmax (1, choices.size() - 1); s.def = (float) defIndex; return s;
    }
    static ParamSpec toggle (juce::String id, juce::String name, bool def)
    {
        ParamSpec s; s.id = id; s.name = name; s.type = Type::Bool; s.def = def ? 1.0f : 0.0f; return s;
    }

    float snap (float v) const
    {
        v = juce::jlimit (min, max, v);
        if (type == Type::Choice) return std::round (v);
        if (type == Type::Bool)   return v >= 0.5f ? 1.0f : 0.0f;
        return v;
    }
    float toNorm (float v) const
    {
        v = juce::jlimit (min, max, v);
        if (max <= min) return 0.0f;
        if (type == Type::Float && logScale && min > 0.0f)
            return std::log (v / min) / std::log (max / min);
        return (v - min) / (max - min);
    }
    float fromNorm (float n) const
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        if (type == Type::Float && logScale && min > 0.0f)
            return min * std::pow (max / min, n);
        return snap (min + n * (max - min));
    }
    int numSteps() const
    {
        if (type == Type::Choice) return juce::jmax (2, choices.size());
        if (type == Type::Bool)   return 2;
        return 0x7fffffff;
    }
    juce::String text (float v) const
    {
        if (type == Type::Choice) return choices[juce::jlimit (0, juce::jmax (0, choices.size() - 1), (int) std::round (v))];
        if (type == Type::Bool)   return v >= 0.5f ? "On" : "Off";
        if (unit == "Hz" && v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
        return juce::String (v, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
    }
    float parse (const juce::String& t) const
    {
        if (type == Type::Choice)
        {
            const int i = choices.indexOf (t.trim(), true);
            return i >= 0 ? (float) i : snap (t.getFloatValue());
        }
        if (type == Type::Bool) return (t.trim().equalsIgnoreCase ("on") || t.getIntValue() != 0) ? 1.0f : 0.0f;
        float v = t.getFloatValue();
        if (t.containsIgnoreCase ("khz")) v *= 1000.0f;
        return snap (v);
    }
};
} // namespace ps
