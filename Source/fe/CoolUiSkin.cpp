/*
  ==============================================================================
    CoolUiSkin.cpp
  ==============================================================================
*/

#include "CoolUiSkin.h"

CoolUiSkin::CoolUiSkin()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colors::background);
    setColour (juce::Label::textColourId,                 Colors::textColor);
    setColour (juce::Slider::thumbColourId,               Colors::accent1);
    setColour (juce::Slider::trackColourId,               Colors::inactive);
    setColour (juce::ComboBox::backgroundColourId,        Colors::panel);
    setColour (juce::ComboBox::textColourId,              Colors::textColor);
    setColour (juce::ComboBox::outlineColourId,           Colors::inactive);
    setColour (juce::ComboBox::arrowColourId,             Colors::accent1);
    setColour (juce::PopupMenu::backgroundColourId,       Colors::panel);
    setColour (juce::PopupMenu::textColourId,             Colors::textColor);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colors::inactive);
    setColour (juce::PopupMenu::highlightedTextColourId,  Colors::accent1);
    setColour (juce::TextButton::buttonColourId,          Colors::panel);
    setColour (juce::TextButton::textColourOffId,         Colors::textColor);
    setColour (juce::TextButton::buttonOnColourId,        Colors::accent1.withAlpha (0.3f));
    setColour (juce::TextButton::textColourOnId,          Colors::accent1);
    setColour (juce::ProgressBar::backgroundColourId,     Colors::inactive);
    setColour (juce::ProgressBar::foregroundColourId,     Colors::accent1);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId,    juce::Colour (0xffF8F8F8));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId,    juce::Colour (0xff3A3A3C));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Colors::inactive);
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
               Colors::accent1.withAlpha (0.4f));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
               Colors::accent1.withAlpha (0.7f));
}

//==============================================================================
void CoolUiSkin::drawRotarySlider (juce::Graphics& g,
                                    int x, int y, int width, int height,
                                    float sliderPos,
                                    float startAngle, float endAngle,
                                    juce::Slider& /*slider*/)
{
    const float cx = (float) x + (float) width  * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float r  = juce::jmin ((float) width, (float) height) * 0.5f - 4.0f;

    {
        juce::Path track;
        track.addArc (cx - r, cy - r, r * 2.0f, r * 2.0f,
                      startAngle, endAngle, true);
        g.setColour (Colors::inactive);
        g.strokePath (track, juce::PathStrokeType (2.5f));
    }

    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    {
        juce::Path led;
        led.addArc (cx - r, cy - r, r * 2.0f, r * 2.0f,
                    startAngle, angle, true);
        g.setColour (Colors::accent1);
        g.strokePath (led, juce::PathStrokeType (2.5f,
                           juce::PathStrokeType::curved,
                           juce::PathStrokeType::rounded));
    }

    const float gx = cx + r * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float gy = cy + r * std::sin (angle - juce::MathConstants<float>::halfPi);
    drawGlow (g, gx, gy, 5.0f, Colors::accent1, 0.7f);

    const float kr = r * 0.62f;
    {
        juce::ColourGradient grad (Colors::knobBody,  cx, cy,
                                   Colors::knobCenter, cx + kr, cy + kr, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
    }
    g.setColour (Colors::panelBorder);
    g.drawEllipse (cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.0f);

    const float px = cx + (kr - 4.0f) * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float py = cy + (kr - 4.0f) * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (Colors::accent1);
    g.drawLine (cx, cy, px, py, 1.5f);
    g.setColour (Colors::accent1.withAlpha (0.9f));
    g.fillEllipse (px - 2.0f, py - 2.0f, 4.0f, 4.0f);
}

//==============================================================================
void CoolUiSkin::drawGlow (juce::Graphics& g,
                             float cx, float cy, float radius,
                             juce::Colour color, float intensity)
{
    for (int i = 3; i >= 1; --i)
    {
        const float r = radius + (float) i * 2.0f;
        g.setColour (color.withAlpha (intensity * 0.15f / (float) i));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    }
    g.setColour (color.withAlpha (intensity));
    g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
}

//==============================================================================
void CoolUiSkin::drawButtonBackground (juce::Graphics& g,
                                        juce::Button& button,
                                        const juce::Colour& /*bgColor*/,
                                        bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool toggled = button.getToggleState();

    juce::Colour fill = toggled ? Colors::accent1.withAlpha (0.25f) : Colors::panel;
    if (highlighted) fill = fill.brighter (0.1f);
    if (down)        fill = fill.darker   (0.1f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (toggled ? Colors::accent1 : Colors::inactive);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    if (toggled)
    {
        g.setColour (Colors::accent1.withAlpha (0.3f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), 5.0f, 1.5f);
    }
}

void CoolUiSkin::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                   bool /*highlighted*/, bool /*down*/)
{
    g.setColour (button.getToggleState() ? Colors::accent1 : Colors::textColor);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds(),
                      juce::Justification::centred, 1);
}

//==============================================================================
void CoolUiSkin::drawComboBox (juce::Graphics& g,
                                 int width, int height, bool /*isDown*/,
                                 int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                                 juce::ComboBox& /*box*/)
{
    g.setColour (Colors::panel);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
    g.setColour (Colors::inactive);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f,
                             (float) height - 1.0f, 4.0f, 1.0f);

    const float arrowX = (float) width - 14.0f;
    const float arrowY = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowX, arrowY - 3.0f,
                       arrowX + 6.0f, arrowY - 3.0f,
                       arrowX + 3.0f, arrowY + 3.0f);
    g.setColour (Colors::accent1);
    g.fillPath (arrow);
}

void CoolUiSkin::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 18, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centred);
}

//==============================================================================
void CoolUiSkin::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setColour (Colors::panel);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
    g.setColour (Colors::inactive);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f,
                             (float) height - 1.0f, 4.0f, 1.0f);
}

//==============================================================================
void CoolUiSkin::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (Colors::textColor.withAlpha (0.75f));
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText(),
                      label.getLocalBounds(),
                      label.getJustificationType(), 2);
}

juce::Font CoolUiSkin::getLabelFont (juce::Label&)
{
    return uiFont;
}
