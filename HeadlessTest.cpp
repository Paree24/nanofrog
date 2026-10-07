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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    printf ("-- NanoFrog headless test --\n");

    // Point the bank at the repo JSON and user presets at a temp dir.
    {
        juce::File here (__FILE__);
        juce::File json = here.getParentDirectory().getChildFile ("FactoryBank")
                              .getChildFile ("NanoFrogFactory.json");
        setenv ("NANOFROG_FACTORY_JSON", json.getFullPathName().toRawUTF8(), 1);
        juce::File udir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("nb_usertest");
        udir.deleteRecursively();
        setenv ("NANOFROG_USER_DIR", udir.getFullPathName().toRawUTF8(), 1);
    }

    NanoFrogProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    // ---- program count ----
    int nProg = proc.getNumPrograms();
    CHECK (nProg == 401, "program count %d, expected 401", nProg);
    printf ("programs: %d\n", nProg);
    CHECK (PresetBank::get (0).name == "Init",
           "preset 0 is '%s', expected Init",
           PresetBank::get (0).name.toRawUTF8());
    CHECK (PresetBank::get (1).name == "Init Lead",
           "preset 1 is '%s', expected Init Lead",
           PresetBank::get (1).name.toRawUTF8());

    // ---- editor layout ----
    NanoFrogEditor* outer = dynamic_cast<NanoFrogEditor*> (proc.createEditor());
    CHECK (outer != nullptr, "editor is null");
    if (outer == nullptr) return 1;
    FrogContent& edref = outer->getContent();
    FrogContent* ed = &edref;
    CHECK (outer->getWidth() == 1280 && outer->getHeight() == 900,
           "editor size %dx%d, expected 1280x900", outer->getWidth(), outer->getHeight());
    CHECK (ed->getSectionCount() == 26, "sections %d, expected 26", ed->getSectionCount());

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
    if (auto* dg = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("osc1_digital")))
        CHECK (dg->choices.size() == 192, "osc1_digital choices %d, expected 192", dg->choices.size());
    else { CHECK (false, "osc1_digital is not a choice"); }
    if (auto* ow = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("osc1_wave")))
        CHECK (ow->choices.size() == 7, "osc1_wave choices %d, expected 7", ow->choices.size());
    else { CHECK (false, "osc1_wave is not a choice"); }
    if (auto* ls = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("lfo1_sync")))
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
        CHECK (proc.importUserWave (src, 4096, "TestSine"), "importUserWave failed");
        CHECK (proc.hasUserWave(), "user wave not flagged loaded");
        CHECK (proc.getUserWaveName() == "TestSine", "user wave name not stored");
    }

    // ---- preset round-trip (epsilon, LESSONS.md #21) ----
    for (const char* pid : { "osc1_octave", "mod1_src", "filter_type", "bogus_id" })
        printf ("  lookup %-12s -> %s\n", pid, proc.apvts.getParameter (pid) ? "found" : "NULL");
    // Direct-conversion probe (no async, no editor involvement).
    if (auto* p = proc.apvts.getParameter ("mix_o1"))
    {
        p->setValueNotifyingHost (p->convertTo0to1 (0.7874f));
        float raw = proc.apvts.getRawParameterValue ("mix_o1")->load();
        printf ("  direct probe mix_o1: raw=%.6f norm=%.6f (want raw 0.787400)\n",
                raw, p->getValue());
    }
    static const char* kIds[152] = {
        "osc1_wave",
        "osc1_digital",
        "osc1_octave",
        "osc1_pitch",
        "osc1_fine",
        "osc1_level",
        "osc1_shape",
        "osc1_pwm",
        "osc1_xmod",
        "osc_mod",
        "osc2_wave",
        "osc2_octave",
        "osc2_pitch",
        "osc2_fine",
        "osc2_level",
        "mix_o1",
        "mix_o2",
        "mix_noise",
        "fm_amt",
        "filter_type",
        "filter_cutoff",
        "filter_reso",
        "filter_keytrack",
        "filter_envamt",
        "amp_level",
        "amp_pan",
        "amp_velocity",
        "amp_dist",
        "amp_drive",
        "env1_a",
        "env1_d",
        "env1_s",
        "env1_r",
        "env2_a",
        "env2_d",
        "env2_s",
        "env2_r",
        "lfo1_wave",
        "lfo1_rate",
        "lfo1_depth",
        "lfo1_sync",
        "lfo2_wave",
        "lfo2_rate",
        "lfo2_depth",
        "lfo2_sync",
        "mod1_src",
        "mod1_dst",
        "mod1_amt",
        "mod2_src",
        "mod2_dst",
        "mod2_amt",
        "mod3_src",
        "mod3_dst",
        "mod3_amt",
        "mod4_src",
        "mod4_dst",
        "mod4_amt",
        "modfx_type",
        "modfx_rate",
        "modfx_depth",
        "modfx_mix",
        "delay_type",
        "delay_time",
        "delay_feedback",
        "delay_mix",
        "eq_low",
        "eq_high",
        "arp_on",
        "arp_mode",
        "arp_rate",
        "arp_gate",
        "arp_octaves",
        "arp_swing",
        "arp_latch",
        "voice_poly",
        "voice_detune",
        "voice_portamento",
        "voice_vib",
        "output_level",
        "master_tune",
        "song_tempo",
        "osc2_shape",
        "osc2_pwm",
        "osc2_digital",
        "osc2_xmod",
        "t2_osc1_wave",
        "t2_osc1_digital",
        "t2_osc1_octave",
        "t2_osc1_pitch",
        "t2_osc1_fine",
        "t2_osc1_level",
        "t2_osc1_shape",
        "t2_osc1_pwm",
        "t2_osc1_xmod",
        "t2_osc_mod",
        "t2_osc2_wave",
        "t2_osc2_octave",
        "t2_osc2_pitch",
        "t2_osc2_fine",
        "t2_osc2_level",
        "t2_mix_o1",
        "t2_mix_o2",
        "t2_mix_noise",
        "t2_fm_amt",
        "t2_filter_type",
        "t2_filter_cutoff",
        "t2_filter_reso",
        "t2_filter_keytrack",
        "t2_filter_envamt",
        "t2_amp_level",
        "t2_amp_pan",
        "t2_amp_velocity",
        "t2_amp_dist",
        "t2_amp_drive",
        "t2_env1_a",
        "t2_env1_d",
        "t2_env1_s",
        "t2_env1_r",
        "t2_env2_a",
        "t2_env2_d",
        "t2_env2_s",
        "t2_env2_r",
        "t2_lfo1_wave",
        "t2_lfo1_rate",
        "t2_lfo1_depth",
        "t2_lfo1_sync",
        "t2_lfo2_wave",
        "t2_lfo2_rate",
        "t2_lfo2_depth",
        "t2_lfo2_sync",
        "t2_mod1_src",
        "t2_mod1_dst",
        "t2_mod1_amt",
        "t2_mod2_src",
        "t2_mod2_dst",
        "t2_mod2_amt",
        "t2_mod3_src",
        "t2_mod3_dst",
        "t2_mod3_amt",
        "t2_mod4_src",
        "t2_mod4_dst",
        "t2_mod4_amt",
        "t2_voice_poly",
        "t2_voice_detune",
        "t2_voice_portamento",
        "t2_voice_vib",
        "t2_osc2_shape",
        "t2_osc2_pwm",
        "t2_osc2_digital",
        "t2_osc2_xmod",
        "tmix",
        "limiter"
    };
    auto checkPreset = [&] (int idx) -> int
    {
        CHECK (proc.getCurrentProgram() == idx, "current program %d after set %d",
               proc.getCurrentProgram(), idx);
        const auto& pr = PresetBank::get (idx);
        int mism = 0;
        for (int k = 0; k < 152; ++k)
        {
            float actual = 0.0f;
            if (auto* pv = proc.apvts.getRawParameterValue (kIds[k])) actual = pv->load();
            if (std::abs (actual - pr.values[k]) > 1e-3f)
            {
                if (mism < 5)
                    printf ("  mismatch preset %d param %s: got %.4f want %.4f\n",
                            idx, kIds[k], actual, pr.values[k]);
                ++mism;
            }
        }
        CHECK (mism == 0, "preset %d (%s): %d/152 params mismatch", idx, pr.name.toRawUTF8(), mism);
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
            for (int k = 0; k < 152; ++k)
                if (auto* p = proc.apvts.getParameter (kIds[k]))
                    p->setValueNotifyingHost (p->convertTo0to1 (pr.values[k]));
            for (int k = 0; k < 152; ++k)
            {
                float actual = 0.0f;
                if (auto* pv = proc.apvts.getRawParameterValue (kIds[k])) actual = pv->load();
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
        int growlIdx = PresetBank::findByName ("Tearout Titan");
        CHECK (growlIdx >= 0, "Tearout Titan missing from bank");
        if (growlIdx >= 0)
            CHECK (PresetBank::get (growlIdx).tags.contains ("Growl"),
                   "Tearout Titan missing Growl tag");
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
                                                       PresetBank::capture (proc.apvts));
        CHECK (savedFile.existsAsFile(), "saveUser failed");
        auto users = PresetBank::scanUser();
        CHECK (users.size() == 1, "scanUser found %d, expected 1", (int) users.size());
        // change state, then load the user preset and confirm values return
        if (auto* p = proc.apvts.getParameter ("filter_cutoff"))
            p->setValueNotifyingHost (0.0f);
        CHECK (proc.loadUserPreset (users[0].file), "loadUserPreset failed");
        CHECK (proc.getCurrentProgram() == -1, "user load did not enter user mode");
        CHECK (proc.getCurrentUserPreset() == users[0].file.getFileName(),
               "user name not tracked");
        ed->openBrowser();
        CHECK (ed->getBrowser()->getRowCount() == nProg + 1,
               "browser All rows %d, expected %d", ed->getBrowser()->getRowCount(), nProg + 1);
        ed->closeBrowser();
        // state round-trips the user mode (message thread: applied immediately)
        juce::MemoryBlock blob;
        proc.getStateInformation (blob);
        NanoFrogProcessor proc3;
        proc3.setStateInformation (blob.getData(), (int) blob.getSize());
        CHECK (proc3.getCurrentProgram() == -1, "restored proc not in user mode");
        CHECK (proc3.getCurrentUserPreset() == users[0].file.getFileName(),
               "restored user name mismatch");
        float a = 0.0f, b = 0.0f;
        if (auto* pv = proc.apvts.getRawParameterValue ("filter_cutoff")) a = pv->load();
        if (auto* pv = proc3.apvts.getRawParameterValue ("filter_cutoff")) b = pv->load();
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
                                                 PresetBank::capture (proc.apvts));
            CHECK (f.existsAsFile(), "DelTest save %d failed", i);
        }
        ed->openBrowser();
        auto* br = ed->getBrowser();
        int before = br->getRowCount();
        CHECK (before == nProg + 6, "browser rows %d, expected %d", before, nProg + 6);
        // last row is a user preset (All category appends users at the end)
        br->selectRowSync (before - 1);
        CHECK (proc.getCurrentProgram() == -1, "row load did not enter user mode");
        br->clickDel(); // async Button post: needs a live dispatch loop
        struct Stopper : juce::Timer
        {
            void timerCallback() override
            {
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
        };
        Stopper stop;
        stop.startTimer (150);
        juce::MessageManager::getInstance()->runDispatchLoop();
        CHECK (br->getRowCount() == before - 1,
               "browser rows after del %d, expected %d", br->getRowCount(), before - 1);
        CHECK ((int) PresetBank::scanUser().size() == 5,
               "user files after del %d, expected 5",
               (int) PresetBank::scanUser().size());
        for (auto& u : PresetBank::scanUser()) PresetBank::deleteUser (u.file);
        ed->closeBrowser();
        printf ("browser delete: ok (no crash, rows/files consistent)\n");
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
        int loaded = proc.importUserFolder (tmpDir.getFullPathName());
        CHECK (loaded == 3, "folder import loaded %d, expected 3", loaded);
        CHECK (proc.getUserBankCount() == 3, "bank count %d, expected 3",
               proc.getUserBankCount());
        CHECK (proc.getUserBankEntryName (0) == "wave0", "bank entry 0 name '%s'",
               proc.getUserBankEntryName (0).toRawUTF8());
        juce::MemoryBlock blob;
        proc.getStateInformation (blob);
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
        ProgDriver driver (proc, checkPreset, folderStage, verifyStage,
                                 [&] { ed->driveAsyncControls(); },
                                 [&] { CHECK (ed->verifyAsyncControls(), "async control check failed"); });
        driver.startTimer (80);
        juce::MessageManager::getInstance()->runDispatchLoop();
        driver.stopTimer();
    }

    // ---- multitimbral smoke: both banks render, mixer balances ----
    // (fresh processor: neutral defaults for both timbres, no program involved)
    {
        NanoFrogProcessor px;
        px.prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        auto renderNote = [&]
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b) px.processBlock (buf, midi);
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
        NanoFrogProcessor px;
        px.prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setP ("arp_on", 1.0f);
        setP ("arp_rate", 2.0f); // 1/16
        setP ("song_tempo", 120.0f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 44; ++b) px.processBlock (buf, midi); // ~0.5 s arp runs
        double hot = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) hot += buf.getSample (ch, i) * buf.getSample (ch, i);
        CHECK (hot > 1e-6, "arp did not sound (energy %.6f)", hot);
        setP ("arp_on", 0.0f);
        juce::MidiBuffer empty;
        for (int b = 0; b < 175; ++b) px.processBlock (buf, empty); // ~2 s decay
        double tail = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) tail += buf.getSample (ch, i) * buf.getSample (ch, i);
        CHECK (tail < 1e-8, "arp-off left ringing voices (energy %.6f)", tail);
        printf ("arp-off release: ok (tail %.2e)\n", tail);
    }

    // ---- digital selector routes by index in both wave modes ----
    {
        float src[4096];
        for (int i = 0; i < 4096; ++i) src[i] = (std::sin (i * 6.2831853f * 8.0f / 4096.0f) > 0 ? 0.9f : -0.9f);
        auto render = [&] (float wave, float digi)
        {
            NanoFrogProcessor px;
            px.prepareToPlay (44100.0, 512);
            CHECK (px.importUserWave (src, 4096, "T"), "user import failed");
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px.apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
            };
            setP ("osc1_wave", wave);
            setP ("osc1_digital", digi);
            setP ("filter_cutoff", 1.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b) px.processBlock (buf, midi);
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
            NanoFrogProcessor px;
            px.prepareToPlay (44100.0, 512);
            auto setP = [&] (const char* id, float v)
            {
                if (auto* p = px.apvts.getParameter (id))
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
            for (int b = 0; b < 4; ++b) px.processBlock (buf, midi);
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
        NanoFrogProcessor px;
        px.prepareToPlay (44100.0, 512);
        auto setP = [&] (const char* id, float v)
        {
            if (auto* p = px.apvts.getParameter (id))
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
        for (int b = 0; b < 4; ++b) px.processBlock (buf, midi);
        float off = buf.getMagnitude (0, 512);
        setP ("limiter", 1.0f);
        for (int b = 0; b < 4; ++b) px.processBlock (buf, midi);
        float on = buf.getMagnitude (0, 512);
        CHECK (on <= 1.0f, "limiter ceiling breached (%.3f)", on);
        CHECK (on < off, "limiter did not engage (%.3f vs %.3f)", on, off);
        printf ("limiter: off=%.3f on=%.3f\n", off, on);
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
            if (auto* pv = proc.apvts.getRawParameterValue (id)) return pv->load();
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
            int idx = PresetBank::findByName ("Reese Criminal");
            CHECK (idx >= 0, "Reese Criminal missing");
            proc.loadFactoryPreset (idx);
            auto envBefore = snapshotEnv();
            auto allBefore = PresetBank::capture (proc.apvts);
            static const char* pitchIds[] = {
                "osc1_octave", "osc1_pitch", "osc2_octave", "osc2_pitch",
                "t2_osc1_octave", "t2_osc1_pitch", "t2_osc2_octave", "t2_osc2_pitch" };
            std::vector<float> pitchBefore;
            for (auto* pid : pitchIds) pitchBefore.push_back (rawOf (pid));
            proc.mutateCurrentPatch();
            auto envAfter = snapshotEnv();
            for (size_t k = 0; k < envBefore.size(); ++k)
                CHECK (envAfter[k] == envBefore[k],
                       "mutate moved bass envelope %d", (int) k);
            for (size_t k = 0; k < pitchBefore.size(); ++k)
                CHECK (rawOf (pitchIds[k]) == pitchBefore[k],
                       "mutate moved pitch %d", (int) k);
            int diffs = 0;
            auto allAfter = PresetBank::capture (proc.apvts);
            for (int k = 0; k < PresetBank::kParamCount; ++k)
                if (allAfter[k] != allBefore[k]) ++diffs;
            CHECK (diffs > 0, "mutate changed nothing on bass");
            CHECK (rawOf ("delay_mix") == 0.0f, "mutate woke the delay");
            CHECK (rawOf ("fm_amt") == 0.0f, "mutate woke FM");
            printf ("mutate bass: %d params moved, envelopes frozen\n", diffs);
        }
        // 808 Menace (silent osc2, no FX, no LFO, no mods): sleepers stay asleep
        {
            int idx = PresetBank::findByName ("808 Menace");
            CHECK (idx >= 0, "808 Menace missing");
            proc.loadFactoryPreset (idx);
            proc.mutateCurrentPatch();
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
            int idx = PresetBank::findByName ("Polar Glow");
            CHECK (idx >= 0, "Polar Glow missing");
            proc.loadFactoryPreset (idx);
            auto envBefore = snapshotEnv();
            proc.mutateCurrentPatch();
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
        auto& st = proc.apvts.state;
        std::set<juce::String> ids;
        for (int i = 0; i < st.getNumChildren(); ++i)
            ids.insert (st.getChild (i).getProperty ("id").toString());
        CHECK ((int) ids.size() == 152, "unique param ids %d, expected 152", (int) ids.size());
        for (int k = 0; k < 152; ++k)
            if (proc.apvts.getParameter (kIds[k]) == nullptr)
            {
                CHECK (false, "kIds[%d] %s missing from APVTS", k, kIds[k]);
                break;
            }
        printf ("param registry: %d unique ids\n", (int) ids.size());
    }

    // ---- control self-test: drive every control, param must follow ----
    CHECK (ed->selfTestControls(), "control self-test failed");

    delete outer;
    if (failures == 0) printf ("HEADLESS TEST PASSED\n");
    else printf ("HEADLESS TEST: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
