/*
  ==============================================================================
    AdvancedModePanel.h
    Advanced mode: section tabs (OSC | FM | NOISE | FILTER | OSC ENV | FX | MOD).
    FM tab shows "INDEPENDENT FM SECTION" banner (REQ-FM-002, HC-4).
    REQ-UI-011.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../RdhColors.h"

//==============================================================================
class AdvancedModePanel : public juce::Component
{
public:
    enum class Tab { OSC, FM, Noise, Filter, OscEnv, FX, Mod };

    explicit AdvancedModePanel (juce::AudioProcessorValueTreeState& apvts)
        : apvtsRef (apvts)
    {
        // Tab buttons
        struct TabInfo { const char* label; juce::Colour color; };
        const TabInfo tabs[] = {
            { "OSC",    RdhColors::sectionOsc   },
            { "FM",     RdhColors::sectionFM    },
            { "NOISE",  RdhColors::sectionNoise },
            { "FILTER", RdhColors::sectionFlt   },
            { "OSC ENV", RdhColors::sectionAmp   },
            { "FX",     RdhColors::textDim      }, // stub
            { "MOD",    RdhColors::textDim      }, // stub
        };

        for (int i = 0; i < 7; ++i)
        {
            auto* btn = tabButtons.add (new juce::TextButton (tabs[i].label));
            btn->setClickingTogglesState (false);
            btn->setColour (juce::TextButton::textColourOffId, RdhColors::textSecondary);
            btn->setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
            tabColors.add (tabs[i].color);
            addAndMakeVisible (*btn);
            const int idx = i;
            btn->onClick = [this, idx] { selectTab (static_cast<Tab> (idx)); };
        }

        buildOSCPanel();
        buildFMPanel();
        buildNoisePanel();
        buildFilterPanel();
        buildOscEnvPanel();

        selectTab (Tab::OSC);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (RdhColors::bg);

        // Tab bar underline
        g.setColour (RdhColors::border);
        g.drawHorizontalLine (tabH, 0.0f, static_cast<float> (getWidth()));

        // Active tab underline + color
        const int tIdx = static_cast<int> (currentTab);
        if (tIdx < tabButtons.size())
        {
            auto* b = tabButtons[tIdx];
            g.setColour (tabColors[tIdx]);
            g.fillRect  (b->getX(), tabH - 2, b->getWidth(), 2);
        }

        // OSC1 frame — drawn to match the visual weight of osc2Enable/osc3Enable buttons
        if (currentTab == Tab::OSC && !osc1FrameRect.isEmpty())
        {
            g.setColour (RdhColors::borderLt);
            g.drawRoundedRectangle (osc1FrameRect.toFloat(), 3.0f, 1.0f);
        }

        // FM separator badge (REQ-FM-002)
        const auto fmBtn = tabButtons[static_cast<int> (Tab::FM)];
        if (fmBtn != nullptr)
        {
            g.setColour (RdhColors::accent.withAlpha (0.3f));
            g.drawVerticalLine (fmBtn->getX() - 1, 2.0f, static_cast<float> (tabH - 2));
            g.drawVerticalLine (fmBtn->getRight(), 2.0f, static_cast<float> (tabH - 2));
        }

        // FM panel banner (REQ-FM-002)
        if (currentTab == Tab::FM)
        {
            const int bannerY = tabH + 2;
            g.setColour (RdhColors::accent.withAlpha (0.12f));
            g.fillRect (0, bannerY, getWidth(), fmBannerH);
            g.setColour (RdhColors::accent);
            g.drawHorizontalLine (bannerY + fmBannerH, 0.0f, static_cast<float> (getWidth()));
            g.setFont (juce::Font (juce::FontOptions (10.0f)).boldened());
            g.drawText ("\xe2\x96\x8c INDEPENDENT FM SECTION  \xc2\xb7  DX7-style 2 Operator (Phase 1)",
                        8, bannerY, getWidth() - 16, fmBannerH, juce::Justification::centredLeft);
        }
    }

private:
    //==========================================================================
    static constexpr int tabH      = 32;
    static constexpr int fmBannerH = 24;

    void selectTab (Tab t)
    {
        currentTab = t;
        for (auto* comp : allPanels)
            comp->setVisible (false);

        juce::Component* active = nullptr;
        switch (t)
        {
            case Tab::OSC:    active = oscPanel.get();    break;
            case Tab::FM:     active = fmPanel.get();     break;
            case Tab::Noise:  active = noisePanel.get();  break;
            case Tab::Filter: active = filterPanel.get(); break;
            case Tab::OscEnv: active = oscEnvPanel.get(); break;
            case Tab::FX:
            case Tab::Mod:    break; // Phase 1 stubs — no panel assigned
        }
        if (active) active->setVisible (true);

        // Update tab button text colors
        for (int i = 0; i < tabButtons.size(); ++i)
        {
            const bool on = (i == static_cast<int> (t));
            tabButtons[i]->setColour (juce::TextButton::textColourOffId,
                                      on ? tabColors[i] : RdhColors::textSecondary);
        }
        resized();
        repaint();
    }

    //==========================================================================
    // OSC panel: 3 oscillators + unison controls
    //==========================================================================
    void buildOSCPanel()
    {
        oscPanel = std::make_unique<juce::Component>();
        addChildComponent (*oscPanel);
        allPanels.add (oscPanel.get());

        auto make = [this] (juce::ComboBox& c)
        {
            c.addItem ("Sine",     1);
            c.addItem ("Saw",      2);
            c.addItem ("Square",   3);
            c.addItem ("Triangle", 4);
            oscPanel->addAndMakeVisible (c);
        };
        make (osc1Combo); make (osc2Combo); make (osc3Combo);

        for (auto* b : { &osc2Enable, &osc3Enable })
        {
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId,
                          RdhColors::primary.withAlpha (0.3f));
            b->setColour (juce::TextButton::textColourOnId, RdhColors::primary);
            oscPanel->addAndMakeVisible (*b);
        }
        osc2Enable.setButtonText ("OSC 2");
        osc3Enable.setButtonText ("OSC 3");

        setupKnob (*oscPanel, unisonVoicesSlider, unisonVoicesLabel, "VOICES");
        setupKnob (*oscPanel, unisonDetuneSlider, unisonDetuneLabel, "DETUNE");
        setupKnob (*oscPanel, pitchAmtSlider,     pitchAmtLabel,     "ENV AMT");
        setupKnob (*oscPanel, pitchAtkSlider,     pitchAtkLabel,     "ENV ATK");

        using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using BA  = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
        osc1Attach  = std::make_unique<CBA> (apvtsRef, "waveType",   osc1Combo);
        osc2Attach  = std::make_unique<CBA> (apvtsRef, "waveType2",  osc2Combo);
        osc2EnAttach= std::make_unique<BA>  (apvtsRef, "osc2Enable", osc2Enable);
        osc3Attach  = std::make_unique<CBA> (apvtsRef, "waveType3",  osc3Combo);
        osc3EnAttach= std::make_unique<BA>  (apvtsRef, "osc3Enable", osc3Enable);
        uniVAttach  = std::make_unique<SA>  (apvtsRef, "unisonVoices",  unisonVoicesSlider);
        uniDAttach  = std::make_unique<SA>  (apvtsRef, "unisonDetune",  unisonDetuneSlider);
        pitchAAttach= std::make_unique<SA>  (apvtsRef, "pitchEnvAmount",pitchAmtSlider);
        pitchBAttach= std::make_unique<SA>  (apvtsRef, "pitchEnvAttack",pitchAtkSlider);

        // Layout inline (simple fixed positions)
        oscPanel->addAndMakeVisible (osc1Label);
        osc1Label.setText ("OSC 1", juce::dontSendNotification);
        osc1Label.setColour (juce::Label::textColourId, RdhColors::primary);
        osc1Label.setFont (juce::Font (juce::FontOptions (12.0f)));
        osc1Label.setJustificationType (juce::Justification::centred);
    }

    //==========================================================================
    // FM panel: FM Enable + OP1 + OP2 controls
    //==========================================================================
    void buildFMPanel()
    {
        fmPanel = std::make_unique<juce::Component>();
        addChildComponent (*fmPanel);
        allPanels.add (fmPanel.get());

        fmEnableBtn.setButtonText ("FM ENABLE");
        fmEnableBtn.setClickingTogglesState (true);
        fmEnableBtn.setColour (juce::TextButton::buttonOnColourId,  RdhColors::accent.withAlpha (0.25f));
        fmEnableBtn.setColour (juce::TextButton::textColourOnId,    RdhColors::accent);
        fmEnableBtn.setColour (juce::TextButton::textColourOffId,   RdhColors::textSecondary);
        fmPanel->addAndMakeVisible (fmEnableBtn);

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        fmEnAttach = std::make_unique<BA> (apvtsRef, "fm_enable", fmEnableBtn);

        const struct { juce::Slider* s; juce::Label* l; const char* lbl; const char* id; } fmKnobs[] = {
            // Global
            { &fmOutLevel, &fmOutLabel, "FM LEVEL", "fm_output_level" },
            // OP1
            { &op1Ratio,   &op1RatioLbl,   "RATIO",   "fm_op1_ratio"   },
            { &op1Detune,  &op1DetuneLbl,  "DETUNE",  "fm_op1_detune"  },
            { &op1Level,   &op1LevelLbl,   "LEVEL",   "fm_op1_level"   },
            { &op1Attack,  &op1AtkLbl,     "ATK",     "fm_op1_attack"  },
            { &op1Decay,   &op1DecLbl,     "DEC",     "fm_op1_decay"   },
            { &op1Sustain, &op1SusLbl,     "SUS",     "fm_op1_sustain" },
            { &op1Release, &op1RelLbl,     "REL",     "fm_op1_release" },
            // OP2
            { &op2Ratio,   &op2RatioLbl,   "RATIO",   "fm_op2_ratio"   },
            { &op2Detune,  &op2DetuneLbl,  "DETUNE",  "fm_op2_detune"  },
            { &op2Level,   &op2LevelLbl,   "LEVEL",   "fm_op2_level"   },
            { &op2Attack,  &op2AtkLbl,     "ATK",     "fm_op2_attack"  },
            { &op2Decay,   &op2DecLbl,     "DEC",     "fm_op2_decay"   },
            { &op2Sustain, &op2SusLbl,     "SUS",     "fm_op2_sustain" },
            { &op2Release, &op2RelLbl,     "REL",     "fm_op2_release" },
        };

        for (auto& k : fmKnobs)
        {
            setupKnob (*fmPanel, *k.s, *k.l, k.lbl);
            fmAttachments.push_back (std::make_unique<SA> (apvtsRef, k.id, *k.s));
        }
    }

    //==========================================================================
    // Noise panel
    //==========================================================================
    void buildNoisePanel()
    {
        noisePanel = std::make_unique<juce::Component>();
        addChildComponent (*noisePanel);
        allPanels.add (noisePanel.get());

        noiseEnableBtn.setButtonText ("NOISE ENABLE");
        noiseEnableBtn.setClickingTogglesState (true);
        noiseEnableBtn.setColour (juce::TextButton::buttonOnColourId,  RdhColors::ai.withAlpha (0.25f));
        noiseEnableBtn.setColour (juce::TextButton::textColourOnId,    RdhColors::ai);
        noiseEnableBtn.setColour (juce::TextButton::textColourOffId,   RdhColors::textSecondary);
        noisePanel->addAndMakeVisible (noiseEnableBtn);

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        noiseEnAttach = std::make_unique<BA> (apvtsRef, "noise_enable", noiseEnableBtn);

        const struct { juce::Slider* s; juce::Label* l; const char* lbl; const char* id; } noiseKnobs[] = {
            { &noiseLevel,     &noiseLevelLbl,   "LEVEL",   "noise_level"       },
            { &noiseFltSend,   &noiseFltLbl,     "FLT SEND","noise_filter_send" },
            { &noiseDirOut,    &noiseDirLbl,     "DIR OUT", "noise_direct_out"  },
            { &noiseEGAtk,     &noiseEGAtkLbl,   "ATK",     "noise_eg_attack"   },
            { &noiseEGDec,     &noiseEGDecLbl,   "DEC",     "noise_eg_decay"    },
            { &noiseEGSus,     &noiseEGSusLbl,   "SUS",     "noise_eg_sustain"  },
            { &noiseEGRel,     &noiseEGRelLbl,   "REL",     "noise_eg_release"  },
        };

        for (auto& k : noiseKnobs)
        {
            setupKnob (*noisePanel, *k.s, *k.l, k.lbl);
            noiseAttachments.push_back (std::make_unique<SA> (apvtsRef, k.id, *k.s));
        }
    }

    //==========================================================================
    // Filter panel
    //==========================================================================
    void buildFilterPanel()
    {
        filterPanel = std::make_unique<juce::Component>();
        addChildComponent (*filterPanel);
        allPanels.add (filterPanel.get());

        setupKnob (*filterPanel, filterCutoff,    filterCutoffLbl,    "CUTOFF");
        setupKnob (*filterPanel, filterResonance, filterResonanceLbl, "RESONANCE");

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        fltCutoffAttach = std::make_unique<SA> (apvtsRef, "filterCutoff",    filterCutoff);
        fltResAttach    = std::make_unique<SA> (apvtsRef, "filterResonance", filterResonance);

        // FilterEnv → Cutoff controls
        const struct { juce::Slider* s; juce::Label* l; const char* lbl; const char* id; } envKnobs[] = {
            { &filterEnvAmount,  &filterEnvAmountLbl, "ENV AMOUNT", "filter_env_amount"  },
            { &filterEnvAttack,  &filterEnvAtkLbl,    "ENV ATK",    "filter_env_attack"  },
            { &filterEnvDecay,   &filterEnvDecLbl,    "ENV DEC",    "filter_env_decay"   },
            { &filterEnvSustain, &filterEnvSusLbl,    "ENV SUS",    "filter_env_sustain" },
            { &filterEnvRelease, &filterEnvRelLbl,    "ENV REL",    "filter_env_release" },
        };
        for (auto& k : envKnobs)
        {
            setupKnob (*filterPanel, *k.s, *k.l, k.lbl);
            fltEnvAttachments.push_back (std::make_unique<SA> (apvtsRef, k.id, *k.s));
        }
    }

    //==========================================================================
    // OSC ENV panel (OSC gain ADSR)
    //==========================================================================
    void buildOscEnvPanel()
    {
        oscEnvPanel = std::make_unique<juce::Component>();
        addChildComponent (*oscEnvPanel);
        allPanels.add (oscEnvPanel.get());

        setupKnob (*oscEnvPanel, oscEnvAttack,  oscEnvAttackLbl,  "ATTACK");
        setupKnob (*oscEnvPanel, oscEnvDecay,   oscEnvDecayLbl,   "DECAY");
        setupKnob (*oscEnvPanel, oscEnvSustain, oscEnvSustainLbl, "SUSTAIN");
        setupKnob (*oscEnvPanel, oscEnvRelease, oscEnvReleaseLbl, "RELEASE");

        // Variables renamed amp* → oscEnv*; APVTS IDs unchanged.
        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        oscEnvAtkAttach = std::make_unique<SA> (apvtsRef, "attack",  oscEnvAttack);
        oscEnvDecAttach = std::make_unique<SA> (apvtsRef, "decay",   oscEnvDecay);
        oscEnvSusAttach = std::make_unique<SA> (apvtsRef, "sustain", oscEnvSustain);
        oscEnvRelAttach = std::make_unique<SA> (apvtsRef, "release", oscEnvRelease);
    }

    //==========================================================================
    void setupKnob (juce::Component& parent, juce::Slider& s, juce::Label& l,
                    const juce::String& text)
    {
        s.setSliderStyle  (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        s.setColour (juce::Slider::textBoxTextColourId,      RdhColors::textPrimary);
        s.setColour (juce::Slider::textBoxBackgroundColourId, RdhColors::surface2);
        s.setColour (juce::Slider::textBoxOutlineColourId,   RdhColors::border);
        parent.addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, RdhColors::textSecondary);
        parent.addAndMakeVisible (l);
    }

    // After selectTab: lay out the visible panel's children.
    // (Simple grid layout – production would add dedicated resized per panel)
    void layoutOSCPanel (juce::Rectangle<int> area)
    {
        const int kW = 64, kH = 50, gap = 4;
        const int oscRowH = 24, oscLabelW = 60, oscComboW = 120, oscRowGap = 6;

        // Children of oscPanel use LOCAL coords (origin = oscPanel top-left).
        // osc1FrameRect is drawn in AdvancedModePanel::paint() → needs panel coords.
        int localY = 8;
        osc1Label.setBounds  (8, localY, oscLabelW, oscRowH);
        osc1Combo.setBounds  (8 + oscLabelW + 6, localY, oscComboW, oscRowH);
        osc1FrameRect = juce::Rectangle<int> (area.getX() + 8, area.getY() + localY,
                                               oscLabelW, oscRowH);

        localY += oscRowH + oscRowGap;
        osc2Enable.setBounds (8, localY, oscLabelW, oscRowH);
        osc2Combo.setBounds  (8 + oscLabelW + 6, localY, oscComboW, oscRowH);

        localY += oscRowH + oscRowGap;
        osc3Enable.setBounds (8, localY, oscLabelW, oscRowH);
        osc3Combo.setBounds  (8 + oscLabelW + 6, localY, oscComboW, oscRowH);

        int x = 8, y = 105;
        auto plK = [&] (juce::Slider& s, juce::Label& lbl)
        {
            lbl.setBounds (x, y, kW, 12);
            s.setBounds   (x, y + 12, kW, kH);
            x += kW + gap;
        };
        plK (unisonVoicesSlider, unisonVoicesLabel);
        plK (unisonDetuneSlider, unisonDetuneLabel);
        plK (pitchAmtSlider, pitchAmtLabel);
        plK (pitchAtkSlider, pitchAtkLabel);
    }

    void layoutFMPanel (juce::Rectangle<int> area)
    {
        // Children of fmPanel use LOCAL coords (origin = fmPanel top-left).
        // fmPanel height = CONTENT_H - (tabH + fmBannerH + 2) = 222 - 58 = 164px.
        // Total layout below must fit within 164px.
        const int kW = 52, kH = 40, gap = 2;

        // Row 0: FM ENABLE (left) + FM OUT LEVEL (right, inline) — ends y=52
        fmEnableBtn.setBounds (8, 6, 110, 24);
        fmOutLabel.setBounds  (130, 0, kW, 12);
        fmOutLevel.setBounds  (130, 12, kW, kH);  // bottom = 52

        // OP1 row at local y=54 — ends y=106
        int x = 8;
        auto plOp = [&] (juce::Slider& s, juce::Label& l, int rowY)
        {
            l.setBounds (x, rowY,      kW, 12);
            s.setBounds (x, rowY + 12, kW, kH);
            x += kW + gap;
        };

        x = 8;
        plOp (op1Ratio,   op1RatioLbl,  54);
        plOp (op1Detune,  op1DetuneLbl, 54);
        plOp (op1Level,   op1LevelLbl,  54);
        plOp (op1Attack,  op1AtkLbl,    54);
        plOp (op1Decay,   op1DecLbl,    54);
        plOp (op1Sustain, op1SusLbl,    54);
        plOp (op1Release, op1RelLbl,    54);  // OP1 ends y=106

        // OP2 row at local y=110 — ends y=162 (<164 ✓)
        x = 8;
        plOp (op2Ratio,   op2RatioLbl,  110);
        plOp (op2Detune,  op2DetuneLbl, 110);
        plOp (op2Level,   op2LevelLbl,  110);
        plOp (op2Attack,  op2AtkLbl,    110);
        plOp (op2Decay,   op2DecLbl,    110);
        plOp (op2Sustain, op2SusLbl,    110);
        plOp (op2Release, op2RelLbl,    110);  // OP2 ends y=162
        juce::ignoreUnused (area);
    }

    void layoutNoisePanel (juce::Rectangle<int> area)
    {
        // LOCAL coords within noisePanel
        const int kW = 64, kH = 50, gap = 4;
        auto plK = [&] (juce::Slider& s, juce::Label& lbl, int x, int y)
        {
            lbl.setBounds (x, y, kW, 12);
            s.setBounds   (x, y + 12, kW, kH);
        };
        noiseEnableBtn.setBounds (8, 4, 130, 26);
        int x = 8, y = 34;
        plK (noiseLevel,   noiseLevelLbl,  x, y); x += kW + gap;
        plK (noiseFltSend, noiseFltLbl,    x, y); x += kW + gap;
        plK (noiseDirOut,  noiseDirLbl,    x, y);
        x = 8; y += kH + 12 + 6;
        plK (noiseEGAtk, noiseEGAtkLbl, x, y); x += kW + gap;
        plK (noiseEGDec, noiseEGDecLbl, x, y); x += kW + gap;
        plK (noiseEGSus, noiseEGSusLbl, x, y); x += kW + gap;
        plK (noiseEGRel, noiseEGRelLbl, x, y);
        juce::ignoreUnused (area);
    }

    void layoutFilterPanel (juce::Rectangle<int> area)
    {
        // LOCAL coords within filterPanel
        const int kW = 90, kH = 68;
        filterCutoffLbl.setBounds    (8,        4, kW, 14);
        filterCutoff.setBounds       (8,        18, kW, kH);
        filterResonanceLbl.setBounds (8 + kW + 8, 4, kW, 14);
        filterResonance.setBounds    (8 + kW + 8, 18, kW, kH);

        // FilterEnv row (smaller knobs, same grid as noise EG row)
        const int eW = 64, eH = 50, gap = 4;
        int x = 8;
        const int y = 18 + kH + 8; // below the cutoff/reso row
        auto plE = [&] (juce::Slider& s, juce::Label& lbl)
        {
            lbl.setBounds (x, y, eW, 12);
            s.setBounds   (x, y + 12, eW, eH);
            x += eW + gap;
        };
        plE (filterEnvAmount,  filterEnvAmountLbl);
        plE (filterEnvAttack,  filterEnvAtkLbl);
        plE (filterEnvDecay,   filterEnvDecLbl);
        plE (filterEnvSustain, filterEnvSusLbl);
        plE (filterEnvRelease, filterEnvRelLbl);
        juce::ignoreUnused (area);
    }

    void layoutOscEnvPanel (juce::Rectangle<int> area)
    {
        // LOCAL coords within oscEnvPanel
        const int kW = 90, kH = 68;
        int x = 8;
        auto plK = [&] (juce::Slider& s, juce::Label& l)
        {
            l.setBounds (x, 4, kW, 14);
            s.setBounds (x, 18, kW, kH);
            x += kW + 8;
        };
        plK (oscEnvAttack, oscEnvAttackLbl);  plK (oscEnvDecay, oscEnvDecayLbl);
        plK (oscEnvSustain,oscEnvSustainLbl); plK (oscEnvRelease,oscEnvReleaseLbl);
        juce::ignoreUnused (area);
    }

public:
    // Called by PluginEditor::resized after panels are sized by this component.
    // Triggers sub-panel layout. Overrides Component::resized.
    void resized() override
    {
        const int contentY = tabH + (currentTab == Tab::FM ? fmBannerH + 2 : 2);
        const auto content = juce::Rectangle<int> (0, contentY, getWidth(), getHeight() - contentY);

        const int tBtnW = getWidth() / 7;
        for (int i = 0; i < tabButtons.size(); ++i)
            tabButtons[i]->setBounds (i * tBtnW, 0, tBtnW, tabH);

        for (auto* comp : allPanels)
            comp->setBounds (content);

        // Layout the active panel's children
        switch (currentTab)
        {
            case Tab::OSC:    layoutOSCPanel    (content); break;
            case Tab::FM:     layoutFMPanel     (content); break;
            case Tab::Noise:  layoutNoisePanel  (content); break;
            case Tab::Filter: layoutFilterPanel (content); break;
            case Tab::OscEnv: layoutOscEnvPanel (content); break;
            case Tab::FX:
            case Tab::Mod:    break; // Phase 1 stubs
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvtsRef;
    Tab currentTab = Tab::OSC;
    juce::Rectangle<int> osc1FrameRect; // set in layoutOSCPanel, drawn in paint()

    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::Array<juce::Colour>          tabColors;

    // Panel containers
    std::unique_ptr<juce::Component> oscPanel, fmPanel, noisePanel, filterPanel, oscEnvPanel;
    juce::Array<juce::Component*>    allPanels;

    //--- OSC controls ---
    juce::Label    osc1Label { {}, "OSC 1" };
    juce::ComboBox osc1Combo, osc2Combo, osc3Combo;
    juce::TextButton osc2Enable, osc3Enable;
    juce::Slider unisonVoicesSlider, unisonDetuneSlider, pitchAmtSlider, pitchAtkSlider;
    juce::Label  unisonVoicesLabel,  unisonDetuneLabel,  pitchAmtLabel,  pitchAtkLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osc1Attach, osc2Attach, osc3Attach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   osc2EnAttach, osc3EnAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   uniVAttach, uniDAttach, pitchAAttach, pitchBAttach;

    //--- FM controls ---
    juce::TextButton fmEnableBtn;
    juce::Slider fmOutLevel;    juce::Label fmOutLabel;
    juce::Slider op1Ratio, op1Detune, op1Level, op1Attack, op1Decay, op1Sustain, op1Release;
    juce::Label  op1RatioLbl, op1DetuneLbl, op1LevelLbl, op1AtkLbl, op1DecLbl, op1SusLbl, op1RelLbl;
    juce::Slider op2Ratio, op2Detune, op2Level, op2Attack, op2Decay, op2Sustain, op2Release;
    juce::Label  op2RatioLbl, op2DetuneLbl, op2LevelLbl, op2AtkLbl, op2DecLbl, op2SusLbl, op2RelLbl;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fmEnAttach;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> fmAttachments;

    //--- Noise controls ---
    juce::TextButton noiseEnableBtn;
    juce::Slider noiseLevel, noiseFltSend, noiseDirOut;
    juce::Label  noiseLevelLbl, noiseFltLbl, noiseDirLbl;
    juce::Slider noiseEGAtk, noiseEGDec, noiseEGSus, noiseEGRel;
    juce::Label  noiseEGAtkLbl, noiseEGDecLbl, noiseEGSusLbl, noiseEGRelLbl;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> noiseEnAttach;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> noiseAttachments;

    //--- Filter controls ---
    juce::Slider filterCutoff, filterResonance;
    juce::Label  filterCutoffLbl, filterResonanceLbl;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fltCutoffAttach, fltResAttach;
    // FilterEnv → Cutoff
    juce::Slider filterEnvAmount, filterEnvAttack, filterEnvDecay, filterEnvSustain, filterEnvRelease;
    juce::Label  filterEnvAmountLbl, filterEnvAtkLbl, filterEnvDecLbl, filterEnvSusLbl, filterEnvRelLbl;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> fltEnvAttachments;

    //--- OSC ENV controls (renamed amp* → oscEnv*; APVTS IDs unchanged) ---
    juce::Slider oscEnvAttack, oscEnvDecay, oscEnvSustain, oscEnvRelease;
    juce::Label  oscEnvAttackLbl, oscEnvDecayLbl, oscEnvSustainLbl, oscEnvReleaseLbl;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> oscEnvAtkAttach, oscEnvDecAttach, oscEnvSusAttach, oscEnvRelAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedModePanel)
};
