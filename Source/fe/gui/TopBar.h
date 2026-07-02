/*
  ==============================================================================
    TopBar.h
    Top bar: Rd-H logo | AI / SIMPLE / ADVANCED / PERFORMANCE mode tabs | preset name.
    REQ-UI-006.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "RdhColors.h"

//==============================================================================
enum class ViewMode { AI, Simple, Advanced, Performance };

class TopBar : public juce::Component
{
public:
    std::function<void (ViewMode)> onModeChanged;
    std::function<void()>          onPresetNameClicked;

    TopBar()
    {
        // Mode tabs
        for (auto* b : { &btnAI, &btnSimple, &btnAdvanced, &btnPerf })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::textColourOffId, RdhColors::textSecondary);
            b->setColour (juce::TextButton::textColourOnId,  RdhColors::primary);
            b->setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
            b->setColour (juce::TextButton::buttonOnColourId,juce::Colours::transparentBlack);
            addAndMakeVisible (*b);
        }

        btnAI.setButtonText       ("AI");
        btnSimple.setButtonText   ("SIMPLE");
        btnAdvanced.setButtonText ("ADVANCED");
        btnPerf.setButtonText     ("PERFORM");

        btnAI.onClick       = [this] { selectMode (ViewMode::AI); };
        btnSimple.onClick   = [this] { selectMode (ViewMode::Simple); };
        btnAdvanced.onClick = [this] { selectMode (ViewMode::Advanced); };
        btnPerf.onClick     = [this] { selectMode (ViewMode::Performance); };

        presetLabel.setColour (juce::Label::textColourId, RdhColors::textPrimary);
        presetLabel.setText ("— No Preset —", juce::dontSendNotification);
        presetLabel.setJustificationType (juce::Justification::centredRight);
        // The preset name doubles as a "load preset" affordance: TopBar handles
        // the click (label must not swallow it) and fires onPresetNameClicked.
        presetLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (presetLabel);

        selectMode (ViewMode::Simple);
    }

    // Clicking the preset-name region opens the preset load flow.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onPresetNameClicked && presetLabel.getBounds().contains (e.getPosition()))
            onPresetNameClicked();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool overPreset = onPresetNameClicked
                             && presetLabel.getBounds().contains (e.getPosition());
        setMouseCursor (overPreset ? juce::MouseCursor::PointingHandCursor
                                   : juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (RdhColors::bg);
        g.setColour (RdhColors::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

        // Logo
        g.setFont (juce::Font (juce::FontOptions (18.0f)).boldened());
        g.setColour (RdhColors::primary);
        g.drawText ("Rd-H", 10, 0, 60, getHeight(), juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (RdhColors::textDim);
        g.drawText ("SYNTH v2.0", 68, 0, 80, getHeight(), juce::Justification::centredLeft);

        // Underline for active tab
        const auto* activeBtn = getActiveModeButton();
        if (activeBtn != nullptr)
        {
            const auto colour = activeModeColour();
            const auto b = activeBtn->getBounds();
            g.setColour (colour);
            g.fillRect (b.getX(), b.getBottom() - 2, b.getWidth(), 2);
        }
    }

    void resized() override
    {
        const int bW = 88, bH = getHeight();
        const int cx = getWidth() / 2;
        const int tabsW = bW * 4;
        int tx = cx - tabsW / 2;

        btnAI.setBounds       (tx,      0, bW, bH);  tx += bW;
        btnSimple.setBounds   (tx,      0, bW, bH);  tx += bW;
        btnAdvanced.setBounds (tx,      0, bW, bH);  tx += bW;
        btnPerf.setBounds     (tx,      0, bW, bH);

        presetLabel.setBounds (getWidth() - 230, 0, 225, bH);
    }

    ViewMode currentMode() const { return mode; }

    void setPresetName (const juce::String& name, bool aiApplied, bool edited)
    {
        juce::String display;
        if (aiApplied) display = juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa1")) + juce::String (" AI: ") + name;
        else           display = name;
        if (edited)    display += " *";
        presetLabel.setText (display, juce::dontSendNotification);
        presetLabel.setColour (juce::Label::textColourId,
                               aiApplied ? RdhColors::ai : RdhColors::textPrimary);
    }

private:
    void selectMode (ViewMode m)
    {
        mode = m;
        if (onModeChanged) onModeChanged (m);
        repaint();
    }

    juce::Colour activeModeColour() const
    {
        switch (mode)
        {
            case ViewMode::AI:          return RdhColors::ai;
            case ViewMode::Simple:      return RdhColors::primary;
            case ViewMode::Advanced:    return RdhColors::primary;
            case ViewMode::Performance: return RdhColors::purple;
        }
        return RdhColors::primary;
    }

    juce::TextButton* getActiveModeButton()
    {
        switch (mode)
        {
            case ViewMode::AI:          return &btnAI;
            case ViewMode::Simple:      return &btnSimple;
            case ViewMode::Advanced:    return &btnAdvanced;
            case ViewMode::Performance: return &btnPerf;
        }
        return nullptr;
    }

    ViewMode         mode = ViewMode::Simple;
    juce::TextButton btnAI, btnSimple, btnAdvanced, btnPerf;
    juce::Label      presetLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
