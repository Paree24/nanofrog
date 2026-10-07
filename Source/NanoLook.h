#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "NanoFrogAssets.h"

// NanoFrog interface: mock-faithful flat dark-grey panels, lime accents,
// red highlight states, Lato throughout. Knob sizes follow the mock:
// BIG (mixer/filter/amp/output), MED (oscillators), SMALL (env/lfo/fx/arp/voice).
struct NanoColors
{
    static inline const juce::Colour bg      { 0xff101011 };
    static inline const juce::Colour header   { 0xff141415 };
    static inline const juce::Colour panel    { 0xff1a1a1c };
    static inline const juce::Colour border   { 0xff333336 };
    static inline const juce::Colour accent   { 0xff82d094 }; // mint: arcs, needles, titles
    static inline const juce::Colour accentHi { 0xffa5e3b0 };
    static inline const juce::Colour red      { 0xffd94a4a }; // LP24 / ARP ON active
    static inline const juce::Colour text     { 0xffd8d4c7 };
    static inline const juce::Colour dim      { 0xff969589 };
    static inline const juce::Colour track    { 0xff2a2a2c };
    static inline const juce::Colour comboBg  { 0xff202022 };
    static inline const juce::Colour knobBody { 0xff232326 };
};

struct NanoLook : juce::LookAndFeel_V4
{
    NanoLook()
    {
        latoReg   = juce::Typeface::createSystemTypefaceFor (NanoFrogAssets::LatoRegular_ttf,
                                                             NanoFrogAssets::LatoRegular_ttfSize);
        latoBold  = juce::Typeface::createSystemTypefaceFor (NanoFrogAssets::LatoBold_ttf,
                                                             NanoFrogAssets::LatoBold_ttfSize);
        latoBlack = juce::Typeface::createSystemTypefaceFor (NanoFrogAssets::LatoBlack_ttf,
                                                             NanoFrogAssets::LatoBlack_ttfSize);
        setColour (juce::ComboBox::textColourId, NanoColors::text);
        setColour (juce::PopupMenu::backgroundColourId, NanoColors::comboBg);
        setColour (juce::PopupMenu::textColourId, NanoColors::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff222a24));
        setColour (juce::PopupMenu::highlightedTextColourId, NanoColors::accent);
    }

    juce::Font uiFont (float h) const
    {
        if (latoReg != nullptr) return juce::Font (latoReg).withHeight (h);
        return juce::Font (juce::FontOptions (h));
    }
    juce::Font titleFont (float h) const
    {
        if (latoBold != nullptr) return juce::Font (latoBold).withHeight (h);
        return juce::Font (juce::FontOptions (h, juce::Font::bold));
    }
    juce::Font logoFont (float h) const
    {
        if (latoBlack != nullptr) return juce::Font (latoBlack).withHeight (h);
        return titleFont (h);
    }

    juce::Font getLabelFont (juce::Label&) override { return uiFont (13.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (14.0f); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return titleFont (13.5f); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& slider) override
    {
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        float r = juce::jmin (w, h) * 0.5f - 1.0f;
        float a = start + pos * (end - start);

        float br = (w <= 36 || h <= 36) ? r - 2.0f : r - 5.0f;
        g.setColour (NanoColors::knobBody);
        g.fillEllipse (cx - br, cy - br, br * 2, br * 2);
        g.setColour (NanoColors::border);
        g.drawEllipse (cx - br + 0.5f, cy - br + 0.5f, br * 2 - 1, br * 2 - 1, 1.0f);

        float arcR = r - 1.5f;
        g.setColour (NanoColors::track);
        juce::Path track;
        track.addArc (cx - arcR, cy - arcR, arcR * 2, arcR * 2, start, end, true);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::butt));
        float v0 = start;
        if (slider.getMinimum() < 0.0)
            v0 = start + (end - start) * float ((0.0 - slider.getMinimum())
                                                / (slider.getMaximum() - slider.getMinimum()));
        if (std::abs (a - v0) > 0.001f)
        {
            g.setColour (NanoColors::accent);
            juce::Path val;
            val.addArc (cx - arcR, cy - arcR, arcR * 2, arcR * 2,
                        std::min (v0, a), std::max (v0, a), true);
            g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::butt));
        }
        g.setColour (NanoColors::dim.withAlpha (0.6f));
        for (float ta : { start, end })
        {
            float tx = cx + std::cos (ta - 1.5707963f) * (arcR + 2.5f);
            float ty = cy + std::sin (ta - 1.5707963f) * (arcR + 2.5f);
            g.fillEllipse (tx - 1.0f, ty - 1.0f, 2.0f, 2.0f);
        }
        float dx = std::cos (a - 1.5707963f), dy = std::sin (a - 1.5707963f);
        g.setColour (NanoColors::accent);
        g.drawLine (cx + dx * 2.0f, cy + dy * 2.0f,
                    cx + dx * (br - 2.0f), cy + dy * (br - 2.0f), 2.5f);
    }

    // Mock-style flat rectangle toggle: dark when off, red or lime when on.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        bool on = b.getToggleState();
        auto r = b.getLocalBounds().reduced (1).toFloat();
        bool red = (bool) b.getProperties().getWithDefault ("togRed", false);
        if (on && red)       g.setColour (NanoColors::red);
        else if (on)         g.setColour (NanoColors::accent.withAlpha (0.28f));
        else                 g.setColour (NanoColors::comboBg);
        g.fillRoundedRectangle (r, 3.0f);
        if (on && red)       g.setColour (NanoColors::red);
        else if (on)         g.setColour (NanoColors::accent);
        else                 g.setColour (NanoColors::border);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (on && red ? juce::Colour (0xff101010)
                               : on ? NanoColors::accent : NanoColors::dim);
        g.setFont (titleFont (12.5f));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, true);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        g.setColour (NanoColors::comboBg);
        g.fillRoundedRectangle (0, 0, w, h, 3.0f);
        g.setColour (box.hasKeyboardFocus (true) ? NanoColors::accent : NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1, h - 1, 3.0f, 1.0f);
        juce::Path arrow;
        arrow.addTriangle (w - 16.0f, h * 0.5f - 3.0f, w - 8.0f, h * 0.5f - 3.0f,
                           w - 12.0f, h * 0.5f + 3.0f);
        g.setColour (NanoColors::dim);
        g.fillPath (arrow);
    }

    // Mock-style section: dark panel, thin border, lime title text, no bars.
    void drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent&) override
    {
        g.setColour (NanoColors::panel);
        g.fillRoundedRectangle (0, 0, w, h, 4.0f);
        g.setColour (NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1, h - 1, 4.0f, 1.0f);
        g.setColour (NanoColors::accent);
        g.setFont (titleFont (17.0f));
        g.drawText (text.toUpperCase(), 0, 4, w, 20,
                    juce::Justification::centred, false);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool hover, bool down) override
    {
        float w = (float) b.getWidth(), h = (float) b.getHeight();
        bool active = b.getToggleState();
        bool red = (bool) b.getProperties().getWithDefault ("segRed", false);
        if (active && red)                  g.setColour (NanoColors::red);
        else if (active)                    g.setColour (NanoColors::accent.withAlpha (0.30f));
        else                                g.setColour (down ? juce::Colour (0xff262628) : NanoColors::comboBg);
        g.fillRoundedRectangle (0, 0, w, h, 3.0f);
        if (active && red)       g.setColour (NanoColors::red);
        else if (active)         g.setColour (NanoColors::accent);
        else                     g.setColour (hover ? NanoColors::accent.withAlpha (0.6f) : NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1, h - 1, 3.0f, 1.0f);
        g.setColour (active && red ? juce::Colour (0xff101010)
                                  : active ? NanoColors::accent : NanoColors::text);
        g.setFont (titleFont (13.0f));
        g.drawText (b.getButtonText(), 2, 0, (int) w - 4, (int) h,
                    juce::Justification::centred, true);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.setColour (NanoColors::panel);
        g.fillRoundedRectangle (0, 0, w, h, 4.0f);
        g.setColour (NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1, h - 1, 4.0f, 1.0f);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text,
                            const juce::String&, const juce::Drawable*, const juce::Colour*) override
    {
        if (isSeparator)
        {
            g.setColour (NanoColors::track);
            g.fillRect (area.getX() + 8, area.getCentreY(), area.getWidth() - 16, 1);
            return;
        }
        if (isHighlighted && isActive)
        {
            g.setColour (NanoColors::accent.withAlpha (0.22f));
            g.fillRoundedRectangle (area.toFloat(), 3.0f);
        }
        g.setColour (! isActive ? NanoColors::dim.withAlpha (0.5f)
                                : isHighlighted ? NanoColors::accentHi : NanoColors::text);
        g.setFont (uiFont (14.0f));
        g.drawText (text, area.getX() + 12, area.getY(), area.getWidth() - 24, area.getHeight(),
                    juce::Justification::centredLeft, true);
        if (isTicked)
        {
            g.setColour (NanoColors::accent);
            float s = 6.0f, cx = (float) area.getX() + 5.0f, cy = (float) area.getCentreY();
            juce::Path tick;
            tick.startNewSubPath (cx - s * 0.4f, cy);
            tick.lineTo (cx, cy + s * 0.4f);
            tick.lineTo (cx + s * 0.6f, cy - s * 0.5f);
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered,
                                                      juce::PathStrokeType::rounded));
        }
        juce::ignoreUnused (hasSubMenu);
    }

    void getIdealPopupMenuItemSize (const juce::String&, bool, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override
    {
        idealWidth = 240;
        idealHeight = juce::jmax (24, standardMenuItemHeight);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float, float, const juce::Slider::SliderStyle,
                           juce::Slider&) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        g.setColour (NanoColors::track);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (NanoColors::accent);
        g.fillRoundedRectangle (r.getX(), r.getY(), r.getWidth() * pos, r.getHeight(), 2.0f);
    }

private:
    juce::Typeface::Ptr latoReg, latoBold, latoBlack;
};

