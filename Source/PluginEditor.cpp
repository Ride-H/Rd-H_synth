/*
  ==============================================================================
    PluginEditor.cpp
    Rd-H Synth v2.0
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
RdhSynthAudioProcessorEditor::RdhSynthAudioProcessorEditor (RdhSynthAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor   (p),
      presetManager    (p.apvts),
      topBar           (),
      bottomBar        (p.apvts),
      simplePanel      (p.apvts),
      aiPanel          (p),
      advancedPanel    (p.apvts),
      performancePanel (),
      spectrumAnalyzer (p),
      keyboard         (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&skin);
    setSize (900, 580);

    //--- Top / Bottom ---
    addAndMakeVisible (topBar);
    addAndMakeVisible (bottomBar);

    topBar.onModeChanged = [this] (ViewMode m) { onModeChanged (m); };

    // Clicking the preset name opens the .rdhs load dialog,
    // and the TopBar name refreshes once a file is loaded. This is the minimal
    // bring-forward of the Sprint 3 preset browser, used for Sprint 1 audition.
    topBar.onPresetNameClicked = [this] { presetManager.loadPresetDialog(); };
    presetManager.onPresetLoaded = [this] (const juce::String& name)
    {
        topBar.setPresetName (name, false, false);
        audioProcessor.setCurrentPresetName (name);   // persist for editor reopen
    };

    //--- Mode panels ---
    addAndMakeVisible (simplePanel);
    addChildComponent (aiPanel);
    addChildComponent (advancedPanel);
    addChildComponent (performancePanel);

    // Start in Simple mode
    onModeChanged (ViewMode::Simple);

    simplePanel.onPresetSelected = [this] (int idx) { onPresetSelected (idx); };

    //--- Spectrum ---
    addAndMakeVisible (spectrumAnalyzer);

    //--- Keyboard ---
    keyboard.setScrollButtonsVisible (false);
    addAndMakeVisible (keyboard);

    octaveDownButton.onClick = [this]
    {
        lowestVisibleNote = juce::jmax (12, lowestVisibleNote - 12);
        applyKeyboardRange();
    };
    octaveUpButton.onClick = [this]
    {
        lowestVisibleNote = juce::jmin (72, lowestVisibleNote + 12);
        applyKeyboardRange();
    };
    addAndMakeVisible (octaveDownButton);
    addAndMakeVisible (octaveUpButton);

    // Restore the displayed preset name from the processor so it survives
    // editor close/reopen. If none was set yet, keep the TopBar default.
    if (const auto saved = audioProcessor.getCurrentPresetName(); saved.isNotEmpty())
        topBar.setPresetName (saved, false, false);
}

//==============================================================================
RdhSynthAudioProcessorEditor::~RdhSynthAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void RdhSynthAudioProcessorEditor::onModeChanged (ViewMode m)
{
    // Hide all panels
    for (auto* c : { (juce::Component*) &simplePanel,
                     (juce::Component*) &aiPanel,
                     (juce::Component*) &advancedPanel,
                     (juce::Component*) &performancePanel })
        c->setVisible (false);

    // Show selected
    switch (m)
    {
        case ViewMode::Simple:      simplePanel.setVisible      (true); break;
        case ViewMode::AI:          aiPanel.setVisible          (true); break;
        case ViewMode::Advanced:    advancedPanel.setVisible    (true); break;
        case ViewMode::Performance: performancePanel.setVisible (true); break;
    }
    resized();
}

//==============================================================================
void RdhSynthAudioProcessorEditor::onPresetSelected (int idx)
{
    presetManager.loadBuiltinPreset (idx);

    // Update top-bar preset name
    const auto nm = PresetManager::getBuiltinPresetName (idx);
    topBar.setPresetName (nm, false, false);
    audioProcessor.setCurrentPresetName (nm);   // persist for editor reopen
}

//==============================================================================
void RdhSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (RdhColors::bg);
}

//==============================================================================
void RdhSynthAudioProcessorEditor::resized()
{
    const int W = getWidth();

    // Top bar
    topBar.setBounds (0, 0, W, TOP_H);

    // Mode content area
    const juce::Rectangle<int> contentArea (0, TOP_H, W, CONTENT_H);
    for (auto* c : { (juce::Component*) &simplePanel,
                     (juce::Component*) &aiPanel,
                     (juce::Component*) &advancedPanel,
                     (juce::Component*) &performancePanel })
        c->setBounds (contentArea);

    // Spectrum
    spectrumAnalyzer.setBounds (4, TOP_H + CONTENT_H + 2, W - 8, SPECTRUM_H - 4);

    // Octave buttons + keyboard
    const int kbY = TOP_H + CONTENT_H + SPECTRUM_H;
    const int kbX = 0, kbW = W;
    octaveDownButton.setBounds (kbX,              kbY, 52, OCT_BTN_H);
    octaveUpButton.setBounds   (kbX + kbW - 52,   kbY, 52, OCT_BTN_H);
    keyboard.setBounds         (kbX, kbY + OCT_BTN_H, kbW, KEYBOARD_H);

    // Bottom bar
    bottomBar.setBounds (0, TOP_H + CONTENT_H + SPECTRUM_H + OCT_BTN_H + KEYBOARD_H,
                         W, BOTTOM_H);

    applyKeyboardRange();
}

//==============================================================================
void RdhSynthAudioProcessorEditor::applyKeyboardRange()
{
    const int lo = lowestVisibleNote;
    const int hi = lo + 36; // 3 octaves = 22 white keys
    keyboard.setAvailableRange (lo, hi);
    keyboard.setLowestVisibleKey (lo);

    const int kbW = keyboard.getWidth();
    if (kbW > 0)
        keyboard.setKeyWidth (static_cast<float> (kbW) / 22.0f);
}
