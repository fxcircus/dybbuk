#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Parameters.h"

// Message-thread preset store. One XML file per preset under
// ~/Library/Audio/Presets/<company>/<product>/; the factory preset is a
// virtual entry applied from parameter defaults. Starred names persist in a
// sidecar file and act as a filter, not a sort key.
//
// Non-parameter state travels through the stampExtraState/applyExtraState
// hooks — wire them in the processor's constructor or that state silently
// will not be saved. That omission has been the single most common bug.
class PresetManager : private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Info
    {
        juce::String name;
        juce::File file;      // invalid for factory entries
        bool factory = false;
        bool starred = false;
    };

    PresetManager (juce::AudioProcessor& processorToUse,
                   juce::AudioProcessorValueTreeState& state);
    ~PresetManager() override;

    juce::File userFolder() const;
    juce::Array<Info> getPresets(); // stable order: factory, then user A-Z

    bool loadPreset (const Info& info);
    bool saveCurrent (const juce::String& name);
    bool deletePreset (const Info& info);
    bool renamePreset (const Info& info, const juce::String& newName); // moves the file
    bool renameCurrentFile (const juce::String& newName); // rename the loaded preset
    bool presetNameExists (const juce::String& name) const; // user presets only
    void stepPreset (int delta);

    void toggleStar (const juce::String& name);
    bool isStarred (const juce::String& name) const;

    juce::String getCurrentName() const;
    void setCurrentName (const juce::String& name);
    bool isDirty() const { return dirty.load (std::memory_order_relaxed); }
    void markDirty() { dirty.store (true, std::memory_order_relaxed); }
    void markClean() { dirty.store (false, std::memory_order_relaxed); }

    // The state the reset bell reverts to: the currently loaded/saved preset
    // (invalid until one is loaded, which means factory defaults).
    const juce::ValueTree& getReferenceState() const { return referenceState; }
    void refreshReferenceFromDisk(); // after a session reload

    // Called on an incoming tree before it replaces the live one, so an older
    // saved layout can be brought forward. Set by the processor.
    std::function<void (juce::ValueTree&)> migrateState;

    // Hooks for state that lives outside the parameters:
    // stamp adds it to the tree being saved; apply pushes a freshly loaded
    // tree's copy back to the engine.
    std::function<void (juce::ValueTree&)> stampExtraState;
    std::function<void()> applyExtraState;

    // Header support: 1-based position of the current preset in the list
    // (-1 if unlisted), and the patent-style date the preset was saved
    // (empty until a save/load, and cleared by the factory preset — the
    // header falls back to the original 1948 engraving).
    int indexOfCurrent();
    juce::String getSavedDate() const;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void applyFactoryDefaults();
    void restoreMissingParameterDefaults (const juce::ValueTree& incoming);
    bool loadFactoryPreset (const juce::String& name);
    void finishLoad (const juce::String& name);
    void loadFavourites();
    void saveFavourites() const;
    juce::File favouritesFile() const { return userFolder().getChildFile (".favourites"); }

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray listenedIds;
    juce::StringArray favourites;
    std::atomic<bool> dirty { false };
    juce::ValueTree referenceState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
