#include "PluginEditor.h"
#include "PluginProcessor.h"

// ---------- value formatting (mock-style readouts under every knob) ----------
static juce::String fmtVal (const juce::String& pidIn, float v)
{
    juce::String pid = pidIn;
    if (pid.startsWith ("t2_")) pid = pid.substring (3); // timbre-2 shares formatting
    auto pct = [] (float x) { return juce::String ((int) std::round (x * 100.0f)); };
    auto pctS = [] (float x)
    {
        int p = (int) std::round (x * 100.0f);
        return (p > 0 ? juce::String ("+") : juce::String()) + juce::String (p);
    };
    auto secs = [] (float x, float maxS)
    {
        float t = 0.002f + x * x * maxS;
        return t < 1.0f ? juce::String ((int) std::round (t * 1000.0f)) + " ms"
                        : juce::String (t, 2) + " s";
    };
    if (pid == "filter_cutoff")
    {
        float hz = 40.0f * std::pow (450.0f, v);
        return hz < 1000.0f ? juce::String ((int) std::round (hz)) + " Hz"
                            : juce::String (hz / 1000.0f, 2) + " kHz";
    }
    if (pid == "output_level")
        return juce::String (20.0f * std::log10 (std::max (v, 0.0001f)), 1) + " dB";
    if (pid == "eq_low" || pid == "eq_high")
        return juce::String ((int) std::round (v)) + " dB";
    if (pid == "lfo1_rate" || pid == "lfo2_rate")
        return juce::String (0.05 * std::pow (600.0, v), 2) + " Hz";
    if (pid == "modfx_rate")
        return juce::String (0.05f + v * v * 2.5f, 2) + " Hz"; // matches ModFx
    if (pid == "env1_a" || pid == "env2_a") return secs (v, 4.0f);
    if (pid == "env1_d" || pid == "env2_d" || pid == "env1_r" || pid == "env2_r")
        return secs (v, 8.0f);
    if (pid == "delay_time")
        return juce::String ((int) std::round (20.0f + v * v * 880.0f)) + " ms";
    if (pid == "amp_pan")
    {
        if (std::abs (v) < 0.02f) return "C";
        int p = (int) std::round (v * 100.0f);
        return (p > 0 ? juce::String ("R") : juce::String ("L")) + juce::String (std::abs (p));
    }
    if (pid == "osc1_octave" || pid == "osc1_pitch" || pid == "osc2_octave" || pid == "osc2_pitch"
        || pid == "osc1_fine" || pid == "osc2_fine" || pid == "master_tune"
        || pid == "voice_poly" || pid == "arp_octaves" || pid == "song_tempo")
        return juce::String ((int) std::round (v));
    if (pid == "voice_unidet") return juce::String ((int) std::round (v)) + " ct";
    if (pid == "bend_range") return juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) std::round (v)) + " st";
    if (pid == "filter_keytrack" || pid == "filter_envamt" || pid == "arp_swing"
        || pid == "mod1_amt" || pid == "mod2_amt" || pid == "mod3_amt" || pid == "mod4_amt")
        return pctS (v);
    if (pid == "arp_gate") return pct (v);
    return pct (v);
}

// the filter view draws in Hz: NanoFrog's cutoff is 0..1 -> 40 Hz .. 18 kHz
static pastel::FilterView* nanoCutoff (pastel::FilterView* fv)
{
    fv->setHzMapping ([] (float v) { return 40.0f * std::pow (450.0f, v); },
                      [] (float hz) { return std::log (hz / 40.0f) / std::log (450.0f); });
    return fv;
}

// ---------- segmented radio strip ----------
SegStrip::SegStrip (NanoFrogProcessor& p, const juce::String& id,
                    const juce::StringArray& labels, bool redActive, int cols)
    : proc (p), pid (id)
{
    juce::ignoreUnused (cols);
    if (redActive) getProperties().set ("segRed", true);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = new juce::TextButton (labels[i]);
        if (redActive) b->getProperties().set ("segRed", true);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x5e9 + (pid.hashCode() & 0xff), juce::dontSendNotification);
        b->onClick = [this, i]
        {
            if (auto* par = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid)))
            {
                par->beginChangeGesture();
                par->setValueNotifyingHost ((float) i / (float) juce::jmax (1, (int) btns.size() - 1));
                par->endChangeGesture();
                sync();
            }
        };
        btns.push_back (b);
        addAndMakeVisible (b);
        juce::ignoreUnused (cols);
    }
    sync();
}

void SegStrip::sync()
{
    float idx = 0.0f;
    if (auto* raw = proc.apvts.getRawParameterValue (pid)) idx = raw->load();
    for (int i = 0; i < (int) btns.size(); ++i)
        btns[i]->setToggleState (i == (int) std::round (idx), juce::dontSendNotification);
}

static void layoutSeg (SegStrip* s, int x, int y, int bw, int bh, int cols, int gap = 6)
{
    s->setBounds (x, y, cols * bw + (cols - 1) * gap,
                  ((int) s->btns.size() + cols - 1) / cols * bh
                  + (((int) s->btns.size() + cols - 1) / cols - 1) * gap);
    for (int i = 0; i < (int) s->btns.size(); ++i)
        s->btns[i]->setBounds ((i % cols) * (bw + gap), (i / cols) * (bh + gap), bw, bh);
}

// ---------- oscilloscope view ----------
struct ScopeView : juce::Component
{
    ScopeView() { setInterceptsMouseClicks (false, false); }
    float ring[2048] = {};
    int wpos = 0, filled = 0;
    float peakDb = -60.0f;

    void pushBlock (const float* d, int n)
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            ring[wpos] = d[i];
            wpos = (wpos + 1) & 2047;
            pk = juce::jmax (pk, std::abs (d[i]));
        }
        filled = juce::jmin (2048, filled + n);
        if (pk > 0.0001f) peakDb = 20.0f * std::log10 (pk);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0c0c0d));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (NanoColors::border);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colour (0xff222224));
        g.drawLine (r.getX() + 4, r.getCentreY(), r.getRight() - 4, r.getCentreY(), 1.0f);
        g.drawLine (r.getX() + 4, r.getY() + r.getHeight() * 0.25f, r.getRight() - 4,
                    r.getY() + r.getHeight() * 0.25f, 1.0f);
        g.drawLine (r.getX() + 4, r.getY() + r.getHeight() * 0.75f, r.getRight() - 4,
                    r.getY() + r.getHeight() * 0.75f, 1.0f);
        int n = juce::jmin (filled, 1024);
        if (n < 32) return;
        float window[1024];
        for (int i = 0; i < n; ++i) window[i] = ring[(wpos - n + i + 4096) & 2047];
        int start = 0;
        for (int i = 0; i < n / 2; ++i)
            if (window[i] <= 0.0f && window[i + 1] > 0.0f) { start = i; break; }
        int len = juce::jmin (512, n - start);
        float pw = r.getWidth() - 8.0f;
        juce::Path trace;
        for (int i = 0; i < len; ++i)
        {
            float x = r.getX() + 4.0f + pw * i / (len - 1);
            float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, window[start + i])
                                        * (r.getHeight() * 0.44f);
            if (i == 0) trace.startNewSubPath (x, y);
            else trace.lineTo (x, y);
        }
        g.setColour (NanoColors::accent);
        g.strokePath (trace, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (NanoColors::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("OUT " + juce::String (peakDb, 1) + " dB",
                    r.getX() + 8, r.getY() + 4, 140, 12, juce::Justification::left);
    }
};

// ---------- output meter view (mock-style L/R bars) ----------
struct MeterView : juce::Component
{
    NanoFrogProcessor& proc;
    float holdL = 0.0f, holdR = 0.0f;
    explicit MeterView (NanoFrogProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        float lv[2] = { proc.getOutputLevelL(), proc.getOutputLevelR() };
        const char* tags[2] = { "L", "R" };
        for (int ch = 0; ch < 2; ++ch)
        {
            float y = r.getY() + ch * (r.getHeight() * 0.5f);
            float bh = r.getHeight() * 0.5f - 6.0f;
            float v = juce::jlimit (0.0f, 1.2f, lv[ch]);
            float pk = ch == 0 ? (holdL = juce::jmax (v, holdL * 0.985f))
                               : (holdR = juce::jmax (v, holdR * 0.985f));
            g.setColour (NanoColors::dim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (tags[ch], r.getX(), y, 14, bh, juce::Justification::centredLeft);
            float bx = r.getX() + 18, bw = r.getWidth() - 18;
            g.setColour (juce::Colour (0xff222224));
            g.fillRoundedRectangle (bx, y + 2, bw, bh - 4, 2.0f);
            float fw = bw * juce::jmin (1.0f, v);
            bool hot = v > 0.95f;
            g.setColour (hot ? NanoColors::red : NanoColors::accent);
            g.fillRoundedRectangle (bx, y + 2, fw, bh - 4, 2.0f);
            float px = bx + bw * juce::jmin (1.0f, pk);
            g.setColour (hot ? NanoColors::red : NanoColors::accentHi);
            g.fillRect (px - 1.0f, y + 2, 2.0f, bh - 4);
        }
    }
};

// ================= editor content (fixed layout, scaled by host) =================
FrogContent::FrogContent (NanoFrogProcessor& p)
    : proc (p), midiBtn (p.midiLearn)
{
    setLookAndFeel (&look);
    pastel::Theme::format = fmtVal;   // the views' readouts read like the knobs
    formatManager.registerBasicFormats();

    addAndMakeVisible (presetLabel);
    presetLabel.setText ("NANOFROG", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetLabel.setColour (juce::Label::textColourId, NanoColors::text);

    addAndMakeVisible (prevBtn);
    prevBtn.onClick = [&]
    {
        int n = proc.getNumPrograms();
        int i = (proc.getCurrentProgram() + n - 1) % n;
        proc.setCurrentProgram (i);
        presetBox.setSelectedId (i + 1, juce::dontSendNotification);
    };
    addAndMakeVisible (nextBtn);
    nextBtn.onClick = [&]
    {
        int n = proc.getNumPrograms();
        int i = (proc.getCurrentProgram() + 1) % n;
        proc.setCurrentProgram (i);
        presetBox.setSelectedId (i + 1, juce::dontSendNotification);
    };
    addAndMakeVisible (presetBox);
    presetBox.setJustificationType (juce::Justification::centred);
    {
        auto names = proc.getFactoryNames();
        for (int i = 0; i < names.size(); ++i)
            presetBox.addItem (names[i], i + 1);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    }
    presetBox.onChange = [&]
    {
        int i = presetBox.getSelectedItemIndex();
        if (i >= 0 && i != proc.getCurrentProgram())
            proc.setCurrentProgram (i);
    };
    addAndMakeVisible (browseBtn);
    browseBtn.setTooltip ("Open preset browser (search + tags)");
    browseBtn.onClick = [&] { openBrowser(); };
    addAndMakeVisible (mutateBtn);
    mutateBtn.setTooltip ("Slightly re-rolls the sound (envelopes frozen for bass/keys/leads/percussive)");
    mutateBtn.onClick = [&] { proc.mutateCurrentPatch(); };
    addAndMakeVisible (midiBtn); // learning = toggled, which NanoLook fills with the accent
    midiBtn.setColour (juce::TextButton::buttonOnColourId, NanoColors::accent);
    // Browser overlay last: paints above everything while open.
    browser = std::make_unique<PresetBrowser> (proc);
    browser->setVisible (false);
    browser->onClose = [&] { closeBrowser(); };
    browser->onPresetChanged = [&] { refreshPresetBox(); };
    addChildComponent (browser.get());
    juce::TextButton* tabs[2] = { &tabT1, &tabT2 };
    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (tabs[i]);
        tabs[i]->setBounds (16 + i * 176, 54, 170, 28);   // folder tabs over the timbre panel
        tabs[i]->setClickingTogglesState (true);
        tabs[i]->setRadioGroupId (0x7418, juce::dontSendNotification);
        tabs[i]->onClick = [this, i] { selectPage (i); };
    }
    addToggle (this, "limiter", "LIMITER", 1090, 11, 84, 24);
    for (auto* b : { &undoBtn, &redoBtn, &locksBtn }) addAndMakeVisible (b);
    undoBtn.setBounds (898, 11, 58, 24);
    redoBtn.setBounds (960, 11, 58, 24);
    locksBtn.setBounds (1022, 11, 62, 24);
    undoBtn.setTooltip ("Undo the last change (Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Ctrl+Shift+Z)");
    locksBtn.setTooltip ("Sections MUTATE leaves alone");
    undoBtn.onClick = [&] { proc.undoEdit(); refreshPresetBox(); };
    redoBtn.onClick = [&] { proc.redoEdit(); refreshPresetBox(); };
    locksBtn.onClick = [&]
    {
        juce::PopupMenu m; // ticked = locked
        m.addSectionHeader ("MUTATE leaves alone");
        const auto names = NanoFrogProcessor::mutateGroupNames();
        for (int g = 0; g < NanoFrogProcessor::MG_COUNT; ++g) m.addItem (g + 1, names[g], true, proc.isMutateLocked (g));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&locksBtn),
                         [this] (int id) { if (id > 0) proc.setMutateLocked (id - 1, ! proc.isMutateLocked (id - 1)); });
    };

    page0.setBounds (0, 0, 1280, 900);
    page1.setBounds (0, 0, 1280, 900);
    // NOTE: pages must NOT swallow clicks (second arg true) or every
    // knob/button inside them goes mouse-dead while staying visible.
    page0.setInterceptsMouseClicks (false, true);
    page1.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (page0);
    addAndMakeVisible (page1);
    // ---- header: single T1/T2 crossfade knob, top right ----
    {
        auto* t1 = new juce::Label ("T1", "T1");
        t1->setJustificationType (juce::Justification::centred);
        t1->setColour (juce::Label::textColourId, NanoColors::dim);
        setOwnFont (*t1, look.uiFont (12.5f));
        t1->setBounds (1190, 12, 24, 16);
        addAndMakeVisible (t1); owned.emplace_back (t1);
        auto* t2 = new juce::Label ("T2", "T2");
        t2->setJustificationType (juce::Justification::centred);
        t2->setColour (juce::Label::textColourId, NanoColors::dim);
        setOwnFont (*t2, look.uiFont (12.5f));
        t2->setBounds (1244, 12, 24, 16);
        addAndMakeVisible (t2); owned.emplace_back (t2);
        auto* cap = new juce::Label ("MIX", "MIX");
        cap->setJustificationType (juce::Justification::centred);
        cap->setColour (juce::Label::textColourId, NanoColors::accent);
        setOwnFont (*cap, look.titleFont (11.0f));
        cap->setBounds (1200, 33, 60, 12);
        addAndMakeVisible (cap); owned.emplace_back (cap);
    }
    mixSlider = new juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    mixSlider->setBounds (1216, 5, 28, 28);
    mixSlider->setTooltip ("Timbre mix — left: Timbre 1, right: Timbre 2");
    addAndMakeVisible (mixSlider); owned.emplace_back (mixSlider);
    sAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, "tmix", *mixSlider));
    // ================= the two timbre pages: one layout, t2_ ids on page 1 =================
    // (v3, after mockup/redesign_v3.png: oscillators / mixer / filter / amp on top,
    //  envelopes / LFOs / matrix / voice under them, the shared row at the bottom)
    using pastel::Wave;
    auto makeTimbrePage = [this] (int t)
    {
        buildParent = t == 0 ? &page0 : &page1;
        buildTimbre = t;
        const juce::String pre = t == 0 ? "" : "t2_";
        auto caption = [this] (juce::Component* parent, const juce::String& text, juce::Rectangle<int> b)
        {
            auto* cap = new juce::Label (text, text);
            cap->setColour (juce::Label::textColourId, NanoColors::dim);
            setOwnFont (*cap, look.titleFont (10.0f));
            cap->setJustificationType (juce::Justification::centred);
            parent->addAndMakeVisible (cap); owned.emplace_back (cap);
            cap->setBounds (b);
        };

        // ---- row 1: OSC 1, OSC 2, MIXER, FILTER, AMP ----
        for (int o = 1; o <= 2; ++o)
        {
            const juce::String id = pre + "osc" + juce::String (o) + "_";
            auto* s = addSection ("OSC " + juce::String (o), o == 1 ? 26 : 334, 92, 300, 226);
            addView<pastel::WaveView> (s, { 12, 34, 276, 56 }, proc.apvts, id + "wave", id + "shape",
                                       std::vector<Wave> { Wave::saw, Wave::pulse, Wave::tri, Wave::sine, Wave::digital, Wave::noise, Wave::digital })
                ->setTooltip ("Drag sideways on the square wave to set its width (Shape)");
            addView<pastel::SegChoice> (s, { 12, 96, 276, 22 }, proc.apvts, id + "wave",
                                        juce::StringArray { "", "", "", "", "DIG", "NOISE", "USER" },
                                        std::vector<Wave> { Wave::saw, Wave::square, Wave::tri, Wave::sine, Wave::none, Wave::none, Wave::none });
            addChoice (s, id + "digital", "", 12, 124, 112)->setTooltip ("Digital wave (with DIG selected)");
            juce::TextButton& ib = o == 1 ? (t == 0 ? importBtn : importBtn2) : impBtnO2[t];
            juce::TextButton& fb = o == 1 ? (t == 0 ? folderBtn : folderBtn2) : fldBtnO2[t];
            ib.setButtonText ("IMPORT");
            fb.setButtonText ("FOLDER");
            ib.setTooltip ("Import a single-cycle WAV as the USER wave");
            fb.setTooltip ("Import a folder of single-cycle WAVs");
            ib.onClick = [this, t, o] { importWaveFile (t, o - 1); };
            fb.onClick = [this, t, o] { importWaveFolder (t, o - 1); };
            s->addAndMakeVisible (ib); ib.setBounds (130, 124, 76, 22);
            s->addAndMakeVisible (fb); fb.setBounds (212, 124, 76, 22);
            if (o == 1)
            {
                juce::Label& wl = t == 0 ? waveNameLabel : waveNameLabel2;
                wl.setText (proc.hasUserWave() ? proc.getUserWaveName() : "No user wave", juce::dontSendNotification);
                wl.setColour (juce::Label::textColourId, NanoColors::dim);
                s->addAndMakeVisible (wl);
                wl.setJustificationType (juce::Justification::centredLeft);
                setOwnFont (wl, look.uiFont (10.5f));
                wl.setBounds (12, 148, 276, 14);
            }
            else
            {
                initWaveLabel (s, waveNameO2[t]);
                waveNameO2[t].setJustificationType (juce::Justification::centredLeft);
                waveNameO2[t].setBounds (12, 148, 276, 14);
                // the oscillator-2 modes (sync / ring) as one segmented switch
                addView<pastel::SegChoice> (s, { 120, 6, 168, 20 }, proc.apvts, pre + "osc_mod",
                                            juce::StringArray { "OFF", "RING", "SYNC", "R+S" })
                    ->setTooltip ("Oscillator 2: hard sync to oscillator 1, ring modulation, or both");
            }
            const char* ks[7] = { "octave", "pitch", "fine", "level", "shape", "pwm", "xmod" };
            const char* kl[7] = { "Oct", "Pitch", "Fine", "Level", "Shape", "PWM", "X-Mod" };
            for (int k = 0; k < 7; ++k)
                addKnob (s, id + ks[k], kl[k], -4 + k * 41, 164, 26, 40);
        }
        auto* mixer = addSection ("MIXER", 642, 92, 150, 226);
        addKnob (mixer, pre + "mix_o1", "Osc 1", 12, 36, 44);
        addKnob (mixer, pre + "mix_o2", "Osc 2", 82, 36, 44);
        addKnob (mixer, pre + "mix_noise", "Noise", 12, 132, 44);
        addKnob (mixer, pre + "fm_amt", "FM 2>1", 82, 132, 44);

        auto* filt = addSection ("FILTER", 800, 92, 250, 226);
        nanoCutoff (addView<pastel::FilterView> (filt, { 12, 34, 226, 84 }, proc.apvts, pre + "filter_cutoff", pre + "filter_reso",
                                                 pre + "filter_type", juce::String(), juce::String(),
                                                 std::vector<pastel::FilterView::Mode> { pastel::FilterView::Mode::lp24, pastel::FilterView::Mode::lp12,
                                                                                         pastel::FilterView::Mode::bp12, pastel::FilterView::Mode::hp12 }))
            ->setTooltip ("Drag: cutoff sideways, resonance up / down");
        addSeg (filt, pre + "filter_type", { "LP24", "LP12", "BP12", "HP12" }, true, 12, 124, 54, 22, 4);
        addKnob (filt, pre + "filter_cutoff", "Cutoff", 2, 154, 30, 52);
        addKnob (filt, pre + "filter_reso", "Reso", 62, 154, 30, 52);
        addKnob (filt, pre + "filter_keytrack", "KeyTrk", 122, 154, 30, 52);
        addKnob (filt, pre + "filter_envamt", "EnvAmt", 182, 154, 30, 52);

        auto* amp = addSection ("AMP", 1058, 92, 196, 226);
        addKnob (amp, pre + "amp_level", "Level", 8, 34, 56);
        addKnob (amp, pre + "amp_pan", "Pan", 96, 34, 30, 48);
        addKnob (amp, pre + "amp_velocity", "Veloc", 96, 100, 30, 48);
        addKnob (amp, pre + "amp_drive", "Drive", 8, 140, 30, 48);
        addToggle (amp, pre + "amp_dist", "DIST", 100, 172, 60, 22)->setTooltip ("Distortion on / off (Drive sets the amount)");

        // ---- row 2: ENV 1, ENV 2, LFO 1, LFO 2, MOD MATRIX, VOICE ----
        const char* envTitle[2] = { "ENV 1  FILTER", "ENV 2  AMP" };
        for (int n = 1; n <= 2; ++n)
        {
            const juce::String id = pre + "env" + juce::String (n) + "_";
            auto* e = addSection (envTitle[n - 1], n == 1 ? 26 : 234, 326, 200, 238);
            auto* ev = addView<pastel::EnvView> (e, { 10, 32, 180, 118 }, proc.apvts,
                                                 juce::StringArray { id + "a", id + "d", id + "s", id + "r" }, juce::String());
            ev->setShowValues (false);   // the knobs underneath carry the readouts
            ev->setTooltip ("Drag the points: attack, decay / sustain, release (double-click = defaults)");
            const char* kl[4] = { "A", "D", "S", "R" };
            const char* ks[4] = { "a", "d", "s", "r" };
            for (int k = 0; k < 4; ++k) addKnob (e, id + ks[k], kl[k], k * 46, 156, 28, 44);
        }
        for (int n = 1; n <= 2; ++n)
        {
            const juce::String id = pre + "lfo" + juce::String (n) + "_";
            auto* l = addSection ("LFO " + juce::String (n), n == 1 ? 442 : 640, 326, 190, 238);
            addView<pastel::LfoView> (l, { 10, 32, 170, 46 }, proc.apvts, id + "wave", juce::String(),
                                      std::vector<Wave> { Wave::sine, Wave::tri, Wave::square, Wave::saw, Wave::snh });
            addSeg (l, id + "wave", { "SIN", "TRI", "SQR", "SAW", "RND" }, false, 10, 84, 30, 22, 5);
            addKnob (l, id + "rate", "Rate", 22, 108, 32);
            addKnob (l, id + "depth", "Depth", 106, 108, 32);
            addChoice (l, id + "sync", "", 8, 186, 84)->setTooltip ("Tempo sync: a note value replaces the Rate knob");
            addChoice (l, id + "keysync", "", 98, 186, 84)->setTooltip ("Key sync. Off: free-running; Timbre: restarts on a new phrase, later notes follow; Voice: restarts with every note");
            caption (l, "SYNC", { 8, 170, 84, 14 });
            caption (l, "KEY SYNC", { 98, 170, 84, 14 });
        }
        auto* mm = addSection ("MOD MATRIX", 838, 326, 250, 238);
        caption (mm, "SOURCE", { 8, 30, 84, 14 });
        caption (mm, "DESTINATION", { 96, 30, 104, 14 });
        caption (mm, "AMT", { 204, 30, 40, 14 });
        for (int i = 1; i <= 4; ++i)
        {
            const int y = 50 + (i - 1) * 44;
            const juce::String id = pre + "mod" + juce::String (i) + "_";
            addChoice (mm, id + "src", "", 8, y, 84);
            addChoice (mm, id + "dst", "", 96, y, 104);
            addKnob (mm, id + "amt", "", 200, y - 6, 28);
        }
        auto* vo = addSection ("VOICE", 1096, 326, 158, 238);
        addChoice (vo, pre + "voice_assign", "", 8, 32, 142)->setTooltip ("Poly; Mono: one voice with note priority; Unison: four stacked, detuned voices");
        addChoice (vo, pre + "voice_trigger", "", 8, 60, 68)->setTooltip ("Mono / Unison: Single = overlapping keys slide (legato), Multi = every key restarts the envelopes");
        addChoice (vo, pre + "voice_priority", "", 82, 60, 68)->setTooltip ("Mono / Unison: which held key sounds");
        addKnob (vo, pre + "voice_poly", "Poly", -2, 88, 26, 46);
        addKnob (vo, pre + "voice_detune", "Detune", 48, 88, 26, 46);
        addKnob (vo, pre + "voice_unidet", "Uni Det", 98, 88, 26, 46)->setTooltip ("Unison detune width in cents");
        addKnob (vo, pre + "voice_portamento", "Porta", 23, 160, 26, 46);
        addKnob (vo, pre + "voice_vib", "Vib", 73, 160, 26, 46);
    };
    makeTimbrePage (0);
    makeTimbrePage (1);

    // ================= shared by both timbres =================
    buildParent = this;
    auto* ar = addSection ("ARPEGGIATOR", 16, 596, 330, 294);
    addToggle (ar, "arp_on", "ON", 12, 34, 56, 24);
    static_cast<juce::ToggleButton*> (owned.back().get())->getProperties().set ("togRed", true);
    addToggle (ar, "arp_latch", "LATCH", 72, 34, 64, 24);
    {
        auto* arpOff = new juce::TextButton ("OFF");
        arpOffBtn = arpOff;
        ar->addAndMakeVisible (arpOff); owned.emplace_back (arpOff);
        arpOff->setBounds (140, 34, 52, 24);
        arpOff->onClick = [&]
        {
            for (auto* pid : { "arp_on", "arp_latch" })
                if (auto* prm = proc.apvts.getParameter (pid))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (0.0f);
                    prm->endChangeGesture();
                }
        };
    }
    addChoice (ar, "arp_rate", "", 200, 34, 118)->setTooltip ("Arp rate");
    // STEPS: eight cells you click on and off (an off step is a rest) + LAST STEP
    for (int k = 1; k <= 8; ++k)
        addToggle (ar, "arp_step" + juce::String (k), juce::String (k), 12 + (k - 1) * 38, 68, 34, 46)
            ->setTooltip ("Step " + juce::String (k) + ": on / rest");
    {
        auto* lc = new juce::Label ("LAST", "LAST");
        lc->setColour (juce::Label::textColourId, NanoColors::dim);
        setOwnFont (*lc, look.titleFont (10.0f));
        ar->addAndMakeVisible (lc); owned.emplace_back (lc); lc->setBounds (12, 122, 40, 18);
        auto* last = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        last->setTextBoxStyle (juce::Slider::TextBoxRight, true, 26, 18);
        last->setTooltip ("Last step: the pattern of on/off steps repeats after this many");
        ar->addAndMakeVisible (last); owned.emplace_back (last);
        last->setBounds (52, 122, 266, 18);
        sAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "arp_last_step", *last));
        last->setRange (1.0, 8.0, 1.0);
        last->setNumDecimalPlacesToDisplay (0);
    }
    addChoice (ar, "arp_mode", "", 12, 148, 150)->setTooltip ("Arp mode");
    addKnob (ar, "arp_gate", "Gate", 12, 184, 34);
    addKnob (ar, "arp_octaves", "Oct", 90, 184, 34);
    addKnob (ar, "arp_swing", "Swing", 168, 184, 34);
    addKnob (ar, "song_tempo", "Tempo", 246, 184, 34);

    // FX: a 3 x 2 grid of half-height cards in signal order (left to right,
    // top then bottom). DRIVE, SATURATION and REVERB extend the classic VA chain.
    // Each card: picture (drag) on the left, ON / mix on the right, the mode
    // strip along the bottom; the knobs the pictures replace are gone, their
    // values show in the picture readouts.
    using FS = pastel::FxView::Style;
    constexpr int cw = 222, ch = 143, gx = 354, gy = 596;
    auto cardAt = [&] (const juce::String& title, int col, int row)
    {
        return addSection (title, gx + col * (cw + 8), gy + row * (ch + 8), cw, ch);
    };
    const juce::Rectangle<int> pic { 10, 30, 140, 84 }, picTall { 10, 30, 140, 104 },
                               seg { 10, 118, 202, 18 };
    auto onSwitch = [&] (juce::Component* c, const juce::String& pid)
    {
        addToggle (c, pid, "ON", 158, 30, 54, 22);
    };

    auto* drv = cardAt ("DRIVE", 0, 0);
    onSwitch (drv, "drive_on");
    auto* drvView = addView<pastel::DriveView> (drv, picTall, proc.apvts, "drive_amount");
    drvView->setTooltip ("Drag up / down: drive");
    driveViews[0] = drvView;
    addKnob (drv, "drive_mix", "Mix", 156, 62, 30);

    auto* sat = cardAt ("SATURATION", 1, 0);
    onSwitch (sat, "sat_on");
    auto* satView = addView<pastel::DriveView> (sat, pic, proc.apvts, "sat_amount", "sat_mode");
    satView->setTooltip ("Drag up / down: amount");
    driveViews[1] = satView;
    addView<pastel::SegChoice> (sat, seg, proc.apvts, "sat_mode", juce::StringArray { "TAPE", "TUBE" });
    addKnob (sat, "sat_tone", "Tone", 156, 56, 30);

    auto* mfx = cardAt ("MOD FX", 2, 0);
    addView<pastel::FxView> (mfx, pic, proc.apvts, FS::chorus, "modfx_rate", "modfx_depth", "modfx_type",
                             juce::String(), juce::String(), "RATE", "DEPTH")
        ->setTooltip ("Drag: rate sideways, depth up / down (click to switch an Off effect on)");
    addView<pastel::SegChoice> (mfx, seg, proc.apvts, "modfx_type",
                                juce::StringArray { "OFF", "CHORUS", "FLANGE", "PHASER" });
    addKnob (mfx, "modfx_mix", "Mix", 156, 40, 30);

    auto* dl = cardAt ("DELAY", 0, 1);
    addView<pastel::FxView> (dl, pic, proc.apvts, FS::delay,
                             "delay_time", "delay_feedback", "delay_mix", "delay_sync")
        ->setTooltip ("Drag: time sideways (when not synced), feedback up / down");
    addView<pastel::SegChoice> (dl, seg, proc.apvts, "delay_type",
                                juce::StringArray { "STEREO", "CROSS", "PING-PONG" });
    addChoice (dl, "delay_sync", "", 156, 30, 58)->setTooltip ("Tempo sync: a note value at the song / host tempo replaces TIME (Off = TIME knob)");
    addKnob (dl, "delay_mix", "Mix", 156, 56, 30);

    auto* rv = cardAt ("REVERB", 1, 1);
    onSwitch (rv, "reverb_on");
    addView<pastel::FxView> (rv, picTall, proc.apvts, FS::reverb, "reverb_size", "reverb_damp", "reverb_on",
                             juce::String(), juce::String(), "SIZE", "DAMP")
        ->setTooltip ("Drag: size sideways, damping up / down (click to switch reverb on)");
    addKnob (rv, "reverb_mix", "Mix", 156, 62, 30);

    auto* eq = cardAt ("EQ", 2, 1);
    addView<pastel::EqView> (eq, { 10, 30, 202, 104 }, proc.apvts, "eq_low", "eq_high")
        ->setTooltip ("Drag the left / right half: low / high shelf (double-click = flat)");

    auto* ou = addSection ("OUTPUT", 1044, 596, 220, 294);
    {
        auto* scopeView = new ScopeView();
        scopeViews.push_back (scopeView);
        ou->addAndMakeVisible (scopeView);
        owned.emplace_back (scopeView);
        scopeView->setBounds (12, 34, 196, 108);
        auto* meterView = new MeterView (proc);
        meterViews.push_back (meterView);
        ou->addAndMakeVisible (meterView);
        owned.emplace_back (meterView);
        meterView->setBounds (12, 150, 196, 40);
    }
    addKnob (ou, "output_level", "Level", 4, 198, 44);
    addKnob (ou, "master_tune", "MTune", 86, 202, 30, 52);
    addKnob (ou, "bend_range", "Bend", 154, 202, 30, 52)->setTooltip ("Pitch bend range, semitones each way");

    refreshValues();
    startTimerHz (30);
    setSize (1280, 900);
    selectPage (0);
}

void FrogContent::selectPage (int i)
{
    curPage = juce::jlimit (0, 1, i);
    page0.setVisible (curPage == 0);
    page1.setVisible (curPage == 1);
    tabT1.setToggleState (curPage == 0, juce::dontSendNotification);
    tabT2.setToggleState (curPage == 1, juce::dontSendNotification);
    repaint();   // the active folder tab opens into the timbre panel
}

bool FrogContent::isPageVisible (int i) const
{
    if (i == 0) return page0.isVisible();
    return page1.isVisible();
}

FrogContent::~FrogContent() { setLookAndFeel (nullptr); }

juce::Slider* FrogContent::addKnob (juce::Component* parent, const juce::String& pid,
                                       const juce::String& label, int x, int y, int size,
                                       int labelW)
{
    int ly = label.isEmpty() ? 0 : 14;
    int lx = x + (56 - labelW) / 2;
    if (! label.isEmpty())
    {
        auto* lb = new juce::Label (label, label);
        lb->setBounds (lx, y, labelW, 14);
        lb->setJustificationType (juce::Justification::centred);
        lb->setColour (juce::Label::textColourId, NanoColors::dim);
        parent->addAndMakeVisible (lb);
        owned.emplace_back (lb);
    }
    auto* sl = new juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    sl->setBounds (x + (56 - size) / 2, y + ly, size, size);
    parent->addAndMakeVisible (sl);
    owned.emplace_back (sl);
    if (! label.isEmpty())
    {
        auto* vb = new juce::Label (pid + "_v", "");
        vb->setBounds (lx, y + ly + size, labelW, 14);
        vb->setJustificationType (juce::Justification::centred);
        vb->setColour (juce::Label::textColourId, NanoColors::text);
        parent->addAndMakeVisible (vb);
        owned.emplace_back (vb);
        knobCells.push_back ({ sl, pid, vb, {} });
    }
    sAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, pid, *sl));
    return sl;
}

juce::ComboBox* FrogContent::addChoice (juce::Component* parent, const juce::String& pid,
                                           const juce::String& label, int x, int y, int w)
{
    int ly = label.isEmpty() ? 0 : 14;
    if (! label.isEmpty())
    {
        auto* lb = new juce::Label (label, label);
        lb->setBounds (x, y, w, 14);
        lb->setJustificationType (juce::Justification::centred);
        lb->setColour (juce::Label::textColourId, NanoColors::dim);
        parent->addAndMakeVisible (lb);
        owned.emplace_back (lb);
    }
    auto* cb = new juce::ComboBox();
    cb->setJustificationType (juce::Justification::centred);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid)))
        cb->addItemList (p->choices, 1);
    cb->setBounds (x, y + ly, w, 22);
    parent->addAndMakeVisible (cb);
    owned.emplace_back (cb);
    choiceCells.push_back ({ cb, pid });
    cAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, pid, *cb));
    return cb;
}

juce::ToggleButton* FrogContent::addToggle (juce::Component* parent, const juce::String& pid,
                                               const juce::String& label, int x, int y, int w, int h)
{
    auto* tb = new juce::ToggleButton (label);
    tb->setBounds (x, y, w, h);
    parent->addAndMakeVisible (tb);
    owned.emplace_back (tb);
    toggleCells.push_back ({ tb, pid });
    bAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, pid, *tb));
    return tb;
}

juce::GroupComponent* FrogContent::addSection (const juce::String& title, int x, int y, int w, int h)
{
    auto* g = new juce::GroupComponent (title, title);
    g->setBounds (x, y, w, h);
    (buildParent != nullptr ? buildParent : this)->addAndMakeVisible (g);
    owned.emplace_back (g);
    secLayout.emplace_back (g, juce::Rectangle<int> (x, y, w, h));
    return g;
}

SegStrip* FrogContent::addSeg (juce::Component* parent, const juce::String& pid,
                                  const juce::StringArray& labels, bool red,
                                  int x, int y, int bw, int bh, int cols)
{
    auto* s = new SegStrip (proc, pid, labels, red, cols);
    parent->addAndMakeVisible (s);
    owned.emplace_back (s);
    strips.push_back (s);
    layoutSeg (s, x, y, bw, bh, cols);
    return s;
}

juce::TextButton* FrogContent::addModButton (juce::Component* parent, const juce::String& text,
                                                bool isSync, int slot, int x, int y, int w, int h)
{
    auto* b = new juce::TextButton (text);
    b->setClickingTogglesState (true);
    b->setBounds (x, y, w, h);
    int tb = buildTimbre;
    b->onClick = [this, isSync, tb] { setOscModBit (tb, isSync); syncModButtons (tb); };
    // note: toggle-off is derived — clicking an active button clears its bit
    parent->addAndMakeVisible (b);
    owned.emplace_back (b);
    if (isSync) syncBtns[tb][slot] = b;
    else ringBtns[tb][slot] = b;
    return b;
}

void FrogContent::setOscModBit (int timbre, bool isSync)
{
    // Buttons reflect the timbre's osc_mod choice (Off/Ring/Sync/RingSync):
    // clicking toggles that button's bit.
    juce::String pid = timbre == 0 ? "osc_mod" : "t2_osc_mod";
    float cur = 0.0f;
    if (auto* raw = proc.apvts.getRawParameterValue (pid)) cur = raw->load();
    bool s = cur == 2 || cur == 3, r = cur == 1 || cur == 3;
    if (isSync) s = ! s;
    else r = ! r;
    int nm = s ? (r ? 3 : 2) : (r ? 1 : 0);
    if (auto* p = proc.apvts.getParameter (pid))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) nm));
        p->endChangeGesture();
    }
}

void FrogContent::syncModButtons (int timbre)
{
    juce::String pid = timbre == 0 ? "osc_mod" : "t2_osc_mod";
    float cur = 0.0f;
    if (auto* raw = proc.apvts.getRawParameterValue (pid)) cur = raw->load();
    bool s = cur == 2 || cur == 3, r = cur == 1 || cur == 3;
    for (int i = 0; i < 2; ++i)
    {
        if (syncBtns[timbre][i] != nullptr)
            syncBtns[timbre][i]->setToggleState (s, juce::dontSendNotification);
        if (ringBtns[timbre][i] != nullptr)
            ringBtns[timbre][i]->setToggleState (r, juce::dontSendNotification);
    }
}

void FrogContent::refreshPresetBox()
{
    int cur = proc.getCurrentProgram();
    juce::String user = proc.getCurrentUserPreset();
    if (cur < 0 && user.isNotEmpty())
    {
        juce::String shown = user;
        if (shown.endsWithIgnoreCase (".nbpreset"))
            shown = shown.dropLastCharacters (9);
        if (presetBox.getText() != shown)
        {
            presetBox.setSelectedItemIndex (-1, juce::dontSendNotification);
            presetBox.setText (shown, juce::dontSendNotification);
        }
        return;
    }
    if (cur < 0) // user file gone (deleted): sound stays, name can't ghost
    {
        if (presetBox.getText() != "Custom")
        {
            presetBox.setSelectedItemIndex (-1, juce::dontSendNotification);
            presetBox.setText ("Custom", juce::dontSendNotification);
        }
        return;
    }
    if (cur >= 0) presetBox.setSelectedId (cur + 1, juce::dontSendNotification);
}

void FrogContent::openBrowser()
{
    if (browser != nullptr)
    {
        browser->setVisible (true);
        browser->toFront (true);
    }
}

void FrogContent::closeBrowser()
{
    if (browser != nullptr) browser->setVisible (false);
    refreshPresetBox();
}

void FrogContent::refreshValues()
{
    for (auto& kc : knobCells)
    {
        if (auto* p = proc.apvts.getParameter (kc.pid))
        {
            juce::String t = fmtVal (kc.pid, p->convertFrom0to1 (p->getValue()));
            if (t != kc.last) { kc.value->setText (t, juce::dontSendNotification); kc.last = t; }
        }
    }
    refreshWaveLabel (waveNameLabel, "osc1_digital");
    refreshWaveLabel (waveNameLabel2, "t2_osc1_digital");
    refreshWaveLabel (waveNameO2[0], "osc2_digital");
    refreshWaveLabel (waveNameO2[1], "t2_osc2_digital");
}

void FrogContent::refreshWaveLabel (juce::Label& lb, const juce::String& digiPid)
{
    // A fresh import status wins on its own oscillator's label (also when no
    // wave is loaded: failed imports used to be overwritten on the next tick).
    if (&lb == statusLabel
        && (juce::int32) (juce::Time::getMillisecondCounter() - statusHoldMs) < 0)
    {
        if (lb.getText() != statusText) lb.setText (statusText, juce::dontSendNotification);
        return;
    }
    if (proc.hasUserWave())
    {
        juce::String t = proc.getUserWaveName();
        int bc = proc.getUserBankCount();
        if (bc > 1)
        {
            float di = 0.0f;
            if (auto* raw = proc.apvts.getRawParameterValue (digiPid)) di = raw->load();
            int idx = juce::jlimit (0, bc - 1, (int) std::round (di >= 128.0f ? di - 128.0f : di));
            t = proc.getUserWaveName() + " [" + juce::String (idx + 1) + "/"
                + juce::String (bc) + "] " + proc.getUserBankEntryName (idx);
        }
        if (lb.getText() != t) lb.setText (t, juce::dontSendNotification);
    }
    else if (lb.getText() != "No user wave")
        lb.setText ("No user wave", juce::dontSendNotification);
}

void FrogContent::refreshStrips()
{
    for (auto* s : strips) s->sync();
}

bool FrogContent::selfTestControls()
{
    int checked = 0;
    auto fail = [&] (const juce::String& what)
    {
        printf ("selftest FAIL: %s\n", what.toRawUTF8());
        return false;
    };
    // knobs: move slider, param must follow, then restore
    for (auto& kc : knobCells)
    {
        auto* p = proc.apvts.getParameter (kc.pid);
        if (p == nullptr) return fail ("knob " + kc.pid + " has no parameter");
        double v0 = kc.slider->getValue();
        double lo = kc.slider->getMinimum(), hi = kc.slider->getMaximum();
        double tgt = (v0 < (lo + hi) * 0.5) ? v0 + 0.3 * (hi - lo) : v0 - 0.3 * (hi - lo);
        tgt = juce::jlimit (lo, hi, tgt);
        if (std::abs (tgt - v0) < 1e-9) continue;
        float p0 = p->getValue();
        kc.slider->setValue (tgt, juce::sendNotificationSync);
        float p1 = p->getValue();
        kc.slider->setValue (v0, juce::sendNotificationSync);
        if (std::abs (p1 - p0) < 1e-6f)
            return fail ("knob " + kc.pid + " did not move parameter");
        ++checked;
    }
    // attached combos + toggles: ComboBox/Button notification paths dispatch
    // through the message loop, so these are driven and verified on separate
    // ticks of a live dispatch loop (see headless test driver stages).
    for (auto& tc : toggleCells)
    {
        auto* tb = dynamic_cast<juce::ToggleButton*> (tc.comp);
        auto* p = proc.apvts.getParameter (tc.pid);
        if (tb == nullptr || p == nullptr) return fail ("toggle " + tc.pid + " missing");
    }
    // header mix knob (no readout cell): same drive/restore pattern
    if (mixSlider != nullptr)
    {
        auto* p = proc.apvts.getParameter ("tmix");
        if (p == nullptr) return fail ("mix knob has no parameter");
        double v0 = mixSlider->getValue();
        double lo = mixSlider->getMinimum(), hi = mixSlider->getMaximum();
        double tgt = (v0 < (lo + hi) * 0.5) ? v0 + 0.3 * (hi - lo) : v0 - 0.3 * (hi - lo);
        tgt = juce::jlimit (lo, hi, tgt);
        if (std::abs (tgt - v0) >= 1e-9)
        {
            float p0 = p->getValue();
            mixSlider->setValue (tgt, juce::sendNotificationSync);
            float p1 = p->getValue();
            mixSlider->setValue (v0, juce::sendNotificationSync);
            if (std::abs (p1 - p0) < 1e-6f)
                return fail ("mix knob did not move parameter");
            ++checked;
        }
    }
    printf ("selftest: %d sync controls verified live\n", checked);
    return true;
}

void FrogContent::driveAsyncControls()
{
    asyncOrig.clear();
    for (auto& cc : choiceCells)
    {
        auto* cb = dynamic_cast<juce::ComboBox*> (cc.comp);
        auto* p = proc.apvts.getParameter (cc.pid);
        if (cb == nullptr || p == nullptr || cb->getNumItems() < 2) continue;
        asyncOrig.emplace_back (cc.pid, p->getValue());
        int cur = cb->getSelectedId(), n = cb->getNumItems();
        cb->setSelectedId ((cur != 1) ? 1 : n, juce::sendNotification);
    }
    for (auto& tc : toggleCells)
    {
        // arp on/latch are covered by the dedicated OFF-button scenario below
        if (tc.pid == "arp_on" || tc.pid == "arp_latch") continue;
        auto* tb = dynamic_cast<juce::ToggleButton*> (tc.comp);
        auto* p = proc.apvts.getParameter (tc.pid);
        if (tb == nullptr || p == nullptr) continue;
        asyncOrig.emplace_back (tc.pid, p->getValue());
        tb->setToggleState (! tb->getToggleState(), juce::sendNotification);
    }
    for (auto* s : strips)
    {
        auto* p = proc.apvts.getParameter (s->pid);
        if (p == nullptr || (int) s->btns.size() < 2) continue;
        asyncOrig.emplace_back (s->pid, p->getValue());
        float pv0 = p->getValue();
        int curIdx = 0;
        float best = 1e9f;
        for (int i = 0; i < (int) s->btns.size(); ++i)
        {
            float cand = (float) i / (float) ((int) s->btns.size() - 1);
            if (std::abs (cand - pv0) < best) { best = std::abs (cand - pv0); curIdx = i; }
        }
        s->btns[(size_t) ((curIdx != 0) ? 0 : 1)]->triggerClick();
    }
    for (int t = 0; t < 2; ++t)
    {
        juce::String pid = t == 0 ? "osc_mod" : "t2_osc_mod";
        auto* p = proc.apvts.getParameter (pid);
        if (p == nullptr || syncBtns[t][0] == nullptr) continue;
        asyncOrig.emplace_back (pid, p->getValue());
        syncBtns[t][0]->triggerClick();
    }
    if (arpOffBtn != nullptr)
    {
        auto* pa = proc.apvts.getParameter ("arp_on");
        auto* pl = proc.apvts.getParameter ("arp_latch");
        if (pa != nullptr && pl != nullptr)
        {
            asyncOrig.emplace_back ("arp_on", pa->getValue());
            asyncOrig.emplace_back ("arp_latch", pl->getValue());
            pa->setValueNotifyingHost (1.0f);
            pl->setValueNotifyingHost (1.0f);
            arpOffBtn->triggerClick();
        }
    }
}

bool FrogContent::verifyAsyncControls()
{
    for (auto& [pid, v0] : asyncOrig)
    {
        auto* p = proc.apvts.getParameter (pid);
        if (p == nullptr)
        {
            printf ("selftest FAIL: async %s missing\n", pid.toRawUTF8());
            return false;
        }
        float v1 = p->getValue();
        bool isArp = (pid == "arp_on" || pid == "arp_latch");
        if (isArp)
        {
            // driven on then cleared by OFF: expect cleared state here
            if (v1 >= 0.5f)
            {
                printf ("selftest FAIL: async %s not cleared (%.4f)\n",
                        pid.toRawUTF8(), v1);
                return false;
            }
        }
        else if (std::abs (v1 - v0) < 1e-6f)
        {
            printf ("selftest FAIL: async %s did not move (%.4f)\n",
                    pid.toRawUTF8(), v0);
            return false;
        }
        p->setValueNotifyingHost (v0); // restore
    }
    printf ("selftest: %d async controls verified live\n", (int) asyncOrig.size());
    asyncOrig.clear();
    return true;
}

juce::Label& FrogContent::waveLabelFor (int timbre, int osc)
{
    if (osc == 1) return waveNameO2[juce::jlimit (0, 1, timbre)];
    return timbre == 0 ? waveNameLabel : waveNameLabel2;
}

void FrogContent::initWaveLabel (juce::Component* parent, juce::Label& lb)
{
    lb.setText ("No user wave", juce::dontSendNotification);
    lb.setColour (juce::Label::textColourId, NanoColors::dim);
    lb.setJustificationType (juce::Justification::centred);
    setOwnFont (lb, look.uiFont (11.0f));
    parent->addAndMakeVisible (lb);
    lb.setBounds (8, 164, 120, 22);
}

void FrogContent::showStatus (const juce::String& t, int timbre, int osc)
{
    statusText = t;
    statusHoldMs = juce::Time::getMillisecondCounter() + 3000;
    statusLabel = &waveLabelFor (timbre, osc);
    statusLabel->setText (t, juce::dontSendNotification);
}

void FrogContent::selectUserWave (int timbre, int osc)
{
    // Point the target oscillator at USER 01 (digital index 128).
    juce::String pre = timbre == 0 ? "" : "t2_";
    juce::String wavePid = pre + (osc == 1 ? juce::String ("osc2_wave") : juce::String ("osc1_wave"));
    juce::String digiPid = pre + (osc == 1 ? juce::String ("osc2_digital") : juce::String ("osc1_digital"));
    if (auto* p = proc.apvts.getParameter (wavePid))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (6.0f));
        p->endChangeGesture();
    }
    if (auto* p = proc.apvts.getParameter (digiPid))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (128.0f));
        p->endChangeGesture();
    }
}

void FrogContent::importWaveFile (int timbre, int osc)
{
    chooser = std::make_unique<juce::FileChooser> ("Import single-cycle wave", juce::File(),
                                                   "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this, timbre, osc] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            std::unique_ptr<juce::AudioFormatReader> reader (
                formatManager.createReaderFor (file));
            if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 64)
            {
                showStatus ("Read failed: " + file.getFileName(), timbre, osc);
                return;
            }
            int n = (int) juce::jmin<int64_t> (reader->lengthInSamples, 1 << 20);
            juce::AudioBuffer<float> buf (1, n);
            reader->read (&buf, 0, n, 0, true, false);
            if (proc.importUserWave (buf.getReadPointer (0), n, file.getFileNameWithoutExtension()))
                selectUserWave (timbre, osc);
            else
                showStatus ("Import failed: " + file.getFileName(), timbre, osc);
        });
}

void FrogContent::importWaveFolder (int timbre, int osc)
{
    auto fch = std::make_shared<juce::FileChooser> ("Import wave folder", juce::File());
    fch->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectDirectories,
        [this, timbre, osc, fch] (const juce::FileChooser& fc)
        {
            auto dir = fc.getResult();
            if (dir == juce::File()) return;
            int loaded = proc.importUserFolder (dir.getFullPathName());
            if (loaded > 0)
                selectUserWave (timbre, osc);
            else
                showStatus ("No waves found in folder", timbre, osc);
        });
}

void FrogContent::paint (juce::Graphics& g)
{
    g.fillAll (NanoColors::bg);
    // the Pastel family mark: four rounded squares
    const juce::uint32 cols[4] = { 0xffe88a8a, 0xff8fd0c2, 0xffe9a3b8, 0xffa9d58e };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (juce::Colour (cols[i]));
        g.fillRoundedRectangle (14.0f + (float) (i % 2) * 10.0f, 13.0f + (float) (i / 2) * 10.0f, 8.0f, 8.0f, 2.0f);
    }
    g.setColour (NanoColors::border);
    g.drawLine (8, 46, (float) getWidth() - 8, 46, 1.0f);
    // the timbre panel under its folder tabs (the active tab opens into it)
    const auto box = juce::Rectangle<float> (16.0f, 82.0f, 1248.0f, 492.0f);
    g.setColour (juce::Colour (0xff141415));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (NanoColors::accent.withAlpha (0.35f));
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
    const auto tab = (curPage == 0 ? tabT1 : tabT2).getBounds().toFloat();
    g.setColour (juce::Colour (0xff141415));
    g.fillRect (tab.getX() + 1.0f, 80.0f, tab.getWidth() - 2.0f, 4.0f);
    // shared section caption
    g.setColour (NanoColors::accent);
    g.fillEllipse (19.0f, 581.0f, 6.0f, 6.0f);
    g.setColour (NanoColors::dim);
    g.setFont (look.titleFont (10.5f));
    g.drawText (juce::String::fromUTF8 ("SHARED  \xc2\xb7  ARP  >  DRIVE  >  SATURATION  >  MOD FX  >  DELAY  >  REVERB  >  EQ  >  OUTPUT"), 30, 576, 600, 16, juce::Justification::centredLeft);
}

void FrogContent::resized()
{
    if (secLayout.empty()) return;
    for (auto& [g, r] : secLayout) g->setBounds (r);
    if (browser != nullptr) browser->setBounds (getLocalBounds());
    presetLabel.setBounds (38, 2, 126, 42);
    setOwnFont (presetLabel, look.logoFont (19.0f));
    prevBtn.setBounds (166, 11, 28, 24);
    nextBtn.setBounds (196, 11, 28, 24);
    presetBox.setBounds (230, 11, 236, 24);
    browseBtn.setBounds (472, 11, 72, 24);
    mutateBtn.setBounds (550, 11, 80, 24);
    midiBtn.setBounds (636, 11, 62, 24);
}

void FrogContent::timerCallback()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    refreshValues();
    refreshStrips();
    // the drive pictures have no on-switch of their own: dim them while off
    for (auto [v, pid] : { std::pair { driveViews[0], "drive_on" }, std::pair { driveViews[1], "sat_on" } })
        if (v != nullptr)
            if (auto* raw = proc.apvts.getRawParameterValue (pid))
                v->setAlpha (raw->load() > 0.5f ? 1.0f : 0.45f);
    syncModButtons (0);
    syncModButtons (1);
    if (presetBox.getSelectedItemIndex() != proc.getCurrentProgram())
        refreshPresetBox();
    float tmp[2048];
    int avail = 2048;
    proc.pullScopeData (tmp, avail);
    for (auto* sv : scopeViews)
        if (auto* s = dynamic_cast<ScopeView*> (sv)) { s->pushBlock (tmp, avail); s->repaint(); }
    for (auto* mv : meterViews) mv->repaint();
}

int FrogContent::getSectionCount() const
{
    int n = 0;
    for (const auto& c : owned)
        if (dynamic_cast<const juce::GroupComponent*> (c.get()) != nullptr) ++n;
    return n;
}

// ================= resizable host editor =================
NanoFrogEditor::NanoFrogEditor (NanoFrogProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p),
      content (std::make_unique<FrogContent> (p)),
      corner (this, getConstrainer())
{
    setSize (FrogContent::baseW, FrogContent::baseH);
    setResizable (true, true);
    setResizeLimits (768, 540, 2048, 1440); // ~0.6x .. 1.6x of base
    addAndMakeVisible (content.get());
    addAndMakeVisible (corner);
    setWantsKeyboardFocus (true);
}

NanoFrogEditor::~NanoFrogEditor() {}

bool NanoFrogEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) proc.redoEdit(); else proc.undoEdit();
        return true;
    }
    return false;
}

void NanoFrogEditor::paint (juce::Graphics& g)
{
    g.fillAll (NanoColors::bg); // letterbox surround
}

void NanoFrogEditor::resized()
{
    float s = juce::jmin ((float) getWidth() / (float) FrogContent::baseW,
                          (float) getHeight() / (float) FrogContent::baseH);
    s = juce::jlimit (0.4f, 3.0f, s);
    float ox = ((float) getWidth() - (float) FrogContent::baseW * s) * 0.5f;
    float oy = ((float) getHeight() - (float) FrogContent::baseH * s) * 0.5f;
    content->setTransform (juce::AffineTransform::scale (s).translated (ox, oy));
    content->setBounds (0, 0, FrogContent::baseW, FrogContent::baseH);
    corner.setBounds (getWidth() - 20, getHeight() - 20, 20, 20);
}
