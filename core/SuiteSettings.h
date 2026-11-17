#pragma once
#include "Theme.h"
#include <juce_data_structures/juce_data_structures.h>

namespace ps
{
/** Look and mode are saved once for the whole suite, not per plugin.

    The standard says the Look setting is saved suite-wide, and that is a real constraint rather
    than a convenience: a session with Scree, Serac and Summit on one track would otherwise open
    with three different skins if each stored its own. So it lives in one properties file shared
    by every plugin and every instance, and changing it in one window updates the others.

    Vintage is never written here. It is a bonus look, never saved and never the opening look, so
    it is held in the editor for the life of that window only. Choosing any Look swatch leaves it.

    Instances in the same process share one object, so a change is picked up by the others on
    their next tick. Across processes the file is re-read on change, which covers a DAW that
    sandboxes plugins into separate processes. */
class SuiteSettings
{
public:
    static SuiteSettings& get()
    {
        static SuiteSettings s;
        return s;
    }

    Theme::Id look() const { return lookId.load (std::memory_order_relaxed); }
    Mode mode() const { return modeVal.load (std::memory_order_relaxed); }

    void setLook (Theme::Id id)
    {
        if (id == Theme::Id::Vintage) return;   // never saved
        lookId.store (id, std::memory_order_relaxed);
        props->setValue ("look", (int) id);
        props->saveIfNeeded();
        ++revision;
    }

    void setMode (Mode m)
    {
        modeVal.store (m, std::memory_order_relaxed);
        props->setValue ("mode", (int) m);
        props->saveIfNeeded();
        ++revision;
    }

    /** Editors poll this; a change means another window (or another plugin) switched the Look. */
    int getRevision() const { return revision.load (std::memory_order_relaxed); }

    /** Picks up a change written by a plugin running in a different process. Cheap enough to
        call from a 30 Hz editor timer: JUCE only touches the disk when the file changed. */
    void reloadIfChangedExternally()
    {
        if (props->reload())
        {
            lookId.store ((Theme::Id) props->getIntValue ("look", (int) Theme::Id::Default_), std::memory_order_relaxed);
            modeVal.store ((Mode) props->getIntValue ("mode", (int) Mode::Normal), std::memory_order_relaxed);
            ++revision;
        }
    }

private:
    SuiteSettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "PodSherpa";
        o.filenameSuffix      = "settings";
        o.folderName          = "Southern Digital";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);
        lookId.store ((Theme::Id) props->getIntValue ("look", (int) Theme::Id::Default_), std::memory_order_relaxed);
        modeVal.store ((Mode) props->getIntValue ("mode", (int) Mode::Normal), std::memory_order_relaxed);
    }

    std::unique_ptr<juce::PropertiesFile> props;
    std::atomic<Theme::Id> lookId { Theme::Id::Default_ };
    std::atomic<Mode> modeVal { Mode::Normal };
    std::atomic<int> revision { 0 };
};
} // namespace ps
