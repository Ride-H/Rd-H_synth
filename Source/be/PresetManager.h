/*
  ==============================================================================
    PresetManager.h
    Preset save/load for Rd-H Synth.
    - Default directory: platform-specific user data folder (REQ-COMPAT-023)
    - File extension: .rdhs (new) + .mss2 (v1.x read-only compatibility)
    - Migration: FM/Noise forced disabled when loading v1.x presets (REQ-COMPAT-002)
    - Backup: *.bak created before overwriting (REQ-COMPAT-003)

    Fixes applied:
      B-2: loadPresetDialog now applies v1.x migration (FM/Noise disable) directly,
           not only via PluginProcessor::setStateInformation.
      B-3: async lambda captures a weak_ptr<bool> alive flag so a dangling `this`
           after destructor is harmless.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

//==============================================================================
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& apvts);
    ~PresetManager() { *alive = false; }

    static int          getNumBuiltinPresets() { return getBuiltinPresetNames().size(); }
    static juce::String getBuiltinPresetName (int index);
    static const juce::StringArray& getBuiltinPresetNames();
    void loadBuiltinPreset (int index);

    void savePresetDialog();
    void loadPresetDialog();

    // Fired (on the message thread) after a file preset is successfully loaded,
    // with the preset's display name. Lets the editor refresh the TopBar (D-1).
    std::function<void (const juce::String& presetName)> onPresetLoaded;

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::shared_ptr<juce::FileChooser>  fileChooser;

    // B-3: alive flag — set to false in destructor, checked in async lambdas.
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void applyParam (const juce::StringRef& id, float rawVal);
    void applyMigration();         // forces FM/Noise off for v1.x presets
    juce::File getDefaultDirectory() const;
    static void writeBackup (const juce::File& file);
};

//==============================================================================
inline PresetManager::PresetManager (juce::AudioProcessorValueTreeState& a)
    : apvts (a) {}

//==============================================================================
inline const juce::StringArray& PresetManager::getBuiltinPresetNames()
{
    static const juce::StringArray names {
        "Default Lead", "Soft Pad", "Deep Bass", "Pluck", "Bright Lead"
    };
    return names;
}

inline juce::String PresetManager::getBuiltinPresetName (int i)
{
    const auto& names = getBuiltinPresetNames();
    return names[juce::jlimit (0, names.size() - 1, i)];
}

//==============================================================================
inline void PresetManager::applyParam (const juce::StringRef& id, float rawVal)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (rawVal));
}

// B-2: shared migration logic, used by both loadPresetDialog and as a pattern
// mirror to PluginProcessor::setStateInformation.
inline void PresetManager::applyMigration()
{
    auto forceOff = [this] (const juce::StringRef& id)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (0.0f);
    };
    forceOff ("fm_enable");
    forceOff ("noise_enable");
}

inline void PresetManager::loadBuiltinPreset (int index)
{
    // Force FM/Noise disabled for all built-in v1.x-style presets
    applyParam ("fm_enable",    0.0f);
    applyParam ("noise_enable", 0.0f);

    switch (index)
    {
        case 0: // Default Lead
            applyParam ("waveType", 1.f);   applyParam ("attack",  0.005f);
            applyParam ("decay",    0.2f);  applyParam ("sustain", 0.6f);
            applyParam ("release",  0.3f);  applyParam ("filterCutoff",    8000.f);
            applyParam ("filterResonance", 1.5f);
            applyParam ("unisonVoices", 1.f); applyParam ("unisonDetune", 0.f);
            applyParam ("pitchEnvAmount", 0.f); applyParam ("volume", 0.7f);
            break;

        case 1: // Soft Pad
            applyParam ("waveType", 0.f);   applyParam ("attack",  0.8f);
            applyParam ("decay",    0.5f);  applyParam ("sustain", 0.8f);
            applyParam ("release",  1.2f);  applyParam ("filterCutoff",    2500.f);
            applyParam ("filterResonance", 0.5f);
            applyParam ("unisonVoices", 3.f); applyParam ("unisonDetune", 8.f);
            applyParam ("pitchEnvAmount", 0.f); applyParam ("volume", 0.65f);
            break;

        case 2: // Deep Bass
            applyParam ("waveType", 2.f);   applyParam ("attack",  0.001f);
            applyParam ("decay",    0.3f);  applyParam ("sustain", 0.7f);
            applyParam ("release",  0.2f);  applyParam ("filterCutoff",    600.f);
            applyParam ("filterResonance", 2.0f);
            applyParam ("unisonVoices", 1.f); applyParam ("unisonDetune", 0.f);
            applyParam ("pitchEnvAmount", -2.f); applyParam ("volume", 0.75f);
            break;

        case 3: // Pluck
            applyParam ("waveType", 3.f);   applyParam ("attack",  0.001f);
            applyParam ("decay",    0.4f);  applyParam ("sustain", 0.0f);
            applyParam ("release",  0.15f); applyParam ("filterCutoff",    5000.f);
            applyParam ("filterResonance", 3.0f);
            applyParam ("unisonVoices", 1.f); applyParam ("unisonDetune", 0.f);
            applyParam ("pitchEnvAmount", 2.f); applyParam ("volume", 0.7f);
            break;

        case 4: // Bright Lead
            applyParam ("waveType", 1.f);   applyParam ("attack",  0.003f);
            applyParam ("decay",    0.15f); applyParam ("sustain", 0.5f);
            applyParam ("release",  0.25f); applyParam ("filterCutoff",    12000.f);
            applyParam ("filterResonance", 4.0f);
            applyParam ("unisonVoices", 2.f); applyParam ("unisonDetune", 12.f);
            applyParam ("pitchEnvAmount", 0.f); applyParam ("volume", 0.65f);
            break;

        default: break;
    }
}

//==============================================================================
// REQ-COMPAT-023: platform-specific user data directory
inline juce::File PresetManager::getDefaultDirectory() const
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("RdH/Synth/Presets");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("RdH\\Synth\\Presets");
   #else // Linux
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile (".config/RdH/Synth/Presets");
   #endif
}

// REQ-COMPAT-003: backup file before overwrite
inline void PresetManager::writeBackup (const juce::File& file)
{
    if (file.existsAsFile())
        file.copyFileTo (file.withFileExtension (".rdhs.bak"));
}

//==============================================================================
inline void PresetManager::savePresetDialog()
{
    auto dir = getDefaultDirectory();
    dir.createDirectory();

    fileChooser = std::make_shared<juce::FileChooser> (
        "Save preset...", dir.getChildFile ("MyPreset.rdhs"), "*.rdhs");

    std::weak_ptr<bool> weakAlive = alive; // B-3

    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, weakAlive] (const juce::FileChooser& fc)
        {
            auto a = weakAlive.lock();      // B-3: bail if PresetManager is gone
            if (!a || !*a) return;

            const auto file = fc.getResult();
            if (file == juce::File{}) return;

            writeBackup (file);
            auto state = apvts.copyState();
            state.setProperty ("rdh_version", "2.0", nullptr);
            std::unique_ptr<juce::XmlElement> xml (state.createXml());
            if (xml != nullptr)
            {
                if (! xml->writeTo (file))
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "Save Failed",
                        "Could not write preset file.\n"
                        "Check disk space and write permissions.");
            }
        });
}

//==============================================================================
inline void PresetManager::loadPresetDialog()
{
    fileChooser = std::make_shared<juce::FileChooser> (
        "Load preset...", getDefaultDirectory(), "*.rdhs;*.mss2");

    std::weak_ptr<bool> weakAlive = alive; // B-3

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, weakAlive] (const juce::FileChooser& fc)
        {
            auto a = weakAlive.lock();      // B-3
            if (!a || !*a) return;

            const auto file = fc.getResult();
            if (file == juce::File{}) return;

            std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
            if (xml == nullptr || !xml->hasTagName (apvts.state.getType())) return;

            auto tree = juce::ValueTree::fromXml (*xml);
            const bool hasVersionTag = tree.hasProperty ("rdh_version"); // B-2
            apvts.replaceState (tree);

            // B-2: same migration as PluginProcessor::setStateInformation —
            // file dialog load path must also disable FM/Noise for v1.x presets.
            if (!hasVersionTag)
                applyMigration();

            // D-1: notify the editor so the TopBar can show the loaded name.
            if (onPresetLoaded)
                onPresetLoaded (file.getFileNameWithoutExtension());
        });
}
