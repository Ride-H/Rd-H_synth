/*
  ==============================================================================
    RdhColors.h
    Color token definitions — Rd-H Synth WHITE theme palette.
    Source: UI design spec.
    HC-5: neon green #7CFC8A retained as primary ACCENT (glow / indicators).
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

namespace RdhColors
{
    // --- Background / Surface (white base) ---
    inline const juce::Colour bg        { 0xffFAFAFA }; // near white
    inline const juce::Colour surface   { 0xffF0F0F2 }; // panel base
    inline const juce::Colour surface2  { 0xffE4E4E8 }; // card / inner block
    inline const juce::Colour border    { 0xffC0C0C8 };
    inline const juce::Colour borderLt  { 0xffD4D4DC };

    // --- Primary (neon green — HC-5 retained as accent/glow) ---
    inline const juce::Colour primary     { 0xff7CFC8A }; // accent glow / indicators
    inline const juce::Colour primaryDark { 0xff00A83A }; // text-safe on white
    inline const juce::Colour primaryGlow { 0x407CFC8A }; // 25% alpha glow

    // --- Accent (FM section, amber — adjusted for white bg) ---
    inline const juce::Colour accent     { 0xffD97706 };
    inline const juce::Colour accentGlow { 0x40D97706 };

    // --- Performance mode (purple) ---
    inline const juce::Colour purple     { 0xff7C3AED };
    inline const juce::Colour purpleGlow { 0x407C3AED };

    // --- AI mode (cyan) ---
    inline const juce::Colour ai         { 0xff0891B2 };
    inline const juce::Colour aiGlow     { 0x400891B2 };

    // --- Text (dark for white bg) ---
    inline const juce::Colour textPrimary   { 0xff1A1A1A };
    inline const juce::Colour textSecondary { 0xff5A5A5A };
    inline const juce::Colour textDim       { 0xff9A9A9A };

    // --- Section tab colors (Advanced mode) ---
    inline const juce::Colour sectionOsc   { primaryDark };
    inline const juce::Colour sectionFM    { accent      };
    inline const juce::Colour sectionNoise { ai          };
    inline const juce::Colour sectionFlt   { purple      };
    inline const juce::Colour sectionAmp   { textPrimary };
    inline const juce::Colour sectionFX    { 0xffDB2777 }; // pink (darker for white)
    inline const juce::Colour sectionMod   { 0xffB45309 }; // amber-brown

    // --- Status ---
    inline const juce::Colour danger  { 0xffEF4444 };

    // --- Legacy aliases (used by CoolUiSkin / SpectrumAnalyzerComponent) ---
    inline const juce::Colour background  { bg };
    inline const juce::Colour panel       { surface };
    inline const juce::Colour panelBorder { borderLt };
    inline const juce::Colour accent1     { primary };
    inline const juce::Colour accent2     { primaryDark };
    inline const juce::Colour hot         { accent };
    inline const juce::Colour textColor   { textPrimary };
    inline const juce::Colour inactive    { border };
    inline const juce::Colour knobBody    { 0xffD4D4D8 }; // light gray
    inline const juce::Colour knobCenter  { 0xffC0C0C4 };
}
