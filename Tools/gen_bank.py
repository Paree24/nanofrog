#!/usr/bin/env python3
"""NanoFrog factory bank generator — 400 original presets, deterministic.

Reads the live parameter order (kIds) and ranges from Source/PluginProcessor.cpp,
so the table can never drift from the plugin. Writes Source/Presets.cpp.

All names, recipes, and DSP are original work. Names are evocative of genre
archetypes (trance, house, dubstep, DnB, ...) without copying any hardware,
software, artist, or label names.

Usage: python3 Tools/gen_bank.py   (run from NanoFrog/)
"""
import random
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Source" / "PluginProcessor.cpp"
BANK_SRC = ROOT / "Source" / "PresetBank.cpp"

# ----------------------------------------------------------------order/ranges
src = SRC.read_text()
bank_src = BANK_SRC.read_text()
m = re.search(r"kParamIds\[kParamCount\] = \{(.*?)\};", bank_src, re.S)
assert m, "kParamIds table not found"
KIDS = re.findall(r'"([^"]+)"', m.group(1))
print(f"kIds: {len(KIDS)} params", file=sys.stderr)

RANGE = {}   # id -> (lo, hi, default)
NCHOICE = {}  # id -> num choices


def _parse_block(text, pre):
    for mm in re.finditer(
            r'\b([FCB])\s*\(\s*pre\s*,\s*"([^"]+)"\s*,\s*"[^"]*"\s*,(.*?)\)\s*;', text):
        kind, short, rest = mm.group(1), mm.group(2), mm.group(3)
        pid = pre + short
        if kind == "F":
            nums = [float(x) for x in re.findall(r"-?[\d.]+", rest)]
            assert len(nums) >= 3, (pid, rest)
            RANGE[pid] = (nums[0], nums[1], nums[2])
        elif kind == "C":
            n = rest.count('"') // 2
            NCHOICE[pid] = n
        else:
            RANGE[pid] = (0.0, 1.0, 1.0 if "true" in rest else 0.0)


# addTimbre("") / addTimbre("t2_") bodies + addVoice + shared handful
body = src[src.index("auto addTimbre"):src.index("return layout;")]
_parse_block(body, "")
_parse_block(body, "t2_")


def _parse_shared(text):
    for mm in re.finditer(
            r'\b([FCB])\s*\(\s*""\s*,\s*"([^"]+)"\s*,\s*"[^"]*"\s*,(.*?)\)\s*;', text):
        kind, pid, rest = mm.group(1), mm.group(2), mm.group(3)
        if kind == "F":
            nums = [float(x) for x in re.findall(r"-?[\d.]+", rest)]
            assert len(nums) >= 3, (pid, rest)
            RANGE[pid] = (nums[0], nums[1], nums[2])
        elif kind == "C":
            NCHOICE[pid] = rest.count('"') // 2
        else:
            RANGE[pid] = (0.0, 1.0, 1.0 if "true" in rest else 0.0)


_parse_shared(body)
# osc digital lists + lfo sync lists are built inline:
for pre in ("", "t2_"):
    for osc in ("osc1_digital", "osc2_digital"):
        NCHOICE[pre + osc] = 128 + 64
    for lfo in ("lfo1_sync", "lfo2_sync"):
        NCHOICE[pre + lfo] = 16
    NCHOICE[pre + "osc1_wave"] = 7
    NCHOICE[pre + "osc2_wave"] = 7
    NCHOICE[pre + "osc_mod"] = 4
    NCHOICE[pre + "filter_type"] = 4
    NCHOICE[pre + "lfo1_wave"] = 5
    NCHOICE[pre + "lfo2_wave"] = 5
    for i in (1, 2, 3, 4):
        NCHOICE[pre + f"mod{i}_src"] = 9
        NCHOICE[pre + f"mod{i}_dst"] = 10
        RANGE[pre + f"mod{i}_amt"] = (-1.0, 1.0, 0.0)
NCHOICE["modfx_type"] = 4
NCHOICE["delay_type"] = 3
NCHOICE["arp_mode"] = 6
NCHOICE["arp_rate"] = 6
RANGE["tmix"] = (0.0, 1.0, 0.0)

for k in KIDS:
    assert k in RANGE or k in NCHOICE, f"no range for {k}"
print("ranges ok", file=sys.stderr)

# ------------------------------------------------------------------- helpers
W_SAW, W_SQR, W_TRI, W_SIN, W_DIG, W_NOI, W_USR = range(7)
F_LP24, F_LP12, F_BP12, F_HP12 = range(4)
L_SIN, L_TRI, L_SQR, L_SAW, L_RND = range(5)
SYNC_DIV = {"1/2": 4, "1/4": 7, "1/8": 10, "1/16": 13}
ARP_RATE = {"1/4": 0, "1/8": 1, "1/16": 2, "1/8T": 3, "1/16T": 4, "1/32": 5}
# digital families: index%8 -> 0 analog 1 reed 2 bell 3 metal 4 vocal 5 bright 6 wood 7 detune
DIG = {fam: [fam + 8 * v + 64 * b for b in (0, 1) for v in range(8)]
       for fam in range(8)}


def base_shared(rng):
    return {
        "modfx_type": 0, "modfx_rate": 0.25, "modfx_depth": 0.25, "modfx_mix": 0.0,
        "delay_type": 0, "delay_time": 0.35, "delay_feedback": 0.25, "delay_mix": 0.0,
        "eq_low": 0.0, "eq_high": 0.0,
        "arp_on": 0, "arp_mode": 0, "arp_rate": 2, "arp_gate": 0.8,
        "arp_octaves": 1, "arp_swing": 0.0, "arp_latch": 0,
        "voice_poly": 1, "voice_detune": 0.0, "voice_portamento": 0.0, "voice_vib": 0.0,
        "t2_voice_poly": 1, "t2_voice_detune": 0.0,
        "t2_voice_portamento": 0.0, "t2_voice_vib": 0.0,
        "output_level": 1.0, "master_tune": 0.0, "song_tempo": 120.0, "tmix": 0.0,
        "limiter": 1,
    }


def base_timbre():
    return {
        "osc1_wave": W_SAW, "osc1_digital": 0, "osc1_octave": 0, "osc1_pitch": 0,
        "osc1_fine": 0.0, "osc1_level": 1.0, "osc1_shape": 0.0, "osc1_pwm": 0.5,
        "osc1_xmod": 0.0, "osc_mod": 0,
        "osc2_wave": W_SAW, "osc2_octave": 0, "osc2_pitch": 0, "osc2_fine": 0.0,
        "osc2_level": 0.0, "osc2_shape": 0.0, "osc2_pwm": 0.5, "osc2_digital": 0,
        "osc2_xmod": 0.0,
        "mix_o1": 1.0, "mix_o2": 0.0, "mix_noise": 0.0, "fm_amt": 0.0,
        "filter_type": F_LP24, "filter_cutoff": 1.0, "filter_reso": 0.0,
        "filter_keytrack": 0.0, "filter_envamt": 0.0,
        "amp_level": 1.0, "amp_pan": 0.0, "amp_velocity": 0.0,
        "amp_dist": 0, "amp_drive": 0.0,
        "env1_a": 0.0, "env1_d": 0.0, "env1_s": 1.0, "env1_r": 0.1,
        "env2_a": 0.0, "env2_d": 0.0, "env2_s": 1.0, "env2_r": 0.1,
        "lfo1_wave": L_SIN, "lfo1_rate": 0.5, "lfo1_depth": 0.0,
        "lfo2_wave": L_SIN, "lfo2_rate": 0.5, "lfo2_depth": 0.0,
        "lfo1_sync": 0, "lfo2_sync": 0,
        "mod1_src": 2, "mod1_dst": 4, "mod1_amt": 0.0,
        "mod2_src": 0, "mod2_dst": 0, "mod2_amt": 0.0,
        "mod3_src": 3, "mod3_dst": 1, "mod3_amt": 0.0,
        "mod4_src": 4, "mod4_dst": 6, "mod4_amt": 0.0,
    }


def apply(prefix, dst, d):
    for k, v in d.items():
        dst[prefix + k] = v


def derive_t2(t1, rng, style):
    """Timbre 2 starts as a copy of timbre 1, then diverges per style."""
    t2 = dict(t1)
    if style == "copy":
        t2["voice_detune"] = min(1.0, t1.get("voice_detune", 0.0) + rng.uniform(0.05, 0.15))
        t2["amp_pan"] = -t1.get("amp_pan", 0.0)
    elif style == "octave_up":
        t2["osc1_octave"] = min(2, t1.get("osc1_octave", 0) + 1)
        t2["osc2_octave"] = min(2, t1.get("osc2_octave", 0) + 1)
        t2["filter_cutoff"] = min(1.0, t1.get("filter_cutoff", 1.0) + 0.1)
        t2["amp_pan"] = 0.3
    elif style == "octave_down":
        t2["osc1_octave"] = max(-2, t1.get("osc1_octave", 0) - 1)
        t2["osc2_octave"] = max(-2, t1.get("osc2_octave", 0) - 1)
        t2["amp_pan"] = -0.3
    elif style == "wide":
        t2["voice_detune"] = min(1.0, t1.get("voice_detune", 0.0) + rng.uniform(0.2, 0.4))
        t2["osc1_fine"] = -t1.get("osc1_fine", 0.0) + rng.uniform(-8, 8)
        t2["amp_pan"] = 0.4 if t1.get("amp_pan", 0.0) <= 0 else -0.4
    elif style == "dark":
        t2["filter_cutoff"] = max(0.0, t1.get("filter_cutoff", 1.0) - rng.uniform(0.15, 0.3))
        t2["filter_reso"] = min(1.0, t1.get("filter_reso", 0.0) + 0.1)
    # keep voice counts matched
    t2["voice_poly"] = t1.get("voice_poly", 1)
    return t2


def arp_on(shared, rng, tempo, rate="1/16", mode=None, gate=None, octs=1):
    shared["arp_on"] = 1
    shared["song_tempo"] = float(tempo)
    shared["arp_rate"] = ARP_RATE[rate]
    shared["arp_mode"] = rng.randint(0, 5) if mode is None else mode
    shared["arp_gate"] = rng.uniform(0.5, 0.85) if gate is None else gate
    shared["arp_octaves"] = octs


def space(shared, rng, chorus=None, delay=None, verb_eq=None):
    if chorus is None:
        chorus = rng.random() < 0.5
    if chorus:
        shared["modfx_type"] = rng.randint(1, 3)
        shared["modfx_rate"] = rng.uniform(0.15, 0.4)
        shared["modfx_depth"] = rng.uniform(0.2, 0.5)
        shared["modfx_mix"] = rng.uniform(0.15, 0.35)
    if delay is None:
        delay = rng.random() < 0.5
    if delay:
        shared["delay_type"] = rng.randint(0, 2)
        shared["delay_time"] = rng.uniform(0.25, 0.5)
        shared["delay_feedback"] = rng.uniform(0.2, 0.4)
        shared["delay_mix"] = rng.uniform(0.1, 0.3)


# ------------------------------------------------------------------- recipes
def r_init(rng, v):
    # Vanilla but complete: single saw + a whisper of detuned osc2.
    t1 = base_timbre()
    t1.update(osc2_wave=W_SAW, osc2_level=0.5, osc2_fine=4.0, mix_o2=0.5)
    return t1, {}, {"tmix": 0.0}


def r_init_lead(rng, v):
    # Fat-but-neutral lead starter: two detuned saws, open filter, mono.
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=W_SAW, osc2_level=0.8, osc2_fine=8.0,
              mix_o2=0.8, filter_cutoff=0.8, filter_reso=0.1,
              filter_envamt=0.25, filter_keytrack=0.5,
              env1_a=0.0, env1_d=0.2, env1_s=0.8, env1_r=0.15)
    return t1, {}, {"voice_poly": 1, "voice_detune": 0.12,
                    "voice_portamento": 0.0, "song_tempo": 120.0}


def _bass_common(t1, rng, cutoff, reso, octv=-1, wave=W_SAW):
    t1.update(osc1_wave=wave, osc1_octave=octv, filter_cutoff=cutoff,
              filter_reso=reso, env1_a=0.0, env1_d=rng.uniform(0.1, 0.25),
              env1_s=rng.uniform(0.3, 0.7), env1_r=0.1,
              env2_a=0.0, env2_d=0.05, env2_s=1.0, env2_r=0.08,
              filter_envamt=rng.uniform(0.3, 0.6),
              # weight: detuned second osc under everything + gentle saturation
              osc2_wave=rng.choice([W_SAW, W_SAW, W_SQR, W_TRI]),
              osc2_level=0.55, osc2_fine=rng.uniform(-7, 7),
              mix_o2=0.55, amp_drive=0.18, amp_dist=1)


def r_sub(rng, v):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.2, 0.35), rng.uniform(0.0, 0.15),
                 octv=-2 if v % 2 == 0 else -1,
                 wave=W_SIN if v % 3 else W_TRI)
    t1["filter_keytrack"] = 0.5
    # subs stay clean but warm
    t1.update(amp_drive=0.12, amp_dist=0)
    return t1, {}, {}


def r_acid(rng, v):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.3, 0.5), rng.uniform(0.55, 0.8),
                 wave=W_SAW if v % 2 == 0 else W_SQR)
    t1.update(osc1_pwm=rng.uniform(0.2, 0.4) if t1["osc1_wave"] == W_SQR else 0.5,
              filter_envamt=rng.uniform(0.5, 0.85),
              env1_d=rng.uniform(0.15, 0.3), env1_s=0.2,
              amp_drive=rng.uniform(0.2, 0.4), amp_dist=1)
    sh = {"song_tempo": float(rng.choice([124, 128, 130, 135]))}
    if v % 4 == 3:
        arp_on(sh, rng, sh["song_tempo"], rate="1/16", mode=0, gate=0.6)
    return t1, {}, sh


def r_reese(rng, v):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.4, 0.6), rng.uniform(0.2, 0.45), octv=-1)
    t1.update(osc2_wave=W_SAW, osc2_level=0.9,
              osc2_fine=rng.uniform(14, 26) * rng.choice([-1.0, 1.0]),
              mix_o2=0.9, filter_envamt=0.35, env1_d=0.3, env1_s=0.8,
              amp_drive=rng.uniform(0.4, 0.65), amp_dist=1)
    v0 = {"voice_detune": rng.uniform(0.5, 0.7), "voice_poly": 1,  # unison spread
          "voice_portamento": 0.0}  # tight retrigger: never drift
    return t1, {}, {"song_tempo": 174.0, **v0}


def r_wobble(rng, v, tempo=140):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.45, 0.6), rng.uniform(0.35, 0.6), octv=-1)
    div = rng.choice(["1/8", "1/8", "1/4", "1/16"])
    t1.update(lfo1_wave=L_SQR if v % 2 else L_SAW, lfo1_sync=SYNC_DIV[div],
              lfo1_depth=0.7, mod1_src=2, mod1_dst=4,
              mod1_amt=rng.uniform(0.5, 0.8),
              osc2_wave=W_SAW, osc2_level=0.7, osc2_fine=rng.uniform(-12, 12),
              mix_o2=0.7, amp_drive=rng.uniform(0.35, 0.6), amp_dist=1,
              env1_s=0.9, env1_d=0.2)
    return t1, {}, {"song_tempo": float(tempo)}


def r_growl(rng, v):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.4, 0.55), rng.uniform(0.4, 0.65), octv=-1,
                 wave=W_DIG)
    t1.update(osc1_digital=rng.choice(DIG[3] + DIG[5]),
              osc1_xmod=rng.uniform(0.4, 0.8), osc1_shape=rng.uniform(0.2, 0.5),
              lfo1_wave=L_SAW, lfo1_rate=rng.uniform(0.55, 0.75), lfo1_depth=0.6,
              mod1_src=2, mod1_dst=2, mod1_amt=rng.uniform(0.3, 0.6),
              mod2_src=2, mod2_dst=4, mod2_amt=rng.uniform(0.3, 0.5),
              amp_drive=rng.uniform(0.4, 0.7), amp_dist=1,
              fm_amt=rng.uniform(0.15, 0.3), # FM snarl under the growl
              env1_s=0.9, env1_d=0.25)
    return t1, {}, {"song_tempo": float(rng.choice([140, 145, 150]))}


def r_gritty(rng, v):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.3, 0.45), rng.uniform(0.3, 0.55), octv=-1,
                 wave=W_SQR if v % 2 else W_SAW)
    t1.update(amp_drive=rng.uniform(0.35, 0.65), amp_dist=1,
              filter_envamt=0.55, env1_d=rng.uniform(0.12, 0.22), env1_s=0.5,
              osc1_pwm=rng.uniform(0.25, 0.45))
    return t1, {}, {"song_tempo": float(rng.choice([124, 126, 128]))}


def r_house_bass(rng, v):
    t1 = base_timbre()
    _bass_common(t1, rng, rng.uniform(0.35, 0.5), rng.uniform(0.1, 0.3), octv=-1,
                 wave=rng.choice([W_SAW, W_SQR, W_SQR]))
    t1.update(env1_d=rng.uniform(0.1, 0.2), env1_s=rng.uniform(0.4, 0.7),
              filter_envamt=0.45, amp_velocity=0.4,
              amp_drive=rng.uniform(0.2, 0.35), amp_dist=1)
    sh = {"song_tempo": float(rng.choice([122, 124, 126, 128]))}
    space(sh, rng, chorus=False, delay=rng.random() < 0.25)
    return t1, {}, sh


def r_lead_saw(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=W_SAW, osc2_level=0.8,
              osc2_fine=rng.uniform(6, 14), mix_o2=0.8,
              filter_cutoff=rng.uniform(0.65, 0.9), filter_reso=rng.uniform(0.1, 0.3),
              filter_envamt=0.3, env1_a=0.02, env1_d=0.25, env1_s=0.8,
              filter_keytrack=0.5)
    if rng.random() < 0.4:
        t1.update(osc2_wave=W_DIG, osc2_digital=rng.choice(DIG[5]),
                  osc2_level=0.5, osc2_fine=rng.uniform(-5, 5))
    v0 = {"voice_detune": rng.uniform(0.2, 0.4), "voice_poly": 1,
          "voice_portamento": 0.0}
    sh = {}
    space(sh, rng, chorus=True, delay=True)
    return t1, {}, {**sh, **v0}


def r_lead_sync(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc_mod=2, osc1_xmod=rng.uniform(0.3, 0.6),
              osc2_wave=W_SAW, osc2_level=0.8, osc2_fine=4.0, mix_o2=0.8,
              filter_cutoff=rng.uniform(0.5, 0.7), filter_reso=rng.uniform(0.3, 0.5),
              filter_envamt=0.5, env1_a=0.0, env1_d=0.3, env1_s=0.7,
              amp_drive=0.25, amp_dist=1 if v % 2 else 0)
    return t1, {}, {"voice_detune": rng.uniform(0.1, 0.25), "voice_poly": 1}


def r_lead_square(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SQR, osc1_pwm=rng.uniform(0.25, 0.4),
              osc2_wave=W_SQR, osc2_level=0.6, osc2_fine=6.0, mix_o2=0.6,
              filter_cutoff=rng.uniform(0.6, 0.85), filter_reso=0.15,
              filter_envamt=0.25)
    v0 = {"voice_vib": rng.uniform(0.1, 0.3), "voice_poly": 1,
          "voice_portamento": 0.0}
    sh = {}
    space(sh, rng, chorus=True, delay=True)
    return t1, {}, {**sh, **v0}


def r_lead_vox(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_DIG, osc1_digital=rng.choice(DIG[4]),
              osc1_shape=rng.uniform(0.2, 0.5),
              filter_cutoff=0.7, filter_reso=0.2, filter_envamt=0.3,
              # formant wobble (shape), not pitch: constant vibrato on a
              # vocal timbre reads as broken tuning
              lfo1_wave=L_SIN, lfo1_rate=0.45, lfo1_depth=0.3,
              mod1_src=2, mod1_dst=2, mod1_amt=rng.uniform(0.3, 0.5),
              mod2_src=2, mod2_dst=0, mod2_amt=0.0,
              osc2_wave=W_SIN, osc2_octave=1, osc2_level=0.3, mix_o2=0.3)
    return t1, {}, {"voice_vib": 0.25, "voice_poly": 1}


def r_pad(rng, v, bright=0.5):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=W_SAW, osc2_level=0.8, mix_o2=0.8,
              osc2_fine=rng.uniform(-10, 10))
    if rng.random() < 0.34:
        # square fifth instead of plain doubling
        t1.update(osc2_wave=W_SQR, osc2_pitch=7, osc2_level=0.6)
    t1.update(
              filter_cutoff=rng.uniform(bright - 0.15, bright + 0.15),
              filter_reso=rng.uniform(0.05, 0.2),
              env1_a=rng.uniform(0.15, 0.3), env1_d=0.3, env1_s=0.9, env1_r=0.4,
              env2_a=rng.uniform(0.15, 0.3), env2_d=0.2, env2_s=0.9, env2_r=0.4,
              filter_envamt=0.2)
    v0 = {"voice_poly": rng.choice([4, 6, 8]), "voice_detune": rng.uniform(0.15, 0.35)}
    sh = {}
    space(sh, rng, chorus=True, delay=True)
    return t1, {}, {**sh, **v0}


def r_strings(rng, v):
    t1, _, sh = r_pad(rng, v, bright=0.6)
    t1.update(env1_a=rng.uniform(0.15, 0.3), env2_a=t1["env1_a"],
              osc1_wave=W_SAW, filter_keytrack=0.4)
    return t1, {}, sh


def _pad_env(t1, rng, slow=0.25):
    # Present from the first beat: audible attack, still a swell.
    t1.update(env1_a=rng.uniform(slow - 0.1, slow + 0.1),
              env1_d=0.3, env1_s=0.9, env1_r=rng.uniform(0.4, 0.6),
              env2_a=t1.get("env1_a", slow), env2_d=0.2, env2_s=0.9,
              env2_r=rng.uniform(0.4, 0.6))


def r_pad_air(rng, v):
    # Airy bandpass/highpass wash with zero moving pitch sources: no LFO
    # routings at all. Motion comes only from phaser (allpass: no pitch
    # shift) + delay. Saws, not triangles: a highpass has nothing to pass
    # from harmonic-poor waves (triangle C3 through 1 kHz HP is -36 dB).
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=W_SAW, osc2_octave=1, osc2_level=0.6,
              mix_o2=0.6, osc2_fine=rng.uniform(-6, 6),
              filter_type=rng.choice([F_BP12, F_HP12]),
              filter_cutoff=rng.uniform(0.35, 0.55),
              filter_reso=rng.uniform(0.08, 0.18), filter_envamt=0.15)
    _pad_env(t1, rng)
    sh = {"modfx_type": 3, "modfx_rate": rng.uniform(0.08, 0.18),
          "modfx_depth": rng.uniform(0.2, 0.35), "modfx_mix": rng.uniform(0.15, 0.3),
          "delay_type": 2, "delay_time": rng.uniform(0.3, 0.45),
          "delay_feedback": rng.uniform(0.2, 0.3),
          "delay_mix": rng.uniform(0.1, 0.2)}
    v0 = {"voice_poly": rng.choice([4, 6, 8]),
          "voice_detune": rng.uniform(0.1, 0.2)}
    return t1, {}, {**sh, **v0}


def r_pad_glass(rng, v):
    # Crystalline digital over a sine sub, shimmering shape drift.
    fam = rng.choice([2, 2, 5, 1])
    t1 = base_timbre()
    t1.update(osc1_wave=W_DIG, osc1_digital=rng.choice(DIG[fam]),
              osc1_octave=1, osc2_wave=W_SIN, osc2_octave=-1, osc2_level=0.5,
              mix_o2=0.5, filter_cutoff=rng.uniform(0.55, 0.8),
              filter_reso=0.05, filter_envamt=0.1,
              lfo1_wave=L_SIN, lfo1_rate=rng.uniform(0.12, 0.3), lfo1_depth=0.4,
              mod1_src=2, mod1_dst=2, mod1_amt=rng.uniform(0.2, 0.35))
    _pad_env(t1, rng, slow=0.25)
    v0 = {"voice_poly": rng.choice([4, 6, 8]),
          "voice_detune": rng.uniform(0.08, 0.2)}
    sh = {}
    space(sh, rng, chorus=False, delay=True)
    return t1, {}, {**sh, **v0}


def r_pad_pulse(rng, v):
    # PWM string machine: squares a fifth apart, breathing width, phaser.
    t1 = base_timbre()
    t1.update(osc1_wave=W_SQR, osc1_pwm=rng.uniform(0.25, 0.4),
              osc2_wave=W_SQR, osc2_pitch=7, osc2_level=0.7, mix_o2=0.7,
              osc2_pwm=rng.uniform(0.25, 0.4),
              filter_cutoff=rng.uniform(0.5, 0.7),
              filter_reso=rng.uniform(0.05, 0.15), filter_envamt=0.2,
              lfo1_wave=L_TRI, lfo1_rate=rng.uniform(0.1, 0.22), lfo1_depth=0.5,
              mod1_src=2, mod1_dst=2, mod1_amt=rng.uniform(0.25, 0.4))
    _pad_env(t1, rng, slow=0.35)
    v0 = {"voice_poly": rng.choice([4, 6]), "voice_detune": rng.uniform(0.12, 0.25)}
    sh = {"modfx_type": 3, "modfx_rate": rng.uniform(0.1, 0.25),
          "modfx_depth": rng.uniform(0.3, 0.5), "modfx_mix": rng.uniform(0.2, 0.35)}
    space(sh, rng, chorus=False, delay=True)
    return t1, {}, {**sh, **v0}


def r_pad_fifth(rng, v):
    # Dark evolving fifth stack: low cutoff that opens slowly under the chord.
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=W_SAW, osc2_pitch=7, osc2_level=0.8,
              mix_o2=0.8, osc2_fine=rng.uniform(-6, 6),
              filter_cutoff=rng.uniform(0.3, 0.45),
              filter_reso=rng.uniform(0.15, 0.3),
              filter_envamt=rng.uniform(0.35, 0.55),
              env1_a=rng.uniform(0.25, 0.4), env1_d=0.4, env1_s=0.85,
              env1_r=rng.uniform(0.4, 0.6),
              env2_a=0.3, env2_d=0.2, env2_s=0.9,
              env2_r=rng.uniform(0.4, 0.6))
    v0 = {"voice_poly": rng.choice([4, 6, 8]),
          "voice_detune": rng.uniform(0.15, 0.3)}
    sh = {}
    space(sh, rng, chorus=True, delay=True)
    return t1, {}, {**sh, **v0}


def r_pad_voxpad(rng, v):
    # Fixed-vowel wash: the vowel is chosen, never morphed (morph LFOs read
    # as pitch drift on formants). Motion comes only from phaser + delay.
    t1 = base_timbre()
    t1.update(osc1_wave=W_DIG, osc1_digital=rng.choice(DIG[4]),
              osc1_shape=rng.uniform(0.35, 0.55),
              osc2_wave=W_TRI, osc2_level=0.4, mix_o2=0.4,
              filter_cutoff=rng.uniform(0.55, 0.7),
              filter_reso=0.1, filter_envamt=0.15)
    _pad_env(t1, rng, slow=0.25)
    sh = {"modfx_type": 3, "modfx_rate": rng.uniform(0.08, 0.15),
          "modfx_depth": rng.uniform(0.25, 0.4), "modfx_mix": rng.uniform(0.2, 0.3),
          "delay_type": 2, "delay_time": rng.uniform(0.3, 0.45),
          "delay_feedback": 0.25, "delay_mix": 0.15}
    v0 = {"voice_poly": rng.choice([6, 8]),
          "voice_detune": rng.uniform(0.1, 0.2)}
    return t1, {}, {**sh, **v0}


def r_arp(rng, v, tempo=138, wave=W_SAW):
    t1 = base_timbre()
    t1.update(osc1_wave=wave,
              osc1_digital=rng.choice(DIG[0] + DIG[5]) if wave == W_DIG else 0,
              filter_cutoff=rng.uniform(0.55, 0.8), filter_reso=rng.uniform(0.1, 0.3),
              filter_envamt=0.35, env1_a=0.0, env1_d=0.2, env1_s=0.4, env1_r=0.15,
              env2_a=0.0, env2_d=0.1, env2_s=0.6, env2_r=0.1)
    if rng.random() < 0.5:
        t1.update(osc2_wave=rng.choice([W_SAW, W_SQR]), osc2_level=0.6,
                  osc2_octave=rng.choice([0, 1]),
                  osc2_pitch=rng.choice([0, 0, 7]), mix_o2=0.5)
    sh = {}
    arp_on(sh, rng, tempo, rate=rng.choice(["1/16", "1/16", "1/8", "1/8T"]),
           gate=rng.uniform(0.5, 0.7), octs=rng.choice([1, 1, 2]))
    space(sh, rng, chorus=rng.random() < 0.4, delay=True)
    return t1, {}, sh


def r_seq(rng, v, tempo=120):
    t1, _, sh = r_arp(rng, v, tempo=tempo,
                      wave=rng.choice([W_SAW, W_SQR, W_DIG]))
    sh["arp_latch"] = 1
    t1["mod2_src"] = 0
    t1["mod2_dst"] = 4
    t1["mod2_amt"] = rng.uniform(0.1, 0.3)
    return t1, {}, sh


def r_ep(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SIN, osc2_wave=W_SIN, osc2_octave=1, osc2_level=0.4,
              mix_o2=0.4, filter_cutoff=0.7, filter_reso=0.05,
              env2_a=0.0, env2_d=rng.uniform(0.3, 0.5), env2_s=0.3, env2_r=0.25,
              lfo1_wave=L_SIN, lfo1_rate=0.5, lfo1_depth=0.25,
              mod1_src=2, mod1_dst=6, mod1_amt=rng.uniform(0.1, 0.2),
              amp_velocity=0.6,
              fm_amt=rng.uniform(0.25, 0.5)) # sine->sine DX-style bite
    return t1, {}, {"voice_poly": 6}


def r_organ(rng, v, drive=0.0):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SIN, osc2_wave=W_SIN, osc2_octave=1, osc2_level=0.6,
              mix_o2=0.6, filter_cutoff=0.85, env2_a=0.0, env2_d=0.05,
              env2_s=1.0, env2_r=0.05, amp_drive=drive,
              amp_dist=1 if drive > 0.3 else 0, amp_velocity=0.3)
    sh = {}
    space(sh, rng, chorus=True, delay=False)
    return t1, {}, {**sh, "voice_poly": 8}


def r_clav(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SQR, osc1_pwm=rng.uniform(0.15, 0.3),
              osc2_wave=W_SQR, osc2_level=0.4, osc2_fine=5.0, mix_o2=0.4,
              filter_cutoff=0.75, filter_reso=0.1, filter_envamt=0.4,
              env2_a=0.0, env2_d=rng.uniform(0.2, 0.35), env2_s=0.2, env2_r=0.1,
              amp_velocity=0.5)
    return t1, {}, {"voice_poly": 6}


def r_bell(rng, v):
    t1 = base_timbre()
    fam = rng.choice([2, 2, 1, 5])
    t1.update(osc1_wave=W_DIG, osc1_digital=rng.choice(DIG[fam]),
              osc1_octave=rng.choice([0, 1]), filter_cutoff=0.8,
              env2_a=0.0, env2_d=rng.uniform(0.4, 0.6), env2_s=0.1,
              env2_r=rng.uniform(0.4, 0.6),
              fm_amt=rng.uniform(0.15, 0.35)) # metallic FM shimmer
    if rng.random() < 0.6:
        t1.update(osc2_wave=W_SIN, osc2_octave=-1, osc2_level=0.4, mix_o2=0.4)
    sh = {}
    space(sh, rng, chorus=False, delay=True)
    return t1, {}, {**sh, "voice_poly": 8}


def r_pluck(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=rng.choice([W_SAW, W_SQR, W_TRI, W_DIG]),
              osc1_digital=rng.choice(DIG[6] + DIG[0]) if t1["osc1_wave"] == W_DIG else 0,
              filter_cutoff=rng.uniform(0.6, 0.85), filter_reso=0.1,
              filter_envamt=rng.uniform(0.4, 0.65),
              env1_a=0.0, env1_d=rng.uniform(0.15, 0.3), env1_s=0.1, env1_r=0.15,
              env2_a=0.0, env2_d=rng.uniform(0.15, 0.3), env2_s=0.2, env2_r=0.15,
              amp_velocity=0.5)
    if rng.random() < 0.3:
        t1.update(osc2_wave=W_SAW, osc2_pitch=7, osc2_level=0.5, mix_o2=0.4)
    sh = {}
    space(sh, rng, chorus=False, delay=True)
    return t1, {}, {**sh, "voice_poly": 4}


def r_stab(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=rng.choice([W_SAW, W_SQR]),
              osc2_level=0.8, mix_o2=0.8, osc2_fine=rng.uniform(-8, 8),
              filter_cutoff=rng.uniform(0.5, 0.7), filter_reso=0.25,
              filter_envamt=0.5, env1_a=0.0, env1_d=0.12, env1_s=0.1, env1_r=0.1,
              env2_a=0.0, env2_d=0.12, env2_s=0.2, env2_r=0.1)
    return t1, {}, {"voice_poly": rng.choice([3, 4, 6])}


def r_brass(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, osc2_wave=W_SAW, osc2_level=0.7, mix_o2=0.7,
              osc2_fine=5.0, filter_cutoff=0.6, filter_reso=0.1,
              filter_envamt=0.4, env1_a=rng.uniform(0.05, 0.12), env1_d=0.2,
              env1_s=0.85, env1_r=0.15, env2_a=0.05, env2_d=0.15, env2_s=0.9)
    return t1, {}, {"voice_poly": 6}


def r_choir(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_DIG, osc1_digital=rng.choice(DIG[4]),
              osc1_shape=rng.uniform(0.1, 0.4), osc2_wave=W_SIN, osc2_level=0.4,
              mix_o2=0.4, filter_cutoff=0.65, filter_reso=0.1,
              env1_a=0.2, env1_d=0.3, env1_s=0.9, env1_r=0.35,
              env2_a=0.2, env2_d=0.2, env2_s=0.9, env2_r=0.35,
              lfo1_wave=L_SIN, lfo1_rate=0.4, lfo1_depth=0.2,
              mod1_src=2, mod1_dst=2, mod1_amt=rng.uniform(0.2, 0.3))
    sh = {}
    space(sh, rng, chorus=True, delay=True)
    return t1, {}, {**sh, "voice_poly": 8,
                    "voice_detune": rng.uniform(0.1, 0.25)}


def r_sfx(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=rng.choice([W_SAW, W_DIG, W_NOI]),
              osc1_digital=rng.choice(DIG[3] + DIG[7]),
              mix_noise=rng.uniform(0.3, 0.8),
              filter_cutoff=rng.uniform(0.3, 0.9), filter_reso=rng.uniform(0.3, 0.7),
              lfo1_wave=L_RND, lfo1_rate=rng.uniform(0.4, 0.8), lfo1_depth=0.8,
              mod1_src=3, mod1_dst=4, mod1_amt=rng.uniform(0.3, 0.7),
              env1_a=rng.uniform(0.0, 0.4), env1_d=0.3, env1_s=0.5, env1_r=0.4,
              env2_a=rng.uniform(0.0, 0.4), env2_d=0.3, env2_s=0.6, env2_r=0.4,
              amp_drive=rng.uniform(0.2, 0.5), amp_dist=1 if v % 2 else 0)
    sh = {}
    space(sh, rng, chorus=rng.random() < 0.3, delay=rng.random() < 0.4)
    return t1, {}, {**sh, "voice_poly": 1}


def r_hit(rng, v):
    t1, _, sh = r_stab(rng, v)
    t1.update(mix_noise=rng.uniform(0.1, 0.3), amp_drive=0.3, amp_dist=1,
              env2_r=0.3)
    return t1, {}, sh


def r_riser(rng, v, up=True):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SAW, mix_noise=0.6, filter_type=F_LP24,
              filter_cutoff=0.15 if up else 0.9, filter_reso=0.4,
              env1_a=0.7, env1_d=0.2, env1_s=1.0 if up else 0.0, env1_r=0.3,
              filter_envamt=0.8 if up else -0.6,
              env2_a=0.7 if up else 0.0, env2_d=0.2, env2_s=1.0, env2_r=0.3)
    return t1, {}, {"voice_poly": 1}


def r_kick808(rng, v):
    t1 = base_timbre()
    t1.update(osc1_wave=W_SIN, osc1_octave=-2, filter_cutoff=0.5,
              env2_a=0.0, env2_d=rng.uniform(0.35, 0.55), env2_s=0.0,
              env2_r=0.1, mix_noise=0.08, amp_drive=0.25, amp_dist=1)
    return t1, {}, {"voice_poly": 1}


def r_lofi(rng, v):
    t1, _, sh = r_ep(rng, v)
    t1.update(filter_cutoff=0.55, amp_drive=0.3, amp_dist=1,
              lfo1_wave=L_SIN, lfo1_rate=0.35, lfo1_depth=0.3,
              mod1_src=2, mod1_dst=6, mod1_amt=rng.uniform(0.1, 0.2))
    return t1, {}, sh


# name, recipe, layer-style (None = near-copy T2, tmix 0)
PATCHES = [
    ("Init", r_init, None, {}),
    ("Init Lead", r_init_lead, None, {}),
    ("Init Sub", r_sub, None, {}),
    ("Init Pad", r_pad, "wide", {"tmix": 0.5}),
    ("Init Spark", r_arp, None, {"song_tempo": 120.0}),
    # TRANCE (32)
    ("Neon Ascension", r_arp, None, {"song_tempo": 138.0}),
    ("Euphoria Engine", r_lead_saw, "wide", {"tmix": 0.5}),
    ("Skyline Runner", r_arp, None, {"song_tempo": 140.0}),
    ("Aurora Anthem", r_lead_saw, None, {}),
    ("Sunset Gate", r_pad_fifth, "wide", {"tmix": 0.5}),
    ("Airwave Dream", r_lead_square, None, {}),
    ("Luminous Drift", r_pad_air, "octave_up", {"tmix": 0.5}),
    ("Cascade Heaven", r_arp, "octave_up", {"tmix": 0.5, "song_tempo": 138.0}),
    ("Afterhours Glow", r_pad_air, None, {}),
    ("Serotonin Rush", r_arp, None, {"song_tempo": 142.0}),
    ("Cloudsurfer", r_lead_saw, None, {}),
    ("Halo Circuit", r_seq, None, {"song_tempo": 138.0}),
    ("Velvet Sunrise", r_pad_pulse, "wide", {"tmix": 0.5}),
    ("Starlight Drive", r_lead_sync, None, {}),
    ("Open Air", r_arp, None, {"song_tempo": 136.0}),
    ("Till Dawn", r_pad_pulse, None, {}),
    ("Eternity Chord", r_strings, "wide", {"tmix": 0.5}),
    ("Hands Up", r_lead_saw, "octave_up", {"tmix": 0.5}),
    ("Baltic Breeze", r_arp, None, {"song_tempo": 138.0}),
    ("Solaris Flare", r_lead_sync, None, {}),
    ("Dreamstate", r_pad, "dark", {"tmix": 0.5}),
    ("Nightflight", r_seq, None, {"song_tempo": 140.0}),
    ("Pure Elevation", r_lead_saw, None, {}),
    ("Moonlit Terrace", r_arp, None, {"song_tempo": 134.0}),
    ("Gravity Choir", r_choir, "wide", {"tmix": 0.5}),
    ("Illuminate", r_lead_square, "octave_up", {"tmix": 0.5}),
    ("White Island", r_arp, None, {"song_tempo": 138.0}),
    ("Unity Chant", r_pad_pulse, "wide", {"tmix": 0.5}),
    ("Overdrive Hymn", r_lead_saw, None, {}),
    ("Blue Horizon", r_arp, "octave_up", {"tmix": 0.5, "song_tempo": 137.0}),
    ("Ever Young", r_strings, None, {}),
    ("Anthem Peak", r_lead_saw, "wide", {"tmix": 0.5}),
    # TECHNO / HOUSE (36)
    ("Warehouse Pulse", r_house_bass, None, {"song_tempo": 132.0}),
    ("Concrete Groove", r_house_bass, None, {"song_tempo": 130.0}),
    ("Rotterdam Raw", r_gritty, None, {"song_tempo": 145.0}),
    ("Acid Rainline", r_acid, None, {"song_tempo": 135.0}),
    ("Jack Track", r_house_bass, None, {"song_tempo": 124.0}),
    ("Rave Stabber", r_stab, None, {"song_tempo": 140.0}),
    ("Loft Party", r_arp, None, {"song_tempo": 122.0}),
    ("Basement Cut", r_house_bass, None, {"song_tempo": 128.0}),
    ("Needle Drop", r_lead_sync, None, {}),
    ("Peak Time Driver", r_gritty, None, {"song_tempo": 138.0}),
    ("Hypnotik Loop", r_seq, None, {"song_tempo": 132.0}),
    ("Dusty Groove", r_lofi, None, {}),
    ("Metal Box", r_sfx, None, {}),
    ("Siren Test", r_sfx, None, {}),
    ("Smoke Machine", r_pad, "dark", {"tmix": 0.5}),
    ("Four AM Floor", r_house_bass, None, {"song_tempo": 126.0}),
    ("Bleep Test", r_sfx, None, {}),
    ("Rave Whistle", r_lead_square, None, {}),
    ("Overdrive Unit", r_gritty, None, {"song_tempo": 140.0}),
    ("Monotone", r_house_bass, None, {"song_tempo": 133.0}),
    ("Filter Freak", r_acid, None, {"song_tempo": 130.0}),
    ("Clap Back", r_hit, None, {}),
    ("Shuffle Play", r_arp, None, {"song_tempo": 124.0}),
    ("Deep Pan", r_sub, None, {}),
    ("Sub Terrain", r_sub, None, {}),
    ("Ghost Note", r_house_bass, None, {"song_tempo": 127.0}),
    ("Rave Cave", r_pad, "dark", {"tmix": 0.5}),
    ("Strobe Light", r_arp, None, {"song_tempo": 140.0}),
    ("Power Chord", r_stab, "octave_down", {"tmix": 0.5}),
    ("Loopy Lou", r_seq, None, {"song_tempo": 128.0}),
    ("Analog Riot", r_lead_saw, None, {}),
    ("Bassbin", r_sub, None, {}),
    ("Night Shift", r_house_bass, None, {"song_tempo": 125.0}),
    ("Sweatbox", r_acid, None, {"song_tempo": 138.0}),
    ("Encore", r_lead_saw, "wide", {"tmix": 0.5}),
    ("Last Train", r_pad, None, {}),
    # BASS HOUSE (24)
    ("Gutter Riddim", r_gritty, None, {"song_tempo": 126.0}),
    ("Wobble Dome", r_wobble, None, {"song_tempo": 126.0}),
    ("Low Slung", r_house_bass, None, {"song_tempo": 125.0}),
    ("Grease Trap", r_gritty, None, {"song_tempo": 127.0}),
    ("Donk Chamber", r_gritty, None, {"song_tempo": 128.0}),
    ("Jackin Box", r_house_bass, None, {"song_tempo": 126.0}),
    ("Rinse Cycle", r_wobble, None, {"song_tempo": 124.0}),
    ("Filthy Habit", r_gritty, None, {"song_tempo": 125.0}),
    ("Bumpy Night", r_house_bass, None, {"song_tempo": 127.0}),
    ("Wobble Wagon", r_wobble, None, {"song_tempo": 128.0}),
    ("Distorted Reality", r_gritty, "octave_down", {"tmix": 0.5, "song_tempo": 126.0}),
    ("Sub Contract", r_sub, None, {}),
    ("Bounce House", r_house_bass, None, {"song_tempo": 128.0}),
    ("Grime Time", r_growl, None, {"song_tempo": 126.0}),
    ("Wobble Board", r_wobble, None, {"song_tempo": 125.0}),
    ("Speaker Box", r_gritty, None, {"song_tempo": 127.0}),
    ("Sleazy Rider", r_house_bass, None, {"song_tempo": 124.0}),
    ("Fidget Factor", r_gritty, None, {"song_tempo": 128.0}),
    ("Wobble Theory", r_wobble, None, {"song_tempo": 126.0}),
    ("Bassline Criminal", r_gritty, None, {"song_tempo": 125.0}),
    ("Ruffneck", r_growl, None, {"song_tempo": 127.0}),
    ("Shuffle Demon", r_house_bass, None, {"song_tempo": 126.0}),
    ("Low End Leader", r_sub, "octave_down", {"tmix": 0.5}),
    ("Wobble Outro", r_wobble, None, {"song_tempo": 124.0}),
    # DUBSTEP (32)
    ("Tearout Titan", r_growl, None, {"song_tempo": 150.0}),
    ("Wobble Monster", r_wobble, None, {"song_tempo": 140.0}),
    ("Dungeon Crawl", r_sub, "dark", {"tmix": 0.5, "song_tempo": 140.0}),
    ("Riddim Kid", r_gritty, None, {"song_tempo": 145.0}),
    ("Growl Garden", r_growl, None, {"song_tempo": 140.0}),
    ("Bass Howitzer", r_gritty, "octave_down", {"tmix": 0.5, "song_tempo": 150.0}),
    ("Wub Machine", r_wobble, None, {"song_tempo": 142.0}),
    ("Mechanical Maw", r_growl, None, {"song_tempo": 148.0}),
    ("Deep Meditation", r_sub, None, {"song_tempo": 140.0}),
    ("Wobble Warden", r_wobble, None, {"song_tempo": 140.0}),
    ("Laser Guided", r_growl, None, {"song_tempo": 150.0}),
    ("Submerged", r_sub, "dark", {"tmix": 0.5, "song_tempo": 142.0}),
    ("Chainsaw Ballet", r_growl, "octave_down", {"tmix": 0.5, "song_tempo": 150.0}),
    ("Wobble Goblin", r_wobble, None, {"song_tempo": 138.0}),
    ("Bass Pressure", r_gritty, None, {"song_tempo": 140.0}),
    ("Growl Power", r_growl, None, {"song_tempo": 145.0}),
    ("Half Time Hero", r_sub, None, {"song_tempo": 140.0}),
    ("Wobble Circus", r_wobble, None, {"song_tempo": 144.0}),
    ("Metal Mouth", r_growl, None, {"song_tempo": 150.0}),
    ("Abyss Walker", r_sub, "dark", {"tmix": 0.5, "song_tempo": 138.0}),
    ("Riddim Royale", r_gritty, None, {"song_tempo": 148.0}),
    ("Wobble Sauce", r_wobble, None, {"song_tempo": 140.0}),
    ("Transistor Form", r_growl, None, {"song_tempo": 142.0}),
    ("Sub Freezing", r_sub, None, {"song_tempo": 140.0}),
    ("Bass Face", r_gritty, None, {"song_tempo": 150.0}),
    ("Growl School", r_growl, None, {"song_tempo": 140.0}),
    ("Wobble Works", r_wobble, None, {"song_tempo": 146.0}),
    ("Cellar Dweller", r_sub, "dark", {"tmix": 0.5, "song_tempo": 140.0}),
    ("Riddim Reactor", r_growl, None, {"song_tempo": 148.0}),
    ("Wobble Warrior", r_wobble, None, {"song_tempo": 140.0}),
    ("Guttural", r_growl, "octave_down", {"tmix": 0.5, "song_tempo": 145.0}),
    ("Weight Plate", r_gritty, None, {"song_tempo": 140.0}),
    # DNB / JUNGLE (32)
    ("Reese Criminal", r_reese, None, {}),
    ("Techstep Terror", r_reese, "dark", {"tmix": 0.5}),
    ("Jungle Protocol", r_house_bass, None, {"song_tempo": 170.0}),
    ("Breakbeat Science", r_stab, None, {"song_tempo": 170.0}),
    ("Reese Lightning", r_reese, "wide", {"tmix": 0.5}),
    ("Neurotic Funk", r_growl, None, {"song_tempo": 174.0}),
    ("Liquid Soul", r_pad, "wide", {"tmix": 0.5}),
    ("Reese Reserve", r_reese, None, {}),
    ("Valve Room", r_sub, None, {"song_tempo": 170.0}),
    ("Dark Roller", r_reese, "dark", {"tmix": 0.5}),
    ("Reese Wraith", r_reese, "octave_up", {"tmix": 0.5}),
    ("Chopped Break", r_stab, None, {"song_tempo": 168.0}),
    ("Techstep Tunnel", r_growl, None, {"song_tempo": 172.0}),
    ("Reese Heritage", r_reese, None, {}),
    ("Jungle Calls", r_lead_square, None, {}),
    ("Sub Assault", r_sub, "octave_down", {"tmix": 0.5, "song_tempo": 174.0}),
    ("Reese Monsoon", r_reese, "wide", {"tmix": 0.5}),
    ("Halfstepper", r_sub, None, {"song_tempo": 170.0}),
    ("Container Ship", r_gritty, None, {"song_tempo": 172.0}),
    ("Reese Anatomy", r_reese, None, {}),
    ("Ninety Four", r_house_bass, None, {"song_tempo": 168.0}),
    ("Stratosphere", r_pad, "octave_up", {"tmix": 0.5}),
    ("Reese Riot", r_reese, "octave_down", {"tmix": 0.5}),
    ("Ragga Tip", r_house_bass, None, {"song_tempo": 170.0}),
    ("Reese Relic", r_reese, None, {}),
    ("Drum Funk", r_stab, None, {"song_tempo": 172.0}),
    ("Reese Runner", r_reese, None, {}),
    ("Green Valley", r_pad, None, {}),
    ("Reese Ransom", r_reese, "wide", {"tmix": 0.5}),
    ("Jump Up Junkie", r_gritty, None, {"song_tempo": 174.0}),
    ("Reese Radiance", r_reese, "octave_up", {"tmix": 0.5}),
    ("Intelligent Waves", r_pad, "wide", {"tmix": 0.5}),
    # ELECTRONICA / IDM (28)
    ("Glitch Ghost", r_sfx, None, {}),
    ("Bleep Census", r_sfx, None, {"song_tempo": 100.0}),
    ("Grain Storm", r_sfx, "wide", {"tmix": 0.5}),
    ("IDM Dream", r_pad_voxpad, None, {}),
    ("Stutter Step", r_seq, None, {"song_tempo": 128.0}),
    ("Bit Crush Hour", r_gritty, None, {"song_tempo": 110.0}),
    ("Warped Factor", r_growl, None, {"song_tempo": 120.0}),
    ("Mind Acrobat", r_sfx, None, {}),
    ("Drill Sergeant", r_gritty, None, {"song_tempo": 160.0}),
    ("Ambient Volunteer", r_pad_voxpad, "wide", {"tmix": 0.5}),
    ("Click Track", r_sfx, None, {}),
    ("Generative Study", r_seq, None, {"song_tempo": 100.0}),
    ("Modem Noise", r_sfx, None, {}),
    ("Ghost Frequencies", r_pad_voxpad, "dark", {"tmix": 0.5}),
    ("Microsound", r_sfx, None, {}),
    ("Folding Sine", r_ep, None, {}),
    ("Data Garden", r_arp, None, {"song_tempo": 110.0}),
    ("Glass Lazers", r_lead_sync, None, {}),
    ("Polygon Park", r_pad, None, {}),
    ("Vowel Soup", r_lead_vox, None, {}),
    ("Tape Loop", r_lofi, None, {}),
    ("Bent Circuit", r_sfx, None, {}),
    ("Sleep Research", r_pad, "dark", {"tmix": 0.5}),
    ("Reflex Test", r_seq, None, {"song_tempo": 132.0}),
    ("Slow Motion", r_pad, None, {"song_tempo": 80.0}),
    ("Wonky Tonk", r_lofi, None, {"song_tempo": 95.0}),
    ("Future Carpark", r_gritty, None, {"song_tempo": 130.0}),
    ("Skwee Ball", r_lead_square, None, {}),
    # HIPHOP / TRAP / VINTAGE (32)
    ("808 Menace", r_kick808, "octave_down", {"tmix": 0.5}),
    ("Dirty Third", r_gritty, None, {"song_tempo": 90.0}),
    ("Boom Clap", r_hit, None, {"song_tempo": 92.0}),
    ("Westside Glide", r_lead_saw, None, {"song_tempo": 90.0}),
    ("Memphis Grit", r_gritty, None, {"song_tempo": 88.0}),
    ("Trap Door", r_kick808, None, {"song_tempo": 140.0}),
    ("808 Lullaby", r_kick808, None, {"song_tempo": 70.0}),
    ("Crunked Up", r_gritty, "octave_down", {"tmix": 0.5, "song_tempo": 95.0}),
    ("Chopped Late", r_lofi, None, {"song_tempo": 85.0}),
    ("Valley Whistle", r_lead_square, None, {}),
    ("808 Avalanche", r_kick808, "dark", {"tmix": 0.5, "song_tempo": 150.0}),
    ("Uptown Story", r_ep, None, {}),
    ("Vinyl Dust", r_lofi, None, {"song_tempo": 84.0}),
    ("Heartbroken 808", r_kick808, None, {"song_tempo": 75.0}),
    ("Talkbox Romeo", r_lead_vox, None, {}),
    ("808 Militia", r_kick808, None, {"song_tempo": 145.0}),
    ("Cadillac Dreams", r_lead_saw, None, {"song_tempo": 88.0}),
    ("Slow Lean", r_lofi, None, {"song_tempo": 70.0}),
    ("808 Wizard", r_kick808, "wide", {"tmix": 0.5, "song_tempo": 140.0}),
    ("Horror Flick", r_pad, "dark", {"tmix": 0.5, "song_tempo": 90.0}),
    ("808 Sermon", r_kick808, None, {"song_tempo": 135.0}),
    ("Funky Relative", r_clav, None, {"song_tempo": 95.0}),
    ("808 Operator", r_kick808, None, {"song_tempo": 148.0}),
    ("Cloud Walker", r_pad_air, None, {"song_tempo": 85.0}),
    ("808 Oracle", r_kick808, "octave_down", {"tmix": 0.5, "song_tempo": 130.0}),
    ("Hyphy Movement", r_gritty, None, {"song_tempo": 100.0}),
    ("808 Omen", r_kick808, None, {"song_tempo": 142.0}),
    ("Lofi Study", r_lofi, None, {"song_tempo": 80.0}),
    ("808 Outlaw", r_kick808, None, {"song_tempo": 138.0}),
    ("Southern Smoke", r_sub, None, {"song_tempo": 90.0}),
    ("808 Odyssey", r_kick808, "wide", {"tmix": 0.5, "song_tempo": 140.0}),
    ("Block Party", r_ep, None, {"song_tempo": 95.0}),
    # KEYS (24)
    ("Velvet EP", r_ep, None, {}),
    ("Suitcase Keys", r_ep, "octave_up", {"tmix": 0.5}),
    ("Dyno Day", r_ep, None, {}),
    ("Gospel Keys", r_organ, "wide", {"tmix": 0.5}),
    ("Tonewheel", r_organ, None, {"amp_drive": 0.0}),
    ("Clav Funk", r_clav, None, {"song_tempo": 105.0}),
    ("Harpsi Rock", r_pluck, None, {}),
    ("Jewelry Box", r_bell, None, {}),
    ("Plastic Piano", r_bell, "octave_up", {"tmix": 0.5}),
    ("Felt Piano", r_ep, None, {}),
    ("Church Organ", r_organ, "octave_down", {"tmix": 0.5}),
    ("Jazz Organ", r_organ, None, {"amp_drive": 0.2}),
    ("Stage Piano", r_ep, "wide", {"tmix": 0.5}),
    ("Funky Clav", r_clav, None, {"song_tempo": 100.0}),
    ("Rotary Club", r_organ, None, {}),
    ("Saloon Piano", r_ep, None, {}),
    ("Seventy Three", r_ep, None, {}),
    ("Gospel Organ", r_organ, "wide", {"tmix": 0.5}),
    ("Shoulder Synth", r_lead_saw, None, {}),
    ("Street Accordion", r_organ, None, {}),
    ("Celestial Keys", r_bell, "wide", {"tmix": 0.5}),
    ("Motor Vibe", r_bell, None, {}),
    ("Wood Bars", r_pluck, None, {}),
    ("Thumb Piano", r_bell, "octave_up", {"tmix": 0.5}),
    # RETRO / SYNTHWAVE (28)
    ("Neon Grid", r_house_bass, None, {"song_tempo": 110.0}),
    ("Chrome Sunset", r_lead_saw, "wide", {"tmix": 0.5}),
    ("VHS Dreams", r_pad_air, "wide", {"tmix": 0.5}),
    ("Laser Highway", r_arp, None, {"song_tempo": 118.0}),
    ("Midnight Overdrive", r_gritty, None, {"song_tempo": 112.0}),
    ("Outrider", r_arp, None, {"song_tempo": 120.0}),
    ("Palm Static", r_pad, None, {}),
    ("Strip Mall Sunset", r_ep, None, {"song_tempo": 100.0}),
    ("Grid Glider", r_seq, None, {"song_tempo": 115.0}),
    ("Turbo Teen", r_lead_square, None, {}),
    ("Cassette Ghost", r_pad, "dark", {"tmix": 0.5}),
    ("Neon Palms", r_house_bass, None, {"song_tempo": 108.0}),
    ("After Midnight", r_sub, None, {"song_tempo": 105.0}),
    ("Chrome Heart", r_lead_sync, None, {}),
    ("VHS Tracking", r_arp, None, {"song_tempo": 122.0}),
    ("Mall Soft", r_pad, None, {}),
    ("Sunset Chaser", r_lead_saw, None, {}),
    ("Analog Rain", r_arp, None, {"song_tempo": 100.0}),
    ("Night Caller", r_sub, None, {"song_tempo": 110.0}),
    ("Roller Rink", r_ep, None, {"song_tempo": 112.0}),
    ("Laser Tag", r_lead_sync, None, {}),
    ("Instant Photo", r_pad, "wide", {"tmix": 0.5}),
    ("Arcade Fever", r_arp, None, {"song_tempo": 128.0}),
    ("Miami Nice", r_house_bass, None, {"song_tempo": 114.0}),
    ("Neon Sign", r_lead_saw, None, {}),
    ("Rewind", r_seq, None, {"song_tempo": 110.0}),
    ("CRT Glow", r_pad, "dark", {"tmix": 0.5}),
    ("Top Down", r_lead_saw, "octave_up", {"tmix": 0.5}),
    # PADS & STRINGS (32)
    ("Velvet Glacier", r_pad_glass, "wide", {"tmix": 0.5}),
    ("Cathedral Air", r_pad, "octave_up", {"tmix": 0.5}),
    ("Wool Blanket", r_pad_fifth, None, {}),
    ("Glasshouse", r_pad_glass, "wide", {"tmix": 0.5}),
    ("Polar Glow", r_pad_air, "octave_up", {"tmix": 0.5}),
    ("Silk Dunes", r_pad_fifth, None, {}),
    ("Heavy Fog", r_pad, "dark", {"tmix": 0.5}),
    ("Sunbaked", r_pad, None, {}),
    ("Ice Cathedral", r_pad_glass, "octave_up", {"tmix": 0.5}),
    ("Velvet Rope", r_strings, "wide", {"tmix": 0.5}),
    ("Tape Strings", r_strings, None, {}),
    ("Chamber Ensemble", r_strings, "wide", {"tmix": 0.5}),
    ("String Ensemble", r_strings, "octave_up", {"tmix": 0.5}),
    ("Slow Attack", r_pad_pulse, None, {}),
    ("Ninth Cloud", r_pad_air, "wide", {"tmix": 0.5}),
    ("Deep Field", r_pad, "dark", {"tmix": 0.5}),
    ("Shimmer Wall", r_pad_air, "octave_up", {"tmix": 0.5}),
    ("Frostbite", r_pad_glass, "octave_up", {"tmix": 0.5}),
    ("Warm Front", r_pad_fifth, None, {}),
    ("Cold Front", r_pad, "dark", {"tmix": 0.5}),
    ("Slow Evolution", r_pad, "wide", {"tmix": 0.5}),
    ("Particle Drift", r_pad_glass, "octave_up", {"tmix": 0.5}),
    ("Sacred Space", r_choir, "wide", {"tmix": 0.5}),
    ("Wide Open", r_pad_pulse, "wide", {"tmix": 0.5}),
    ("Dark Energy", r_pad, "dark", {"tmix": 0.5}),
    ("Light Leak", r_pad_air, "octave_up", {"tmix": 0.5}),
    ("Undertow", r_pad_fifth, "dark", {"tmix": 0.5}),
    ("High Tide", r_pad_fifth, "wide", {"tmix": 0.5}),
    ("Low Mist", r_pad, "dark", {"tmix": 0.5}),
    ("Golden Hour", r_pad_pulse, None, {}),
    ("Blue Hour", r_pad_glass, "dark", {"tmix": 0.5}),
    ("Afterglow", r_pad_voxpad, "wide", {"tmix": 0.5}),
    # LEADS (28)
    ("Screamer", r_lead_saw, None, {}),
    ("Velvet Knife", r_lead_square, None, {}),
    ("Feedback Loop", r_lead_sync, None, {}),
    ("Laser Fingers", r_lead_saw, "octave_up", {"tmix": 0.5}),
    ("Tin Whistle", r_lead_square, "octave_up", {"tmix": 0.5}),
    ("Perfect Fifth", r_lead_saw, None, {}),    ("Octave Leopard", r_lead_saw, "octave_up", {"tmix": 0.5}),
    ("Glide Path", r_lead_saw, None, {}),
    ("Showtime", r_lead_saw, "wide", {"tmix": 0.5}),
    ("Shred Ready", r_lead_sync, None, {}),
    ("Prima Donna", r_lead_square, None, {}),
    ("Air Guitar", r_lead_saw, None, {}),
    ("Electric Fiddle", r_lead_square, "octave_up", {"tmix": 0.5}),
    ("Silver Flute", r_lead_vox, "octave_up", {"tmix": 0.5}),
    ("Midnight Sax", r_lead_vox, None, {}),
    ("Pocket Trumpet", r_lead_square, None, {}),
    ("Blues Harp", r_lead_square, None, {}),
    ("Highland Drone", r_lead_saw, "octave_down", {"tmix": 0.5}),
    ("Electric Sitar", r_pluck, "octave_down", {"tmix": 0.5}),
    ("Neon Koto", r_pluck, None, {}),
    ("Ghost Hands", r_lead_vox, None, {}),
    ("Mouth Tube", r_lead_vox, "wide", {"tmix": 0.5}),
    ("Robot Voice", r_lead_vox, None, {}),
    ("Bullhorn", r_lead_square, None, {}),
    ("Stadium Horn", r_lead_saw, "wide", {"tmix": 0.5}),
    ("Siren Song", r_sfx, None, {}),
    ("Cop Siren", r_sfx, None, {}),
    ("Opera Singer", r_lead_vox, "octave_up", {"tmix": 0.5}),
    # BELLS & PLUCKS (20)
    ("Glass Bell", r_bell, None, {}),
    ("Temple Bell", r_bell, "octave_down", {"tmix": 0.5}),
    ("Wind Chime", r_bell, "octave_up", {"tmix": 0.5}),
    ("Icicle", r_bell, "octave_up", {"tmix": 0.5}),
    ("Music Box Dancer", r_bell, "octave_up", {"tmix": 0.5}),
    ("Electric FM", r_bell, None, {}),
    ("Vibra Tone", r_bell, None, {}),
    ("Oil Barrel", r_bell, "octave_down", {"tmix": 0.5}),
    ("Steel Tongue", r_bell, None, {}),
    ("Mbira Nights", r_pluck, "octave_up", {"tmix": 0.5}),
    ("Bottle Blower", r_bell, None, {}),
    ("Wine Glass", r_bell, "octave_up", {"tmix": 0.5}),
    ("Church Chimes", r_bell, "wide", {"tmix": 0.5}),
    ("Chicken Pluckin", r_pluck, None, {}),
    ("Three Strings", r_pluck, "octave_down", {"tmix": 0.5}),
    ("Backporch", r_pluck, None, {}),
    ("Harp Gliss", r_pluck, "octave_up", {"tmix": 0.5}),
    ("Pizz Party", r_pluck, None, {}),
    ("Nylon Dreams", r_pluck, None, {}),
    ("Double Course", r_pluck, "wide", {"tmix": 0.5}),
    # BRASS & CHOIR (16)
    ("Section Brass", r_brass, "wide", {"tmix": 0.5}),
    ("Muted Horn", r_brass, None, {}),
    ("Stadium Brass", r_brass, "octave_down", {"tmix": 0.5}),
    ("Grand Fanfare", r_brass, "octave_up", {"tmix": 0.5}),
    ("Slider", r_lead_saw, None, {}),
    ("Hunting Horn", r_brass, None, {}),
    ("Oompah", r_brass, "octave_down", {"tmix": 0.5}),
    ("Doomsday Fall", r_brass, None, {}),
    ("Choir Ahh", r_choir, None, {}),
    ("Choir Ohh", r_choir, "dark", {"tmix": 0.5}),
    ("Monks At Dawn", r_choir, "octave_down", {"tmix": 0.5}),
    ("Youth Choir", r_choir, "octave_up", {"tmix": 0.5}),
    ("Gospel Choir", r_choir, "wide", {"tmix": 0.5}),
    ("Four Voices", r_choir, None, {}),
    ("Throat Singer", r_lead_vox, "octave_down", {"tmix": 0.5}),
    ("Mouth Drums", r_sfx, None, {}),
    # SFX & HITS (20)
    ("Impact Zone", r_hit, "octave_down", {"tmix": 0.5}),
    ("Twelve Bar Riser", r_riser, None, {}),
    ("Downlifter", r_sfx, None, {}),
    ("Alarm Bells", r_sfx, None, {}),
    ("Laser Battle", r_sfx, "octave_up", {"tmix": 0.5}),
    ("Power Down", r_sfx, "octave_down", {"tmix": 0.5}),
    ("Air Siren", r_sfx, None, {}),
    ("Distant Blast", r_sfx, "dark", {"tmix": 0.5}),
    ("Starter Pistol", r_hit, None, {}),
    ("Stadium Roar", r_sfx, "wide", {"tmix": 0.5}),
    ("Thunder Roll", r_sfx, "dark", {"tmix": 0.5}),
    ("Desert Rain", r_sfx, None, {}),
    ("Ocean Wave", r_sfx, "wide", {"tmix": 0.5}),
    ("Jungle Night", r_sfx, None, {}),
    ("City Traffic", r_sfx, None, {}),
    ("Robot Choir", r_choir, "dark", {"tmix": 0.5}),
    ("Alien Landing", r_sfx, "octave_up", {"tmix": 0.5}),
    ("Time Machine", r_sfx, "wide", {"tmix": 0.5}),
    ("Coin Shower", r_bell, "octave_up", {"tmix": 0.5}),
    ("Stabbed Orchestra", r_hit, "wide", {"tmix": 0.5}),
    # SEQUENCES (12)
    ("Capital Circuit", r_seq, None, {"song_tempo": 120.0}),
    ("Citrus Dream", r_seq, "wide", {"tmix": 0.5, "song_tempo": 110.0}),
    ("Step Runner", r_seq, None, {"song_tempo": 128.0}),
    ("Modular Jam", r_seq, None, {"song_tempo": 115.0}),
    ("Clockwork", r_seq, None, {"song_tempo": 132.0}),
    ("Generative Etude", r_seq, "wide", {"tmix": 0.5, "song_tempo": 100.0}),
    ("Euclids Groove", r_seq, None, {"song_tempo": 124.0}),
    ("Ratchet Set", r_seq, None, {"song_tempo": 140.0}),
    ("Silver Box", r_acid, None, {"song_tempo": 130.0}),
    ("Bassline Generator", r_seq, "octave_down", {"tmix": 0.5, "song_tempo": 126.0}),
    ("Arp Study Five", r_arp, None, {"song_tempo": 120.0}),
    ("Endless Stairs", r_seq, "octave_up", {"tmix": 0.5, "song_tempo": 118.0}),
]

# r_riser takes an up/down flag via variant: even=up, odd=down
_orig_riser = r_riser


def _riser_wrap(rng, v):
    return _orig_riser(rng, v if v % 2 == 0 else v)


r_riser = _riser_wrap
r_riser.__name__ = "r_riser"

# Groups are contiguous index ranges (see PresetMap.md bank layout).
GROUPS = [
    (0, 4, "Init"), (5, 36, "Trance"), (37, 72, "Techno & House"),
    (73, 96, "Bass House"), (97, 128, "Dubstep"), (129, 160, "DnB & Jungle"),
    (161, 188, "Electronica"), (189, 220, "HipHop & Trap"), (221, 244, "Keys"),
    (245, 272, "Retro & Synthwave"), (273, 304, "Pads & Strings"),
    (305, 332, "Leads"), (333, 352, "Bells & Plucks"),
    (353, 368, "Brass & Choir"), (369, 388, "SFX & Hits"),
    (389, 400, "Sequences"),
]

RECIPE_TAGS = {
    "r_init": ("Init", []), "r_init_lead": ("Lead", ["Init"]), "r_sub": ("Bass", ["Sub"]),
    "r_acid": ("Bass", ["Acid"]), "r_reese": ("Bass", ["Reese"]),
    "r_wobble": ("Bass", ["Wobble"]), "r_growl": ("Bass", ["Growl"]),
    "r_gritty": ("Bass", ["Gritty"]), "r_house_bass": ("Bass", []),
    "r_kick808": ("Bass", ["808", "Sub"]), "r_lead_saw": ("Lead", ["Saw"]),
    "r_lead_sync": ("Lead", ["Sync"]), "r_lead_square": ("Lead", ["Square"]),
    "r_lead_vox": ("Lead", ["Vocal"]), "r_lead_saw": ("Lead", ["Glide"]),
    "r_pad": ("Pad", []), "r_pad_air": ("Pad", ["Airy"]),
    "r_pad_glass": ("Pad", ["Glass"]), "r_pad_pulse": ("Pad", ["Pulse"]),
    "r_pad_fifth": ("Pad", ["Fifth"]), "r_pad_voxpad": ("Pad", ["Vocal"]), "r_strings": ("Pad", ["Strings"]),
    "r_arp": ("Arp", []), "r_seq": ("Sequence", []),
    "r_ep": ("Keys", ["EP"]), "r_organ": ("Keys", ["Organ"]),
    "r_clav": ("Keys", ["Clav"]), "r_bell": ("Bell", []),
    "r_pluck": ("Bell", ["Pluck"]), "r_stab": ("Hit", ["Stab"]),
    "r_hit": ("Hit", []), "r_brass": ("Brass", []),
    "r_choir": ("Choir", []), "r_sfx": ("SFX", []),
    "r_riser": ("SFX", ["Riser"]), "r_lofi": ("Keys", ["LoFi"]),
}


def tags_for(recipe_fn, t1, shared):
    # Category + character + state flags only — no collection/genre tags.
    cat, extra = RECIPE_TAGS[recipe_fn.__name__]
    tags = [cat] + list(extra)
    if shared.get("arp_on"):
        tags.append("Arp")
    if t1.get("amp_dist"):
        tags.append("Distorted")
    tags.append("Layer" if shared.get("tmix", 0.0) >= 0.5 else "Single")
    # de-dupe, preserve order
    seen, out = set(), []
    for t in tags:
        if t not in seen:
            seen.add(t)
            out.append(t)
    return out


# ------------------------------------------------------------------ assembly
assert len(PATCHES) == 401, f"{len(PATCHES)} patches, expected 401"
names = [p[0] for p in PATCHES]
assert len(set(names)) == 401, "duplicate preset names"
for n in names:
    assert n and len(n) <= 32, f"bad name: {n!r}"
    assert re.match(r"^[A-Za-z0-9 .'\-]+$", n), f"bad chars: {n!r}"


def build_one(idx, rng):
    name, recipe, layer, fixed = PATCHES[idx]
    t1, t2ov, sh = recipe(rng, idx)
    full = {}
    apply("", full, t1)
    if layer:
        t2 = derive_t2(t1, rng, layer)
        apply("t2_", full, t2)
        full["tmix"] = 0.5
    else:
        t2 = derive_t2(t1, rng, "copy")
        apply("t2_", full, t2)
        full["tmix"] = 0.0
    for k, v in t2ov.items():
        full["t2_" + k] = v
    shared = base_shared(rng)
    shared.update(sh)
    shared.update(fixed)
    full.update(shared)
    # ---- NO-DRIFT INVARIANTS (bank-wide, machine-checked) ----
    # 1. No modulation source may touch pitch with nonzero amount
    #    (t2 matrix is a copy of t1's: check both).
    for i in (1, 2, 3, 4):
        for pre in ("", "t2_"):
            if full.get(f"{pre}mod{i}_dst") in (0, 1):
                assert full.get(f"{pre}mod{i}_amt", 0.0) == 0.0, \
                    f"{name}: {pre}mod{i} targets pitch"
    # 2. No portamento anywhere: glides are pitch drift by definition.
    assert full.get("voice_portamento", 0.0) == 0.0, f"{name}: portamento"
    assert full.get("t2_voice_portamento", 0.0) == 0.0, f"{name}: t2 portamento"
    # 3. Chorus/flanger are delay-modulation: keep bank values in the
    #    musical zone (rewritten DSP: shallow sweeps, dry-anchored pitch).
    #    Phaser needs no caps (allpass: zero pitch shift).
    if full.get("modfx_type") in (1, 2):
        full["modfx_depth"] = min(full.get("modfx_depth", 0.0), 0.35)
        full["modfx_rate"] = min(full.get("modfx_rate", 0.0), 0.35)
    tags = tags_for(recipe, full_t1_probe(t1), shared)
    # validate + order
    vals = []
    for pid in KIDS:
        assert pid in full, f"{name}: missing {pid}"
        v = full[pid]
        if pid in NCHOICE:
            v = float(int(round(v)))
            assert 0 <= v < NCHOICE[pid], f"{name}: {pid}={v}"
        else:
            lo, hi, _d = RANGE[pid]
            v = float(min(hi, max(lo, v)))
        vals.append(v)
    return name, vals, tags


def full_t1_probe(t1partial):
    full = dict(base_timbre())
    full.update(t1partial)
    return full


def main():
    import json as _json
    rng = random.Random(20261007)
    bank = {"type": "NanoFrogBank", "version": 1, "param_count": len(KIDS),
            "param_ids": KIDS, "presets": []}
    for i in range(len(PATCHES)):
        name, vals, tags = build_one(i, rng)
        bank["presets"].append({"name": name, "tags": tags, "values": vals})
    outdir = ROOT / "FactoryBank"
    outdir.mkdir(exist_ok=True)
    (outdir / "NanoFrogFactory.json").write_text(_json.dumps(bank))
    print(f"wrote {len(PATCHES)} presets", file=sys.stderr)


main()
