"""Measured loudness trims for the factory bank -> Tools/bank_levels.json.

Renders every preset (headless test, NANOFROG_DUMP_FEATURES mode: C4 held
1 s + release) and takes its loudest 0.1 s stretch, so plucks and stabs are
judged by their body rather than by an average over their silence. Each
preset's output_level then moves it most of the way to TARGET_DB, within
the knob's range (0.2 .. 1.25). Run from a clean slate (it deletes the old
file first); gen_bank.py applies the file.

Usage (from NanoBerg/, headless test built): python3 Tools/level_bank.py
"""
import json, os, statistics, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "build" / "NanoFrogHeadlessTest_artefacts" / "Release" / "NanoFrogHeadlessTest"
LEVELS = ROOT / "Tools" / "bank_levels.json"
BANK = ROOT / "FactoryBank" / "NanoFrogFactory.json"
TARGET_DB = -17.0
SPECTRA = 3 * 24  # feature rows: 3 x 24 band spectra, then a 15-point RMS envelope


def measure():
    subprocess.run([sys.executable, str(ROOT / "Tools" / "gen_bank.py")], check=True,
                   stderr=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "f.tsv"
        env = dict(os.environ, NANOFROG_FACTORY_JSON=str(BANK),
                   NANOFROG_USER_DIR=str(Path(tmp) / "ud"), NANOFROG_DUMP_FEATURES=str(out))
        subprocess.run([str(TEST)], env=env, check=True, stdout=subprocess.DEVNULL)
        loud = {}
        for line in out.read_text().splitlines():
            f = line.split("\t")
            if int(f[3]) != 0:  # skip the MUTATE-ed copy
                continue
            loud[int(f[0])] = max(float(x) for x in f[4 + SPECTRA:])
    return loud


def main():
    LEVELS.unlink(missing_ok=True)  # measure the untrimmed bank
    first = measure()
    levels = {}
    for i, l in first.items():
        want = 0.85 * (TARGET_DB - l)
        levels[i] = max(0.2, min(1.25, 10 ** (want / 20)))
    LEVELS.write_text(json.dumps({str(k): round(v, 4) for k, v in sorted(levels.items())}, indent=0))
    final = measure()
    for lab, d in (("before", first), ("after ", final)):
        v = sorted(d.values())
        n = len(v)
        print(f"loudest 0.1 s {lab}: median {statistics.median(v):.1f} dB, "
              f"p5..p95 {v[n // 20]:.1f}..{v[n - 1 - n // 20]:.1f}, range {v[0]:.1f}..{v[-1]:.1f}")


if __name__ == "__main__":
    main()
