#include "PresetManager.h"

namespace
{
    // Not JucePlugin_* macros: the console-app test targets don't define them.
    constexpr auto companyFolder = "fxcircus";
    constexpr auto productFolder = "Dybbuk";
    constexpr auto presetExtension = ".preset";
    constexpr auto factoryDefaultName = "Init";

    juce::String formatPatentDate (juce::Time t)
    {
        static const char* months[] = { "jan.", "feb.", "mar.", "apr.", "may", "jun.",
                                        "jul.", "aug.", "sep.", "oct.", "nov.", "dec." };
        return juce::String (months[juce::jlimit (0, 11, t.getMonth())]) + " "
               + juce::String (t.getDayOfMonth()) + ", " + juce::String (t.getYear());
    }
}

PresetManager::PresetManager (juce::AudioProcessor& processorToUse,
                              juce::AudioProcessorValueTreeState& state)
    : processor (processorToUse), apvts (state)
{
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            listenedIds.add (ranged->paramID);
            apvts.addParameterListener (ranged->paramID, this);
        }

    loadFavourites();
}

PresetManager::~PresetManager()
{
    for (const auto& id : listenedIds)
        apvts.removeParameterListener (id, this);
}

void PresetManager::parameterChanged (const juce::String& parameterID, float)
{
    // Performance controls (momentary triggers, bypass) are not preset content.
    if (! params::performanceParams().contains (parameterID))
        dirty.store (true, std::memory_order_relaxed);
}

juce::File PresetManager::userFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Audio").getChildFile ("Presets")
        .getChildFile (companyFolder).getChildFile (productFolder);
}

juce::Array<PresetManager::Info> PresetManager::getPresets()
{
    juce::Array<Info> factory, user, starred;

    factory.add ({ factoryDefaultName, {}, true, isStarred (factoryDefaultName) });

    auto files = userFolder().findChildFiles (juce::File::findFiles, false,
                                              "*" + juce::String (presetExtension));
    files.sort();
    for (const auto& f : files)
        user.add ({ f.getFileNameWithoutExtension(), f, false,
                    isStarred (f.getFileNameWithoutExtension()) });

    // Stable order: factory first, then user presets A-Z. Starring never
    // moves a row — the browser filters by star instead.
    struct ByName
    {
        static int compareElements (const Info& a, const Info& b)
        {
            return a.name.compareIgnoreCase (b.name);
        }
    };
    ByName byName;
    user.sort (byName);

    juce::Array<Info> ordered;
    ordered.addArray (factory);
    ordered.addArray (user);
    return ordered;
}

void PresetManager::applyFactoryDefaults()
{
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

void PresetManager::finishLoad (const juce::String& name)
{
    // A preset never arrives mid-performance: drop the performance controls so
    // nothing is left latched from the previous preset.
    for (const auto& id : params::performanceParams())
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (0.0f);

    setCurrentName (name);
    dirty.store (false, std::memory_order_relaxed);
    processor.updateHostDisplay();
}

bool PresetManager::loadPreset (const Info& info)
{
    if (info.factory)
    {
        applyFactoryDefaults();
        referenceState = juce::ValueTree(); // factory: reset means defaults
        if (applyExtraState)
            applyExtraState();
        finishLoad (info.name);
        apvts.state.removeProperty ("presetDate", nullptr); // back to 1948
        return true;
    }

    const auto xml = juce::parseXML (info.file);
    if (xml == nullptr || ! xml->hasTagName ("SustainerPreset"))
        return false;

    auto* stateXml = xml->getFirstChildElement();
    if (stateXml == nullptr || ! stateXml->hasTagName (apvts.state.getType()))
        return false;

    apvts.replaceState (juce::ValueTree::fromXml (*stateXml));
    referenceState = apvts.copyState(); // carries the seat properties too
    if (applyExtraState)
        applyExtraState();
    finishLoad (info.name);

    const auto created = xml->getStringAttribute ("created");
    if (created.isNotEmpty())
        apvts.state.setProperty ("presetDate",
                                 formatPatentDate (juce::Time::fromISO8601 (created)), nullptr);
    else
        apvts.state.removeProperty ("presetDate", nullptr);
    return true;
}

bool PresetManager::saveCurrent (const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;

    auto folder = userFolder();
    folder.createDirectory();

    juce::XmlElement root ("SustainerPreset");
    root.setAttribute ("name", trimmed);
    root.setAttribute ("pluginVersion", JucePlugin_VersionString);
    root.setAttribute ("created", juce::Time::getCurrentTime().toISO8601 (true));
    auto state = apvts.copyState();
    if (stampExtraState)
        stampExtraState (state);
    referenceState = state; // what was saved is what reset reverts to
    root.addChildElement (state.createXml().release());

    const auto file = folder.getChildFile (juce::File::createLegalFileName (trimmed)
                                           + presetExtension);
    if (! root.writeTo (file))
        return false;

    setCurrentName (trimmed);
    apvts.state.setProperty ("presetDate",
                             formatPatentDate (juce::Time::getCurrentTime()), nullptr);
    dirty.store (false, std::memory_order_relaxed);
    return true;
}

int PresetManager::indexOfCurrent()
{
    const auto presets = getPresets();
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).name == getCurrentName())
            return i + 1;
    return -1;
}

juce::String PresetManager::getSavedDate() const
{
    return apvts.state.getProperty ("presetDate", juce::String()).toString();
}

bool PresetManager::deletePreset (const Info& info)
{
    if (info.factory || ! info.file.existsAsFile())
        return false;
    return info.file.deleteFile();
}

bool PresetManager::presetNameExists (const juce::String& name) const
{
    return userFolder()
        .getChildFile (juce::File::createLegalFileName (name.trim()) + presetExtension)
        .existsAsFile();
}

bool PresetManager::renamePreset (const Info& info, const juce::String& newNameIn)
{
    const auto newName = newNameIn.trim();
    if (info.factory || ! info.file.existsAsFile()
        || newName.isEmpty() || newName == info.name)
        return false;

    const auto target = userFolder().getChildFile (
        juce::File::createLegalFileName (newName) + presetExtension);
    if (target.existsAsFile())
        return false; // never silently clobber another preset

    if (! info.file.moveFileTo (target))
        return false;

    // The name inside the file follows, so future loads carry it.
    if (auto xml = juce::parseXML (target))
    {
        xml->setAttribute ("name", newName);
        xml->writeTo (target);
    }

    if (favourites.contains (info.name)) // the star follows the rename
    {
        favourites.set (favourites.indexOf (info.name), newName);
        saveFavourites();
    }

    if (getCurrentName() == info.name)
        setCurrentName (newName);
    return true;
}

bool PresetManager::renameCurrentFile (const juce::String& newName)
{
    for (const auto& info : getPresets())
        if (! info.factory && info.name == getCurrentName())
            return renamePreset (info, newName);
    return false;
}

void PresetManager::stepPreset (int delta)
{
    const auto presets = getPresets();
    if (presets.isEmpty())
        return;

    int current = 0;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).name == getCurrentName())
        {
            current = i;
            break;
        }

    const int next = (current + delta + presets.size()) % presets.size();
    loadPreset (presets.getReference (next));
}

void PresetManager::toggleStar (const juce::String& name)
{
    if (favourites.contains (name))
        favourites.removeString (name);
    else
        favourites.add (name);
    saveFavourites();
}

bool PresetManager::isStarred (const juce::String& name) const
{
    return favourites.contains (name);
}

juce::String PresetManager::getCurrentName() const
{
    return apvts.state.getProperty ("presetName", factoryDefaultName);
}

void PresetManager::setCurrentName (const juce::String& name)
{
    apvts.state.setProperty ("presetName", name, nullptr);
}

void PresetManager::loadFavourites()
{
    favourites.clear();
    if (favouritesFile().existsAsFile())
        favouritesFile().readLines (favourites);
    favourites.removeEmptyStrings();
}

// After a session reload the reference must come from the preset file on
// disk — the session state itself may hold unsaved tweaks.
void PresetManager::refreshReferenceFromDisk()
{
    referenceState = juce::ValueTree();
    const auto name = getCurrentName();
    if (name.isEmpty())
        return;

    for (const auto& info : getPresets())
        if (! info.factory && info.name == name)
        {
            if (const auto xml = juce::parseXML (info.file))
                if (auto* stateXml = xml->getFirstChildElement())
                    if (stateXml->hasTagName (apvts.state.getType()))
                        referenceState = juce::ValueTree::fromXml (*stateXml);
            return;
        }
}

void PresetManager::saveFavourites() const
{
    userFolder().createDirectory();
    favouritesFile().replaceWithText (favourites.joinIntoString ("\n"));
}
