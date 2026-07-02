/*
  ==============================================================================
    AIModePanel.h
    AI mode: wraps the existing AIMatchPanel + adds AI APPLIED banner.
    REQ-UI-009, REQ-AI-002, REQ-AI-004.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../../AIMatchPanel.h"
#include "../RdhColors.h"

class RdhSynthAudioProcessor;

//==============================================================================
class AIModePanel : public juce::Component
{
public:
    enum class AIState { Idle, Analyzing, Preview, Applied, AppliedEdited };

    explicit AIModePanel (RdhSynthAudioProcessor& proc)
        : aiPanel (proc)
    {
        addAndMakeVisible (aiPanel);

        resetBtn.setButtonText ("RESET AI \xe2\x86\xba");
        resetBtn.setColour (juce::TextButton::buttonColourId,  RdhColors::surface2);
        resetBtn.setColour (juce::TextButton::textColourOffId, RdhColors::ai);
        resetBtn.setVisible (false);
        addAndMakeVisible (resetBtn);

        resetBtn.onClick = [this] { if (onResetAI) onResetAI(); };
    }

    std::function<void()> onResetAI;

    void setAIState (AIState s, const juce::String& presetName = {})
    {
        currentState  = s;
        currentPreset = presetName;
        resetBtn.setVisible (s == AIState::Applied || s == AIState::AppliedEdited);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (RdhColors::bg);

        if (currentState == AIState::Applied || currentState == AIState::AppliedEdited)
        {
            const auto bannerRect = juce::Rectangle<int> (0, 0, getWidth(), bannerH);
            g.setColour (RdhColors::ai.withAlpha (0.15f));
            g.fillRect  (bannerRect);
            g.setColour (RdhColors::ai);
            g.drawHorizontalLine (bannerH, 0.0f, static_cast<float> (getWidth()));

            g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
            g.setColour (RdhColors::ai);
            juce::String txt = juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa1"))
                               + juce::String (" AI APPLIED  ") + currentPreset;
            if (currentState == AIState::AppliedEdited) txt += " *";
            g.drawText (txt, 8, 0, getWidth() - 120, bannerH, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        const bool showBanner = (currentState == AIState::Applied
                                 || currentState == AIState::AppliedEdited);
        const int contentY = showBanner ? bannerH : 0;
        aiPanel.setBounds (0, contentY, getWidth(), getHeight() - contentY);

        if (showBanner)
            resetBtn.setBounds (getWidth() - 120, 4, 114, bannerH - 8);
    }

private:
    static constexpr int bannerH = 36;

    AIMatchPanel     aiPanel;
    juce::TextButton resetBtn;
    AIState          currentState  = AIState::Idle;
    juce::String     currentPreset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIModePanel)
};
