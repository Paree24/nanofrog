// Headless verification for NanoFrog (LESSONS.md #2, #12, #17, #21):
//  - editor layout: window size, 14 sections non-empty and contained,
//    every control inside its section (numeric, no eyeballing)
//  - editor paints to an image without crashing
//  - preset round-trip: setCurrentProgram -> params equal the bank table
//    (epsilon compare, never float ==)
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>
#include <set>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; printf ("FAIL: "); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static bool rectInside (juce::Rectangle<int> inner, juce::Rectangle<int> outer)
{
    return outer.contains (inner);
}

// ---- audio feature dump (NANOFROG_DUMP_FEATURES=out.csv): renders every
// factory preset (C4 held 1 s + 0.5 s release) and a MUTATE-ed copy, writes
// 24 log-band spectra for attack/sustain/release + a 15-point RMS envelope.
// Used to measure preset similarity by sound rather than by parameters.
static std::vector<float> renderFeatures (NanoFrogProcessor& px)
{
    srand (1);
    const int bs = 512, nBlocks = 130, offBlock = 86;
    std::vector<float> mono;
    juce::AudioBuffer<float> buf (2, bs);
    for (int b = 0; b < nBlocks; ++b)
    {
        juce::MidiBuffer m;
        if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        if (b == offBlock) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        px.processBlock (buf, m);
        for (int i = 0; i < bs; ++i) mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
    }
    const int N = 2048, hop = 1024;
    juce::dsp::FFT fft (11);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann);
    const int nb = 24;
    double edges[nb + 1];
    for (int k = 0; k <= nb; ++k) edges[k] = 40.0 * std::pow (16000.0 / 40.0, k / (double) nb);
    double seg[3][nb] = {}; int segN[3] = {};
    std::vector<float> fr ((size_t) N * 2);
    for (int st = 0; st + N <= (int) mono.size(); st += hop)
    {
        double t = (st + N / 2) / 44100.0;
        int sgi = t < 0.15 ? 0 : (t >= 0.3 && t < 1.0 ? 1 : (t >= 1.0 ? 2 : -1));
        if (sgi < 0) continue;
        std::fill (fr.begin(), fr.end(), 0.0f);
        std::copy (mono.begin() + st, mono.begin() + st + N, fr.begin());
        win.multiplyWithWindowingTable (fr.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (fr.data());
        for (int bin = 1; bin < N / 2; ++bin)
        {
            double f = bin * 44100.0 / N;
            for (int k = 0; k < nb; ++k)
                if (f >= edges[k] && f < edges[k + 1]) { seg[sgi][k] += (double) fr[(size_t) bin] * fr[(size_t) bin]; break; }
        }
        ++segN[sgi];
    }
    std::vector<float> out;
    for (int sg = 0; sg < 3; ++sg)
        for (int k = 0; k < nb; ++k)
            out.push_back ((float) (10.0 * std::log10 (seg[sg][k] / juce::jmax (1, segN[sg]) + 1e-10)));
    const int env = 15, len = (int) mono.size() / env;
    for (int e = 0; e < env; ++e)
    {
        double a = 0;
        for (int i = e * len; i < (e + 1) * len; ++i) a += (double) mono[(size_t) i] * mono[(size_t) i];
        out.push_back ((float) (10.0 * std::log10 (a / len + 1e-10)));
    }
    return out;
}

static int dumpFeatures (const char* path)
{
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    int n = PresetBank::count();
    for (int idx = 0; idx < n; ++idx)
    {
        for (int mut = 0; mut < 2; ++mut)
        {
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            px->loadFactoryPreset (idx);
            if (mut)
            {
                px->mutateCurrentPatch();
                auto vals = PresetBank::capture (px->apvts);
                px = std::make_unique<NanoFrogProcessor>(); // fresh voice state
                px->prepareToPlay (44100.0, 512);
                PresetBank::apply (px->apvts, vals);
            }
            auto feats = renderFeatures (*px);
            const auto& pr = PresetBank::get (idx);
            os << idx << "\t" << pr.name << "\t" << (pr.tags.isEmpty() ? "" : pr.tags[0]) << "\t" << mut;
            for (auto v : feats) os << "\t" << juce::String (v, 2);
            os << "\n";
        }
    }
    printf ("features: %d presets -> %s\n", n, path);
    return 0;
}

// ---- pitch dump (NANOFROG_DUMP_PITCH=out.tsv): every factory preset at
// C3 and E4, 16k-point spectrum of 0.10-0.47 s, top spectral peaks (Hz, dB,
// parabolic-interpolated) + RMS level. Verifies presets stay in key.
static int dumpPitch (const char* path)
{
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    const int N = 16384, bs = 512;
    juce::dsp::FFT fft (14);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann);
    int n = PresetBank::count();
    for (int idx = 0; idx < n; ++idx)
        for (int note : { 48, 64 })
        {
            srand (1);
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, bs);
            px->loadFactoryPreset (idx);
            std::vector<float> mono;
            juce::AudioBuffer<float> buf (2, bs);
            for (int b = 0; b < 48; ++b) // ~0.56 s held
            {
                juce::MidiBuffer m;
                if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                px->processBlock (buf, m);
                for (int i = 0; i < bs; ++i) mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
            }
            const int st = 4410;
            double rms = 0;
            for (int i = st; i < st + N; ++i) rms += (double) mono[(size_t) i] * mono[(size_t) i];
            rms = std::sqrt (rms / N);
            std::vector<float> fr ((size_t) N * 2, 0.0f);
            std::copy (mono.begin() + st, mono.begin() + st + N, fr.begin());
            win.multiplyWithWindowingTable (fr.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (fr.data());
            std::vector<double> db ((size_t) N / 2);
            double mx = -300;
            for (int k = 0; k < N / 2; ++k) { db[(size_t) k] = 20.0 * std::log10 (fr[(size_t) k] + 1e-12); mx = std::max (mx, db[(size_t) k]); }
            std::vector<std::pair<double, double>> peaks; // (dB, Hz)
            for (int k = 2; k < N / 2 - 1; ++k)
            {
                double hz = k * 44100.0 / N;
                if (hz < 25.0 || hz > 4000.0) continue;
                if (db[(size_t) k] > db[(size_t) k - 1] && db[(size_t) k] >= db[(size_t) k + 1] && db[(size_t) k] > mx - 40.0)
                {
                    double a = db[(size_t) k - 1], b = db[(size_t) k], c = db[(size_t) k + 1];
                    double off = 0.5 * (a - c) / (a - 2 * b + c);
                    peaks.push_back ({ b - 0.25 * (a - c) * off, (k + off) * 44100.0 / N });
                }
            }
            std::sort (peaks.begin(), peaks.end(), [] (auto& x, auto& y) { return x.first > y.first; });
            const auto& pr = PresetBank::get (idx);
            os << idx << "\t" << pr.name << "\t" << (pr.tags.isEmpty() ? "" : pr.tags[0]) << "\t" << note
               << "\t" << juce::String (rms, 6);
            for (size_t k = 0; k < std::min<size_t> (16, peaks.size()); ++k)
                os << "\t" << juce::String (peaks[k].second, 3) << ":" << juce::String (peaks[k].first - mx, 1);
            os << "\n";
        }
    printf ("pitch: %d presets -> %s\n", n, path);
    return 0;
}

// first factory preset carrying a tag (the bank is generated: names may change)
static int firstWithTag (const juce::String& tag)
{
    for (int i = 0; i < PresetBank::count(); ++i)
        if (PresetBank::get (i).tags.contains (tag)) return i;
    return -1;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    setvbuf (stdout, nullptr, _IONBF, 0); // unbuffered: CI logs survive crashes
    printf ("-- NanoFrog headless test --\n");
    if (const char* dump = std::getenv ("NANOFROG_DUMP_FEATURES"))
        return dumpFeatures (dump); // bank from NANOFROG_FACTORY_JSON as given
    if (const char* dump = std::getenv ("NANOFROG_DUMP_PITCH"))
        return dumpPitch (dump);

    // Point the bank at the repo JSON and user presets at a temp dir.
    {
        juce::File here (__FILE__);
        juce::File json = here.getParentDirectory().getChildFile ("FactoryBank")
                              .getChildFile ("NanoFrogFactory.json");
        juce::File udir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nb_usertest");
        udir.deleteRecursively();
        juce::File learnDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("nb_midilearn");
        learnDir.deleteRecursively();
        pastel::MidiLearn::setDirectoryForTest (learnDir); // never the user's ~/.config
#ifdef _WIN32
        _putenv_s ("NANOFROG_FACTORY_JSON", json.getFullPathName().toRawUTF8());
        _putenv_s ("NANOFROG_USER_DIR", udir.getFullPathName().toRawUTF8());
#else
        setenv ("NANOFROG_FACTORY_JSON", json.getFullPathName().toRawUTF8(), 1);
        setenv ("NANOFROG_USER_DIR", udir.getFullPathName().toRawUTF8(), 1);
#endif
    }

    auto proc = std::make_unique<NanoFrogProcessor>();
    proc->prepareToPlay (44100.0, 512);

    // ---- program count ----
    int nProg = proc->getNumPrograms();
    CHECK (nProg == 401, "program count %d, expected 401", nProg);
    printf ("programs: %d\n", nProg);
    CHECK (PresetBank::get (0).name == "Init",
           "preset 0 is '%s', expected Init",
           PresetBank::get (0).name.toRawUTF8());
    CHECK (PresetBank::get (1).tags.contains ("Bass") && PresetBank::get (1).tags.contains ("808"),
           "preset 1 is '%s', expected the first 808 bass",
           PresetBank::get (1).name.toRawUTF8());

    // ---- MIDI learn: global CC map + preset knob (scratch file, not ~/.config) ----
    {
        using Enc = pastel::MidiLearn::Enc;
        auto ok = [&] (bool c, const char* m) { CHECK (c, "midi learn: %s", m); if (c) printf ("midi learn ok: %s\n", m); };
        auto det = [] (std::vector<int> v) { return pastel::MidiLearn::detectEncoding (v); };
        ok (det ({ 65, 65, 65, 65 }) == Enc::offset64 && det ({ 63, 62, 63 }) == Enc::offset64, "offset-64 (64+n) encoder detected");
        ok (det ({ 1, 1, 2, 1 }) == Enc::twos && det ({ 127, 127, 126 }) == Enc::twos, "two's complement encoder detected");
        ok (det ({ 60, 61, 62, 63 }) == Enc::absolute && det ({ 0, 1, 2, 3 }) == Enc::absolute
                && det ({ 127, 126, 125, 124 }) == Enc::absolute, "turned absolute knob stays absolute");

        auto cc = [] (int num, int val) { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, num, val), 0); return m; };
        auto a = std::make_unique<NanoFrogProcessor>();
        auto& ml = a->midiLearn;
        ml.clearAll();
        auto* cut = a->apvts.getParameter ("filter_cutoff");
        cut->setValueNotifyingHost (0.5f); // defaults fully open: leave room to turn up
        ml.setLearning (true);
        cut->beginChangeGesture(); cut->endChangeGesture(); // touching the knob on screen arms it
        ok (ml.getArmedName() == cut->getName (40), "touching a control arms it");
        const float before = cut->getValue();
        for (int i = 0; i < 4; ++i) ml.process (cc (74, 65));
        ok (std::abs (cut->getValue() - before) < 1e-6f, "learning CCs never move the armed control");
        ml.tick();
        auto maps = ml.getMappings();
        ok (maps.size() == 1 && maps[0].cc == 74 && maps[0].paramId == "filter_cutoff" && maps[0].enc == Enc::offset64,
            "CC 74 learned as a relative knob on the filter cutoff");
        ml.setLearning (false);
        ml.process (cc (74, 66));
        const float up = cut->getValue();
        ml.process (cc (74, 62));
        ok (up > before && cut->getValue() < up, "endless knob turns cutoff the right way both directions");

        auto b = std::make_unique<NanoFrogProcessor>(); // another project / instance
        auto* cutB = b->apvts.getParameter ("filter_cutoff");
        cutB->setValueNotifyingHost (0.5f);
        b->midiLearn.process (cc (74, 70));
        ok (b->midiLearn.getMappings().size() == 1 && cutB->getValue() > 0.5f, "mapping is global: a new instance has it");

        ml.setLearning (true);
        cut->beginChangeGesture(); cut->endChangeGesture();
        for (int v : { 10, 11, 12, 13 }) ml.process (cc (21, v));
        ml.tick();
        maps = ml.getMappings();
        ok (maps.size() == 1 && maps[0].cc == 21 && maps[0].enc == Enc::absolute, "re-learning moves the control to the new knob");
        ml.setLearning (false);
        ml.process (cc (21, 127));
        ok (std::abs (cut->getValue() - 1.0f) < 1e-6f, "absolute knob sets the value directly");

        int stepped = 0;
        ml.onPresetStep = [&] (int d) { stepped += d; };
        ml.armPresetKnob();
        ml.process (cc (114, 1));
        for (int i = 0; i < 10; ++i) ml.tick();
        ok (ml.getPresetKnobCC() == 114, "preset knob learned from one nudge");
        ml.process (cc (114, 1)); ml.process (cc (114, 3));
        ml.tick();
        ml.process (cc (114, 127));
        ml.tick();
        ok (stepped == 1, "preset knob: two steps forward, one back");
        ml.onPresetStep = nullptr;

        auto c = std::make_unique<NanoFrogProcessor>();
        const int n = c->getNumPrograms(), prog = c->getCurrentProgram();
        c->midiLearn.process (cc (114, 1));
        c->midiLearn.tick();
        ok (c->getCurrentProgram() == ((prog + 1) % n + n) % n, "preset knob loads the next program in a new instance");
        c->midiLearn.process (cc (114, 127)); c->midiLearn.process (cc (114, 127));
        c->midiLearn.tick();
        ok (c->getCurrentProgram() == ((prog - 1) % n + n) % n, "preset knob steps back and wraps like the < button");
        ml.clearAll();
        ok (ml.getMappings().empty() && ml.getPresetKnobCC() < 0, "clear all");
    }

    // ---- editor layout ----
    NanoFrogEditor* outer = dynamic_cast<NanoFrogEditor*> (proc->createEditor());
    CHECK (outer != nullptr, "editor is null");
    if (outer == nullptr) return 1;
    FrogContent& edref = outer->getContent();
    FrogContent* ed = &edref;
    CHECK (outer->getWidth() == 1280 && outer->getHeight() == 900,
           "editor size %dx%d, expected 1280x900", outer->getWidth(), outer->getHeight());
    // v3 layout: 11 per timbre page + ARPEGGIATOR, DRIVE, SATURATION, MOD FX, DELAY,
    // REVERB, EQ, OUTPUT (scope inside OUTPUT)
    CHECK (ed->getSectionCount() == 30, "sections %d, expected 30", ed->getSectionCount());

    // recursive geometry: every descendant non-empty and inside its parent
    // (scrollable containers like ListBox legitimately overflow: skipped)
    std::function<void (juce::Component*, const std::string&)> checkTree =
        [&] (juce::Component* c, const std::string& path)
    {
        for (int i = 0; i < c->getNumChildComponents(); ++i)
        {
            auto* g = c->getChildComponent (i);
            juce::Rectangle<int> local = g->getBounds();
            std::string p = path + "/" + std::to_string (i);
            if (dynamic_cast<juce::ListBox*> (c) != nullptr
                || dynamic_cast<juce::Viewport*> (c) != nullptr
                || dynamic_cast<juce::ScrollBar*> (c) != nullptr)
                continue;
            CHECK (! local.isEmpty(), "%s (%s) has empty bounds",
                   p.c_str(), g->getName().toRawUTF8());
            CHECK (c->getLocalBounds().contains (local), "%s %s outside parent",
                   p.c_str(), local.toString().toRawUTF8());
            if (g->getNumChildComponents() > 0) checkTree (g, p);
        }
    };
    checkTree (ed, "content");
    // sibling sections must never overlap (AMP once sat 14 px under EFFECTS)
    std::function<void (juce::Component*)> checkOverlap = [&] (juce::Component* c)
    {
        std::vector<juce::Component*> groups;
        for (int i = 0; i < c->getNumChildComponents(); ++i)
        {
            auto* g = c->getChildComponent (i);
            if (dynamic_cast<juce::GroupComponent*> (g) != nullptr) groups.push_back (g);
            else if (g->getNumChildComponents() > 0
                     && dynamic_cast<juce::GroupComponent*> (g) == nullptr) checkOverlap (g);
        }
        for (size_t a = 0; a < groups.size(); ++a)
            for (size_t b = a + 1; b < groups.size(); ++b)
                CHECK (! groups[a]->getBounds().intersects (groups[b]->getBounds()),
                       "sections '%s' and '%s' overlap",
                       groups[a]->getName().toRawUTF8(), groups[b]->getName().toRawUTF8());
    };
    checkOverlap (ed);
    // pages + tabs (2 timbre pages; mix is a header knob)
    CHECK (ed->isPageVisible (0), "page 0 not visible initially");
    CHECK (! ed->isPageVisible (1), "page 1 visible initially");
    ed->selectPage (1);
    CHECK (! ed->isPageVisible (0), "page 0 still visible after select 1");
    CHECK (ed->isPageVisible (1), "page 1 not visible after select 1");
    ed->selectPage (0);
    CHECK (ed->isPageVisible (0), "page 0 not visible after reselect");
    printf ("pages/tabs: ok\n");
    CHECK (ed->getPresetBox().getNumItems() == nProg,
           "preset box items %d, expected %d", ed->getPresetBox().getNumItems(), nProg);

    // ---- editor paints without crashing ----
    juce::Rectangle<int> win = ed->getLocalBounds();
    juce::Image snap = ed->createComponentSnapshot (win);
    CHECK (snap.isValid(), "editor snapshot invalid");
    printf ("editor snapshot: %dx%d\n", snap.getWidth(), snap.getHeight());
    {
        juce::File f ("/tmp/nanofrog_ui.png");
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat png;
        CHECK (png.writeImageToStream (snap, fos), "failed to write UI snapshot");
        printf ("ui snapshot: %s\n", f.getFullPathName().toRawUTF8());
    }
    ed->selectPage (1);
    {
        juce::Image s2 = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::File f ("/tmp/nanofrog_ui_t2.png");
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (s2, fos);
        ed->selectPage (0);
    }

    // ---- browser smoke: opens, rows match bank, paints, closes ----
    {
        CHECK (ed->getBrowser() != nullptr, "browser missing");
        CHECK (! ed->getBrowser()->isVisible(), "browser visible initially");
        ed->openBrowser();
        CHECK (ed->getBrowser()->isVisible(), "browser did not open");
        CHECK (ed->getBrowser()->getRowCount() == nProg,
               "browser rows %d, expected %d", ed->getBrowser()->getRowCount(), nProg);
        juce::Image sb = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (sb.isValid(), "browser snapshot invalid");
        juce::File f ("/tmp/nanofrog_ui_browser.png");
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat png;
        CHECK (png.writeImageToStream (sb, fos), "failed to write browser snapshot");
        ed->closeBrowser();
        CHECK (! ed->getBrowser()->isVisible(), "browser did not close");
        printf ("browser: ok (%d rows)\n", nProg);
    }

    // ---- parameter shape checks ----
    if (auto* dg = dynamic_cast<juce::AudioParameterChoice*> (proc->apvts.getParameter ("osc1_digital")))
        CHECK (dg->choices.size() == 192, "osc1_digital choices %d, expected 192", dg->choices.size());
    else { CHECK (false, "osc1_digital is not a choice"); }
    if (auto* ow = dynamic_cast<juce::AudioParameterChoice*> (proc->apvts.getParameter ("osc1_wave")))
        CHECK (ow->choices.size() == 7, "osc1_wave choices %d, expected 7", ow->choices.size());
    else { CHECK (false, "osc1_wave is not a choice"); }
    if (auto* ls = dynamic_cast<juce::AudioParameterChoice*> (proc->apvts.getParameter ("lfo1_sync")))
        CHECK (ls->choices.size() == 16, "lfo1_sync choices %d, expected 16", ls->choices.size());
    else { CHECK (false, "lfo1_sync is not a choice"); }

    {
        float src[4096];
        for (int i = 0; i < 4096; ++i) src[i] = std::sin (i * 6.2831853f / (4096.0f / 3.0f));
        float cyc[NanoFrogProcessor::kUserLen];
        CHECK (NanoFrogProcessor::makeSingleCycle (src, 4096, cyc), "makeSingleCycle failed");
        float pk = 0.0f, mean = 0.0f;
        for (int i = 0; i < NanoFrogProcessor::kUserLen; ++i)
        {
            pk = juce::jmax (pk, std::abs (cyc[i]));
            mean += cyc[i];
        }
        mean /= NanoFrogProcessor::kUserLen;
        CHECK (std::abs (pk - 0.9f) < 0.01f, "user cycle peak %.4f, want 0.9", pk);
        CHECK (std::abs (mean) < 1e-4f, "user cycle mean %.6f, want ~0", mean);
        CHECK (std::abs (cyc[0] - cyc[NanoFrogProcessor::kUserLen - 1]) < 1e-6f,
               "user cycle seam not closed");
        CHECK (proc->importUserWave (src, 4096, "TestSine"), "importUserWave failed");
        CHECK (proc->hasUserWave(), "user wave not flagged loaded");
        CHECK (proc->getUserWaveName() == "TestSine", "user wave name not stored");
    }

    // ---- preset round-trip (epsilon, LESSONS.md #21) ----
    for (const char* pid : { "osc1_octave", "mod1_src", "filter_type", "bogus_id" })
        printf ("  lookup %-12s -> %s\n", pid, proc->apvts.getParameter (pid) ? "found" : "NULL");
    // Direct-conversion probe (no async, no editor involvement).
    if (auto* p = proc->apvts.getParameter ("mix_o1"))
    {
        p->setValueNotifyingHost (p->convertTo0to1 (0.7874f));
        float raw = proc->apvts.getRawParameterValue ("mix_o1")->load();
        printf ("  direct probe mix_o1: raw=%.6f norm=%.6f (want raw 0.787400)\n",
                raw, p->getValue());
    }
    const char* const* kIds = PresetBank::kParamIds; // canonical order (bank files map by id)
    constexpr int kNIds = PresetBank::kParamCount;
    auto checkPreset = [&] (int idx) -> int
    {
        CHECK (proc->getCurrentProgram() == idx, "current program %d after set %d",
               proc->getCurrentProgram(), idx);
        const auto& pr = PresetBank::get (idx);
        int mism = 0;
        for (int k = 0; k < kNIds; ++k)
        {
            float actual = 0.0f;
            if (auto* pv = proc->apvts.getRawParameterValue (kIds[k])) actual = pv->load();
            if (! std::isfinite (pr.values[k])) continue; // not in the bank file: default
            if (std::abs (actual - pr.values[k]) > 1e-3f)
            {
                if (mism < 5)
                    printf ("  mismatch preset %d param %s: got %.4f want %.4f\n",
                            idx, kIds[k], actual, pr.values[k]);
                ++mism;
            }
        }
        CHECK (mism == 0, "preset %d (%s): %d params mismatch", idx, pr.name.toRawUTF8(), mism);
        printf ("preset %3d (%s): round-trip %s\n", idx, pr.name.toRawUTF8(), mism == 0 ? "ok" : "FAILED");
        return mism;
    };

    // ---- full-bank round-trip: every preset converts exactly ----
    // (synchronous; the async deferral path itself is covered above)
    {
        int bad = 0;
        for (int idx = 0; idx < nProg; ++idx)
        {
            const auto& pr = PresetBank::get (idx);
            for (int k = 0; k < kNIds; ++k)
                if (auto* p = proc->apvts.getParameter (kIds[k]); p != nullptr && std::isfinite (pr.values[k]))
                    p->setValueNotifyingHost (p->convertTo0to1 (pr.values[k]));
            for (int k = 0; k < kNIds; ++k)
            {
                float actual = 0.0f;
                if (auto* pv = proc->apvts.getRawParameterValue (kIds[k])) actual = pv->load();
                if (! std::isfinite (pr.values[k])) continue;
                if (std::abs (actual - pr.values[k]) > 1e-3f && bad < 5)
                    printf ("  bank mismatch preset %d param %s: got %.4f want %.4f\n",
                            idx, kIds[k], actual, pr.values[k]);
                if (std::abs (actual - pr.values[k]) > 1e-3f) ++bad;
            }
        }
        CHECK (bad == 0, "bank round-trip: %d mismatches", bad);
        printf ("bank round-trip: %d presets ok\n", nProg);
    }

    // ---- tags + filter (browser model) ----
    {
        CHECK (! PresetBank::isFallback(), "factory JSON did not load (fallback active)");
        int growlIdx = firstWithTag ("Growl");
        CHECK (growlIdx >= 0, "no Growl preset in the bank");
        if (growlIdx >= 0)
            CHECK (PresetBank::findByName (PresetBank::get (growlIdx).name) == growlIdx,
                   "findByName misses %s", PresetBank::get (growlIdx).name.toRawUTF8());
        auto wob = PresetBank::filter ("wobble", "All");
        CHECK (! wob.empty(), "filter 'wobble' empty");
        for (int id : wob)
        {
            auto& pr = PresetBank::get (id);
            bool ok = pr.name.toLowerCase().contains ("wobble");
            for (auto& t : pr.tags) if (t.toLowerCase().contains ("wobble")) ok = true;
            if (! ok) { CHECK (false, "filter 'wobble' hit unrelated preset %s", pr.name.toRawUTF8()); break; }
        }
        auto bass = PresetBank::filter ("", "Bass");
        CHECK (! bass.empty(), "Bass category empty");
        for (int id : bass)
            if (! PresetBank::get (id).tags.contains ("Bass"))
            { CHECK (false, "Bass filter leaked non-bass"); break; }
        auto none = PresetBank::filter ("zzz-no-such-preset", "All");
        CHECK (none.empty(), "nonsense query returned rows");
        auto users0 = PresetBank::filter ("", "User");
        CHECK (users0.empty(), "User bank not empty initially");
        printf ("tags/filter: wobble=%d bass=%d\n", (int) wob.size(), (int) bass.size());
    }

    // ---- user preset save/load/delete + state persistence ----
    {
        juce::File savedFile = PresetBank::saveUser ("Test Wobble", { "User", "Bass" },
                                                       PresetBank::capture (proc->apvts));
        CHECK (savedFile.existsAsFile(), "saveUser failed");
        auto users = PresetBank::scanUser();
        CHECK (users.size() == 1, "scanUser found %d, expected 1", (int) users.size());
        // change state, then load the user preset and confirm values return
        if (auto* p = proc->apvts.getParameter ("filter_cutoff"))
            p->setValueNotifyingHost (0.0f);
        CHECK (proc->loadUserPreset (users[0].file), "loadUserPreset failed");
        CHECK (proc->getCurrentProgram() == -1, "user load did not enter user mode");
        CHECK (proc->getCurrentUserPreset() == users[0].file.getFileName(),
               "user name not tracked");
        ed->openBrowser();
        CHECK (ed->getBrowser()->getRowCount() == nProg + 1,
               "browser All rows %d, expected %d", ed->getBrowser()->getRowCount(), nProg + 1);
        ed->closeBrowser();
        // state round-trips the user mode (message thread: applied immediately)
        juce::MemoryBlock blob;
        proc->getStateInformation (blob);
        auto proc3 = std::make_unique<NanoFrogProcessor>();
        proc3->setStateInformation (blob.getData(), (int) blob.getSize());
        CHECK (proc3->getCurrentProgram() == -1, "restored proc not in user mode");
        CHECK (proc3->getCurrentUserPreset() == users[0].file.getFileName(),
               "restored user name mismatch");
        float a = 0.0f, b = 0.0f;
        if (auto* pv = proc->apvts.getRawParameterValue ("filter_cutoff")) a = pv->load();
        if (auto* pv = proc3->apvts.getRawParameterValue ("filter_cutoff")) b = pv->load();
        CHECK (std::abs (a - b) < 1e-6f, "user state values diverged");
        CHECK (PresetBank::deleteUser (users[0].file), "deleteUser failed");
        CHECK (PresetBank::scanUser().empty(), "user preset not deleted");
        printf ("user presets: save/load/state/delete ok\n");
    }

    // ---- browser delete path (DAW crash regression): real handler, real
    // dispatch, several user rows so the users vector reallocates ----
    {
        for (int i = 0; i < 6; ++i)
        {
            juce::File f = PresetBank::saveUser ("DelTest " + juce::String (i),
                                                 { "User", "Bass" },
                                                 PresetBank::capture (proc->apvts));
            CHECK (f.existsAsFile(), "DelTest save %d failed", i);
        }
        ed->openBrowser();
        auto* br = ed->getBrowser();
        int before = br->getRowCount();
        CHECK (before == nProg + 6, "browser rows %d, expected %d", before, nProg + 6);
        // last row is a user preset (All category appends users at the end)
        br->selectRowSync (before - 1);
        CHECK (proc->getCurrentProgram() == -1, "row load did not enter user mode");
        // Synchronous handler bodies (no message-queue timing involved).
        CHECK (br->testDeleteSelected(), "browser delete handler failed");
        CHECK (br->getRowCount() == before - 1,
               "browser rows after del %d, expected %d", br->getRowCount(), before - 1);
        CHECK ((int) PresetBank::scanUser().size() == 5,
               "user files after del %d, expected 5",
               (int) PresetBank::scanUser().size());
        for (auto& u : PresetBank::scanUser()) PresetBank::deleteUser (u.file);
        ed->closeBrowser();
        printf ("browser delete: ok (no crash, rows/files consistent)\n");
    }

    // ---- drumsynth-style save: inline name, armed overwrite, factory shadows ----
    {
        auto* br = ed->getBrowser();
        ed->openBrowser();
        auto setCut = [&] (float v)
        {
            if (auto* p = proc->apvts.getParameter ("filter_cutoff"))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        auto rawCut = [&] { return proc->apvts.getRawParameterValue ("filter_cutoff")->load(); };
        setCut (0.25f);
        br->testSetName ("Save Twice");
        br->testClickSave();
        CHECK (PresetBank::userFileFor ("Save Twice").existsAsFile(), "inline save failed");
        CHECK (proc->getCurrentUserPreset() == "Save Twice.nbpreset", "saved preset not current");
        setCut (0.75f);
        br->testSetName ("Save Twice");
        br->testClickSave(); // existing name: arms only
        CHECK (br->testOverwriteArmed(), "existing name did not arm OVERWRITE?");
        CHECK ((int) PresetBank::scanUser().size() == 1, "armed save wrote a file");
        br->testClickSave(); // confirm
        CHECK (! br->testOverwriteArmed(), "overwrite stayed armed");
        CHECK ((int) PresetBank::scanUser().size() == 1, "overwrite created a duplicate");
        NanoPreset np;
        PresetBank::readUser (PresetBank::userFileFor ("Save Twice"), np);
        float savedCut = np.values[(size_t) (std::find_if (PresetBank::kParamIds,
            PresetBank::kParamIds + PresetBank::kParamCount,
            [] (const char* id) { return juce::String (id) == "filter_cutoff"; }) - PresetBank::kParamIds)];
        CHECK (std::abs (savedCut - 0.75f) < 1e-3f, "overwrite kept old values (%.3f)", savedCut);
        for (auto& u : PresetBank::scanUser()) PresetBank::deleteUser (u.file);

        // factory overwrite -> shadow; reload gives new values; DEL restores
        const int slot = 7;
        float shippedCut = PresetBank::get (slot).values[0];
        juce::ignoreUnused (shippedCut);
        proc->loadFactoryPreset (slot);
        float orig = rawCut();
        setCut (orig < 0.5f ? 0.9f : 0.1f);
        float edited = rawCut();
        ed->closeBrowser(); ed->openBrowser();
        br->testShowCategory ("All"); // save switched to User; factory rows first
        br->selectRowSync (slot);
        setCut (edited); // selecting reloaded the slot: re-apply the edit
        CHECK (br->testOverwriteSelected(), "factory overwrite failed");
        CHECK (PresetBank::get (slot).shadowed, "factory slot not shadowed");
        CHECK (PresetBank::factoryShadowDir().findChildFiles (juce::File::findFiles, false).size() == 1,
               "shadow file not written");
        setCut (orig);
        proc->loadFactoryPreset (slot);
        CHECK (std::abs (rawCut() - edited) < 1e-3f, "shadowed slot did not load the overwrite");
        br->selectRowSync (slot);
        CHECK (br->testDeleteSelected(), "factory restore failed");
        CHECK (! PresetBank::get (slot).shadowed, "slot still shadowed after DEL");
        proc->loadFactoryPreset (slot);
        CHECK (std::abs (rawCut() - orig) < 1e-3f, "DEL did not restore shipped values");
        CHECK (PresetBank::count() == nProg, "shadowing changed the program count");
        ed->closeBrowser();
        printf ("inline save / armed overwrite / factory shadow+restore: ok\n");
    }

    // Drive everything needing the message loop from inside ONE dispatch run
    // (runDispatchLoop is one-shot): preset changes, then folder/state test.
    struct ProgDriver : juce::Timer
    {
        NanoFrogProcessor& proc;
        std::function<int(int)> check;
        std::function<void()> folderStage;
        std::function<void()> verifyStage;
        std::function<void()> edDriveAsync;
        std::function<void()> edVerifyAsync;
        int stage = 0;
        int seq[2];
        ProgDriver (NanoFrogProcessor& p, std::function<int(int)> c, std::function<void()> f,
                    std::function<void()> v, std::function<void()> d, std::function<void()> e)
            : proc (p), check (c), folderStage (f), verifyStage (v), edDriveAsync (d), edVerifyAsync (e) { seq[0] = 0; seq[1] = 1; }
        void timerCallback() override
        {
            if (stage == 0) proc.setCurrentProgram (seq[0]);
            else if (stage <= 2)
            {
                check (seq[stage - 1]);
                if (stage < 2) proc.setCurrentProgram (seq[stage]);
                else folderStage();
            }
            else if (stage == 3)
            {
                // folder rescan (deferred by setStateInformation) has dispatched
                verifyStage();
                edDriveAsync();
            }
            else if (stage == 4)
            {
                edVerifyAsync();
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
            ++stage;
        }
    };
    // Folder-bank test with synthetic wavs only (no external files): create,
    // import, verify, save state, restore into a second processor, verify
    // the remembered folder path rescans.
    juce::File tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("nb_banktest");
    std::unique_ptr<NanoFrogProcessor> proc2;
    auto folderStage = [&]
    {
        tmpDir.deleteRecursively();
        tmpDir.createDirectory();
        for (int f = 0; f < 3; ++f)
        {
            juce::AudioBuffer<float> b (1, 4096);
            for (int i = 0; i < 4096; ++i)
                b.setSample (0, i, std::sin (i * 6.2831853f * (f + 1) * 8.0f / 4096.0f));
            juce::File wav = tmpDir.getChildFile ("wave" + juce::String (f) + ".wav");
            if (auto fos = wav.createOutputStream())
            {
                juce::WavAudioFormat fmt;
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                        fmt.createWriterFor (fos.release(), 44100.0, 1, 16, {}, 0)))
                    w->writeFromAudioSampleBuffer (b, 0, 4096);
            }
        }
        int loaded = proc->importUserFolder (tmpDir.getFullPathName());
        CHECK (loaded == 3, "folder import loaded %d, expected 3", loaded);
        CHECK (proc->getUserBankCount() == 3, "bank count %d, expected 3",
               proc->getUserBankCount());
        CHECK (proc->getUserBankEntryName (0) == "wave0", "bank entry 0 name '%s'",
               proc->getUserBankEntryName (0).toRawUTF8());
        juce::MemoryBlock blob;
        proc->getStateInformation (blob);
        {
            std::unique_ptr<juce::XmlElement> xml (
                juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize()));
            CHECK (xml != nullptr, "state blob has no XML");
            if (xml != nullptr)
                CHECK (xml->getStringAttribute ("userFolderPath") == tmpDir.getFullPathName(),
                       "state did not remember folder path");
        }
        proc2 = std::make_unique<NanoFrogProcessor>();
        proc2->setStateInformation (blob.getData(), (int) blob.getSize());
        // deferred rescan lands on the next tick (verifyStage checks it)
    };
    auto verifyStage = [&]
    {
        CHECK (proc2 != nullptr, "proc2 missing");
        if (proc2 != nullptr)
        {
            CHECK (proc2->getUserBankCount() == 3, "restored bank count %d, expected 3",
                   proc2->getUserBankCount());
            CHECK (proc2->getUserFolderPath() == tmpDir.getFullPathName(),
                   "restored folder path not remembered");
            printf ("folder rescan after state restore: ok (%d waves)\n",
                    proc2->getUserBankCount());
        }
        tmpDir.deleteRecursively();
    };
    {
        ProgDriver driver (*proc, checkPreset, folderStage, verifyStage,
                                 [&] { ed->driveAsyncControls(); },
                                 [&] { CHECK (ed->verifyAsyncControls(), "async control check failed"); });
        driver.startTimer (80);
        juce::MessageManager::getInstance()->runDispatchLoop();
        driver.stopTimer();
    }

    // ---- multitimbral smoke: both banks render, mixer balances ----
    // (fresh processor: neutral defaults for both timbres, no program involved)
    {
        auto px = std::make_unique<NanoFrogProcessor>();
        px->prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px->apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        auto renderNote = [&]
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b) px->processBlock (buf, midi);
            double acc = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) acc += buf.getSample (ch, i) * buf.getSample (ch, i);
            return std::sqrt (acc / 1024);
        };
        double r1 = renderNote(); // defaults: tmix=0 -> T1 audible, T2 muted
        CHECK (r1 > 0.005, "T1-only render silent (rms %.5f)", r1);
        setP ("tmix", 1.0f);
        setP ("t2_osc1_octave", 1.0f); // distinct from T1: proves independence
        double r2 = renderNote();
        CHECK (r2 > 0.005, "T2-only render silent (rms %.5f)", r2);
        setP ("tmix", 0.5f);
        double rBoth = renderNote();
        CHECK (rBoth > r1 * 0.9, "mix render too quiet (%.5f vs %.5f)", rBoth, r1);
        printf ("multitimbral render: T1=%.4f T2=%.4f both=%.4f\n", r1, r2, rBoth);
    }

    // ---- arp-off releases ringing arp voices (stuck-note regression) ----
    {
        auto px = std::make_unique<NanoFrogProcessor>();
        px->prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px->apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setP ("arp_on", 1.0f);
        setP ("arp_rate", 2.0f); // 1/16
        setP ("song_tempo", 120.0f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 44; ++b) px->processBlock (buf, midi); // ~0.5 s arp runs
        double hot = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) hot += buf.getSample (ch, i) * buf.getSample (ch, i);
        CHECK (hot > 1e-6, "arp did not sound (energy %.6f)", hot);
        setP ("arp_on", 0.0f);
        juce::MidiBuffer empty;
        for (int b = 0; b < 175; ++b) px->processBlock (buf, empty); // ~2 s decay
        double tail = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) tail += buf.getSample (ch, i) * buf.getSample (ch, i);
        CHECK (tail < 1e-8, "arp-off left ringing voices (energy %.6f)", tail);
        printf ("arp-off release: ok (tail %.2e)\n", tail);
    }

    // ---- arp latch keeps playing after release; swing reshapes the grid;
    //      steps are sample-accurate (not block-quantized) ----
    {
        auto onsets = [&] (bool latch, float swing, std::vector<int>& out, double& lateEnergy)
        {
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px->apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("arp_on", 1.0f);
            setP ("arp_latch", latch ? 1.0f : 0.0f);
            setP ("arp_rate", 2.0f); // 1/16 @120 = 5512.5 samples
            setP ("arp_gate", 0.3f);
            setP ("arp_swing", swing);
            setP ("song_tempo", 120.0f);
            setP ("env2_r", 0.0f);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer on, off, none;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 100);
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            bool prevLoud = false; long t = 0; float env = 0.0f;
            lateEnergy = 0.0;
            for (int b = 0; b < 120; ++b)
            {
                px->processBlock (buf, b == 0 ? on : (b == 30 ? off : none));
                for (int i = 0; i < 512; ++i, ++t)
                {
                    float a = std::abs (buf.getSample (0, i));
                    env = std::max (a, env * 0.995f); // peak follower (~5 ms)
                    bool loud = prevLoud ? env > 2e-3f : env > 2e-2f; // hysteresis
                    if (loud && ! prevLoud) out.push_back ((int) t);
                    prevLoud = loud;
                    if (b >= 80) lateEnergy += a * a;
                }
            }
        };
        std::vector<int> o1, o2, o3; double e1, e2, e3;
        onsets (false, 0.0f, o1, e1);
        onsets (true, 0.0f, o2, e2);
        onsets (true, 0.6f, o3, e3);
        CHECK (e1 < 1e-8, "arp kept playing after release without latch (%.3e)", e1);
        CHECK (e2 > 1e-4, "latched arp stopped after key release (%.3e)", e2);
        // key lands at sample 100; the detector trips ~100 samples into the
        // attack. Block-quantized stepping would put it at >= 512.
        CHECK (! o2.empty() && o2[0] >= 100 && o2[0] < 300,
               "first arp step not on the key press (onset %d, want 100..300)", o2.empty() ? -1 : o2[0]);
        // Detector jitter is ~+-100 samples (waveform phase at onset);
        // block-quantized stepping would be off by up to a whole 512 block.
        bool evenGrid = o2.size() > 5
            && std::abs ((o2[5] - o2[0]) / 5.0 - 5512.5) < 40.0;
        for (size_t k = 1; evenGrid && k < 6; ++k)
            evenGrid = std::abs ((o2[k] - o2[k - 1]) - 5512) < 150;
        CHECK (evenGrid, "straight arp steps not on the 5512-sample grid");
        bool swung = o3.size() > 3
            && (o3[1] - o3[0]) > (o3[2] - o3[1]) + 2000;
        CHECK (swung, "arp swing had no effect");
        printf ("arp latch/swing/timing: ok (%d / %d onsets)\n", (int) o2.size(), (int) o3.size());
    }

    // ---- host-synced arp: with the transport playing (from mid-beat), steps
    //      count from the key in host beats, swung pairs too ----
    {
        struct FakePlayHead : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (120.0); p.setPpqPosition (ppq); p.setIsPlaying (true);
                return p;
            }
        };
        for (float swing : { 0.0f, 0.6f })
        {
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px->apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("arp_on", 1.0f); setP ("arp_rate", 2.0f); setP ("arp_gate", 0.3f);
            setP ("arp_swing", swing); setP ("env2_r", 0.0f);
            FakePlayHead ph; ph.ppq = 0.4; // 8820 samples before beat 1
            px->setPlayHead (&ph);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer on, none;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            std::vector<int> o; bool prevLoud = false; long t = 0; float env = 0.0f;
            for (int b = 0; b < 120; ++b)
            {
                px->processBlock (buf, b == 0 ? on : none);
                ph.ppq += 512.0 * 2.0 / 44100.0;
                for (int i = 0; i < 512; ++i, ++t)
                {
                    env = std::max (std::abs (buf.getSample (0, i)), env * 0.995f);
                    bool loud = prevLoud ? env > 2e-3f : env > 2e-2f;
                    if (loud && ! prevLoud) o.push_back ((int) t);
                    prevLoud = loud;
                }
            }
            px->setPlayHead (nullptr);
            // steps count from the key (sample 0): step k at k*5512.5 samples,
            // odd steps swung late by 0.5*swing of a step
            int bad = 0;
            for (int x : o)
            {
                double best = 1e9;
                for (int k = 0; k < 40; ++k)
                {
                    const double at = (k + ((k & 1) ? 0.5 * swing : 0.0)) * 5512.5;
                    if (std::abs (x - at - 100.0) < std::abs (best)) best = x - at - 100.0;
                }
                if (std::abs (best) > 150.0) ++bad;
            }
            CHECK (o.size() > 8 && bad == 0, "synced arp (swing %.1f): %d of %d onsets off the host grid", swing, bad, (int) o.size());
            CHECK (! o.empty() && o[0] < 300, "synced arp's first step not on the key (onset %d)", o.empty() ? -1 : o[0]);
            printf ("synced arp (swing %.1f): ok (%d onsets, first %d)\n", swing, (int) o.size(), o.empty() ? -1 : o[0]);
        }
    }

    // ---- short note inside a voice-steal fade must not hang ----
    {
        auto px = std::make_unique<NanoFrogProcessor>();
        px->prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px->apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setP ("voice_poly", 1.0f);
        setP ("env2_r", 0.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer a, b, none;
        a.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        b.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 10);
        b.addEvent (juce::MidiMessage::noteOff (1, 64), 30); // inside the 2.5 ms fade
        b.addEvent (juce::MidiMessage::noteOff (1, 60), 40);
        px->processBlock (buf, a);
        px->processBlock (buf, a);
        px->processBlock (buf, b);
        for (int k = 0; k < 100; ++k) px->processBlock (buf, none);
        double tail = 0;
        for (int i = 0; i < 512; ++i) tail += buf.getSample (0, i) * buf.getSample (0, i);
        CHECK (tail < 1e-8, "note released during steal fade hung (energy %.3e)", tail);
        printf ("steal-fade note-off: ok\n");
    }

    // ---- digital selector routes by index in both wave modes ----
    {
        float src[4096];
        for (int i = 0; i < 4096; ++i) src[i] = (std::sin (i * 6.2831853f * 8.0f / 4096.0f) > 0 ? 0.9f : -0.9f);
        auto render = [&] (float wave, float digi)
        {
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            CHECK (px->importUserWave (src, 4096, "T"), "user import failed");
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px->apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("osc1_wave", wave);
            setP ("osc1_digital", digi);
            setP ("filter_cutoff", 1.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b) px->processBlock (buf, midi);
            return buf;
        };
        auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            float m = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    m = juce::jmax (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
            return m;
        };
        // User mode + factory indices: different indices must sound different
        // (previously all clamped to the same bank entry).
        auto u5 = render (6.0f, 5.0f), u20 = render (6.0f, 20.0f);
        CHECK (maxDiff (u5, u20) > 0.01f, "digital combo inert in User mode");
        // USER 01 sounds identical from Digital and User wave modes.
        auto d128 = render (4.0f, 128.0f), w128 = render (6.0f, 128.0f);
        CHECK (maxDiff (d128, w128) < 1e-6f, "Digital/User USER-01 mismatch");
        printf ("digital routing: ok\n");
    }

    // ---- FM (osc2 -> osc1): amount 0 is inert, >0 reshapes the tone ----
    {
        auto renderFM = [] (float fm)
        {
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px->apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("osc1_wave", 3.0f); // sine carrier
            setP ("osc2_wave", 3.0f); // sine modulator
            setP ("osc2_level", 1.0f); setP ("mix_o2", 0.0f); // modulator only
            setP ("fm_amt", fm);
            setP ("filter_cutoff", 1.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b) px->processBlock (buf, midi);
            return buf;
        };
        auto a = renderFM (0.0f), b = renderFM (0.5f);
        float m = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                m = juce::jmax (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        CHECK (m > 0.05f, "FM amount did not reshape the tone (%.4f)", m);
        printf ("fm: ok (delta %.3f)\n", m);
    }

    // ---- limiter ceiling ----
    {
        auto px = std::make_unique<NanoFrogProcessor>();
        px->prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px->apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setP ("eq_low", 12.0f); setP ("eq_high", 12.0f);
        setP ("output_level", 1.25f);
        setP ("osc2_level", 1.0f); setP ("mix_o2", 1.0f);
        setP ("voice_detune", 0.6f); setP ("voice_poly", 1.0f); // unison stack
        setP ("limiter", 0.0f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 4; ++b) px->processBlock (buf, midi);
        float off = buf.getMagnitude (0, 512);
        setP ("limiter", 1.0f);
        for (int b = 0; b < 4; ++b) px->processBlock (buf, midi);
        float on = buf.getMagnitude (0, 512);
        CHECK (on <= 1.0f, "limiter ceiling breached (%.3f)", on);
        CHECK (on < off, "limiter did not engage (%.3f vs %.3f)", on, off);
        printf ("limiter: off=%.3f on=%.3f\n", off, on);
    }

    // ---- in-key invariant: no factory oscillator more than 25 cents off the
    // played key (or its 4th/5th/octave). Covers every detune path: fine,
    // Detune spread (+-15c*d), mono unison taps (+-25c*d), triangle Shape
    // retune (+19 st, LFO1 via PWM), ring/FM off-octave ratios, vibrato. ----
    {
        auto idxOf = [] (const juce::String& id)
        {
            for (int k = 0; k < PresetBank::kParamCount; ++k)
                if (id == PresetBank::kParamIds[k]) return k;
            return -1;
        };
        int bad = 0; double worstAll = 0.0;
        for (int idx = 0; idx < nProg; ++idx)
        {
            const auto& pr = PresetBank::get (idx);
            auto g = [&] (const juce::String& id) { return pr.values[(size_t) idxOf (id)]; };
            double worst = std::abs (g ("master_tune"));
            for (juce::String pre : { juce::String(), juce::String ("t2_") })
            {
                if (pre.isNotEmpty() && g ("tmix") <= 0.0f) continue;
                double det = g (pre + "voice_detune");
                bool uni = (int) std::round (g (pre + "voice_poly")) == 1 && det > 0.01;
                bool ringFm = ((int) g (pre + "osc_mod") % 2 == 1) || g (pre + "fm_amt") > 0.0f;
                for (int o = 0; o < 2; ++o)
                {
                    juce::String os = pre + (o == 0 ? "osc1" : "osc2");
                    bool audible = g (os + "_level") * g (pre + (o == 0 ? "mix_o1" : "mix_o2")) > 0.0f
                                   || (o == 1 && ringFm);
                    if (! audible || (int) g (os + "_wave") == 5) continue; // noise: no pitch
                    double c = std::abs (g (os + "_fine") + (o == 0 ? -15.0 : 15.0) * det)
                               + (uni ? 25.0 * det : 0.0);
                    int semis = ((int) std::round (g (os + "_pitch")) % 12 + 12) % 12;
                    if (semis != 0 && semis != 5 && semis != 7) c += 100.0;
                    if (o == 1 && ringFm && semis != 0) c += 100.0;
                    if ((int) g (os + "_wave") == 2
                        && (g (os + "_shape") > 0.0f || g (os + "_pwm") > 0.0f)) c += 100.0;
                    worst = std::max (worst, c);
                }
                double vib = 0.02 * g (pre + "voice_vib") * g (pre + "lfo2_depth");
                worst = std::max (worst, 1200.0 * std::log2 (1.0 + vib));
            }
            worstAll = std::max (worstAll, worst);
            if (worst > 25.0 && ++bad <= 5)
                printf ("  off-key preset %d (%s): %.1f cents\n", idx, pr.name.toRawUTF8(), worst);
        }
        CHECK (bad == 0, "%d factory presets can sound > 25 cents off key", bad);
        printf ("in-key bank: ok (worst oscillator %.1f cents)\n", worstAll);
    }

    // ---- FM keeps the pitch centre (exponential FM used to drag sharp) ----
    {
        auto fmPitch = [&] (float amt)
        {
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px->apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("osc1_wave", 3.0f); setP ("fm_amt", amt); // sine carrier, osc2 = modulator
            setP ("osc2_level", 0.0f); setP ("mix_o2", 0.0f);
            juce::MidiBuffer on; on.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            juce::MidiBuffer none;
            juce::AudioBuffer<float> buf (2, 512);
            std::vector<float> x;
            for (int b = 0; b < 40; ++b)
            {
                px->processBlock (buf, b == 0 ? on : none);
                if (b >= 8) for (int i = 0; i < 512; ++i) x.push_back (buf.getSample (0, i));
            }
            // autocorrelation peak near the A3 period (220 Hz) = pitch centre
            int bestLag = 0; double best = -1e9;
            for (int lag = 150; lag < 260; ++lag)
            {
                double acc = 0;
                for (size_t i = 0; i + (size_t) lag < x.size(); ++i) acc += x[i] * x[i + (size_t) lag];
                if (acc > best) { best = acc; bestLag = lag; }
            }
            return 1200.0 * std::log2 ((44100.0 / bestLag) / 220.0);
        };
        double c0 = fmPitch (0.0f), c45 = fmPitch (0.45f);
        CHECK (std::abs (c45 - c0) < 12.0, "FM moved the pitch centre by %.1f cents", c45 - c0);
        printf ("fm pitch centre: ok (%.1f -> %.1f cents)\n", c0, c45);
    }

    // ---- audibility audit: every pitched factory preset must speak ----
    // (SFX excluded: risers/swells start silent by design.) Catches recipes
    // that look fine on paper but render silence — e.g. harmonic-poor
    // waves through a high highpass.
    {
        int checked = 0, quiet = 0;
        for (int idx = 0; idx < nProg; ++idx)
        {
            const auto& pr = PresetBank::get (idx);
            juce::String cat = pr.tags.isEmpty() ? juce::String() : pr.tags[0];
            if (cat == "SFX") continue;
            auto px = std::make_unique<NanoFrogProcessor>();
            px->prepareToPlay (44100.0, 512);
            px->loadFactoryPreset (idx);
            juce::MidiBuffer on, off;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, 512);
            float peak = 0;
            for (int b = 0; b < 110; ++b) // ~2.5 s, note held throughout
            {
                px->processBlock (buf, b == 0 ? on : off);
                float e = 0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i) e += buf.getSample (ch, i) * buf.getSample (ch, i);
                peak = juce::jmax (peak, (float) std::sqrt (e / 1024));
            }
            ++checked;
            if (peak < 0.008f)
            {
                ++quiet;
                if (quiet <= 10)
                    printf ("  quiet preset %d (%s): peak %.4f\n",
                            idx, pr.name.toRawUTF8(), peak);
            }
        }
        CHECK (quiet == 0, "%d/%d presets below audibility floor", quiet, checked);
        printf ("audibility: %d presets checked\n", checked);
    }

    // ---- mutate: ADSR freeze, no-enable gates, pad freedom ----
    {
        static const char* envIds[] = {
            "env1_a", "env1_d", "env1_s", "env1_r",
            "env2_a", "env2_d", "env2_s", "env2_r",
            "t2_env1_a", "t2_env1_d", "t2_env1_s", "t2_env1_r",
            "t2_env2_a", "t2_env2_d", "t2_env2_s", "t2_env2_r" };
        auto rawOf = [&] (const char* id)
        {
            if (auto* pv = proc->apvts.getRawParameterValue (id)) return pv->load();
            return 0.0f;
        };
        auto snapshotEnv = [&]
        {
            std::vector<float> v;
            for (auto* eid : envIds) v.push_back (rawOf (eid));
            return v;
        };
        // Bass: envelopes frozen, pitch frozen, something else moves,
        // gated modules stay off
        {
            int idx = firstWithTag ("Reese");
            CHECK (idx >= 0, "no Reese bass in the bank");
            proc->loadFactoryPreset (idx);
            auto envBefore = snapshotEnv();
            auto allBefore = PresetBank::capture (proc->apvts);
            static const char* pitchIds[] = {
                "osc1_octave", "osc1_pitch", "osc2_octave", "osc2_pitch",
                "t2_osc1_octave", "t2_osc1_pitch", "t2_osc2_octave", "t2_osc2_pitch" };
            std::vector<float> pitchBefore;
            for (auto* pid : pitchIds) pitchBefore.push_back (rawOf (pid));
            proc->mutateCurrentPatch();
            auto envAfter = snapshotEnv();
            for (size_t k = 0; k < envBefore.size(); ++k)
                CHECK (envAfter[k] == envBefore[k],
                       "mutate moved bass envelope %d", (int) k);
            for (size_t k = 0; k < pitchBefore.size(); ++k)
                CHECK (rawOf (pitchIds[k]) == pitchBefore[k],
                       "mutate moved pitch %d", (int) k);
            int diffs = 0;
            auto allAfter = PresetBank::capture (proc->apvts);
            for (int k = 0; k < PresetBank::kParamCount; ++k)
                if (allAfter[k] != allBefore[k]) ++diffs;
            CHECK (diffs > 0, "mutate changed nothing on bass");
            CHECK (rawOf ("delay_mix") == 0.0f, "mutate woke the delay");
            CHECK (rawOf ("fm_amt") == 0.0f, "mutate woke FM");
            printf ("mutate bass: %d params moved, envelopes frozen\n", diffs);
        }
        // the first 808 (pure sine: silent osc2, no FX, no LFO, no mods): sleepers stay asleep
        {
            int idx = firstWithTag ("808");
            CHECK (idx >= 0, "no 808 in the bank");
            proc->loadFactoryPreset (idx);
            proc->mutateCurrentPatch();
            CHECK (rawOf ("osc2_level") == 0.0f && rawOf ("mix_o2") == 0.0f,
                   "mutate woke silent osc2");
            CHECK (rawOf ("delay_mix") == 0.0f, "mutate woke the delay");
            CHECK (rawOf ("lfo1_depth") == 0.0f && rawOf ("lfo2_depth") == 0.0f,
                   "mutate woke a sleeping LFO");
            CHECK (rawOf ("modfx_type") == 0.0f, "mutate enabled modfx");
            for (int m = 1; m <= 4; ++m)
            {
                juce::String aid = "mod" + juce::String (m) + "_amt";
                CHECK (rawOf (aid.toRawUTF8()) == 0.0f,
                       "mutate woke mod slot %d", m);
            }
            printf ("mutate gates: sleepers stayed asleep\n");
        }
        // Pad: envelopes free to move, and something does move
        {
            int idx = firstWithTag ("Pad");
            CHECK (idx >= 0, "no Pad in the bank");
            proc->loadFactoryPreset (idx);
            auto envBefore = snapshotEnv();
            proc->mutateCurrentPatch();
            auto envAfter = snapshotEnv();
            bool moved = false;
            for (size_t k = 0; k < envBefore.size(); ++k)
                if (envAfter[k] != envBefore[k]) moved = true;
            CHECK (moved, "mutate froze pad envelopes");
            printf ("mutate pad: envelopes free\n");
        }
        printf ("mutate: ok\n");
    }
    {
        auto& st = proc->apvts.state;
        std::set<juce::String> ids;
        for (int i = 0; i < st.getNumChildren(); ++i)
            ids.insert (st.getChild (i).getProperty ("id").toString());
        CHECK ((int) ids.size() == kNIds, "unique param ids %d, expected %d", (int) ids.size(), kNIds);
        for (int k = 0; k < kNIds; ++k)
            if (proc->apvts.getParameter (kIds[k]) == nullptr)
            {
                CHECK (false, "kIds[%d] %s missing from APVTS", k, kIds[k]);
                break;
            }
        printf ("param registry: %d unique ids\n", (int) ids.size());
    }

    // ---- voice assign, unipolar patch, bend / delay sync, arp, filter, undo ----
    {
        juce::MessageManager::getInstance(); // undo records message-thread gestures
        auto fresh = []
        {
            auto q = std::make_unique<NanoFrogProcessor>();
            q->setRateAndBufferSizeDetails (44100.0, 512);
            q->prepareToPlay (44100.0, 512);
            PresetBank::resetToDefaults (q->apvts);
            return q;
        };
        auto set = [] (NanoFrogProcessor& q, const juce::String& id, float plain)
        {
            if (auto* p = q.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain));
        };
        // render `blocks` of 512 with scheduled events (sample, msg)
        auto run = [] (NanoFrogProcessor& q, int blocks, std::vector<std::pair<int, juce::MidiMessage>> ev)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> b (2, 512);
            for (int k = 0; k < blocks; ++k)
            {
                juce::MidiBuffer m;
                for (auto& e : ev) if (e.first >= k * 512 && e.first < (k + 1) * 512) m.addEvent (e.second, e.first - k * 512);
                b.clear(); q.processBlock (b, m);
                for (int i = 0; i < 512; ++i) out.push_back (0.5f * (b.getSample (0, i) + b.getSample (1, i)));
            }
            return out;
        };
        auto rms = [] (const std::vector<float>& v, int a, int n) { double s = 0; for (int i = a; i < a + n; ++i) s += (double) v[(size_t) i] * v[(size_t) i]; return std::sqrt (s / n); };
        auto freqAt = [] (const std::vector<float>& v, int a, int n) { int z = 0; for (int i = a + 1; i < a + n; ++i) if ((v[(size_t) i - 1] < 0) != (v[(size_t) i] < 0)) ++z; return z * 44100.0 / (2.0 * n); };
        auto on = [] (int n, float v = 0.8f) { return juce::MidiMessage::noteOn (1, n, v); };
        auto off = [] (int n) { return juce::MidiMessage::noteOff (1, n); };
        const int sr = 44100;

        // Mono, Single trigger: an overlapping key slides without restarting a
        // decaying envelope; Multi restarts it
        auto decayCase = [&] (int trig)
        {
            auto q = fresh();
            set (*q, "voice_assign", 1); set (*q, "voice_trigger", (float) trig);
            set (*q, "env2_d", 0.35f); set (*q, "env2_s", 0.0f);
            auto o = run (*q, 60, { { 0, on (60) }, { sr / 2, on (64) } });
            return rms (o, sr / 2 + 2000, 4000) / rms (o, 1000, 4000);
        };
        const double single = decayCase (0), multi = decayCase (1);
        printf ("mono legato: level after the second key %.3f (Single) vs %.3f (Multi)\n", single, multi);
        CHECK (single < 0.3 && multi > 0.6, "mono Single trigger slides legato, Multi retriggers (%.3f / %.3f)", single, multi);

        // note priority LOW: a higher second key doesn't take over
        {
            auto q = fresh();
            set (*q, "voice_assign", 1); set (*q, "voice_priority", 1); set (*q, "osc1_wave", 3);
            auto o = run (*q, 100, { { 0, on (57) }, { sr / 4, on (69) } });
            const double f = freqAt (o, sr / 2, 8000);
            CHECK (std::abs (f - 220.0) < 6.0, "Low note priority keeps the low key (%.1f Hz)", f);
            auto q2 = fresh();
            set (*q2, "voice_assign", 1); set (*q2, "osc1_wave", 3);
            auto o2 = run (*q2, 100, { { 0, on (57) }, { sr / 4, on (69) }, { sr / 2, off (69) } });
            const double back = freqAt (o2, sr / 2 + 4000, 8000);
            CHECK (std::abs (back - 220.0) < 6.0, "Last priority falls back to the held key on release (%.1f Hz)", back);
        }
        // unison: four detuned voices around the key
        {
            auto q = fresh();
            set (*q, "voice_assign", 2); set (*q, "voice_unidet", 30.0f); set (*q, "osc1_wave", 3);
            auto o = run (*q, 100, { { 0, on (57) } });
            const double f = freqAt (o, 8000, 16000);
            auto q1 = fresh(); set (*q1, "osc1_wave", 3);
            auto o1 = run (*q1, 40, { { 0, on (57) } });
            // the stack beats: its level wanders where a single voice is steady
            double lo = 1e9, hi = 0;
            for (int a = 4000; a + 2205 < (int) o.size(); a += 2205) { const double r = rms (o, a, 2205); lo = std::min (lo, r); hi = std::max (hi, r); }
            printf ("unison: %.1f Hz centre, level %.3f..%.3f\n", f, lo, hi);
            CHECK (std::abs (f - 220.0) < 8.0 && hi > 1.3 * lo, "unison stacks detuned voices around the key");
            juce::ignoreUnused (o1);
        }
        // patch PITCH moves both oscillators; ENV2 (unipolar) sits at 1 while held
        {
            auto q = fresh();
            set (*q, "osc1_level", 0.0f); set (*q, "osc2_level", 1.0f); set (*q, "mix_o2", 1.0f); set (*q, "osc2_wave", 2);
            set (*q, "mod1_src", 1); set (*q, "mod1_dst", 0); set (*q, "mod1_amt", 1.0f / 12.0f);
            set (*q, "mod2_amt", 0.0f); set (*q, "mod3_amt", 0.0f); set (*q, "mod4_amt", 0.0f);
            auto o = run (*q, 100, { { 0, on (57) } });
            const double f = freqAt (o, sr / 2, 8000);
            CHECK (std::abs (f / 220.0 - std::pow (2.0, 1.0 / 12.0)) < 0.02, "PITCH destination moves OSC2 too (unipolar ENV2: +1 st, %.1f Hz)", f);
        }
        // bend range
        {
            auto q = fresh();
            set (*q, "bend_range", 12.0f); set (*q, "osc1_wave", 3);
            auto o = run (*q, 100, { { 0, juce::MidiMessage::pitchWheel (1, 16383) }, { 0, on (57) } });
            const double f = freqAt (o, sr / 2, 8000);
            CHECK (std::abs (f - 440.0) < 10.0, "bend range 12 bends an octave (%.1f Hz)", f);
        }
        // delay sync: 1/4 at 120 BPM = 500 ms
        {
            auto q = fresh();
            set (*q, "song_tempo", 120.0f); set (*q, "delay_sync", 7); set (*q, "delay_mix", 1.0f); set (*q, "delay_feedback", 0.0f);
            set (*q, "env2_d", 0.05f); set (*q, "env2_s", 0.0f); set (*q, "env2_r", 0.02f);
            auto o = run (*q, 90, { { 0, on (60) }, { 600, off (60) } });
            int echo = -1;
            for (int i = 8000; i < (int) o.size(); ++i) if (std::abs (o[(size_t) i]) > 0.02f) { echo = i; break; }
            printf ("delay sync echo at %.0f ms\n", echo * 1000.0 / sr);
            CHECK (std::abs (echo * 1000.0 / sr - 500.0) < 25.0, "1/4 delay sync echoes at 500 ms (%.0f ms)", echo * 1000.0 / sr);
        }
        // extension FX (drive / saturation / reverb): off by default, each audibly
        // changes the sound when on, stays finite and bounded
        {
            auto render = [&] (const char* onId)
            {
                auto q = fresh();
                set (*q, "osc1_wave", 3); set (*q, "env2_r", 0.02f);
                if (onId != nullptr) set (*q, onId, 1.0f);
                return run (*q, 120, { { 0, on (57) }, { sr * 3 / 10, off (57) } }); // 300 ms note
            };
            const auto dry = render (nullptr);
            for (auto* id : { "drive_on", "sat_on", "reverb_on" })
            {
                CHECK (fresh()->apvts.getRawParameterValue (id)->load() == 0.0f, "%s defaults off", id);
                const auto o = render (id);
                double diff = 0.0, peak = 0.0;
                bool finite = true;
                for (size_t i = 0; i < o.size() && i < dry.size(); ++i)
                {
                    finite = finite && std::isfinite (o[i]);
                    diff += std::abs (o[i] - dry[i]);
                    peak = std::max (peak, (double) std::abs (o[i]));
                }
                diff /= (double) o.size();
                printf ("fx %s: mean |diff| %.4f, peak %.3f\n", id, diff, peak);
                CHECK (finite && peak < 1.0, "%s output finite and under the ceiling", id);
                CHECK (diff > 1e-3, "%s audibly changes the sound", id);
            }
            // the reverb rings on after the dry voice has gone
            const auto wet = render ("reverb_on");
            const int tail = (int) (sr * 1.0);
            printf ("reverb tail rms %.5f vs dry %.5f\n", rms (wet, tail, 4000), rms (dry, tail, 4000));
            CHECK (rms (wet, tail, 4000) > 10.0 * rms (dry, tail, 4000) + 1e-5, "reverb leaves a tail");
        }
        // arp ALT2 repeats the end notes; switched-off steps are rests
        {
            auto q = fresh();
            set (*q, "osc1_wave", 3); set (*q, "arp_on", 1); set (*q, "arp_mode", 3); set (*q, "arp_rate", 1); // 1/8
            set (*q, "song_tempo", 120.0f); set (*q, "arp_gate", 0.9f);
            auto o = run (*q, 160, { { 0, on (57) }, { 0, on (61) }, { 0, on (64) } });
            const int step = sr / 4; // 1/8 at 120
            int notes[6];
            for (int k = 0; k < 6; ++k)
            {
                const double f = freqAt (o, k * step + 2000, 6000);
                notes[k] = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
            }
            printf ("alt2 steps: %d %d %d %d %d %d\n", notes[0], notes[1], notes[2], notes[3], notes[4], notes[5]);
            CHECK (notes[0] == 57 && notes[1] == 61 && notes[2] == 64 && notes[3] == 64 && notes[4] == 61 && notes[5] == 57,
                   "Alt2 runs up and down repeating both end notes");
            auto q2 = fresh();
            set (*q2, "osc1_wave", 3); set (*q2, "arp_on", 1); set (*q2, "arp_rate", 1); set (*q2, "song_tempo", 120.0f);
            set (*q2, "arp_last_step", 4.0f); set (*q2, "arp_step2", 0.0f); set (*q2, "env2_r", 0.01f);
            auto o2 = run (*q2, 160, { { 0, on (57) } });
            // LAST STEP 4 with step 2 off: steps 1 3 4 | 5 7 8 play, 2 and 6 rest
            const double played = rms (o2, 0 * step + 2000, 6000), rest = rms (o2, 1 * step + 4000, 4000);
            const double again = rms (o2, 4 * step + 2000, 6000), rest6 = rms (o2, 5 * step + 4000, 4000);
            printf ("arp steps: %.3f rest %.4f | %.3f rest %.4f\n", played, rest, again, rest6);
            CHECK (played > 0.02 && rest < 0.1 * played && again > 0.02 && rest6 < 0.1 * again,
                   "a switched-off step is a rest, and LAST STEP 4 repeats the pattern");
        }
        // user waves: mipmapped tables keep the level of the imported cycle
        {
            std::vector<float> saw (2048);
            for (int i = 0; i < 2048; ++i) saw[(size_t) i] = 1.0f - 2.0f * (float) i / 2048.0f;
            auto q = fresh();
            CHECK (q->importUserWave (saw.data(), 2048, "test saw"), "user wave import");
            set (*q, "osc1_wave", 6); set (*q, "osc1_digital", 128);
            auto user = run (*q, 40, { { 0, on (60) } });
            auto qs = fresh();
            set (*qs, "osc1_wave", 0);
            auto plain = run (*qs, 40, { { 0, on (60) } });
            const double d = 20.0 * std::log10 (rms (user, 4000, 12000) / juce::jmax (1e-9, rms (plain, 4000, 12000)));
            auto qh = fresh();
            qh->importUserWave (saw.data(), 2048, "test saw");
            set (*qh, "osc1_wave", 6); set (*qh, "osc1_digital", 128);
            auto high = run (*qh, 40, { { 0, on (108) } });
            bool fin = true; for (float x : high) fin = fin && std::isfinite (x);
            printf ("user saw vs built-in saw: %+.1f dB\n", d);
            CHECK (std::abs (d) < 4.0 && fin, "user wave mipmaps keep the level (%+.1f dB) and play up top", d);
        }
        // LP24 and LP12 resonance peaks line up
        {
            auto peak = [] (int type)
            {
                SvFilter f; f.setSampleRate (44100.0);
                juce::Random r (3); double s = 0;
                for (int i = 0; i < 44100; ++i) { float y = f.process (type, 1000.0f, 1.0f, (r.nextFloat() * 2.0f - 1.0f) * 0.1f); if (i > 2000) s += (double) y * y; }
                return 10.0 * std::log10 (s);
            };
            const double d = peak (0) - peak (1);
            printf ("LP24 vs LP12 at full resonance: %+.1f dB\n", d);
            CHECK (std::abs (d) < 4.0, "LP24 resonance level matches LP12 (%+.1f dB)", d);
        }
        // undo a knob gesture; MUTATE leaves locked sections alone
        {
            auto q = fresh();
            auto* cut = q->apvts.getParameter ("filter_cutoff");
            const float before = cut->getValue();
            cut->beginChangeGesture(); cut->setValueNotifyingHost (0.3f); cut->endChangeGesture();
            q->undoEdit();
            CHECK (std::abs (cut->getValue() - before) < 1e-5f, "undo a knob gesture");
            q->redoEdit();
            q->setMutateLocked (NanoFrogProcessor::MG_FILTER, true);
            const float res = q->apvts.getParameter ("filter_reso")->getValue();
            for (int k = 0; k < 5; ++k) q->mutateCurrentPatch();
            CHECK (std::abs (cut->getValue() - 0.3f) < 1e-5f && q->apvts.getParameter ("filter_reso")->getValue() == res,
                   "MUTATE never touches a locked section");
            q->undoEdit();
            CHECK (q->canRedo(), "MUTATE is an undo step");
            {
                auto qp = fresh();
                set (*qp, "mod1_src", 2); set (*qp, "mod1_dst", 0); set (*qp, "mod1_amt", 0.2f); set (*qp, "lfo1_depth", 0.5f);
                set (*qp, "osc1_fine", 3.0f); set (*qp, "voice_detune", 0.3f);
                const char* ids[] = { "osc1_octave", "osc1_pitch", "osc1_fine", "osc2_octave", "osc2_pitch", "osc2_fine",
                                      "voice_detune", "voice_unidet", "voice_vib", "mod1_amt", "master_tune" };
                float before[11]; for (int k = 0; k < 11; ++k) before[k] = qp->apvts.getRawParameterValue (ids[k])->load();
                bool same = true;
                for (int n = 0; n < 20; ++n) qp->mutateCurrentPatch();
                for (int k = 0; k < 11; ++k) same = same && qp->apvts.getRawParameterValue (ids[k])->load() == before[k];
                CHECK (same, "MUTATE never touches pitch (tune, detune, vibrato, pitch-routed patch slots)");
            }
        }
    }

    // ---- control self-test: drive every control, param must follow ----
    CHECK (ed->selfTestControls(), "control self-test failed");

    delete outer;
    if (failures == 0) printf ("HEADLESS TEST PASSED\n");
    else printf ("HEADLESS TEST: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
