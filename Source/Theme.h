#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace fbd
{

// Palette grafica FBTECH (dal sito crea.css)
struct Neon
{
    static const juce::Colour bg0    { 0xff031019 };
    static const juce::Colour bg1    { 0xff04101a };
    static const juce::Colour panel  { 0xff0a2a3c };
    static const juce::Colour cyan   { 0xff2fd6ff };
    static const juce::Colour pink   { 0xffff4fa3 };
    static const juce::Colour green  { 0xff35f2a0 };
    static const juce::Colour yellow { 0xffffd24d };
    static const juce::Colour text   { 0xffcfe9f7 };
};

// Look and feel neon per tutta l'applicazione
class FBLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FBLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Neon::bg0);
        setColour (juce::Label::textColourId, Neon::text);
        setColour (juce::TextButton::buttonColourId, Neon::panel);
        setColour (juce::TextButton::buttonOnColourId, Neon::cyan.withAlpha (0.35f));
        setColour (juce::TextButton::textColourOffId, Neon::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, Neon::panel);
        setColour (juce::ComboBox::textColourId, Neon::text);
        setColour (juce::PopupMenu::backgroundColourId, Neon::bg1);
        setColour (juce::PopupMenu::textColourId, Neon::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Neon::cyan.withAlpha (0.25f));
        setColour (juce::ScrollBar::thumbColourId, Neon::cyan.withAlpha (0.4f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                 bool down, bool toggled) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        auto base = toggled ? Neon::cyan.withAlpha (0.35f) : Neon::panel;
        if (down) base = base.brighter (0.2f);
        g.setColour (base);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Neon::cyan.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        auto track = juce::Rectangle<float> ((float) x + (float) w * 0.5f - 2.5f,
                                             (float) y + 6.0f, 5.0f, (float) h - 12.0f);
        g.setColour (Neon::bg0);
        g.fillRoundedRectangle (track, 2.5f);
        g.setColour (Neon::cyan.withAlpha (0.75f));
        g.fillRoundedRectangle (track.withTop (pos), 2.5f);
        auto th = juce::Rectangle<float> ((float) x + 3.0f, pos - 5.0f,
                                          (float) w - 6.0f, 10.0f);
        g.setColour (Neon::text);
        g.fillRoundedRectangle (th, 3.0f);
        g.setColour (Neon::cyan);
        g.drawRoundedRectangle (th, 3.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto radius = juce::jmin (w, h) * 0.45f;
        auto centre = juce::Point<float> ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (Neon::panel);
        g.strokePath (p, juce::PathStrokeType (4.0f));
        juce::Path filled;
        filled.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle,
                              startAngle + (endAngle - startAngle) * pos, true);
        g.setColour (Neon::cyan);
        g.strokePath (filled, juce::PathStrokeType (4.0f));
        g.setColour (Neon::text);
        g.fillEllipse (centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }
};

} // namespace fbd
