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
        return juce::String (0.05f + v * v * 8.0f, 2) + " Hz";
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
    if (pid == "filter_keytrack" || pid == "filter_envamt" || pid == "arp_swing"
        || pid == "mod1_amt" || pid == "mod2_amt" || pid == "mod3_amt" || pid == "mod4_amt")
        return pctS (v);
    if (pid == "arp_gate") return pct (v);
    return pct (v);
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
    : proc (p)
{
    setLookAndFeel (&look);
    formatManager.registerBasicFormats();

    addAndMakeVisible (presetLabel);
    presetLabel.setText ("NANOFROG", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setColour (juce::Label::textColourId, NanoColors::accent);

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
        tabs[i]->setBounds (552 + i * 106, 11, 102, 24);
        tabs[i]->setClickingTogglesState (true);
        tabs[i]->setRadioGroupId (0x7418, juce::dontSendNotification);
        tabs[i]->onClick = [this, i] { selectPage (i); };
    }
    addToggle (this, "limiter", "LIMITER", 990, 11, 90, 24);

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
        t1->setFont (look.uiFont (12.5f));
        t1->setBounds (1144, 13, 24, 16);
        addAndMakeVisible (t1); owned.emplace_back (t1);
        auto* t2 = new juce::Label ("T2", "T2");
        t2->setJustificationType (juce::Justification::centred);
        t2->setColour (juce::Label::textColourId, NanoColors::dim);
        t2->setFont (look.uiFont (12.5f));
        t2->setBounds (1198, 13, 24, 16);
        addAndMakeVisible (t2); owned.emplace_back (t2);
        auto* cap = new juce::Label ("MIX", "MIX");
        cap->setJustificationType (juce::Justification::centred);
        cap->setColour (juce::Label::textColourId, NanoColors::accent);
        cap->setFont (look.titleFont (11.5f));
        cap->setBounds (1154, 34, 60, 10);
        addAndMakeVisible (cap); owned.emplace_back (cap);
    }
    mixSlider = new juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    mixSlider->setBounds (1170, 5, 28, 28);
    mixSlider->setTooltip ("Timbre mix — left: Timbre 1, right: Timbre 2");
    addAndMakeVisible (mixSlider); owned.emplace_back (mixSlider);
    sAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, "tmix", *mixSlider));
    buildParent = &page0; buildTimbre = 0;

    // ---- Row 1 ----
    auto* osc1 = addSection ("OSC 1", 8, 66, 340, 256);
    addChoice (osc1, "osc1_wave", "Wave", 8, 30, 120);
    addChoice (osc1, "osc1_digital", "Digital", 8, 72, 120);
    addAndMakeVisible (importBtn);
    importBtn.onClick = [&] { importWaveFile (0, 0); };
    osc1->addAndMakeVisible (importBtn);
    importBtn.setBounds (8, 114, 120, 22);
    folderBtn.onClick = [&] { importWaveFolder (0, 0); };
    osc1->addAndMakeVisible (folderBtn);
    folderBtn.setBounds (8, 142, 120, 22);
    waveNameLabel.setText (proc.hasUserWave() ? proc.getUserWaveName() : "No user wave",
                           juce::dontSendNotification);
    waveNameLabel.setColour (juce::Label::textColourId, NanoColors::dim);
    osc1->addAndMakeVisible (waveNameLabel);
    waveNameLabel.setBounds (8, 168, 120, 28);
    waveNameLabel.setJustificationType (juce::Justification::centred);
    waveNameLabel.setFont (look.uiFont (10.5f));
    addKnob (osc1, "osc1_octave", "Octave", 136, 80, 36, 50);
    addKnob (osc1, "osc1_pitch", "Pitch", 186, 80, 36, 50);
    addKnob (osc1, "osc1_fine", "Fine", 236, 80, 36, 50);
    addKnob (osc1, "osc1_level", "Level", 286, 80, 36, 50);
    addKnob (osc1, "osc1_shape", "Shape", 136, 160, 36, 50);
    addKnob (osc1, "osc1_pwm", "PWM", 186, 160, 36, 50);
    addKnob (osc1, "osc1_xmod", "X-Mod", 236, 160, 36, 50);
    addModButton (osc1, "SYNC", true, 0, 8, 200, 120, 24);
    addModButton (osc1, "RING", false, 0, 8, 228, 120, 24);

    auto* osc2 = addSection ("OSC 2", 354, 66, 340, 256);
    addChoice (osc2, "osc2_wave", "Wave", 8, 30, 120);
    addChoice (osc2, "osc2_digital", "Digital", 8, 72, 120);
    impBtnO2[0].setButtonText ("IMPORT WAV");
    osc2->addAndMakeVisible (impBtnO2[0]);
    impBtnO2[0].setBounds (8, 114, 120, 22);
    impBtnO2[0].onClick = [&] { importWaveFile (0, 1); };
    fldBtnO2[0].setButtonText ("FOLDER");
    osc2->addAndMakeVisible (fldBtnO2[0]);
    fldBtnO2[0].setBounds (8, 142, 120, 22);
    fldBtnO2[0].onClick = [&] { importWaveFolder (0, 1); };
    addModButton (osc2, "SYNC", true, 1, 8, 200, 120, 24);
    addModButton (osc2, "RING", false, 1, 8, 228, 120, 24);
    addKnob (osc2, "osc2_octave", "Octave", 136, 80, 36, 50);
    addKnob (osc2, "osc2_pitch", "Pitch", 186, 80, 36, 50);
    addKnob (osc2, "osc2_fine", "Fine", 236, 80, 36, 50);
    addKnob (osc2, "osc2_level", "Level", 286, 80, 36, 50);
    addKnob (osc2, "osc2_shape", "Shape", 136, 160, 36, 50);
    addKnob (osc2, "osc2_pwm", "PWM", 186, 160, 36, 50);
    addKnob (osc2, "osc2_xmod", "X-Mod", 236, 160, 36, 50);
    auto* mixer = addSection ("MIXER", 700, 66, 190, 256);
    addKnob (mixer, "mix_o1", "Osc 1", 24, 60, 52);
    addKnob (mixer, "mix_o2", "Osc 2", 114, 60, 52);
    addKnob (mixer, "mix_noise", "Noise", 24, 152, 52);
    addKnob (mixer, "fm_amt", "FM", 114, 152, 52);

    buildParent = this;
    auto* scope = addSection ("SCOPE", 896, 66, 384, 256);
    auto* scopeView = new ScopeView();
    scopeViews.push_back (scopeView);
    scope->addAndMakeVisible (scopeView);
    owned.emplace_back (scopeView);
    scopeView->setBounds (8, 32, 368, 216);
    buildParent = &page0;

    // ---- Row 2 ----
    auto* filt = addSection ("FILTER", 8, 326, 350, 190);
    {
        auto* cap = new juce::Label ("TYPE", "TYPE");
        cap->setColour (juce::Label::textColourId, NanoColors::dim);
        filt->addAndMakeVisible (cap); owned.emplace_back (cap); cap->setBounds (8, 57, 100, 14);
    }
    auto* fstrip = addSeg (filt, "filter_type",
                           { "LP24", "LP12", "BP12", "HP12" }, true, 8, 75, 72, 26, 2);
    juce::ignoreUnused (fstrip);
    addKnob (filt, "filter_cutoff", "Cutoff", 176, 24, 52);
    addKnob (filt, "filter_reso", "Reso", 254, 24, 52);
    addKnob (filt, "filter_keytrack", "KeyTrk", 176, 108, 52);
    addKnob (filt, "filter_envamt", "EnvAmt", 254, 108, 52);

    auto* amp = addSection ("AMP", 364, 326, 250, 190);
    addKnob (amp, "amp_level", "Level", 8, 60, 52);
    addKnob (amp, "amp_pan", "Pan", 60, 60, 52);
    addKnob (amp, "amp_velocity", "Veloc", 112, 60, 52);
    addKnob (amp, "amp_drive", "Drive", 164, 60, 52);
    addToggle (amp, "amp_dist", "DIST", 164, 146, 52, 26);

    buildParent = this;
    auto* fx = addSection ("EFFECTS", 600, 326, 672, 190);
    {
        auto* cap = new juce::Label ("MOD FX", "MOD FX");
        cap->setColour (juce::Label::textColourId, NanoColors::dim);
        fx->addAndMakeVisible (cap); owned.emplace_back (cap); cap->setBounds (40, 28, 100, 14);
        cap->setJustificationType (juce::Justification::centred);
    }
    addChoice (fx, "modfx_type", "", 40, 42, 100);
    addKnob (fx, "modfx_rate", "Rate", 40, 80, 36);
    addKnob (fx, "modfx_depth", "Depth", 90, 80, 36);
    addKnob (fx, "modfx_mix", "Mix", 140, 80, 36);
    {
        auto* cap = new juce::Label ("DELAY", "DELAY");
        cap->setColour (juce::Label::textColourId, NanoColors::dim);
        fx->addAndMakeVisible (cap); owned.emplace_back (cap); cap->setBounds (270, 28, 100, 14);
        cap->setJustificationType (juce::Justification::centred);
    }
    addChoice (fx, "delay_type", "", 270, 42, 100);
    addKnob (fx, "delay_time", "Time", 270, 80, 36);
    addKnob (fx, "delay_feedback", "Fdbk", 320, 80, 36);
    addKnob (fx, "delay_mix", "Mix", 370, 80, 36);
    {
        auto* cap = new juce::Label ("EQ", "EQ");
        cap->setColour (juce::Label::textColourId, NanoColors::dim);
        fx->addAndMakeVisible (cap); owned.emplace_back (cap); cap->setBounds (500, 28, 80, 14);
        cap->setJustificationType (juce::Justification::centred);
    }
    addKnob (fx, "eq_low", "Low", 500, 80, 36);
    addKnob (fx, "eq_high", "High", 550, 80, 36);
    {
        // thin vertical dividers between MOD / DELAY / EQ (Labels paint their bg)
        for (int dx : { 223, 453 })
        {
            auto* div = new juce::Label();
            div->setColour (juce::Label::backgroundColourId, NanoColors::border);
            fx->addAndMakeVisible (div); owned.emplace_back (div);
            div->setBounds (dx, 30, 1, 152);
        }
    }

    buildParent = &page0;
    // ---- Row 3 ----
    auto* e1 = addSection ("ENV 1 (FILTER)", 8, 520, 220, 210);
    addKnob (e1, "env1_a", "A", 48, 50, 36);
    addKnob (e1, "env1_d", "D", 124, 50, 36);
    addKnob (e1, "env1_s", "S", 48, 120, 36);
    addKnob (e1, "env1_r", "R", 124, 120, 36);

    auto* e2 = addSection ("ENV 2 (AMP)", 234, 520, 220, 210);
    addKnob (e2, "env2_a", "A", 48, 50, 36);
    addKnob (e2, "env2_d", "D", 124, 50, 36);
    addKnob (e2, "env2_s", "S", 48, 120, 36);
    addKnob (e2, "env2_r", "R", 124, 120, 36);

    auto* l1 = addSection ("LFO 1", 460, 520, 235, 210);
    addSeg (l1, "lfo1_wave", { "SIN", "TRI", "SQR", "SAW", "RND" }, false, 8, 32, 38, 24, 5);
    addKnob (l1, "lfo1_rate", "Rate", 59, 100, 36);
    addKnob (l1, "lfo1_depth", "Depth", 139, 100, 36);
    addChoice (l1, "lfo1_sync", "Sync", 59, 168, 116);

    auto* l2 = addSection ("LFO 2", 701, 520, 235, 210);
    addSeg (l2, "lfo2_wave", { "SIN", "TRI", "SQR", "SAW", "RND" }, false, 8, 32, 38, 24, 5);
    addKnob (l2, "lfo2_rate", "Rate", 59, 100, 36);
    addKnob (l2, "lfo2_depth", "Depth", 139, 100, 36);
    addChoice (l2, "lfo2_sync", "Sync", 59, 168, 116);

    auto* mm = addSection ("MOD MATRIX", 942, 520, 330, 210);
    {
        const char* heads[3] = { "SOURCE", "DESTINATION", "AMOUNT" };
        int hxs[3] = { 8, 122, 244 };
        int hws[3] = { 90, 110, 76 };
        for (int i = 0; i < 3; ++i)
        {
            auto* cap = new juce::Label (heads[i], heads[i]);
            cap->setColour (juce::Label::textColourId, NanoColors::dim);
            mm->addAndMakeVisible (cap); owned.emplace_back (cap);
            cap->setBounds (hxs[i], 30, hws[i], 14);
            cap->setJustificationType (juce::Justification::centred);
        }
    }
    const char* ss[4] = { "mod1_src", "mod2_src", "mod3_src", "mod4_src" };
    const char* dd[4] = { "mod1_dst", "mod2_dst", "mod3_dst", "mod4_dst" };
    const char* aa[4] = { "mod1_amt", "mod2_amt", "mod3_amt", "mod4_amt" };
    for (int i = 0; i < 4; ++i)
    {
        int y = 52 + i * 42;
        addChoice (mm, ss[i], "", 8, y, 108);
        addChoice (mm, dd[i], "", 122, y, 118);
        addKnob (mm, aa[i], "", 240, y - 4, 30);
    }

    // ---- Row 4 ----
    buildParent = this;
    auto* ar = addSection ("ARPEGGIATOR", 8, 734, 470, 160);
    addToggle (ar, "arp_on", "ON", 8, 42, 64, 22);
    static_cast<juce::ToggleButton*> (owned.back().get())->getProperties().set ("togRed", true);
    addToggle (ar, "arp_latch", "LATCH", 8, 68, 64, 22);
    auto* arpOff = new juce::TextButton ("OFF");
    arpOffBtn = arpOff;
    ar->addAndMakeVisible (arpOff); owned.emplace_back (arpOff);
    arpOff->setBounds (8, 94, 64, 22);
    arpOff->onClick = [&]
    {
        for (auto* pid : { "arp_on", "arp_latch" })
            if (auto* p = proc.apvts.getParameter (pid))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (0.0f);
                p->endChangeGesture();
            }
    };
    addChoice (ar, "arp_mode", "Mode", 90, 40, 120);
    addChoice (ar, "arp_rate", "Rate", 90, 86, 120);
    addKnob (ar, "arp_gate", "Gate", 300, 26, 36, 40);
    addKnob (ar, "arp_octaves", "Oct", 392, 26, 36, 40);
    addKnob (ar, "arp_swing", "Swing", 300, 92, 36, 40);
    addKnob (ar, "song_tempo", "Tempo", 392, 92, 36, 40);

    buildParent = &page0;
    auto* vo = addSection ("VOICE", 484, 734, 400, 160);
    addKnob (vo, "voice_poly", "Poly", 47, 58, 36);
    addKnob (vo, "voice_detune", "Detune", 137, 58, 36);
    addKnob (vo, "voice_portamento", "Porta", 227, 58, 36);
    addKnob (vo, "voice_vib", "Vib", 317, 58, 36);

    buildParent = &page1; buildTimbre = 1;
    {
    auto* osc1 = addSection ("OSC 1", 8, 66, 340, 256);
    addChoice (osc1, "t2_osc1_wave", "Wave", 8, 30, 120);
    addChoice (osc1, "t2_osc1_digital", "Digital", 8, 72, 120);
    addAndMakeVisible (importBtn2);
    importBtn2.onClick = [&] { importWaveFile (1, 0); };
    osc1->addAndMakeVisible (importBtn2);
    importBtn2.setBounds (8, 114, 120, 22);
    folderBtn2.onClick = [&] { importWaveFolder (1, 0); };
    osc1->addAndMakeVisible (folderBtn2);
    folderBtn2.setBounds (8, 142, 120, 22);
    waveNameLabel2.setText (proc.hasUserWave() ? proc.getUserWaveName() : "No user wave",
                           juce::dontSendNotification);
    waveNameLabel2.setColour (juce::Label::textColourId, NanoColors::dim);
    osc1->addAndMakeVisible (waveNameLabel2);
    waveNameLabel2.setBounds (8, 168, 120, 28);
    waveNameLabel2.setJustificationType (juce::Justification::centred);
    waveNameLabel2.setFont (look.uiFont (10.5f));
    addKnob (osc1, "t2_osc1_octave", "Octave", 136, 80, 36, 50);
    addKnob (osc1, "t2_osc1_pitch", "Pitch", 186, 80, 36, 50);
    addKnob (osc1, "t2_osc1_fine", "Fine", 236, 80, 36, 50);
    addKnob (osc1, "t2_osc1_level", "Level", 286, 80, 36, 50);
    addKnob (osc1, "t2_osc1_shape", "Shape", 136, 160, 36, 50);
    addKnob (osc1, "t2_osc1_pwm", "PWM", 186, 160, 36, 50);
    addKnob (osc1, "t2_osc1_xmod", "X-Mod", 236, 160, 36, 50);
    addModButton (osc1, "SYNC", true, 0, 8, 200, 120, 24);
    addModButton (osc1, "RING", false, 0, 8, 228, 120, 24);

    auto* osc2 = addSection ("OSC 2", 354, 66, 340, 256);
    addChoice (osc2, "t2_osc2_wave", "Wave", 8, 30, 120);
    addChoice (osc2, "t2_osc2_digital", "Digital", 8, 72, 120);
    impBtnO2[1].setButtonText ("IMPORT WAV");
    osc2->addAndMakeVisible (impBtnO2[1]);
    impBtnO2[1].setBounds (8, 114, 120, 22);
    impBtnO2[1].onClick = [&] { importWaveFile (1, 1); };
    fldBtnO2[1].setButtonText ("FOLDER");
    osc2->addAndMakeVisible (fldBtnO2[1]);
    fldBtnO2[1].setBounds (8, 142, 120, 22);
    fldBtnO2[1].onClick = [&] { importWaveFolder (1, 1); };
    addModButton (osc2, "SYNC", true, 1, 8, 200, 120, 24);
    addModButton (osc2, "RING", false, 1, 8, 228, 120, 24);
    addKnob (osc2, "t2_osc2_octave", "Octave", 136, 80, 36, 50);
    addKnob (osc2, "t2_osc2_pitch", "Pitch", 186, 80, 36, 50);
    addKnob (osc2, "t2_osc2_fine", "Fine", 236, 80, 36, 50);
    addKnob (osc2, "t2_osc2_level", "Level", 286, 80, 36, 50);
    addKnob (osc2, "t2_osc2_shape", "Shape", 136, 160, 36, 50);
    addKnob (osc2, "t2_osc2_pwm", "PWM", 186, 160, 36, 50);
    addKnob (osc2, "t2_osc2_xmod", "X-Mod", 236, 160, 36, 50);
    auto* mixer = addSection ("MIXER", 700, 66, 190, 256);
    addKnob (mixer, "t2_mix_o1", "Osc 1", 24, 60, 52);
    addKnob (mixer, "t2_mix_o2", "Osc 2", 114, 60, 52);
    addKnob (mixer, "t2_mix_noise", "Noise", 24, 152, 52);
    addKnob (mixer, "t2_fm_amt", "FM", 114, 152, 52);

    auto* filt = addSection ("FILTER", 8, 326, 350, 190);
    {
        auto* cap = new juce::Label ("TYPE", "TYPE");
        cap->setColour (juce::Label::textColourId, NanoColors::dim);
        filt->addAndMakeVisible (cap); owned.emplace_back (cap); cap->setBounds (8, 57, 100, 14);
    }
    auto* fstrip = addSeg (filt, "t2_filter_type",
                           { "LP24", "LP12", "BP12", "HP12" }, true, 8, 75, 72, 26, 2);
    juce::ignoreUnused (fstrip);
    addKnob (filt, "t2_filter_cutoff", "Cutoff", 176, 24, 52);
    addKnob (filt, "t2_filter_reso", "Reso", 254, 24, 52);
    addKnob (filt, "t2_filter_keytrack", "KeyTrk", 176, 108, 52);
    addKnob (filt, "t2_filter_envamt", "EnvAmt", 254, 108, 52);

    auto* amp = addSection ("AMP", 364, 326, 250, 190);
    addKnob (amp, "t2_amp_level", "Level", 8, 60, 52);
    addKnob (amp, "t2_amp_pan", "Pan", 60, 60, 52);
    addKnob (amp, "t2_amp_velocity", "Veloc", 112, 60, 52);
    addKnob (amp, "t2_amp_drive", "Drive", 164, 60, 52);
    addToggle (amp, "t2_amp_dist", "DIST", 164, 146, 52, 26);

    auto* e1 = addSection ("ENV 1 (FILTER)", 8, 520, 220, 210);
    addKnob (e1, "t2_env1_a", "A", 48, 50, 36);
    addKnob (e1, "t2_env1_d", "D", 124, 50, 36);
    addKnob (e1, "t2_env1_s", "S", 48, 120, 36);
    addKnob (e1, "t2_env1_r", "R", 124, 120, 36);

    auto* e2 = addSection ("ENV 2 (AMP)", 234, 520, 220, 210);
    addKnob (e2, "t2_env2_a", "A", 48, 50, 36);
    addKnob (e2, "t2_env2_d", "D", 124, 50, 36);
    addKnob (e2, "t2_env2_s", "S", 48, 120, 36);
    addKnob (e2, "t2_env2_r", "R", 124, 120, 36);

    auto* l1 = addSection ("LFO 1", 460, 520, 235, 210);
    addSeg (l1, "t2_lfo1_wave", { "SIN", "TRI", "SQR", "SAW", "RND" }, false, 8, 32, 38, 24, 5);
    addKnob (l1, "t2_lfo1_rate", "Rate", 59, 100, 36);
    addKnob (l1, "t2_lfo1_depth", "Depth", 139, 100, 36);
    addChoice (l1, "t2_lfo1_sync", "Sync", 59, 168, 116);

    auto* l2 = addSection ("LFO 2", 701, 520, 235, 210);
    addSeg (l2, "t2_lfo2_wave", { "SIN", "TRI", "SQR", "SAW", "RND" }, false, 8, 32, 38, 24, 5);
    addKnob (l2, "t2_lfo2_rate", "Rate", 59, 100, 36);
    addKnob (l2, "t2_lfo2_depth", "Depth", 139, 100, 36);
    addChoice (l2, "t2_lfo2_sync", "Sync", 59, 168, 116);

    auto* mm = addSection ("MOD MATRIX", 942, 520, 330, 210);
    {
        const char* heads[3] = { "SOURCE", "DESTINATION", "AMOUNT" };
        int hxs[3] = { 8, 122, 244 };
        int hws[3] = { 90, 110, 76 };
        for (int i = 0; i < 3; ++i)
        {
            auto* cap = new juce::Label (heads[i], heads[i]);
            cap->setColour (juce::Label::textColourId, NanoColors::dim);
            mm->addAndMakeVisible (cap); owned.emplace_back (cap);
            cap->setBounds (hxs[i], 30, hws[i], 14);
            cap->setJustificationType (juce::Justification::centred);
        }
    }
    const char* ss[4] = { "t2_mod1_src", "t2_mod2_src", "t2_mod3_src", "t2_mod4_src" };
    const char* dd[4] = { "t2_mod1_dst", "t2_mod2_dst", "t2_mod3_dst", "t2_mod4_dst" };
    const char* aa[4] = { "t2_mod1_amt", "t2_mod2_amt", "t2_mod3_amt", "t2_mod4_amt" };
    for (int i = 0; i < 4; ++i)
    {
        int y = 52 + i * 42;
        addChoice (mm, ss[i], "", 8, y, 108);
        addChoice (mm, dd[i], "", 122, y, 118);
        addKnob (mm, aa[i], "", 240, y - 4, 30);
    }

    // ---- Row 4 ----
    auto* vo = addSection ("VOICE", 484, 734, 400, 160);
    addKnob (vo, "t2_voice_poly", "Poly", 47, 58, 36);
    addKnob (vo, "t2_voice_detune", "Detune", 137, 58, 36);
    addKnob (vo, "t2_voice_portamento", "Porta", 227, 58, 36);
    addKnob (vo, "t2_voice_vib", "Vib", 317, 58, 36);

    }

    buildParent = this;
    auto* ou = addSection ("OUTPUT", 890, 734, 382, 160);
    addKnob (ou, "output_level", "Level", 8, 50, 52);
    addKnob (ou, "master_tune", "MTune", 322, 58, 36, 56);
    auto* meterView = new MeterView (proc);
    meterViews.push_back (meterView);
    ou->addAndMakeVisible (meterView);
    owned.emplace_back (meterView);
    meterView->setBounds (76, 58, 232, 60);

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
}

void FrogContent::refreshWaveLabel (juce::Label& lb, const juce::String& digiPid)
{
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
        if ((juce::int32) (juce::Time::getMillisecondCounter() - statusHoldMs) < 0)
            t = statusText;
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

void FrogContent::showStatus (const juce::String& t)
{
    statusText = t;
    statusHoldMs = juce::Time::getMillisecondCounter() + 3000;
    waveNameLabel.setText (t, juce::dontSendNotification);
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
                showStatus ("Read failed: " + file.getFileName());
                return;
            }
            int n = (int) juce::jmin<int64_t> (reader->lengthInSamples, 1 << 20);
            juce::AudioBuffer<float> buf (1, n);
            reader->read (&buf, 0, n, 0, true, false);
            if (proc.importUserWave (buf.getReadPointer (0), n, file.getFileNameWithoutExtension()))
                selectUserWave (timbre, osc);
            else
                showStatus ("Import failed: " + file.getFileName());
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
                showStatus ("No waves found in folder");
        });
}

void FrogContent::paint (juce::Graphics& g)
{
    g.fillAll (NanoColors::bg);
    g.setColour (NanoColors::header);
    g.fillRect (0, 0, getWidth(), 46);
    g.setColour (NanoColors::border);
    g.drawLine (0, 46, (float) getWidth(), 46, 1.0f);
}

void FrogContent::resized()
{
    if (secLayout.empty()) return;
    for (auto& [g, r] : secLayout) g->setBounds (r);
    if (browser != nullptr) browser->setBounds (getLocalBounds());
    presetLabel.setBounds (8, 2, 190, 42);
    presetLabel.setFont (look.logoFont (28.0f));
    prevBtn.setBounds (164, 11, 28, 24);
    nextBtn.setBounds (194, 11, 28, 24);
    presetBox.setBounds (228, 11, 240, 24);
    browseBtn.setBounds (472, 11, 72, 24);
    mutateBtn.setBounds (837, 11, 100, 24);
}

void FrogContent::timerCallback()
{
    refreshValues();
    refreshStrips();
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
}

NanoFrogEditor::~NanoFrogEditor() {}

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
