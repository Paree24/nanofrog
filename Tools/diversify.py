"""Pick, for every preset slot, the designed candidate that sounds least like
its siblings -> Tools/bank_choice.json (read by gen_bank.py).

gen_bank.py builds CANDIDATES designed variants per slot (one option per
axis: source, filter, layer, movement, effects). This renders all of them
with the headless test (NANOFROG_DUMP_FEATURES: C4 held 1 s + release ->
attack / sustain / release spectra and a loudness envelope), then walks each
archetype in order and keeps the candidate whose nearest already-chosen
sibling is farthest away. Silent candidates are never picked.

Usage (from NanoBerg/, headless test built): python3 Tools/diversify.py
"""
import importlib.util, json, os, subprocess, sys, tempfile
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "build" / "NanoFrogHeadlessTest_artefacts" / "Release" / "NanoFrogHeadlessTest"
CHOICE = ROOT / "Tools" / "bank_choice.json"


def load_gen():
    spec = importlib.util.spec_from_file_location("gen_bank", ROOT / "Tools" / "gen_bank.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def render(gen, presets):
    with tempfile.TemporaryDirectory() as tmp:
        bank = {"type": "NanoFrogBank", "version": 1, "param_count": len(gen.KIDS), "param_ids": gen.KIDS,
                "presets": [{"name": n, "tags": t, "values": v} for n, t, v, *_ in presets]}
        bf = Path(tmp) / "cands.json"
        bf.write_text(json.dumps(bank))
        out = Path(tmp) / "f.tsv"
        env = dict(os.environ, NANOFROG_FACTORY_JSON=str(bf), NANOFROG_USER_DIR=str(Path(tmp) / "ud"),
                   NANOFROG_DUMP_FEATURES=str(out), NANOFROG_DUMP_ORIG_ONLY="1")
        subprocess.run([str(TEST)], env=env, check=True, stdout=subprocess.DEVNULL)
        feats = {}
        for line in out.read_text().splitlines():
            f = line.split("\t")
            feats[int(f[0])] = np.array([float(x) for x in f[4:]])
    return feats


def norm(v):
    return np.maximum(v - v.max(), -60.0)  # loudness-normalised, 60 dB floor


def main():
    gen = load_gen()
    cands = gen.build(all_candidates=True)
    print(f"rendering {len(cands)} candidates...", file=sys.stderr)
    feats = render(gen, cands)
    by_slot = {}
    for i, (_, _, _, noun, v, k) in enumerate(cands):
        if noun != "init":
            by_slot.setdefault((noun, v), []).append((k, i))
    choice, report = {}, []
    loud = {i: float(f[-15:].max()) for i, f in feats.items()}
    for arch in gen.ARCHES:
        # a candidate far quieter than its archetype is a dead combination (e.g. a
        # filter with nothing to pass): never picked
        levels = [loud[i] for v in range(arch.count) for _, i in by_slot[(arch.noun, v)]]
        floor_db = float(np.median(levels)) - 12.0
        chosen = []  # feature vectors of the siblings picked so far
        dists = []
        for v in range(arch.count):
            best, best_d = None, -1.0
            for k, i in by_slot[(arch.noun, v)]:
                f = feats[i]
                if loud[i] < floor_db:
                    continue
                x = norm(f)
                d = min((float(np.mean(np.abs(x - c))) for c in chosen), default=99.0)
                if d > best_d:
                    best, best_d = (k, x), d
            choice[f"{arch.noun}/{v}"] = best[0]
            chosen.append(best[1])
            if v > 0:
                dists.append(best_d)
        if dists:
            report.append((arch.noun, min(dists), float(np.median(dists))))
    CHOICE.write_text(json.dumps(choice, indent=0, sort_keys=True))
    for noun, mn, md in report:
        print(f"  {noun:16s} nearest-sibling distance: min {mn:4.1f}  median {md:4.1f}")


if __name__ == "__main__":
    main()
