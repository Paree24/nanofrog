"""Audio-verified preset diversity loop.

Parameter distance is a weak proxy for "sounds different", so this renders
the bank and re-rolls presets that SOUND like an earlier one:

  1. python3 Tools/gen_bank.py              (bank from recipes + salts)
  2. NanoFrogHeadlessTest in feature-dump mode renders every preset (C4 held
     1 s + release) and a MUTATE-ed copy -> spectra + envelope features
  3. a preset is a clone when an earlier preset sounds closer to it than a
     MUTATE moves a preset (median, measured on the same render)
  4. clones get salt+1 in Tools/bank_salts.json (fresh roll, stronger
     variation); repeat until the clone rate is under the target.

Usage (from NanoBerg/, after building the headless test):
  python3 Tools/audio_diversity.py [--rounds 8] [--target 0.08]
The result is deterministic: gen_bank.py + bank_salts.json reproduce it.
"""
import argparse, json, os, subprocess, sys, tempfile
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "build" / "NanoFrogHeadlessTest_artefacts" / "Release" / "NanoFrogHeadlessTest"
SALTS = ROOT / "Tools" / "bank_salts.json"
BANK = ROOT / "FactoryBank" / "NanoFrogFactory.json"


def render(tmp):
    out = Path(tmp) / "feat.tsv"
    env = dict(os.environ, NANOFROG_FACTORY_JSON=str(BANK),
               NANOFROG_USER_DIR=str(Path(tmp) / "ud"),  # no user shadows
               NANOFROG_DUMP_FEATURES=str(out))
    subprocess.run([str(TEST)], env=env, check=True, stdout=subprocess.DEVNULL)
    orig, mut, cats, names = {}, {}, {}, {}
    for line in out.read_text().splitlines():
        f = line.split("\t")
        i, m = int(f[0]), int(f[3])
        (mut if m else orig)[i] = np.array([float(x) for x in f[4:]])
        names[i], cats[i] = f[1], f[2]
    return orig, mut, cats, names


def norm(v):
    return np.maximum(v - v.max(), -60.0)  # loudness-normalised, 60 dB floor


def analyse(orig, mut):
    ids = sorted(orig)
    X = np.array([norm(orig[i]) for i in ids])
    tau = float(np.median([np.mean(np.abs(norm(orig[i]) - norm(mut[i]))) for i in ids]))
    D = np.mean(np.abs(X[:, None, :] - X[None, :, :]), axis=2)
    clones = []
    for a in range(len(ids)):
        if a > 0 and D[a, :a].min() <= tau:  # sounds like an EARLIER preset
            clones.append(ids[a])
    return tau, clones, D


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=8)
    ap.add_argument("--target", type=float, default=0.08)
    ap.add_argument("--tau", type=float, default=None,
                    help="fixed clone threshold (default: measured MUTATE median, round 1)")
    args = ap.parse_args()
    salts = {int(k): v for k, v in json.loads(SALTS.read_text()).items()} if SALTS.exists() else {}
    tau_fixed = args.tau
    for rnd in range(1, args.rounds + 1):
        subprocess.run([sys.executable, str(ROOT / "Tools" / "gen_bank.py")], check=True,
                       stderr=subprocess.DEVNULL)
        with tempfile.TemporaryDirectory() as tmp:
            orig, mut, cats, names = render(tmp)
        tau, clones, _ = analyse(orig, mut)
        if tau_fixed is None:
            tau_fixed = tau  # freeze the yardstick so rounds are comparable
        ids = sorted(orig)
        X = np.array([norm(orig[i]) for i in ids])
        D = np.mean(np.abs(X[:, None, :] - X[None, :, :]), axis=2)
        clones = [ids[a] for a in range(1, len(ids)) if D[a, :a].min() <= tau_fixed]
        clones = [c for c in clones if not names[c].startswith("Init")]
        rate = len(clones) / len(ids)
        print(f"round {rnd}: yardstick {tau_fixed:.2f}, clones {len(clones)} ({100*rate:.1f}%)",
              flush=True)
        if rate <= args.target:
            break
        for c in clones:
            salts[c] = salts.get(c, 0) + 1
        SALTS.write_text(json.dumps({str(k): v for k, v in sorted(salts.items())}, indent=0))
    # final bank on disk matches the salts on disk
    subprocess.run([sys.executable, str(ROOT / "Tools" / "gen_bank.py")], check=True,
                   stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
