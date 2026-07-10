/*
  ==============================================================================
    SimpleModePanel.h
    Simple mode: SAMPLE browser with tabs (FACTORY / USER) + 4 main knobs.
    REQ-UI-010, REQ-UI-010.1 — tab-based sample management, extensible banks.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../RdhColors.h"
#include "../../../be/PresetManager.h"

class RdhSynthAudioProcessor;

//==============================================================================
// Data model for a preset bank (REQ-UI-010.1)
struct PresetBank
{
    juce::String name;
    juce::StringArray presetNames;
};

//==============================================================================
class SimpleModePanel : public juce::Component
{
public:
    explicit SimpleModePanel (juce::AudioProcessorValueTreeState& apvts)
    {
        // --- Build initial banks ---
        // FACTORY bank: built-in presets — single source of truth is
        // PresetManager::getBuiltinPresetNames() (single source, no duplication).
        PresetBank factory;
        factory.name = "FACTORY";
        factory.presetNames = PresetManager::getBuiltinPresetNames();
        banks.push_back (std::move (factory));

        // USER bank: placeholder (populated from disk in Phase 2)
        PresetBank user;
        user.name = "USER";
        banks.push_back (std::move (user));

        // --- Tab buttons ---
        for (int i = 0; i < (int) banks.size(); ++i)
        {
            auto* btn = bankTabs.add (new juce::TextButton (banks[(size_t) i].name));
            btn->setClickingTogglesState (false);
            btn->setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
            btn->setColour (juce::TextButton::textColourOffId,  RdhColors::textSecondary);
            btn->setColour (juce::TextButton::buttonOnColourId, RdhColors::primaryGlow);
            btn->setColour (juce::TextButton::textColourOnId,   RdhColors::primaryDark);
            addAndMakeVisible (*btn);

            const int idx = i;
            btn->onClick = [this, idx] { selectBank (idx); };
        }

        // --- 4 main knobs ---
        setupKnob (cutoffSlider,    cutoffLabel,    "CUTOFF");
        setupKnob (resonanceSlider, resonanceLabel, "RESO");
        setupKnob (attackSlider,    attackLabel,    "ATTACK");
        setupKnob (releaseSlider,   releaseLabel,   "RELEASE");

        cutoffAttach    = std::make_unique<SA> (apvts, "filterCutoff",    cutoffSlider);
        resonanceAttach = std::make_unique<SA> (apvts, "filterResonance", resonanceSlider);
        attackAttach    = std::make_unique<SA> (apvts, "attack",          attackSlider);
        releaseAttach   = std::make_unique<SA> (apvts, "release",         releaseSlider);

        selectBank (0); // FACTORY active by default
    }

    std::function<void (int)> onPresetSelected; // called when preset button clicked

    // Append a new bank at runtime (Phase 2+: e.g., load from disk)
    void addBank (PresetBank b)
    {
        banks.push_back (std::move (b));
        auto* btn = bankTabs.add (new juce::TextButton (banks.back().name));
        btn->setClickingTogglesState (false);
        btn->setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        btn->setColour (juce::TextButton::textColourOffId, RdhColors::textSecondary);
        addAndMakeVisible (*btn);
        const int idx = (int) banks.size() - 1;
        btn->onClick = [this, idx] { selectBank (idx); };
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (RdhColors::bg);

        // Tab bar underline
        g.setColour (RdhColors::border);
        g.drawHorizontalLine (tabH, 0.0f, static_cast<float> (getWidth() - knobAreaW));

        // Active tab underline
        if (currentBank < bankTabs.size())
        {
            auto* b = bankTabs[currentBank];
            g.setColour (RdhColors::primary);
            g.fillRect (b->getX(), tabH - 2, b->getWidth(), 2);
        }

        // Knob section label
        g.setColour (RdhColors::textDim);
        g.setFont   (juce::Font (juce::FontOptions (9.0f)));
        g.drawText  ("QUICK EDIT", getWidth() - knobAreaW + 4, 4, 120, 14, juce::Justification::centredLeft);

        // Divider between browser and knobs
        g.setColour (RdhColors::border);
        g.drawVerticalLine (getWidth() - knobAreaW, static_cast<float> (tabH), static_cast<float> (getHeight()));
    }

    void resized() override
    {
        // Tab bar
        const int tabW = 72;
        for (int i = 0; i < bankTabs.size(); ++i)
            bankTabs[i]->setBounds (i * tabW, 0, tabW, tabH);

        // Rebuild preset grid
        rebuildPresetButtons();

        // Preset grid area (below tab bar)
        const int browserW = getWidth() - knobAreaW - 4;
        const int cardW = (browserW - 20) / 2;
        const int cardH = 52; // slightly shorter cards to fit more
        const int marginX = 10, marginY = tabH + 6;
        int col = 0, row = 0;
        for (auto* btn : presetButtons)
        {
            const int bx = marginX + col * (cardW + 8);
            const int by = marginY + row * (cardH + 6);
            btn->setBounds (bx, by, cardW, cardH);
            ++col;
            if (col >= 2) { col = 0; ++row; }
        }

        // 4 knobs: 2×2 grid in the right area (smaller: 96×70)
        const int knobAreaX = getWidth() - knobAreaW + 4;
        const int kW = 96, kH = 70;
        auto placeKnob = [&] (juce::Slider& s, juce::Label& l, int x, int y)
        {
            l.setBounds (x, y,      kW, 12);
            s.setBounds (x, y + 12, kW, kH);
        };
        const int ky = (getHeight() - kH * 2 - 12 * 2 - 8) / 2;
        placeKnob (cutoffSlider,    cutoffLabel,    knobAreaX,      ky);
        placeKnob (resonanceSlider, resonanceLabel, knobAreaX + kW, ky);
        placeKnob (attackSlider,    attackLabel,    knobAreaX,      ky + kH + 12 + 8);
        placeKnob (releaseSlider,   releaseLabel,   knobAreaX + kW, ky + kH + 12 + 8);
    }

private:
    static constexpr int tabH       = 28;
    static constexpr int knobAreaW  = 220; // narrower than before

    void selectBank (int idx)
    {
        currentBank = idx;

        // Update tab colors
        for (int i = 0; i < bankTabs.size(); ++i)
            bankTabs[i]->setColour (juce::TextButton::textColourOffId,
                                    i == idx ? RdhColors::primaryDark : RdhColors::textSecondary);

        rebuildPresetButtons();
        resized();
        repaint();
    }

    void rebuildPresetButtons()
    {
        presetButtons.clear();
        selectedPreset = -1;

        if (currentBank >= (int) banks.size()) return;
        const auto& bank = banks[(size_t) currentBank];

        for (int i = 0; i < bank.presetNames.size(); ++i)
        {
            auto* btn = presetButtons.add (new juce::TextButton (bank.presetNames[i]));
            btn->setClickingTogglesState (false);
            btn->setColour (juce::TextButton::buttonColourId,   RdhColors::surface2);
            btn->setColour (juce::TextButton::textColourOffId,  RdhColors::textPrimary);
            btn->setColour (juce::TextButton::buttonOnColourId, RdhColors::primaryGlow);
            btn->setColour (juce::TextButton::textColourOnId,   RdhColors::primaryDark);
            addAndMakeVisible (*btn);

            const int bankIdx = currentBank, presetIdx = i;
            btn->onClick = [this, bankIdx, presetIdx] { handlePresetClicked (bankIdx, presetIdx); };
        }
    }

    void handlePresetClicked (int bankIdx, int presetIdx)
    {
        selectedPreset = presetIdx;
        for (int i = 0; i < presetButtons.size(); ++i)
            presetButtons[i]->setToggleState (i == presetIdx, juce::dontSendNotification);

        // FACTORY bank maps 1:1 to built-in preset indices
        if (bankIdx == 0 && onPresetSelected)
            onPresetSelected (presetIdx);
    }

    void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        s.setSliderStyle   (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle  (juce::Slider::TextBoxBelow, false, 56, 14);
        s.setColour (juce::Slider::textBoxTextColourId,       RdhColors::textPrimary);
        s.setColour (juce::Slider::textBoxBackgroundColourId, RdhColors::surface2);
        s.setColour (juce::Slider::textBoxOutlineColourId,    RdhColors::border);
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, RdhColors::textSecondary);
        addAndMakeVisible (l);
    }

    // --- Bank data ---
    std::vector<PresetBank>             banks;
    int                                 currentBank = 0;
    juce::OwnedArray<juce::TextButton>  bankTabs;

    // --- Preset grid (rebuilt on bank switch) ---
    juce::OwnedArray<juce::TextButton>  presetButtons;
    int selectedPreset = -1;

    // --- Knobs ---
    juce::Slider cutoffSlider, resonanceSlider, attackSlider, releaseSlider;
    juce::Label  cutoffLabel,  resonanceLabel,  attackLabel,  releaseLabel;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> cutoffAttach, resonanceAttach, attackAttach, releaseAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleModePanel)
};
