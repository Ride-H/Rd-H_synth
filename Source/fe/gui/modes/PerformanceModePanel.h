/*
  ==============================================================================
    PerformanceModePanel.h
    Performance mode placeholder — implemented in Phase 3.
    REQ-UI-012.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../RdhColors.h"

//==============================================================================
class PerformanceModePanel : public juce::Component
{
public:
    PerformanceModePanel() = default;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (RdhColors::bg);

        g.setColour (RdhColors::purple.withAlpha (0.15f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (40.0f), 12.0f);

        g.setColour (RdhColors::purple);
        g.setFont (juce::Font (juce::FontOptions (20.0f)).boldened());
        g.drawText ("PERFORMANCE MODE", 0, getHeight() / 2 - 30, getWidth(), 32,
                    juce::Justification::centred);

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.setColour (RdhColors::textSecondary);
        g.drawText ("XY Pad  |  8 Macros  |  coming in Phase 3",
                    0, getHeight() / 2 + 4, getWidth(), 20,
                    juce::Justification::centred);
    }

    void resized() override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformanceModePanel)
};
