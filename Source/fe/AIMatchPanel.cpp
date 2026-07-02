/*
  ==============================================================================
    AIMatchPanel.cpp
  ==============================================================================
*/

#include "AIMatchPanel.h"
#include "../PluginProcessor.h"
#include "CoolUiSkin.h"

AIMatchPanel::AIMatchPanel (RdhSynthAudioProcessor& proc)
    : processor (proc), progressBar (progressValue)
{
    addAndMakeVisible (aiMatchButton);
    addChildComponent (progressBar);

    aiMatchButton.setEnabled (false);
    aiMatchButton.onClick = [this]
    {
        if (currentResult.isValid)
        {
            // Trigger demo note via keyboard state (C thread safe)
            processor.keyboardState.noteOn (1, demoNoteNumber, 0.8f);
            demoCountdown = 15; // ~0.5s at 30 Hz timer
        }
    };

    startTimerHz (30);
}

AIMatchPanel::~AIMatchPanel()
{
    stopTimer();
}

//==============================================================================
bool AIMatchPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const juce::String ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac")
            return true;
    }
    return false;
}

void AIMatchPanel::fileDragEnter (const juce::StringArray&, int, int)
{
    isDragOver = true;
    repaint();
}

void AIMatchPanel::fileDragExit (const juce::StringArray&)
{
    isDragOver = false;
    repaint();
}

void AIMatchPanel::filesDropped (const juce::StringArray& files, int, int)
{
    isDragOver = false;
    if (files.isEmpty()) return;
    startAnalysis (juce::File (files[0]));
}

//==============================================================================
void AIMatchPanel::startAnalysis (const juce::File& file)
{
    isAnalysing   = true;
    progressValue = 0.0;
    currentResult = AnalysisResult{};
    aiMatchButton.setEnabled (false);
    progressBar.setVisible (true);
    repaint();

    if (! processor.analyzer.loadFile (file))
    {
        isAnalysing = false;
        progressBar.setVisible (false);
        repaint();
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Error",
                                                "Could not read file.\n"
                                                "Supported: WAV, AIFF, FLAC");
        return;
    }

    processor.analyzer.analyseAsync();
}

//==============================================================================
void AIMatchPanel::timerCallback()
{
    if (demoCountdown > 0)
    {
        if (--demoCountdown == 0)
            processor.keyboardState.noteOff (1, demoNoteNumber, 0.0f);
    }

    if (! isAnalysing) return;

    progressValue = (double) processor.analyzer.getProgress();

    if (processor.analyzer.isComplete())
    {
        isAnalysing   = false;
        progressBar.setVisible (false);
        currentResult = processor.analyzer.getResult();

        if (currentResult.isValid)
        {
            applyResult (currentResult);
            aiMatchButton.setEnabled (true);
        }
    }

    repaint();
}

//==============================================================================
void AIMatchPanel::applyResult (const AnalysisResult& result)
{
    auto mapped = ParameterMapper::mapToParameters (result);
    demoNoteNumber = mapped.noteNumber;

    auto& apvts = processor.apvts;

    auto setParam = [&] (const juce::String& id, float rawVal)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (rawVal));
    };

    auto setParamNorm = [&] (const juce::String& id, float normVal)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normVal));
    };

    setParamNorm ("waveType",         (float) mapped.waveType / 3.0f);
    setParam     ("filterCutoff",      mapped.filterCutoff);
    setParam     ("filterResonance",   mapped.filterResonance);
    setParam     ("attack",            mapped.attack);
    setParam     ("decay",             mapped.decay);
    setParam     ("sustain",           mapped.sustain);
    setParam     ("release",           mapped.release);
}

//==============================================================================
void AIMatchPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Background
    g.setColour (Colors::panel);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

    // Section label
    g.setColour (Colors::inactive);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("AI MATCH", bounds.removeFromTop (18), juce::Justification::centred);

    if (! isAnalysing)
    {
        if (! currentResult.isValid)
        {
            // Drop zone
            g.setColour (isDragOver ? Colors::accent1.withAlpha (0.4f)
                                    : Colors::inactive.withAlpha (0.4f));
            auto dropZone = bounds.reduced (8).removeFromTop (70);

            // Dashed border
            juce::Path dashBorder;
            dashBorder.addRoundedRectangle (dropZone.toFloat(), 4.0f);
            juce::PathStrokeType stroke (1.5f);
            float dashLengths[2] = { 6.0f, 4.0f };
            stroke.createDashedStroke (dashBorder, dashBorder, dashLengths, 2);
            g.fillPath (dashBorder);

            g.setColour (isDragOver ? Colors::accent1 : Colors::textColor);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("DROP AUDIO FILE HERE\n(WAV / AIFF / FLAC)",
                              dropZone, juce::Justification::centred, 2);
        }
        else
        {
            // Feature bars
            auto barArea = bounds.reduced (8);
            barArea.removeFromTop (4);

            drawFeatureBar (g, barArea.removeFromTop (18),
                            "Brightness",
                            juce::jlimit (0.0f, 1.0f,
                                currentResult.spectralCentroid / 8000.0f));
            barArea.removeFromTop (4);

            drawFeatureBar (g, barArea.removeFromTop (18),
                            "Harmonics",
                            currentResult.harmonicRatio);
            barArea.removeFromTop (4);

            drawFeatureBar (g, barArea.removeFromTop (18),
                            "Transient",
                            currentResult.transientSharpness);
        }
    }

    // Border
    g.setColour (Colors::panelBorder);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void AIMatchPanel::drawFeatureBar (juce::Graphics& g,
                                    juce::Rectangle<int> bounds,
                                    const juce::String& label,
                                    float value)
{
    auto labelArea = bounds.removeFromLeft (70);
    g.setColour (Colors::textColor.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (label, labelArea, juce::Justification::centredRight);

    bounds.removeFromLeft (4);
    const float filled = static_cast<float> (bounds.getWidth()) * juce::jlimit (0.0f, 1.0f, value);

    g.setColour (Colors::inactive);
    g.fillRoundedRectangle (bounds.toFloat(), 2.0f);

    g.setColour (Colors::accent1);
    g.fillRoundedRectangle (bounds.removeFromLeft ((int) filled).toFloat(), 2.0f);
}

//==============================================================================
void AIMatchPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (18); // label

    // Progress bar: just above the AI MATCH button
    auto bottom = area.removeFromBottom (28);
    aiMatchButton.setBounds (bottom.reduced (4, 0));
    progressBar.setBounds (bottom.reduced (4, 0));
}
