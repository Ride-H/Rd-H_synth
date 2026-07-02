/*
  ==============================================================================
    BottomBar.h
    Bottom bar: CPU meter + voice count (left) | master volume (right).
    REQ-UI-008.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "RdhColors.h"

class RdhSynthAudioProcessor;

//==============================================================================
class BottomBar : public juce::Component,
                  private juce::Timer
{
public:
    explicit BottomBar (juce::AudioProcessorValueTreeState& apvts)
    {
        // Master volume knob
        volumeSlider.setSliderStyle       (juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle      (juce::Slider::TextBoxLeft, false, 48, 20);
        volumeSlider.setColour (juce::Slider::textBoxTextColourId,      RdhColors::textPrimary);
        volumeSlider.setColour (juce::Slider::textBoxBackgroundColourId, RdhColors::surface2);
        volumeSlider.setColour (juce::Slider::textBoxOutlineColourId,    RdhColors::border);
        addAndMakeVisible (volumeSlider);

        volumeLabel.setText ("VOL", juce::dontSendNotification);
        volumeLabel.setColour (juce::Label::textColourId, RdhColors::textSecondary);
        volumeLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (volumeLabel);

        volumeAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, "volume", volumeSlider);

        startTimerHz (10);
    }

    ~BottomBar() override { stopTimer(); }

    void setCpuUsage (float cpuPercent) { cpu = cpuPercent; }
    void setVoiceCount (int n)          { voiceCount = n; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (RdhColors::bg);
        g.setColour (RdhColors::border);
        g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));

        // CPU meter
        const float cpuNorm = juce::jlimit (0.0f, 1.0f, cpu / 100.0f);
        const int meterW = 80, meterH = 10, meterX = 8, meterY = (getHeight() - meterH) / 2;

        g.setColour (RdhColors::surface2);
        g.fillRect (meterX, meterY, meterW, meterH);

        const juce::Colour cpuCol = cpuNorm < 0.5f ? RdhColors::primary
                                  : cpuNorm < 0.8f ? RdhColors::accent
                                                   : RdhColors::danger;
        g.setColour (cpuCol);
        g.fillRect (meterX, meterY, (int)(meterW * cpuNorm), meterH);

        g.setColour (RdhColors::border);
        g.drawRect (meterX, meterY, meterW, meterH);

        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (RdhColors::textSecondary);
        g.drawText ("CPU  " + juce::String ((int)(cpu + 0.5f)) + "%",
                    meterX + meterW + 4, 0, 64, getHeight(), juce::Justification::centredLeft);

        // Voice count
        g.drawText ("VOICES " + juce::String (voiceCount),
                    meterX + meterW + 70, 0, 80, getHeight(), juce::Justification::centredLeft);
    }

    void resized() override
    {
        volumeLabel.setBounds  (getWidth() - 170, 0, 28, getHeight());
        volumeSlider.setBounds (getWidth() - 140, 0, 135, getHeight());
    }

private:
    void timerCallback() override { repaint(); }

    juce::Slider volumeSlider;
    juce::Label  volumeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttach;

    float cpu        = 0.0f;
    int   voiceCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomBar)
};
