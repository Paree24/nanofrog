#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "NanoFrogAssets.h"
#include "PastelViews.h"

// NanoFrog interface (v3, after Pastel EFX Grid; see mockup/redesign_v3.png):
// NanoFrog's own near-black panels with its mint and red softened to the
// nearest pastels (red only for key active states: LP24, ARP ON), dot + title
// headers, arc knobs, live views you drag, Lato throughout.
struct NanoColors
{
    static inline const juce::Colour bg      { 0xff101011 };
    static inline const juce::Colour header   { 0xff141415 };
    static inline const juce::Colour panel    { 0xff1a1a1c };
    static inline const juce::Colour border   { 0xff333336 };
    static inline const juce::Colour accent   { 0xff93d9a5 }; // pastel mint: arcs, dots, active states
    static inline const juce::Colour accentHi { 0xffb6e8c2 };
    static inline const juce::Colour red      { 0xffe88a8a }; // pastel red: LP24 / ARP ON active
    static inline const juce::Colour ink      { 0xff151716 }; // text on a filled accent
    static inline const juce::Colour handle   { 0xfff4f1ec };
    static inline const juce::Colour inner    { 0xff151516 };
    static inline const juce::Colour text     { 0xffd8d4c7 };
    static inline const juce::Colour dim      { 0xff969589 };
    static inline const juce::Colour track    { 0xff2a2a2c };
    static inline const juce::Colour comboBg  { 0xff202022 };
    static inline const juce::Colour knobBody { 0xff232326 };
};

// Labels keep the look's 13 px UI font unless given their own font through
// this helper (a bare Label::setFont is overridden by getLabelFont).
inline void setOwnFont (juce::Label& l, const juce::Font& f)
{
    l.getProperties().set ("ownFont", true);
    l.setFont (f);
}

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
        // the shared v3 views
        pastel::Theme::accent = NanoColors::accent;  pastel::Theme::inner = NanoColors::inner;
        pastel::Theme::grid = juce::Colour (0xff222224); pastel::Theme::edge = NanoColors::border;
        pastel::Theme::comboBg = NanoColors::comboBg; pastel::Theme::text = NanoColors::text;
        pastel::Theme::textDim = NanoColors::dim;    pastel::Theme::faint = juce::Colour (0xff5c5b55);
        pastel::Theme::handle = NanoColors::handle;  pastel::Theme::ink = NanoColors::ink;
        pastel::Theme::regular = latoReg;            pastel::Theme::bold = latoBold;
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

    juce::Font getLabelFont (juce::Label& l) override
    {
        if (l.getProperties().contains ("ownFont")) return l.getFont();
        return uiFont (13.0f);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return titleFont (12.5f); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return titleFont (13.5f); }

    // v3 arc knob: neutral track, accent value arc (from zero on bipolar
    // ranges), body + light needle
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& slider) override
    {
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        const float r = juce::jmin (w, h) * 0.5f - 1.5f;
        const float a = start + pos * (end - start);
        const float sw = juce::jmax (2.4f, r * 0.15f), arcR = r - sw * 0.5f;
        const auto stroke = juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path track;
        track.addCentredArc (cx, cy, arcR, arcR, 0.0f, start, end, true);
        g.setColour (NanoColors::border);
        g.strokePath (track, stroke);
        float v0 = start;
        if (slider.getMinimum() < 0.0)
            v0 = start + (end - start) * float ((0.0 - slider.getMinimum())
                                                / (slider.getMaximum() - slider.getMinimum()));
        if (std::abs (a - v0) > 0.01f)
        {
            juce::Path val;
            val.addCentredArc (cx, cy, arcR, arcR, 0.0f, std::min (v0, a), std::max (v0, a), true);
            g.setColour (NanoColors::accent);
            g.strokePath (val, stroke);
        }
        const float br = r - sw - 1.5f;
        g.setColour (NanoColors::knobBody);
        g.fillEllipse (cx - br, cy - br, br * 2, br * 2);
        g.setColour (NanoColors::border);
        g.drawEllipse (cx - br, cy - br, br * 2, br * 2, 1.0f);
        g.setColour (NanoColors::handle);
        g.drawLine (cx + std::sin (a) * br * 0.25f, cy - std::cos (a) * br * 0.25f,
                    cx + std::sin (a) * (br - 2.0f), cy - std::cos (a) * (br - 2.0f), 2.0f);
    }

    // Mock-style flat rectangle toggle: dark when off, red or lime when on.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        bool on = b.getToggleState();
        auto r = b.getLocalBounds().reduced (1).toFloat();
        bool red = (bool) b.getProperties().getWithDefault ("togRed", false);
        g.setColour (on ? (red ? NanoColors::red : NanoColors::accent) : NanoColors::comboBg);
        g.fillRoundedRectangle (r, 5.0f);
        if (! on)
        {
            g.setColour (NanoColors::border);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }
        g.setColour (on ? NanoColors::ink : NanoColors::dim);
        g.setFont (titleFont (12.5f));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, true);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        g.setColour (NanoColors::comboBg);
        g.fillRoundedRectangle (0, 0, (float) w, (float) h, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) ? NanoColors::accent : NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 5.0f, 1.0f);
        juce::Path arrow;
        arrow.startNewSubPath (w - 15.0f, h * 0.5f - 2.0f);
        arrow.lineTo (w - 11.0f, h * 0.5f + 2.0f);
        arrow.lineTo (w - 7.0f, h * 0.5f - 2.0f);
        g.setColour (NanoColors::dim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Mock-style section: dark panel, thin border, lime title text, no bars.
    void drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent&) override
    {
        g.setColour (NanoColors::panel);
        g.fillRoundedRectangle (0, 0, (float) w, (float) h, 8.0f);
        g.setColour (NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 8.0f, 1.0f);
        g.setColour (NanoColors::accent);
        g.fillEllipse (12.4f, 12.4f, 7.2f, 7.2f);
        g.setColour (NanoColors::text);
        g.setFont (logoFont (13.0f));
        g.drawText (text.toUpperCase(), 26, 4, w - 32, 24,
                    juce::Justification::centredLeft, false);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool hover, bool down) override
    {
        float w = (float) b.getWidth(), h = (float) b.getHeight();
        bool active = b.getToggleState();
        bool red = (bool) b.getProperties().getWithDefault ("segRed", false);
        if (active)
        {
            g.setColour (red ? NanoColors::red : NanoColors::accent);
            g.fillRoundedRectangle (0, 0, w, h, 5.0f);
            return;
        }
        g.setColour (down ? juce::Colour (0xff262628) : NanoColors::comboBg);
        g.fillRoundedRectangle (0, 0, w, h, 5.0f);
        g.setColour (hover ? NanoColors::accent.withAlpha (0.6f) : NanoColors::border);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1, h - 1, 5.0f, 1.0f);
    }

    // Text is drawn here only (drawing it in the background too painted it
    // twice and hid any per-button textColourOffId, LESSONS.md #6 again).
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        bool active = b.getToggleState();
        bool red = (bool) b.getProperties().getWithDefault ("segRed", false);
        juce::Colour c = NanoColors::text;
        if (active)          c = NanoColors::ink;
        else if (b.isColourSpecified (juce::TextButton::textColourOffId))
            c = b.findColour (juce::TextButton::textColourOffId);
        g.setColour (b.isEnabled() ? c : c.withAlpha (0.4f));
        g.setFont (titleFont (12.0f));
        g.drawText (b.getButtonText(), 2, 0, b.getWidth() - 4, b.getHeight(),
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
        // pos is the thumb's pixel position, not a 0..1 proportion
        g.setColour (NanoColors::accent);
        g.fillRoundedRectangle (r.getX(), r.getY(),
                                juce::jlimit (0.0f, r.getWidth(), pos - r.getX()),
                                r.getHeight(), 2.0f);
    }

private:
    juce::Typeface::Ptr latoReg, latoBold, latoBlack;
};

