#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

// Snapshot undo/redo for a JUCE plugin, ported from the Pastel Drum Sampler.
// Self-contained so other plugins can copy it as-is.
//
// One history for everything:
//  - full-state snapshots: call pushSnapshot() (or open a Transaction) right
//    before an edit that isn't a single parameter gesture (preset load, a
//    batch of parameter writes, non-parameter state such as user scales);
//  - single-parameter steps ("PARAM"): recorded automatically for every
//    change gesture (begin..end = one step) made on the message thread by a
//    UI: editor knobs/combos/toggles or an embedding host's own UI.
// Host automation never sends gestures, so it is never recorded.
// Message thread only.
class SnapshotUndo : private juce::AudioProcessorListener
{
public:
    using Capture = std::function<juce::ValueTree()>;
    using Restore = std::function<void(const juce::ValueTree&)>;

    SnapshotUndo(juce::AudioProcessor& p, Capture captureFn, Restore restoreFn, size_t maxSteps = 30)
        : proc(p), capture(std::move(captureFn)), restore(std::move(restoreFn)), maxUndo(maxSteps)
    {
        startValue.assign((size_t) proc.getParameters().size(), -1.0f);
        ignored.assign(startValue.size(), false);
        proc.addListener(this);
    }

    ~SnapshotUndo() override { proc.removeListener(this); }

    // parameters whose gestures shouldn't become undo steps (e.g. a UI theme)
    void ignoreParameter(int index)
    {
        if (index >= 0 && index < (int) ignored.size()) ignored[(size_t) index] = true;
    }

    void pushSnapshot() { pushEntry(capture()); }

    // Snapshot first, then mute PARAM recording while the scope runs, so an
    // edit that also moves parameters through gestures stays one undo step.
    struct Transaction
    {
        explicit Transaction(SnapshotUndo& u) : undo(u), wasApplying(u.applying)
        {
            if (! wasApplying) undo.pushSnapshot();
            undo.applying = true;
        }
        ~Transaction() { undo.applying = wasApplying; }
        SnapshotUndo& undo;
        bool wasApplying;
    };

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void clear() { undoStack.clear(); redoStack.clear(); }

    bool undo() { return step(undoStack, redoStack); }
    bool redo() { return step(redoStack, undoStack); }

private:
    juce::AudioProcessor& proc;
    Capture capture;
    Restore restore;
    size_t maxUndo;
    std::vector<juce::ValueTree> undoStack, redoStack;
    std::vector<float> startValue; // normalised value at gesture start, -1 = no gesture
    std::vector<bool> ignored;
    bool applying = false;         // undo/redo (or a Transaction) is moving parameters: don't record

    void pushEntry(juce::ValueTree entry)
    {
        undoStack.push_back(std::move(entry));
        if (undoStack.size() > maxUndo) undoStack.erase(undoStack.begin());
        redoStack.clear(); // a new edit forks history
    }

    juce::ValueTree paramEntry(int index) const
    {
        juce::ValueTree e("PARAM");
        e.setProperty("index", index, nullptr);
        if (auto* prm = proc.getParameters()[index]) e.setProperty("value", prm->getValue(), nullptr);
        return e;
    }

    void applyParamEntry(const juce::ValueTree& e)
    {
        auto* prm = proc.getParameters()[(int) e.getProperty("index", -1)];
        if (prm == nullptr) return;
        const juce::ScopedValueSetter<bool> svs(applying, true);
        prm->beginChangeGesture(); // hosts writing automation see a normal edit
        prm->setValueNotifyingHost((float) e.getProperty("value", prm->getValue()));
        prm->endChangeGesture();
    }

    // pop from `from`, push the current state of the same kind onto `to`, apply
    bool step(std::vector<juce::ValueTree>& from, std::vector<juce::ValueTree>& to)
    {
        if (from.empty()) return false;
        auto v = from.back();
        from.pop_back();
        if (v.hasType("PARAM"))
        {
            to.push_back(paramEntry((int) v.getProperty("index")));
            applyParamEntry(v);
            return true;
        }
        to.push_back(capture());
        const juce::ScopedValueSetter<bool> svs(applying, true);
        restore(v);
        return true;
    }

    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}

    // gestures arrive on the message thread (they come from a UI, never from host automation)
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override
    {
        if (! recordable(index)) return;
        if (auto* prm = proc.getParameters()[index]) startValue[(size_t) index] = prm->getValue();
    }

    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index) override
    {
        if (! recordable(index)) return;
        const float was = startValue[(size_t) index];
        startValue[(size_t) index] = -1.0f;
        auto* prm = proc.getParameters()[index];
        if (was < 0.0f || prm == nullptr || std::abs(prm->getValue() - was) < 1.0e-6f) return; // a click without a change
        auto e = paramEntry(index);
        e.setProperty("value", was, nullptr);
        pushEntry(e);
    }

    bool recordable(int index) const
    {
        return ! applying && juce::MessageManager::existsAndIsCurrentThread()
               && index >= 0 && index < (int) startValue.size() && ! ignored[(size_t) index];
    }

    JUCE_DECLARE_NON_COPYABLE(SnapshotUndo)
};
