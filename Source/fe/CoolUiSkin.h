/*
  ==============================================================================
    CoolUiSkin.h
    Custom JUCE LookAndFeel: Rd-H Synth dark-green neon theme.
    Colors sourced from RdhColors.h (spec-compliant color_palette.md tokens).
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "gui/RdhColors.h"

// Legacy alias so any existing code using "Colors::" continues to compile.
namespace Colors = RdhColors;

//==============================================================================
class CoolUiSkin : public juce::LookAndFeel_V4
{
public:
    CoolUiSkin();

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float startAngle, float endAngle,
                           juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& bgColor,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics& g,
                       int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;

    juce::Font getLabelFont (juce::Label&) override;

private:
    static void drawGlow (juce::Graphics& g,
                          float cx, float cy,
                          float radius,
                          juce::Colour color,
                          float intensity);

    juce::Font uiFont { juce::FontOptions (13.0f) };
};
