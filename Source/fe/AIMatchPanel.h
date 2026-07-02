/*
  ==============================================================================
    AIMatchPanel.h
    File drag-drop zone, analysis progress, feature bars, AI MATCH button.
    Updated for Rd-H Synth (processor renamed to RdhSynthAudioProcessor).
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../be/AudioAnalyzer.h"
#include "../ParameterMapper.h"

class RdhSynthAudioProcessor;

//==============================================================================
class AIMatchPanel : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::Timer
{
public:
    explicit AIMatchPanel (RdhSynthAudioProcessor& proc);
    ~AIMatchPanel() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter           (const juce::StringArray&, int, int) override;
    void fileDragExit            (const juce::StringArray&) override;
    void filesDropped            (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void startAnalysis (const juce::File& file);
    void applyResult   (const AnalysisResult& result);
    void drawFeatureBar (juce::Graphics& g, juce::Rectangle<int> bounds,
                         const juce::String& label, float value);

    RdhSynthAudioProcessor& processor;

    juce::TextButton aiMatchButton { "AI MATCH" };
    juce::ProgressBar progressBar;

    bool isDragOver    = false;
    bool isAnalysing   = false;

    double progressValue = 0.0;

    AnalysisResult currentResult;
    int    demoNoteNumber = 60;
    int    demoCountdown  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMatchPanel)
};
