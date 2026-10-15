"""Measured loudness trims for the factory bank -> Tools/bank_levels.json.

Renders every preset (headless test, NANOFROG_DUMP_PITCH mode: C3 + E4,
0.10-0.47 s RMS) and sets output_level so the bank sits a little lower and
more even: presets above TARGET lose 75% of the excess (full RMS matching
over-cuts basses, which read hot on RMS), quieter ones gain half the
shortfall within the knob's headroom (max 1.25 = +1.9 dB). Run from a clean
slate (it deletes the old file first); gen_bank.py applies the file.

Usage (from NanoBerg/, headless test built): python3 Tools/level_bank.py
"""
import json, math, os, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "build" / "NanoFrogHeadlessTest_artefacts" / "Release" / "NanoFrogHeadlessTest"
LEVELS = ROOT / "Tools" / "bank_levels.json"
BANK = ROOT / "FactoryBank" / "NanoFrogFactory.json"
TARGET_DB = -21.5


def measure():
    subprocess.run([sys.executable, str(ROOT / "Tools" / "gen_bank.py")], check=True,
                   stderr=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "p.tsv"
        env = dict(os.environ, NANOFROG_FACTORY_JSON=str(BANK),
                   NANOFROG_USER_DIR=str(Path(tmp) / "ud"), NANOFROG_DUMP_PITCH=str(out))
        subprocess.run([str(TEST)], env=env, check=True, stdout=subprocess.DEVNULL)
        rms = {}
        for line in out.read_text().splitlines():
            f = line.split("\t")
            rms.setdefault(int(f[0]), []).append(float(f[4]))
    return {i: 20 * math.log10(max(sum(v) / len(v), 1e-6)) for i, v in rms.items()}


def main():
    LEVELS.unlink(missing_ok=True)  # measure the untrimmed bank
    first = measure()
    levels = {}
    for i, l in first.items():
        err = TARGET_DB - l
        want = err * 0.75 if err < 0 else min(err * 0.5, 1.9)
        levels[i] = max(0.2, min(1.25, 10 ** (want / 20)))
    LEVELS.write_text(json.dumps({str(k): round(v, 4) for k, v in sorted(levels.items())}, indent=0))
    final = measure()
    import statistics as st
    for lab, d in (("before", first), ("after ", final)):
        v = sorted(d.values())
        print(f"RMS {lab}: median {st.median(v):.1f} dB, p10..p90 {v[40]:.1f}..{v[360]:.1f}, loudest {v[-1]:.1f}")


if __name__ == "__main__":
    main()
