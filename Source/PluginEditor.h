/*
  ==============================================================================
    PluginEditor.h
    Rd-H Synth v2.0 — 4-mode UI: AI | SIMPLE | ADVANCED | PERFORMANCE
    Window: 900 × 580 px.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "fe/CoolUiSkin.h"
#include "fe/SpectrumAnalyzerComponent.h"
#include "be/PresetManager.h"
#include "fe/gui/TopBar.h"
#include "fe/gui/BottomBar.h"
#include "fe/gui/modes/SimpleModePanel.h"
#include "fe/gui/modes/AIModePanel.h"
#include "fe/gui/modes/AdvancedModePanel.h"
#include "fe/gui/modes/PerformanceModePanel.h"

//==============================================================================
class RdhSynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit RdhSynthAudioProcessorEditor (RdhSynthAudioProcessor&);
    ~RdhSynthAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    void onModeChanged (ViewMode m);
    void onPresetSelected (int idx);
    void applyKeyboardRange();

    [[maybe_unused]] RdhSynthAudioProcessor& audioProcessor;

    CoolUiSkin skin;
    PresetManager presetManager;

    // Layout constants — total must equal window height (580px)
    // TOP_H(44) + CONTENT_H(222) + SPECTRUM_H(130) + OCT_BTN_H(20) + KEYBOARD_H(128) + BOTTOM_H(36) = 580
    static constexpr int TOP_H     = 44;
    static constexpr int CONTENT_H = 222; // reduced to give SPECTRUM extra ~1cm (REQ-UI-010.3)
    static constexpr int SPECTRUM_H = 130; // ~1cm taller than before (REQ-UI-010.3)
    static constexpr int OCT_BTN_H  = 20;
    static constexpr int KEYBOARD_H = 128;
    static constexpr int BOTTOM_H   = 36;

    // Top / Bottom bars
    TopBar    topBar;
    BottomBar bottomBar;

    // Mode panels
    SimpleModePanel      simplePanel;
    AIModePanel          aiPanel;
    AdvancedModePanel    advancedPanel;
    PerformanceModePanel performancePanel;

    // Always-visible components
    SpectrumAnalyzerComponent spectrumAnalyzer;
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton octaveDownButton { "<" };
    juce::TextButton octaveUpButton   { ">" };
    int lowestVisibleNote = 48; // C3

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RdhSynthAudioProcessorEditor)
};
