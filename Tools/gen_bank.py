#!/usr/bin/env python3
"""NanoFrog factory bank generator: Init + 400 presets, deterministic.

Reads the live parameter order (kParamIds in Source/PresetBank.cpp) and the
ranges from Source/PluginProcessor.cpp, so the table can never drift from the
plugin. Writes FactoryBank/NanoFrogFactory.json, applying the loudness trims
in Tools/bank_levels.json (made by Tools/level_bank.py).

Presets are designed, not rolled: each archetype (808, reese, supersaw lead,
future pluck, ...) is a recipe in real units (seconds, Hz, cents) and its
variants pick from lists of musically sensible settings. All names and
recipes are original; no hardware, software, artist or label names.

Usage: python3 Tools/gen_bank.py   (run from NanoBerg/)
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
NCHOICE["arp_rate"] = 7  # ... 1/32, then 1/6 appended
for pre in ("", "t2_"):
    NCHOICE[pre + "lfo1_keysync"] = 3  # Off / Timbre / Voice (built inline)
    NCHOICE[pre + "lfo2_keysync"] = 3
for k in range(1, 9):
    RANGE[f"arp_step{k}"] = (0.0, 1.0, 1.0)  # built inline: on/off per step
RANGE["tmix"] = (0.0, 1.0, 0.0)

for k in KIDS:
    assert k in RANGE or k in NCHOICE, f"no range for {k}"
print("ranges ok", file=sys.stderr)

# ------------------------------------------------------------------- units
# Knob curves (Source/PluginProcessor.cpp, Source/DSP/Voice.h):
#   attack  = 0.002 + v^2 * 4 s      decay / release = 0.002 + v^2 * 8 s (to ~-60 dB)
#   cutoff  = 40 * 450^v Hz          LFO rate = 0.05 * 600^v Hz
#   mod amount 1.0 = +-12 semitones on pitch, +-0.5 of the cutoff knob
import math

W_SAW, W_SQR, W_TRI, W_SIN, W_DIG, W_NOI = range(6)
F_LP24, F_LP12, F_BP12, F_HP12 = range(4)
L_SIN, L_TRI, L_SQR, L_SAW, L_RND = range(5)
S_ENV1, S_ENV2, S_LFO1, S_LFO2, S_VEL, S_KEY, S_BEND, S_WHEEL, S_RAND = range(9)
D_PITCH, D_O2PITCH, D_O1SHAPE, D_NOISE, D_CUT, D_RESO, D_AMP, D_PAN, D_L1RATE, D_L2RATE = range(10)
# tempo divisions shared by the LFO and delay sync menus (0 = free)
DIV = {"1/1": 1, "3/4": 2, "1/2": 4, "3/8": 5, "1/4": 7, "3/16": 8, "1/6": 9,
       "1/8": 10, "3/32": 11, "1/12": 12, "1/16": 13, "1/32": 15}
ARP = {"1/4": 0, "1/8": 1, "1/16": 2, "1/8T": 3, "1/16T": 4, "1/32": 5, "1/6": 6}
ARP_UP, ARP_DOWN, ARP_ALT1, ARP_ALT2, ARP_RND, ARP_TRIG = range(6)
# digital waves: index % 8 = family, + 8 * variant (0..7), + 64 for the second set
FAM = {"analog": 0, "reed": 1, "bell": 2, "metal": 3, "vocal": 4, "bright": 5, "wood": 6, "detune": 7}


def dig(fam, var=0, alt=0):
    return FAM[fam] + 8 * (var % 8) + 64 * alt


def atk(sec):
    return round(math.sqrt(max(0.0, sec - 0.002) / 4.0), 4)


def tm(sec):
    return round(math.sqrt(max(0.0, min(8.0, sec) - 0.002) / 8.0), 4)


def fall(sec):
    """Decay / release knob for a sound that falls ~20 dB in `sec`: the
    knob's time is the fall to ~-60 dB (exponential), three times longer."""
    return tm(3.0 * sec)


def cut(hz):
    return round(min(1.0, max(0.0, math.log(hz / 40.0) / math.log(450.0))), 4)


def glide_knob(arrive):
    """Portamento knob for a slide that arrives in ~`arrive` seconds (the
    engine approaches the new pitch exponentially, tau = 2 * knob^2 s)."""
    return round(math.sqrt(max(0.0, arrive) / 6.0), 4)


def lrate(hz):
    return round(min(1.0, max(0.0, math.log(hz / 0.05) / math.log(600.0))), 4)


def base_timbre():
    return {
        "osc1_wave": W_SAW, "osc1_digital": 0, "osc1_octave": 0, "osc1_pitch": 0,
        "osc1_fine": 0.0, "osc1_level": 1.0, "osc1_shape": 0.0, "osc1_pwm": 0.0,
        "osc1_xmod": 0.0, "osc_mod": 0,
        "osc2_wave": W_SAW, "osc2_octave": 0, "osc2_pitch": 0, "osc2_fine": 0.0,
        "osc2_level": 0.0, "osc2_shape": 0.0, "osc2_pwm": 0.0, "osc2_digital": 0,
        "osc2_xmod": 0.0,
        "mix_o1": 0.8, "mix_o2": 0.0, "mix_noise": 0.0, "fm_amt": 0.0,
        "filter_type": F_LP24, "filter_cutoff": 1.0, "filter_reso": 0.0,
        "filter_keytrack": 0.5, "filter_envamt": 0.0,
        "amp_level": 1.0, "amp_pan": 0.0, "amp_velocity": 0.25,
        "amp_dist": 0, "amp_drive": 0.0,
        "env1_a": 0.0, "env1_d": tm(0.3), "env1_s": 0.0, "env1_r": tm(0.3),
        "env2_a": 0.0, "env2_d": tm(0.3), "env2_s": 1.0, "env2_r": tm(0.3),
        "lfo1_wave": L_SIN, "lfo1_rate": lrate(1.0), "lfo1_depth": 0.0,
        "lfo2_wave": L_SIN, "lfo2_rate": lrate(5.0), "lfo2_depth": 0.0,
        "lfo1_sync": 0, "lfo2_sync": 0, "lfo1_keysync": 2, "lfo2_keysync": 0,
        "mod1_src": S_LFO1, "mod1_dst": D_CUT, "mod1_amt": 0.0,
        "mod2_src": S_ENV1, "mod2_dst": D_PITCH, "mod2_amt": 0.0,
        "mod3_src": S_LFO2, "mod3_dst": D_O2PITCH, "mod3_amt": 0.0,
        "mod4_src": S_VEL, "mod4_dst": D_CUT, "mod4_amt": 0.0,
    }


def base_shared():
    return {
        "modfx_type": 0, "modfx_rate": 0.25, "modfx_depth": 0.25, "modfx_mix": 0.0,
        "delay_type": 0, "delay_time": 0.35, "delay_feedback": 0.25, "delay_mix": 0.0,
        "delay_sync": 0, "eq_low": 0.0, "eq_high": 0.0,
        "drive_on": 0, "drive_amount": 0.3, "drive_mix": 0.5,
        "sat_on": 0, "sat_amount": 0.3, "sat_tone": 0.6, "sat_mode": 0,
        "reverb_on": 0, "reverb_size": 0.45, "reverb_damp": 0.5, "reverb_mix": 0.0,
        "arp_on": 0, "arp_mode": 0, "arp_rate": ARP["1/16"], "arp_gate": 0.6,
        "arp_octaves": 1, "arp_swing": 0.0, "arp_latch": 0,
        "arp_last_step": 8, **{f"arp_step{k}": 1 for k in range(1, 9)},
        "output_level": 1.0, "master_tune": 0.0, "song_tempo": 124.0, "bend_range": 2.0,
        "tmix": 0.0, "limiter": 1,
    }


def base_voice(pre=""):
    return {pre + "voice_poly": 8, pre + "voice_detune": 0.0, pre + "voice_portamento": 0.0,
            pre + "voice_vib": 0.35, pre + "voice_assign": 0, pre + "voice_trigger": 1,
            pre + "voice_priority": 0, pre + "voice_unidet": 15.0}


# ------------------------------------------------------------------ patch
class Patch:
    """One preset: timbre 1, optional timbre 2 (layered by tmix), shared."""

    def __init__(self):
        self.t1 = base_timbre()
        self.t2 = None
        self.sh = base_shared()
        self.sh.update(base_voice(""))
        self.sh.update(base_voice("t2_"))

    # --- voice
    def mono(self, glide=0.0, legato=False, t2=True):   # glide: seconds to arrive
        for pre in ("", "t2_") if t2 else ("",):
            self.sh[pre + "voice_assign"] = 1
            self.sh[pre + "voice_poly"] = 1
            self.sh[pre + "voice_portamento"] = glide_knob(glide)
            self.sh[pre + "voice_trigger"] = 0 if legato else 1
        return self

    def unison(self, cents=20.0):
        for pre in ("", "t2_"):
            self.sh[pre + "voice_assign"] = 2
            self.sh[pre + "voice_poly"] = 1
            self.sh[pre + "voice_unidet"] = cents
        return self

    def spread(self, amount):
        self.sh["voice_detune"] = amount
        self.sh["t2_voice_detune"] = amount
        return self

    # --- timbre 2: a copy of timbre 1 that diverges, mixed in equal power
    def layer(self, mix=0.5, **changes):
        self.t2 = dict(self.t1)
        self.t2.update(changes)
        self.sh["tmix"] = mix
        return self

    def wide(self, cents=9.0, pan=0.35, mix=0.5):
        """Stereo double: timbre 2 detuned the other way, panned opposite."""
        self.layer(mix,
                   osc1_fine=self.t1["osc1_fine"] - cents, osc2_fine=self.t1["osc2_fine"] - cents,
                   amp_pan=pan)
        self.t1["osc1_fine"] += cents * 0.5
        self.t1["osc2_fine"] += cents * 0.5
        self.t1["amp_pan"] = -pan
        return self

    # --- shaping
    # times: attack in seconds; decay / release = seconds to fall ~20 dB
    def amp(self, a=0.0, d=0.3, s=1.0, r=0.3, vel=None):
        self.t1.update(env2_a=atk(a), env2_d=fall(d), env2_s=s, env2_r=fall(r))
        if vel is not None:
            self.t1["amp_velocity"] = vel
        return self

    def fenv(self, a=0.0, d=0.3, s=0.0, r=0.3, amt=0.0):
        self.t1.update(env1_a=atk(a), env1_d=fall(d), env1_s=s, env1_r=fall(r), filter_envamt=amt)
        return self

    def filt(self, hz, reso=0.0, kind=F_LP24, key=0.5):
        self.t1.update(filter_type=kind, filter_cutoff=cut(hz), filter_reso=reso, filter_keytrack=key)
        return self

    def mod(self, slot, src, dst, amt):
        self.t1.update({f"mod{slot}_src": src, f"mod{slot}_dst": dst, f"mod{slot}_amt": amt})
        return self

    def lfo1(self, wave=L_SIN, hz=1.0, depth=1.0, sync=None, key=2):
        self.t1.update(lfo1_wave=wave, lfo1_rate=lrate(hz), lfo1_depth=depth,
                       lfo1_sync=DIV[sync] if sync else 0, lfo1_keysync=key)
        return self

    def lfo2(self, wave=L_SIN, hz=5.0, depth=1.0, sync=None, key=0):
        self.t1.update(lfo2_wave=wave, lfo2_rate=lrate(hz), lfo2_depth=depth,
                       lfo2_sync=DIV[sync] if sync else 0, lfo2_keysync=key)
        return self

    def o1(self, wave, octave=0, pitch=0, fine=0.0, shape=0.0, level=0.8, digital=0, pwm=0.0):
        self.t1.update(osc1_wave=wave, osc1_octave=octave, osc1_pitch=pitch, osc1_fine=fine,
                       osc1_shape=shape, mix_o1=level, osc1_digital=digital, osc1_pwm=pwm)
        return self

    def o2(self, wave, octave=0, pitch=0, fine=0.0, shape=0.0, level=0.6, digital=0, pwm=0.0):
        self.t1.update(osc2_wave=wave, osc2_octave=octave, osc2_pitch=pitch, osc2_fine=fine,
                       osc2_shape=shape, mix_o2=level, osc2_level=1.0 if level > 0 else 0.0,
                       osc2_digital=digital, osc2_pwm=pwm)
        return self

    def noise(self, level):
        self.t1["mix_noise"] = level
        return self

    # --- effects (shared)
    def chorus(self, mix=0.3, depth=0.35, rate=0.25, kind=1):
        self.sh.update(modfx_type=kind, modfx_mix=mix, modfx_depth=depth, modfx_rate=rate)
        return self

    def delay(self, div="3/16", fb=0.3, mix=0.18, kind=2):
        self.sh.update(delay_sync=DIV[div], delay_feedback=fb, delay_mix=mix, delay_type=kind)
        return self

    def verb(self, mix=0.22, size=0.55, damp=0.5):
        self.sh.update(reverb_on=1, reverb_mix=mix, reverb_size=size, reverb_damp=damp)
        return self

    def drive(self, amount=0.3, mix=0.5):
        self.sh.update(drive_on=1, drive_amount=amount, drive_mix=mix)
        return self

    def sat(self, amount=0.3, tone=0.6, tube=False):
        self.sh.update(sat_on=1, sat_amount=amount, sat_tone=tone, sat_mode=1 if tube else 0)
        return self

    def eq(self, low=0.0, high=0.0):
        self.sh.update(eq_low=low, eq_high=high)
        return self

    def arp(self, rate="1/16", mode=ARP_UP, gate=0.55, octaves=1, tempo=124.0, swing=0.0,
            steps=None, latch=False):
        self.sh.update(arp_on=1, arp_rate=ARP[rate], arp_mode=mode, arp_gate=gate,
                       arp_octaves=octaves, song_tempo=tempo, arp_swing=swing,
                       arp_latch=1 if latch else 0)
        if steps:
            self.sh["arp_last_step"] = len(steps)
            for k in range(8):
                self.sh[f"arp_step{k + 1}"] = 1 if k < len(steps) and steps[k] == "x" else 0
        return self

    def keep_in_tune(self, limit=24.0):
        """Fine tune + DETUNE spread (+-15 c x detune, +25 c x detune on a mono
        stack) stays within `limit` cents per oscillator, so chords and
        unisons never read as out of key. Unison assign (4 voices) is free."""
        for pre, t in (("", self.t1), ("t2_", self.t2)):
            if t is None:
                continue
            det = self.sh[pre + "voice_detune"]
            uni = 25.0 * det if self.sh[pre + "voice_poly"] == 1 and det > 0.01 else 0.0
            for o, sign in (("osc1", -15.0), ("osc2", 15.0)):
                room = max(0.0, limit - uni)
                spread = sign * det
                t[o + "_fine"] = min(room - spread, max(-room - spread, t[o + "_fine"]))

    def values(self):
        self.keep_in_tune()
        vals = {}
        for k, v in self.t1.items():
            vals[k] = v
        t2 = self.t2 if self.t2 is not None else self.t1
        for k, v in t2.items():
            vals["t2_" + k] = v
        vals.update(self.sh)
        out = []
        for k in KIDS:
            if k in vals:
                v = float(vals[k])
            elif k in RANGE:
                v = RANGE[k][2]
            else:
                v = 0.0
            if k in RANGE:
                lo, hi, _ = RANGE[k]
                v = min(hi, max(lo, v))
            elif k in NCHOICE:
                v = float(min(NCHOICE[k] - 1, max(0, int(round(v)))))
            out.append(round(v, 4))
        return out


# ---------------------------------------------------------------- recipes
# Each recipe builds variant v (0..n-1) of an archetype. Variation is chosen,
# not rolled: lists of musically sensible settings indexed by v, with a
# little seeded jitter on top so neighbours never sound identical.


# ================================================================ design system
# A sound type ("archetype": reese, supersaw pad, mallet...) is a CORE that
# fixes what makes it that sound (register, envelope shape, voice mode), plus
# AXES of designed options: oscillator source, filter, second-timbre layer,
# movement and effects. A preset is one option per axis. Every option is a
# musically sensible setting; variety comes from combining them, not from
# random knob noise. Tools/diversify.py renders several candidate
# combinations per slot and keeps the one that sounds least like its
# siblings (Tools/bank_choice.json); gen_bank.py applies that choice.

FAM_WORD = {"analog": "Vintage", "reed": "Reedy", "bell": "Bell", "metal": "Metal",
            "vocal": "Vocal", "bright": "Bright", "wood": "Wooden", "detune": "Chorused"}


class Opt:
    """One designed option on an axis: a name word (or None), tags, and the change."""

    def __init__(self, word, fn, tags=()):
        self.word, self.fn, self.tags = word, fn, list(tags)


# ---------------------------------------------------------------- sources
# fn(p, o, r): o = the archetype's base octave, r = seeded RNG for small jitter
def src_saws(det=(6, 12)):
    def f(p, o, r):
        d = r.uniform(*det)
        p.o1(W_SAW, octave=o, fine=-d, level=0.7).o2(W_SAW, octave=o, fine=d, level=0.65)
    return Opt(None, f, ["Saw"])


def src_saw_pulse():
    def f(p, o, r):
        p.o1(W_SAW, octave=o, fine=-r.uniform(4, 9), level=0.7)
        p.o2(W_SQR, octave=o, fine=r.uniform(4, 9), shape=r.uniform(0.15, 0.45), level=0.6)
    return Opt("Pulse", f, ["Pulse"])


def src_squares():
    def f(p, o, r):
        d = r.uniform(5, 11)
        p.o1(W_SQR, octave=o, fine=-d, shape=r.uniform(0.0, 0.3), level=0.7)
        p.o2(W_SQR, octave=o, fine=d, shape=r.uniform(0.3, 0.6), level=0.6)
    return Opt("Square", f, ["Square"])


def src_pwm():
    def f(p, o, r):
        p.o1(W_SQR, octave=o, shape=0.2, pwm=r.uniform(0.35, 0.6), level=0.7)
        p.o2(W_SAW, octave=o, fine=r.uniform(5, 10), level=0.5)
        if p.t1["lfo1_depth"] == 0.0:
            p.lfo1(L_TRI, hz=r.uniform(0.4, 1.2), depth=1.0, key=0)
    return Opt("PWM", f, ["PWM"])


def src_digital(fam, partner=W_SAW):
    def f(p, o, r):
        p.o1(W_DIG, octave=o, digital=dig(fam, r.randrange(8), r.randrange(2)), fine=-r.uniform(4, 9), level=0.75)
        p.o2(partner, octave=o, fine=r.uniform(4, 9), level=0.55)
    return Opt(FAM_WORD[fam], f, ["Digital", FAM_WORD[fam]])


def src_hybrid():
    def f(p, o, r):
        fa, fb = r.sample(list(FAM), 2)
        p.o1(W_DIG, octave=o, digital=dig(fa, r.randrange(8)), fine=-r.uniform(5, 10), level=0.7)
        p.o2(W_DIG, octave=o, digital=dig(fb, r.randrange(8), 1), fine=r.uniform(5, 10), level=0.6)
    return Opt("Hybrid", f, ["Digital"])


def src_sync(sweep=(0.25, 0.5)):
    def f(p, o, r):
        p.o1(W_SAW, octave=o, level=0.35).o2(W_SAW, octave=o, pitch=r.choice([7, 12, 12, 5]), level=0.75)
        p.t1["osc_mod"] = 2
        p.mod(2, S_ENV1, D_O2PITCH, r.uniform(*sweep))
    return Opt("Sync", f, ["Sync"])


def src_fm(amt=(0.12, 0.3)):
    def f(p, o, r):
        p.o1(r.choice([W_SAW, W_SIN, W_SQR]), octave=o, level=0.75)
        p.o2(W_SIN, octave=o + r.choice([0, 1]), level=0.0)
        p.t1["osc2_level"] = 1.0
        p.t1["fm_amt"] = r.uniform(*amt)
    return Opt("FM", f, ["FM"])


def src_ring():
    def f(p, o, r):
        p.o1(W_SAW, octave=o, level=0.6).o2(r.choice([W_SQR, W_SAW]), octave=o + 1, level=0.55)
        p.t1["osc_mod"] = 1
    return Opt("Ring", f, ["Ring"])


def src_sine_tri():
    def f(p, o, r):
        p.o1(r.choice([W_SIN, W_TRI]), octave=o, level=0.85).o2(W_SIN, octave=o + 1, level=r.uniform(0.1, 0.3))
    return Opt("Round", f, ["Round"])


# ---------------------------------------------------------------- filters
def flt(word, kind, hz, reso, env=0.0, dec=0.3, sus=0.0, key=0.5):
    def f(p, o, r):
        p.filt(hz * r.uniform(0.85, 1.15), reso, kind=kind, key=key)
        if env:
            p.fenv(0.0, dec, sus, dec, amt=env)
    return Opt(word, f, [])


# ---------------------------------------------------------------- layers (timbre 2)
def lay_none():
    return Opt(None, lambda p, o, r: None)


def lay_wide():
    return Opt("Wide", lambda p, o, r: p.wide(r.uniform(6, 11), r.uniform(0.3, 0.5)), ["Wide"])


def lay_octave(up=True):
    def f(p, o, r):
        d = 1 if up else -1
        p.layer(r.uniform(0.4, 0.55), osc1_octave=p.t1["osc1_octave"] + d, osc2_octave=p.t1["osc2_octave"] + d,
                osc1_wave=r.choice([W_SQR, W_SAW, W_DIG]), osc1_digital=dig(r.choice(list(FAM)), r.randrange(8)),
                amp_pan=r.uniform(-0.3, 0.3))
    return Opt("Octave" if up else "Deep", f, ["Layer"])


def lay_fifth():
    def f(p, o, r):
        if p.t1["osc_mod"] % 2 == 1 or p.t1["fm_amt"] > 0.0:
            p.wide(r.uniform(6, 10), 0.35)   # a fifth on ring / FM would be inharmonic: double instead
            return
        p.layer(r.uniform(0.4, 0.5), osc1_pitch=7, osc2_pitch=7, amp_pan=r.choice([-0.25, 0.25]))
    return Opt("Fifth", f, ["Layer", "Fifth"])


def lay_sub():
    def f(p, o, r):
        if p.t2 is not None:
            return
        p.layer(r.uniform(0.4, 0.5), osc1_wave=W_SIN, osc1_octave=p.t1["osc1_octave"] - 1, mix_o2=0.0,
                osc2_level=0.0, filter_cutoff=cut(400), filter_reso=0.0, mod1_amt=0.0, amp_pan=0.0)
    return Opt(None, f, ["Layer", "Sub"])


def lay_texture():
    def f(p, o, r):
        fam = r.choice(["metal", "bell", "bright", "vocal", "wood"])
        p.layer(r.uniform(0.35, 0.5), osc1_wave=W_DIG, osc1_digital=dig(fam, r.randrange(8), 1),
                filter_type=F_HP12, filter_cutoff=cut(r.uniform(500, 1200)), amp_pan=r.choice([-0.35, 0.35]))
    return Opt("Textured", f, ["Layer", "Texture"])


def lay_split():
    def f(p, o, r):
        p.layer(r.uniform(0.4, 0.5), filter_type=F_BP12, filter_cutoff=cut(r.uniform(900, 2500)),
                filter_reso=r.uniform(0.3, 0.5), amp_pan=r.choice([-0.4, 0.4]))
    return Opt("Split", f, ["Layer"])


# ---------------------------------------------------------------- movement (LFO1 / LFO2)
def mv_still():
    return Opt(None, lambda p, o, r: None)


def mv_drift(amt=(0.08, 0.18)):
    def f(p, o, r):
        p.lfo1(r.choice([L_TRI, L_SIN]), hz=r.uniform(0.08, 0.3), depth=1.0, key=0).mod(1, S_LFO1, D_CUT, r.uniform(*amt))
    return Opt("Drifting", f, ["Moving"])


def mv_synced(divs=("1/4", "1/8", "1/16", "1/12", "3/16"), amt=(0.3, 0.6)):
    def f(p, o, r):
        p.lfo1(r.choice([L_SIN, L_TRI, L_SQR, L_SAW]), sync=r.choice(divs), depth=1.0, key=1)
        p.mod(1, S_LFO1, D_CUT, r.uniform(*amt))
    return Opt("Pulsing", f, ["Synced", "Moving"])


def mv_steps(amt=(0.2, 0.35)):
    def f(p, o, r):
        p.lfo1(L_RND, sync=r.choice(["1/16", "1/8"]), depth=1.0, key=1).mod(1, S_LFO1, D_CUT, r.uniform(*amt))
    return Opt("Stepping", f, ["Synced", "Moving"])


def mv_morph():
    def f(p, o, r):
        p.lfo1(L_TRI, hz=r.uniform(0.2, 0.8), depth=1.0, key=0)
        if p.t1["osc1_wave"] in (W_DIG, W_NOI):   # digital waves ignore the shape control
            p.mod(1, S_LFO1, D_CUT, r.uniform(0.12, 0.2))
            return "Drifting"
        p.mod(1, S_LFO1, D_O1SHAPE, r.uniform(0.4, 0.8))
    return Opt("Morphing", f, ["Moving"])


def mv_pan():
    def f(p, o, r):
        p.lfo2(L_SIN, hz=r.uniform(0.3, 1.5), depth=1.0, key=0).mod(3, S_LFO2, D_PAN, r.uniform(0.5, 0.9))
    return Opt("Panning", f, ["Moving"])


def mv_wheel():
    def f(p, o, r):
        p.mod(4, S_WHEEL, D_CUT, r.uniform(0.3, 0.5))
    return Opt(None, f, [])


# ---------------------------------------------------------------- effects
def fx_dry():
    return Opt(None, lambda p, o, r: None)


def fx_chorus():
    return Opt(None, lambda p, o, r: p.chorus(r.uniform(0.2, 0.35), r.uniform(0.3, 0.5), r.uniform(0.15, 0.35)), ["Chorus"])


def fx_phaser():
    return Opt("Phased", lambda p, o, r: p.chorus(r.uniform(0.3, 0.5), r.uniform(0.4, 0.7), r.uniform(0.08, 0.25), kind=3), ["Phaser"])


def fx_flanger():
    return Opt("Flanged", lambda p, o, r: p.chorus(r.uniform(0.3, 0.45), r.uniform(0.4, 0.7), r.uniform(0.05, 0.2), kind=2), ["Flanger"])


def fx_drive():
    return Opt("Dirty", lambda p, o, r: p.drive(r.uniform(0.3, 0.55), r.uniform(0.35, 0.6)), ["Distorted"])


def fx_tape():
    return Opt("Tape", lambda p, o, r: p.sat(r.uniform(0.3, 0.5), r.uniform(0.4, 0.6)), ["Saturated"])


def fx_tube():
    return Opt("Warm", lambda p, o, r: p.sat(r.uniform(0.3, 0.5), r.uniform(0.45, 0.65), tube=True), ["Saturated"])


def fx_delay():
    def f(p, o, r):
        p.delay(r.choice(["3/16", "1/8", "1/4", "3/8"]), r.uniform(0.25, 0.45), r.uniform(0.12, 0.22), kind=r.choice([0, 1, 2]))
    return Opt("Echo", f, ["Delay"])


def fx_space():
    def f(p, o, r):
        p.verb(r.uniform(0.25, 0.4), r.uniform(0.6, 0.9), r.uniform(0.3, 0.6))
        p.delay(r.choice(["1/4", "3/8"]), r.uniform(0.3, 0.45), r.uniform(0.1, 0.18))
    return Opt("Vast", f, ["Space"])


def fx_room():
    return Opt(None, lambda p, o, r: p.verb(r.uniform(0.12, 0.22), r.uniform(0.35, 0.55)), ["Reverb"])


def fx_chorus_delay():
    def f(p, o, r):
        p.chorus(r.uniform(0.2, 0.3), r.uniform(0.3, 0.45), r.uniform(0.2, 0.35))
        p.delay(r.choice(["3/16", "1/8"]), r.uniform(0.25, 0.4), r.uniform(0.12, 0.2))
    return Opt(None, f, ["Chorus", "Delay"])


def fx_phase_drive():
    def f(p, o, r):
        p.chorus(r.uniform(0.3, 0.45), r.uniform(0.4, 0.6), r.uniform(0.08, 0.2), kind=3)
        p.drive(r.uniform(0.3, 0.45), r.uniform(0.35, 0.5))
    return Opt("Phased", f, ["Phaser", "Distorted"])


# ---------------------------------------------------------------- archetypes
class Arch:
    def __init__(self, cat, noun, count, tags, core, axes, first=None, words=None, boost=1.0, cycle=None):
        self.cat, self.noun, self.count, self.tags, self.boost = cat, noun, count, tags, boost
        self.cycle = cycle   # an axis whose options are used in turn (slot v -> option v % n)
        self.core, self.axes, self.first = core, axes, first
        self.words = words or ["source", "layer", "motion", "fx", "filter"]


def core_bass(mono=True, glide=0.0, legato=True):
    def f(p, r):
        if mono:
            p.mono(glide=glide, legato=legato)
        p.amp(0.002, 0.4, 1.0, 0.12, vel=0.15)
    return f


def core_808(p, r):
    p.mono(glide=r.choice([0.0, 0.0, 0.12]), legato=True)
    p.o1(W_SIN, octave=0, level=0.95).filt(2400, key=0.0)
    p.amp(0.0, r.choice([0.6, 0.8, 1.1, 1.5]), 0.0, r.choice([0.12, 0.2]), vel=0.15)


def src_808_click():
    def f(p, o, r):
        p.fenv(0.0, 0.06, 0.0, 0.1).mod(2, S_ENV1, D_PITCH, r.uniform(0.06, 0.16))
    return Opt("Punch", f, [])


def src_808_tri():
    def f(p, o, r):
        p.o1(W_TRI, octave=0, level=0.95)
        p.fenv(0.0, 0.05, 0.0, 0.1).mod(2, S_ENV1, D_PITCH, r.uniform(0.05, 0.1))
    return Opt("Knock", f, [])


def src_808_square():
    def f(p, o, r):
        p.o2(W_SQR, octave=0, level=0.18).filt(r.uniform(500, 900), 0.1, key=0.0)
    return Opt("Edge", f, [])


def src_808_pure():
    return Opt(None, lambda p, o, r: None)


def first_808_clean(p, r):
    """the first 808: pure sine, no FX, no LFO, no mods (the self-test checks MUTATE keeps it asleep)"""


def reese_stack(cents, word, uni=0.0, pulse=False):
    """Two saws detuned +-cents/2 (slow phasing .. rough beating); uni > 0 stacks
    four voices of them, spread over `uni` cents."""
    def f(p, o, r):
        d = cents * r.uniform(0.85, 1.15) / 2.0
        p.o1(W_SAW, octave=o, fine=-d, level=0.72)
        if pulse:
            p.o2(W_SQR, octave=o, fine=d, shape=r.uniform(0.25, 0.4), level=0.62)
        else:
            p.o2(W_SAW, octave=o, fine=d, level=0.68)
        if uni:
            p.unison(uni)
    return Opt(word, f, ["Saw"] + (["Unison"] if uni else []))


def reese_sub():
    def f(p, o, r):
        if p.t2 is not None:
            return
        # timbre 2: a clean sine on the fundamental, so the low end stays solid while the saws beat
        p.layer(r.uniform(0.38, 0.46), osc1_wave=W_SIN, osc1_octave=p.t1["osc1_octave"], osc1_fine=0.0,
                mix_o1=0.9, mix_o2=0.0, osc2_level=0.0, filter_type=F_LP24, filter_cutoff=cut(300), filter_reso=0.0,
                filter_envamt=0.0, mod1_amt=0.0, amp_pan=0.0)
        # one centred voice, even when timbre 1 is a unison stack
        p.sh.update(t2_voice_assign=1, t2_voice_poly=1, t2_voice_detune=0.0, t2_voice_unidet=0.0)
    return Opt(None, f, ["Sub"])


def reese_octave():
    def f(p, o, r):
        if p.t2 is not None:
            return
        p.layer(r.uniform(0.38, 0.46), osc1_octave=p.t1["osc1_octave"] - 1, osc2_octave=p.t1["osc2_octave"] - 1,
                filter_cutoff=cut(400), filter_reso=0.1, amp_pan=0.0)
    return Opt("Deep", f, ["Layer"])


# growls: the vowel-like movement is shaped per note by ENV1 (up and back,
# opening, ratio / sync / ring sweeps, wave morph). No steady LFO sweep:
# that would be a wobble. LFO1 only adds fast random grit; the mod wheel
# opens the tone for performance.
def _grit(p, r, amt=(0.04, 0.08)):
    p.lfo1(L_RND, hz=r.uniform(14.0, 22.0), depth=1.0, key=2).mod(1, S_LFO1, D_CUT, r.uniform(*amt))


def growl_yoy(p, o, r):
    # a resonant band-pass swept up and back down by ENV1: "yoy"
    p.o1(W_SAW, octave=o, fine=-6, level=0.75).o2(W_SQR, octave=o, fine=6, shape=0.3, level=0.6)
    p.filt(r.uniform(300, 420), r.uniform(0.6, 0.72), kind=F_BP12, key=0.4)
    p.fenv(r.uniform(0.08, 0.14), r.uniform(0.25, 0.35), 0.15, 0.15, amt=r.uniform(0.3, 0.38))
    _grit(p, r)
    p.mod(4, S_WHEEL, D_CUT, 0.3)


def growl_wow(p, o, r):
    # a low-pass with heavy resonance opening slowly: "wow"
    p.o1(W_SAW, octave=o, level=0.75).o2(W_SAW, octave=o, fine=9, level=0.65)
    p.filt(r.uniform(150, 220), r.uniform(0.6, 0.7), kind=F_LP24, key=0.4)
    p.fenv(r.uniform(0.18, 0.3), 0.6, 0.6, 0.2, amt=r.uniform(0.35, 0.45))
    _grit(p, r)
    p.mod(4, S_WHEEL, D_CUT, 0.3)


def growl_fm(p, o, r):
    # FM whose ratio is swept by ENV1, settling back on the octave: metallic vowels
    p.o1(W_SAW, octave=o, level=0.8).o2(W_SIN, octave=o + 1, level=0.0)
    p.t1["osc2_level"] = 1.0
    p.t1["fm_amt"] = r.uniform(0.35, 0.5)
    p.fenv(r.uniform(0.04, 0.08), r.uniform(0.35, 0.5), 0.0, 0.2, amt=0.3).mod(2, S_ENV1, D_O2PITCH, r.uniform(0.6, 0.9))
    p.filt(r.uniform(450, 700), 0.45, kind=F_LP24)
    _grit(p, r)
    p.mod(4, S_WHEEL, D_O2PITCH, 0.25)


def growl_ring(p, o, r):
    # ring mod against a square whose pitch dives with ENV1: grinding metal
    p.o1(W_SAW, octave=o, level=0.7).o2(W_SQR, octave=o + 1, level=0.6)
    p.t1["osc_mod"] = 1
    p.fenv(0.0, r.uniform(0.35, 0.55), 0.0, 0.2).mod(2, S_ENV1, D_O2PITCH, r.uniform(0.3, 0.5))
    p.filt(r.uniform(900, 1400), 0.45, kind=F_LP12)
    _grit(p, r)
    p.mod(4, S_WHEEL, D_O2PITCH, 0.2)


def growl_sync(p, o, r):
    # hard sync swept by ENV1: a screaming tear that settles
    p.o1(W_SAW, octave=o, level=0.4).o2(W_SAW, octave=o, pitch=12, level=0.8)
    p.t1["osc_mod"] = 2
    p.fenv(r.uniform(0.04, 0.08), r.uniform(0.35, 0.5), 0.15, 0.2, amt=0.28).mod(2, S_ENV1, D_O2PITCH, r.uniform(0.65, 0.9))
    p.filt(r.uniform(600, 900), 0.35, kind=F_LP24)
    _grit(p, r)
    p.mod(4, S_WHEEL, D_O2PITCH, 0.3)


def growl_morph(p, o, r):
    # a pulse whose width ENV1 sweeps thin -> square, through a band-pass: "nyow"
    # (on digital waves the shape control does nothing, so this is a pulse)
    p.o1(W_SQR, octave=o, shape=0.05, level=0.8).o2(W_SAW, octave=o, fine=7, level=0.45)
    p.fenv(r.uniform(0.06, 0.12), 0.45, 0.25, 0.2, amt=0.22).mod(2, S_ENV1, D_O1SHAPE, -0.9)
    p.t1["osc1_shape"] = 0.9
    p.filt(r.uniform(500, 800), 0.5, kind=F_BP12, key=0.4)
    _grit(p, r)
    p.mod(4, S_WHEEL, D_O1SHAPE, -0.4)


def growl_talking(p, o, r):
    # two band-passes (a low and a high formant) swept together by ENV1: speech-like
    p.o1(W_SAW, octave=o, level=0.75).o2(W_SQR, octave=o, fine=5, shape=0.25, level=0.6)
    p.filt(r.uniform(350, 450), 0.6, kind=F_BP12, key=0.3)
    p.fenv(r.uniform(0.06, 0.12), 0.4, 0.2, 0.2, amt=r.uniform(0.22, 0.3))
    p.layer(0.5, filter_cutoff=cut(r.uniform(1100, 1500)), filter_reso=0.6, amp_pan=0.0)
    p.mod(4, S_WHEEL, D_CUT, 0.25)


def growl_snarl(p, o, r):
    # noisy FM with a resonance sweep: a snarling, breathy growl
    p.o1(W_SAW, octave=o, level=0.75).o2(W_SIN, octave=o, level=0.0).noise(r.uniform(0.08, 0.14))
    p.t1["osc2_level"] = 1.0
    p.t1["fm_amt"] = r.uniform(0.25, 0.35)
    p.filt(r.uniform(500, 700), 0.55, kind=F_LP24).fenv(0.02, 0.3, 0.3, 0.2, amt=0.3)
    p.mod(3, S_ENV1, D_RESO, 0.4)
    _grit(p, r, (0.08, 0.12))
    p.mod(4, S_WHEEL, D_CUT, 0.3)


ARCHES = [
    # ------------------------------------------------------------ BASS (84)
    Arch("Bass", "808", 10, ["808", "Sub"], core_808, {
        "source": [src_808_pure(), src_808_click(), src_808_tri(), src_808_square()],
        "fx": [fx_dry(), fx_tape(), fx_drive(), fx_tube()],
        "motion": [mv_still(), mv_still(), mv_wheel()],
        "layer": [lay_none(), lay_none(), Opt("Grit", lambda p, o, r: p.layer(r.uniform(0.25, 0.35), osc1_wave=W_SQR, osc1_octave=0,
                  filter_cutoff=cut(r.uniform(700, 1400)), filter_reso=0.2, amp_pan=0.0), ["Layer"])],
    }, first={"source": 0, "fx": 0, "motion": 0, "layer": 0}),
    Arch("Bass", "Sub", 6, ["Sub"], lambda p, r: (p.mono(glide=r.choice([0.0, 0.08]), legato=True),
                                                    p.amp(0.004, 0.2, 1.0, 0.12, vel=0.1)), {
        "source": [src_sine_tri(), Opt(None, lambda p, o, r: p.o1(W_SIN, octave=o, level=0.9)),
                   Opt("Pulse", lambda p, o, r: (p.o1(W_SIN, octave=o, level=0.85), p.o2(W_SQR, octave=o + 1, shape=0.3, level=0.2)))],
        "filter": [flt(None, F_LP24, 600, 0.0, key=0.0), flt("Soft", F_LP12, 350, 0.1, key=0.0), flt(None, F_LP24, 900, 0.05, key=0.0)],
        "fx": [fx_dry(), fx_tape(), fx_tube()],
    }),
    Arch("Bass", "Reese", 14, ["Reese"], core_bass(glide=0.0), {
        # always detuned saws: the stack decides how they beat against each other
        "stack": [reese_stack(7, "Slow"), reese_stack(13, None), reese_stack(22, "Rough"),
                  reese_stack(10, "Massive", uni=14), reese_stack(16, "Thick", uni=24), reese_stack(12, "Pulse", pulse=True)],
        "filter": [flt(None, F_LP24, 900, 0.2, env=0.1, dec=0.5, sus=0.7), flt("Dark", F_LP24, 550, 0.25, env=0.08, dec=0.6, sus=0.7),
                   flt("Open", F_LP12, 2200, 0.1), flt("Nasal", F_LP24, 1200, 0.45, env=0.1, dec=0.4, sus=0.6)],
        "weight": [reese_sub(), reese_sub(), lay_none()],
        "motion": [mv_still(), mv_still(), mv_drift((0.06, 0.12)), mv_wheel()],
        "fx": [fx_tape(), fx_drive(), fx_tube(), fx_phaser(), fx_flanger(), fx_phase_drive(), fx_chorus(), fx_dry()],
    }, first={"stack": 1, "fx": 7, "motion": 0, "weight": 2}, words=["stack", "filter", "fx", "weight", "motion"]),
    Arch("Bass", "Growl", 8, ["Growl", "Distorted"], core_bass(glide=0.05), {
        # one hand-built growl per slot: the movement lives inside each note
        "design": [Opt("Yoy", growl_yoy), Opt("Wow", growl_wow), Opt("FM", growl_fm), Opt("Ring", growl_ring),
                   Opt("Sync", growl_sync), Opt("Morph", growl_morph), Opt("Talking", growl_talking), Opt("Snarl", growl_snarl)],
        "grit": [Opt(None, lambda p, o, r: p.drive(r.uniform(0.5, 0.65), r.uniform(0.55, 0.7))),
                 Opt(None, lambda p, o, r: (p.drive(r.uniform(0.45, 0.6), 0.6), p.sat(0.35, 0.5))),
                 Opt(None, lambda p, o, r: (p.drive(r.uniform(0.55, 0.7), 0.65), p.chorus(0.25, 0.45, 0.12, kind=3)))],
        "weight": [reese_sub(), lay_none()],
    }, cycle="design", words=["design"]),
    Arch("Bass", "Wobble", 8, ["Wobble"], core_bass(glide=0.06), {
        "source": [src_saws((4, 8)), src_saw_pulse(), src_squares(), src_digital("analog"), src_fm((0.1, 0.2))],
        "filter": [flt(None, F_LP24, 200, 0.4, key=0.3), flt("Talking", F_BP12, 400, 0.5, key=0.3), flt("Rubber", F_LP12, 250, 0.55, key=0.3)],
        "motion": [mv_synced(("1/4", "1/8", "1/12", "3/8", "1/6"), (0.6, 0.8))],
        "layer": [lay_none(), lay_sub(), lay_octave(True)],
        "fx": [fx_drive(), fx_dry(), fx_tape(), fx_phase_drive()],
    }),
    Arch("Bass", "Pluck Bass", 10, ["Pluck"], lambda p, r: (p.mono(t2=True) if r.random() < 0.6 else None,
                                                           p.amp(0.0, r.choice([0.3, 0.45, 0.6]), r.choice([0.0, 0.15, 0.25]), 0.12, vel=0.4),
                                                           p.mod(4, S_VEL, D_CUT, 0.15)), {
        "source": [src_saws((3, 7)), src_saw_pulse(), src_squares(), src_digital("wood"), src_digital("reed"),
                   src_fm((0.15, 0.3)), src_sine_tri()],
        "filter": [flt(None, F_LP24, 200, 0.2, env=0.5, dec=0.2), flt("Snappy", F_LP24, 160, 0.35, env=0.6, dec=0.12),
                   flt("Round", F_LP12, 260, 0.1, env=0.4, dec=0.3), flt("Hollow", F_BP12, 500, 0.3, env=0.35, dec=0.2)],
        "layer": [lay_none(), lay_sub(), lay_octave(True)],
        "fx": [fx_dry(), fx_tape(), fx_tube(), fx_chorus(), fx_delay()],
    }),
    Arch("Bass", "Groove Bass", 8, ["House"], lambda p, r: (p.mono(t2=False), p.amp(0.0, r.choice([0.35, 0.5]), r.choice([0.0, 0.3]), 0.1, vel=0.35)), {
        "source": [Opt("Organ", lambda p, o, r: (p.o1(W_SIN, octave=0, level=0.85), p.o2(W_SQR, octave=1, level=0.3)), ["Organ"]),
                   src_squares(), src_digital("reed"), src_sine_tri(), src_fm((0.1, 0.2))],
        "filter": [flt(None, F_LP24, 1400, 0.0), flt("Rubbery", F_LP24, 400, 0.3, env=0.35, dec=0.15), flt("Muted", F_LP12, 700, 0.1)],
        "fx": [fx_dry(), fx_tape(), fx_chorus()],
    }),
    Arch("Bass", "Mono Bass", 10, ["Analog"], lambda p, r: (p.mono(glide=r.choice([0.0, 0.0, 0.1]), legato=r.random() < 0.5),
                                                          p.amp(0.002, 0.4, 0.9, 0.15, vel=0.3)), {
        "source": [src_saws((4, 9)), src_saw_pulse(), src_squares(), src_pwm(), src_digital("analog"), src_sync((0.15, 0.3))],
        "filter": [flt(None, F_LP24, 350, 0.25, env=0.35, dec=0.3, sus=0.2), flt("Bright", F_LP12, 700, 0.2, env=0.3, dec=0.4, sus=0.3),
                   flt("Squelchy", F_LP24, 300, 0.55, env=0.45, dec=0.25), flt("Dark", F_LP24, 220, 0.15, env=0.25, dec=0.5, sus=0.4)],
        "layer": [lay_none(), lay_sub(), lay_wide()],
        "fx": [fx_dry(), fx_tube(), fx_tape(), fx_drive(), fx_chorus()],
    }),
    Arch("Bass", "Acid", 6, ["Acid", "Glide"], lambda p, r: (p.mono(glide=r.choice([0.08, 0.1]), legato=True),
                                                            p.amp(0.0, 0.3, 0.85, 0.08, vel=0.2), p.mod(4, S_VEL, D_CUT, 0.2)), {
        "source": [Opt(None, lambda p, o, r: p.o1(W_SAW, octave=0, level=0.85)), Opt("Square", lambda p, o, r: p.o1(W_SQR, octave=0, level=0.85), ["Square"]),
                   Opt("Digital", lambda p, o, r: p.o1(W_DIG, octave=0, digital=dig("analog", r.randrange(8)), level=0.85), ["Digital"])],
        "filter": [flt(None, F_LP24, 260, 0.72, env=0.5, dec=0.18, key=0.35), flt("Screaming", F_LP24, 320, 0.82, env=0.55, dec=0.14, key=0.35),
                   flt("Rubber", F_LP12, 240, 0.7, env=0.45, dec=0.25, key=0.35)],
        "fx": [fx_drive(), fx_delay(), fx_dry(), fx_phase_drive()],
    }),
    Arch("Bass", "FM Bass", 4, ["FM"], lambda p, r: (p.mono(t2=False), p.amp(0.0, 0.5, r.choice([0.4, 0.6]), 0.12, vel=0.35)), {
        "source": [src_fm((0.25, 0.45)), src_fm((0.15, 0.3))],
        "filter": [flt(None, F_LP24, 1300, 0.1, env=0.3, dec=0.2, sus=0.25), flt("Knocky", F_LP24, 900, 0.2, env=0.45, dec=0.1)],
        "fx": [fx_dry(), fx_tape(), fx_chorus()],
    }),
    # ------------------------------------------------------------ LEAD (56)
    Arch("Lead", "Supersaw", 10, ["Supersaw"], lambda p, r: (p.unison(r.uniform(14, 22)), p.amp(0.004, 0.4, 1.0, r.choice([0.25, 0.35, 0.5]), vel=0.2)), {
        "source": [src_saws((4, 8)), src_saw_pulse(), src_digital("detune"), src_digital("bright"), src_hybrid()],
        "filter": [flt(None, F_LP24, 6000, 0.1, env=0.12, dec=0.5, sus=0.6), flt("Smooth", F_LP12, 3500, 0.15), flt("Cutting", F_LP24, 9000, 0.2)],
        "layer": [lay_none(), lay_octave(True), lay_wide(), lay_fifth()],
        "motion": [mv_still(), mv_wheel(), mv_pan()],
        "fx": [fx_chorus_delay(), fx_space(), fx_delay(), fx_phaser()],
    }),
    Arch("Lead", "Square Lead", 6, ["Square"], lambda p, r: (p.mono(legato=r.random() < 0.5), p.amp(0.003, 0.3, 1.0, 0.2, vel=0.2),
                                                           p.lfo2(L_TRI, 5.5), p.sh.__setitem__("voice_vib", 0.5)), {
        "source": [src_squares(), src_pwm(), Opt("Chip", lambda p, o, r: p.o1(W_SQR, shape=r.choice([0.0, 0.5]), level=0.8), ["Chip"]),
                   src_saw_pulse(), src_digital("reed"), src_ring()],
        "filter": [flt(None, F_LP24, 4000, 0.1), flt("Soft", F_LP12, 2400, 0.1), flt("Nasal", F_BP12, 1600, 0.4), flt("Thin", F_HP12, 500, 0.1)],
        "fx": [fx_delay(), fx_room(), fx_chorus_delay(), fx_dry()],
        "layer": [lay_none(), lay_octave(False), lay_wide(), lay_fifth()],
        "motion": [mv_still(), mv_wheel(), mv_pan(), mv_morph()],
    }),
    Arch("Lead", "Sync Lead", 7, ["Sync"], lambda p, r: (p.mono(legato=True), p.amp(0.003, 0.3, 1.0, 0.25, vel=0.2),
                                                       p.fenv(0.0, r.choice([0.25, 0.4, 0.6]), r.choice([0.15, 0.3]), 0.3), p.mod(4, S_WHEEL, D_O2PITCH, 0.35)), {
        "source": [src_sync((0.4, 0.75))],
        "filter": [flt(None, F_LP24, 5000, 0.12), flt("Thin", F_HP12, 400, 0.1), flt("Biting", F_LP12, 3000, 0.35)],
        "layer": [lay_none(), lay_octave(False), lay_wide()],
        "fx": [fx_delay(), fx_drive(), fx_room(), fx_phaser()],
    }),
    Arch("Lead", "Glide Lead", 8, ["Glide", "Mono"], lambda p, r: (p.mono(glide=r.choice([0.15, 0.18, 0.25, 0.3]), legato=True),
                                                                p.amp(0.004, 0.3, 1.0, 0.3, vel=0.15), p.lfo2(L_SIN, 5.2), p.sh.__setitem__("voice_vib", 0.6)), {
        "source": [src_saws((4, 8)), src_saw_pulse(), src_digital("reed"), src_digital("vocal"), src_sine_tri()],
        "filter": [flt(None, F_LP24, 3000, 0.25, env=0.2, dec=0.5, sus=0.45), flt("Mellow", F_LP12, 1800, 0.15), flt("Nasal", F_BP12, 1500, 0.4)],
        "fx": [fx_delay(), fx_space(), fx_tube(), fx_chorus_delay()],
    }),
    Arch("Lead", "Soft Lead", 6, ["Soft"], lambda p, r: (p.mono(legato=True), p.amp(r.choice([0.03, 0.06]), 0.3, 1.0, 0.35, vel=0.3),
                                                       p.lfo2(L_SIN, 5.0), p.sh.__setitem__("voice_vib", 0.7)), {
        "source": [src_sine_tri(), Opt("Breathy", lambda p, o, r: (p.o1(W_SIN, level=0.85), p.noise(r.uniform(0.05, 0.12))), ["Breath"]),
                   src_digital("wood", W_SIN), src_digital("reed", W_SIN)],
        "filter": [flt(None, F_LP24, 3000, 0.0), flt("Dark", F_LP12, 1800, 0.05)],
        "fx": [fx_space(), fx_delay(), fx_chorus_delay()],
        "layer": [lay_none(), lay_octave(True), lay_wide()],
    }),
    Arch("Lead", "Digital Lead", 7, ["Digital"], lambda p, r: (p.mono(legato=True), p.amp(0.005, 0.3, 1.0, 0.25, vel=0.2),
                                                             p.mod(4, S_WHEEL, D_O1SHAPE, 0.5)), {
        "source": [src_digital("vocal"), src_digital("bright"), src_digital("metal"), src_digital("reed"), src_hybrid()],
        "filter": [flt(None, F_LP24, 4500, 0.12), flt("Formant", F_BP12, 1800, 0.45), flt("Thin", F_HP12, 600, 0.15)],
        "motion": [mv_still(), mv_morph(), mv_drift()],
        "fx": [fx_delay(), fx_room(), fx_phaser(), fx_chorus_delay()],
    }),
    Arch("Lead", "Rave Lead", 6, ["Rave"], lambda p, r: None, {   # six fixed designs (see l_rave)
        "design": [Opt(None, lambda p, o, r, k=k: rave_design(p, k, r)) for k in range(6)],
    }, words=["design"]),
    Arch("Lead", "Pluck Lead", 6, ["Pluck"], lambda p, r: (p.spread(0.3), p.amp(0.0, 0.45, 0.35, 0.3, vel=0.4)), {
        "source": [src_saws((4, 8)), src_saw_pulse(), src_digital("bell"), src_digital("wood"), src_fm((0.1, 0.25))],
        "filter": [flt(None, F_LP24, 800, 0.2, env=0.5, dec=0.25, sus=0.1), flt("Glassy", F_LP12, 1500, 0.3, env=0.4, dec=0.2)],
        "fx": [fx_delay(), fx_chorus_delay(), fx_space()],
        "layer": [lay_none(), lay_wide(), lay_octave(True), lay_fifth()],
        "motion": [mv_still(), mv_pan(), mv_morph()],
    }),
    # ------------------------------------------------------------ PAD (56)
    Arch("Pad", "Supersaw Pad", 10, ["Supersaw"], lambda p, r: (p.spread(r.uniform(0.5, 0.85)),
                                                             p.amp(r.choice([0.2, 0.4, 0.7]), 1.0, 1.0, r.choice([1.2, 1.8, 2.4]), vel=0.15)), {
        "source": [src_saws((6, 11)), src_saw_pulse(), src_digital("detune"), src_digital("bright"), src_hybrid()],
        "filter": [flt(None, F_LP24, 4000, 0.08), flt("Warm", F_LP12, 2000, 0.1), flt("Airy", F_HP12, 300, 0.1)],
        "layer": [lay_wide(), lay_octave(True), lay_fifth(), lay_texture()],
        "motion": [mv_still(), mv_drift(), mv_pan(), mv_synced(("1/2", "1/4", "1/1"), (0.15, 0.3))],
        "fx": [fx_space(), fx_chorus(), fx_phaser(), fx_chorus_delay()],
    }),
    Arch("Pad", "Analog Pad", 10, ["Warm", "Analog"], lambda p, r: (p.spread(0.35),
                                                                  p.amp(r.choice([0.3, 0.6, 0.9]), 1.0, 1.0, r.choice([1.2, 1.8]), vel=0.15),
                                                                  p.fenv(r.choice([0.4, 0.8]), 1.5, 0.6, 1.5, amt=r.uniform(0.1, 0.2))), {
        "source": [src_saws((5, 9)), src_saw_pulse(), src_pwm(), src_squares(), src_digital("analog")],
        "filter": [flt(None, F_LP24, 1100, 0.15), flt("Dark", F_LP24, 650, 0.2), flt("Soft", F_LP12, 1600, 0.1), flt("Resonant", F_LP24, 900, 0.45)],
        "layer": [lay_wide(), lay_octave(False), lay_none()],
        "motion": [mv_drift(), mv_morph(), mv_pan(), mv_still()],
        "fx": [fx_chorus(), fx_space(), fx_tube(), fx_phaser()],
    }),
    Arch("Pad", "Air Pad", 8, ["Airy"], lambda p, r: (p.spread(0.5), p.amp(r.choice([0.6, 1.0]), 1.0, 1.0, 2.5, vel=0.1), p.eq(high=2.0)), {
        "source": [Opt("Breathy", lambda p, o, r: (p.o1(W_SAW, octave=r.choice([0, 1]), level=0.9), p.o2(W_SAW, octave=1, fine=8, level=0.6), p.noise(r.uniform(0.08, 0.18))), ["Breath"]),
                   src_digital("vocal"), src_digital("bell"), src_sine_tri()],
        "filter": [flt(None, F_HP12, 250, 0.1), flt("Thin", F_HP12, 450, 0.2), flt("Soft", F_LP12, 3500, 0.05)],
        "layer": [lay_wide(), lay_octave(True), lay_texture()],
        "motion": [mv_drift(), mv_pan(), mv_still()],
        "fx": [fx_space(), fx_chorus_delay()],
    }),
    Arch("Pad", "Motion Pad", 8, ["Evolving", "Digital"], lambda p, r: (p.spread(0.3), p.amp(r.choice([0.7, 1.0, 1.5]), 1.0, 1.0, 2.5, vel=0.1)), {
        "source": [src_digital("analog"), src_digital("bright"), src_digital("vocal"), src_digital("wood"), src_digital("detune"), src_hybrid()],
        "filter": [flt(None, F_LP24, 1800, 0.2), flt("Glassy", F_BP12, 1500, 0.3), flt("Open", F_LP12, 4000, 0.1)],
        "layer": [lay_texture(), lay_octave(True), lay_fifth(), lay_wide()],
        "motion": [mv_morph(), mv_synced(("1/2", "1/4", "1/8"), (0.15, 0.3)), mv_steps((0.12, 0.22)), mv_drift((0.15, 0.25))],
        "fx": [fx_space(), fx_phaser(), fx_flanger(), fx_chorus_delay()],
    }),
    Arch("Pad", "Dark Pad", 8, ["Dark", "Cinematic"], lambda p, r: (p.spread(0.4), p.amp(r.choice([0.8, 1.2]), 1.0, 1.0, 2.5, vel=0.1)), {
        "source": [Opt(None, lambda p, o, r: (p.o1(W_SAW, octave=-1, level=0.7), p.o2(W_SQR, octave=-1, fine=-9, level=0.5))),
                   src_digital("metal"), src_digital("vocal"), src_ring(), src_squares()],
        "filter": [flt(None, F_LP24, 500, 0.25), flt("Murky", F_LP24, 320, 0.35), flt("Hollow", F_BP12, 600, 0.4)],
        "layer": [lay_octave(True), lay_texture(), lay_sub(), lay_fifth()],
        "motion": [mv_drift(), mv_pan(), mv_morph()],
        "fx": [fx_space(), fx_phaser(), fx_flanger()],
    }),
    Arch("Pad", "Glass Pad", 6, ["Glass"], lambda p, r: (p.spread(0.25), p.amp(r.choice([0.4, 0.7]), 1.2, 0.8, 2.2, vel=0.25), p.eq(high=1.5)), {
        "source": [src_digital("bell", W_SIN), src_digital("bright", W_SIN), src_digital("metal", W_SIN), src_fm((0.1, 0.2))],
        "filter": [flt(None, F_LP24, 6000, 0.05), flt("Ringing", F_BP12, 2500, 0.4)],
        "layer": [lay_wide(), lay_octave(True), lay_fifth()],
        "motion": [mv_still(), mv_pan(), mv_synced(("1/4", "1/8"), (0.1, 0.2))],
        "fx": [fx_space(), fx_chorus_delay()],
    }),
    Arch("Pad", "Strings", 6, ["Strings"], lambda p, r: (p.spread(0.3), p.amp(r.choice([0.25, 0.4, 0.6]), 1.0, 1.0, 1.2, vel=0.3),
                                                       p.lfo2(L_SIN, 5.0), p.sh.__setitem__("voice_vib", 0.4)), {
        "source": [src_pwm(), src_saws((5, 9)), src_digital("reed"), src_digital("analog")],
        "filter": [flt(None, F_LP24, 2600, 0.05), flt("Silky", F_LP12, 3500, 0.05), flt("Dark", F_LP24, 1500, 0.1)],
        "layer": [lay_wide(), lay_octave(True)],
        "fx": [fx_chorus(), fx_space(), fx_phaser()],
        "motion": [mv_still(), mv_drift((0.05, 0.1)), mv_pan()],
    }),
    # ------------------------------------------------------------ PLUCK (40)
    Arch("Pluck", "Future Pluck", 10, ["Future", "Chord"], lambda p, r: (p.spread(r.uniform(0.5, 0.75)),
                                                                       p.amp(0.0, r.choice([0.4, 0.55, 0.7]), 0.0, 0.4, vel=0.35)), {
        "source": [src_saws((6, 10)), src_saw_pulse(), src_digital("bright"), src_digital("detune"), src_hybrid(), src_squares()],
        "filter": [flt(None, F_LP24, 700, 0.15, env=0.5, dec=0.3, sus=0.05), flt("Snappy", F_LP24, 500, 0.3, env=0.6, dec=0.18),
                   flt("Soft", F_LP12, 900, 0.1, env=0.4, dec=0.35)],
        "layer": [lay_wide(), lay_octave(True), lay_fifth(), lay_none()],
        "fx": [fx_chorus_delay(), fx_space(), fx_delay(), fx_phaser()],
    }),
    Arch("Pluck", "Mallet", 8, ["Mallet"], lambda p, r: p.amp(0.0, r.choice([0.3, 0.45, 0.6]), 0.0, 0.4, vel=0.5), {
        "source": [src_fm((0.08, 0.2)), src_sine_tri(), src_digital("wood", W_SIN), src_digital("bell", W_SIN)],
        "filter": [flt(None, F_LP24, 5000, 0.0), flt("Soft", F_LP12, 2500, 0.05)],
        "layer": [lay_none(), lay_octave(True), lay_wide()],
        "fx": [fx_room(), fx_delay(), fx_chorus(), fx_space()],
    }),
    Arch("Pluck", "House Pluck", 8, ["Deep", "House"], lambda p, r: (p.spread(0.2), p.amp(0.0, 0.6, 0.2, 0.35, vel=0.4)), {
        "source": [src_squares(), src_saws((4, 8)), src_digital("reed"), src_digital("analog"), src_pwm()],
        "filter": [flt(None, F_LP24, 450, 0.3, env=0.35, dec=0.3, sus=0.15), flt("Dub", F_LP24, 350, 0.45, env=0.3, dec=0.4, sus=0.2),
                   flt("Bright", F_LP12, 900, 0.2, env=0.3, dec=0.25)],
        "fx": [fx_chorus_delay(), fx_delay(), fx_space(), fx_phaser()],
        "layer": [lay_none(), lay_wide(), lay_octave(True), lay_fifth()],
        "motion": [mv_still(), mv_pan(), mv_steps((0.1, 0.2))],
    }),
    Arch("Pluck", "Kalimba", 6, ["Wood"], lambda p, r: p.amp(0.0, r.choice([0.3, 0.45]), 0.0, 0.35, vel=0.5), {
        "source": [src_digital("wood", W_SIN), src_digital("bell", W_SIN), src_digital("reed", W_SIN), src_fm((0.1, 0.18)),
                   src_hybrid(), src_digital("metal", W_SIN)],
        "filter": [flt(None, F_LP24, 4000, 0.05), flt("Muted", F_LP12, 2000, 0.1)],
        "fx": [fx_delay(), fx_room(), fx_chorus_delay()],
        "layer": [lay_none(), lay_octave(True), lay_wide()],
    }),
    Arch("Pluck", "Harp", 8, ["Harp"], lambda p, r: p.amp(0.0, r.choice([0.7, 1.0, 1.3]), 0.0, 0.8, vel=0.45), {
        "source": [src_saws((3, 6)), src_sine_tri(), src_squares(), src_digital("reed"), src_digital("wood")],
        "filter": [flt(None, F_LP24, 1500, 0.1, env=0.35, dec=0.5), flt("Bright", F_LP12, 2500, 0.1, env=0.3, dec=0.4)],
        "layer": [lay_none(), lay_wide(), lay_octave(True)],
        "fx": [fx_space(), fx_chorus(), fx_room()],
    }),
    # ------------------------------------------------------------ KEYS (32)
    Arch("Keys", "Electric Piano", 10, ["EP"], lambda p, r: (p.amp(0.0, r.choice([1.1, 1.5, 2.2]), 0.25, 0.4, vel=0.6), p.mod(4, S_VEL, D_CUT, 0.2)), {
        "source": [src_fm((0.12, 0.3)), Opt("Bell", lambda p, o, r: (p.o1(W_SIN, level=0.8), p.o2(W_SIN, octave=2, level=0.0), p.t1.__setitem__("osc2_level", 1.0), p.t1.__setitem__("fm_amt", r.uniform(0.2, 0.35))), ["FM"]),
                   src_digital("bell", W_SIN), src_sine_tri()],
        "filter": [flt(None, F_LP24, 3500, 0.0), flt("Mellow", F_LP12, 2000, 0.0), flt("Bright", F_LP24, 6000, 0.05)],
        "motion": [mv_still(), mv_pan()],
        "fx": [fx_chorus(), fx_phaser(), fx_tube(), fx_room(), fx_chorus_delay()],
        "layer": [lay_none(), lay_octave(True), lay_wide()],
    }),
    Arch("Keys", "Organ", 8, ["Organ"], lambda p, r: p.amp(0.002, 0.2, 1.0, 0.08, vel=0.1), {
        "source": [Opt(None, lambda p, o, r: (p.o1(W_SIN, level=0.75), p.o2(W_SIN, octave=1, level=0.55))),
                   Opt("Fifth", lambda p, o, r: (p.o1(W_SIN, level=0.75), p.o2(W_SIN, octave=1, pitch=7, level=0.4))),
                   Opt("Reedy", lambda p, o, r: (p.o1(W_SQR, level=0.7), p.o2(W_SIN, octave=1, level=0.5)), ["Reedy"]),
                   src_digital("reed", W_SIN)],
        "filter": [flt(None, F_LP24, 4000, 0.0), flt("Muted", F_LP12, 2200, 0.0)],
        "fx": [fx_chorus(), fx_phaser(), Opt("Driven", lambda p, o, r: (p.chorus(0.4, 0.4, 0.4), p.drive(0.3, 0.4)), ["Distorted"]), fx_room()],
        "motion": [mv_still(), mv_pan(), mv_synced(("1/8", "1/16"), (0.08, 0.15))],
    }),
    Arch("Keys", "Chord Keys", 8, ["Chord", "Stab"], lambda p, r: (p.spread(0.3), p.amp(0.0, r.choice([0.45, 0.6, 0.35]), r.choice([0.0, 0.25]), 0.2, vel=0.4)), {
        "source": [src_saws((4, 8)), src_saw_pulse(), src_squares(), src_digital("analog"), src_digital("reed")],
        "filter": [flt(None, F_LP24, 1800, 0.15, env=0.3, dec=0.25), flt("Dark", F_LP24, 1100, 0.2, env=0.3, dec=0.3), flt("Bright", F_LP12, 3000, 0.1)],
        "layer": [lay_none(), lay_wide(), lay_octave(True)],
        "fx": [fx_chorus_delay(), fx_room(), fx_phaser(), fx_delay()],
    }),
    Arch("Keys", "Clav", 6, ["Clav"], lambda p, r: p.amp(0.0, 0.4, 0.3, 0.1, vel=0.6), {
        "source": [Opt(None, lambda p, o, r: (p.o1(W_SQR, shape=0.55, level=0.95), p.o2(W_SAW, octave=1, level=0.4))),
                   src_digital("reed"), src_digital("wood")],
        "filter": [flt(None, F_LP12, 2600, 0.3, env=0.35, dec=0.15, sus=0.1), flt("Wah", F_BP12, 1500, 0.45, env=0.4, dec=0.2)],
        "fx": [fx_phaser(), fx_chorus(), fx_dry(), fx_tube()],
        "motion": [mv_still(), mv_synced(("1/8", "1/16"), (0.2, 0.35)), mv_drift((0.1, 0.2))],
        "layer": [lay_none(), lay_octave(False)],
    }),
    # ------------------------------------------------------------ BELL (18)
    Arch("Bell", "Bell", 10, ["FM"], lambda p, r: p.amp(0.0, r.choice([1.8, 2.5, 3.5]), 0.0, 2.0, vel=0.5), {
        "source": [Opt(None, lambda p, o, r: (p.o1(W_SIN, level=0.8), p.o2(W_SIN, octave=r.choice([1, 2]), level=0.0),
                                              p.t1.__setitem__("osc2_level", 1.0), p.t1.__setitem__("fm_amt", r.uniform(0.25, 0.5)))),
                   Opt("Ring", lambda p, o, r: (p.o1(W_SIN, level=0.7), p.o2(W_SIN, octave=2, level=0.5), p.t1.__setitem__("osc_mod", 1)), ["Ring"]),
                   src_digital("bell", W_SIN), src_digital("metal", W_SIN)],
        "filter": [flt(None, F_LP24, 8000, 0.0), flt("Soft", F_LP12, 4000, 0.0)],
        "layer": [lay_none(), lay_octave(True), lay_wide(), lay_fifth()],
        "fx": [fx_space(), fx_delay(), fx_chorus_delay()],
    }),
    Arch("Bell", "Music Box", 8, ["Digital"], lambda p, r: p.amp(0.0, r.choice([1.5, 2.0, 2.8]), 0.0, 1.6, vel=0.45), {
        "source": [src_digital("bell", W_SIN), src_digital("bright", W_SIN), src_digital("metal", W_SIN), src_hybrid()],
        "filter": [flt(None, F_LP24, 9000, 0.0), flt("Hollow", F_BP12, 3000, 0.3)],
        "layer": [lay_wide(), lay_octave(True), lay_none()],
        "fx": [fx_delay(), fx_space(), fx_room()],
    }, boost=1.35),
    # ------------------------------------------------------------ ARP (34) / SEQUENCE (18)
    Arch("Arp", "Arp", 34, [], lambda p, r: (p.amp(0.0, r.choice([0.2, 0.3, 0.45]), r.choice([0.0, 0.3, 0.5]), 0.15, vel=0.3)), {
        "source": [src_saws((6, 10)), src_squares(), src_digital("bell", W_SIN), src_digital("bright"), src_fm((0.1, 0.2)),
                   src_sine_tri(), src_digital("wood", W_SIN), src_pwm(), src_sync((0.2, 0.4))],
        "filter": [flt(None, F_LP24, 2500, 0.15, env=0.35, dec=0.15), flt("Resonant", F_LP24, 600, 0.45, env=0.45, dec=0.12),
                   flt("Open", F_LP12, 7000, 0.05), flt("Thin", F_HP12, 500, 0.15)],
        "pattern": [Opt(None, lambda p, o, r, m=m, rt=rt, oc=oc: p.arp(rate=rt, mode=m, gate=r.uniform(0.4, 0.7), octaves=oc,
                                                                        tempo=r.choice([110.0, 120.0, 124.0, 128.0, 138.0])))
                    for m, rt, oc in [(ARP_UP, "1/16", 2), (ARP_ALT1, "1/16", 2), (ARP_DOWN, "1/16", 1), (ARP_UP, "1/8", 3),
                                      (ARP_ALT2, "1/16T", 2), (ARP_RND, "1/16", 2), (ARP_UP, "1/8T", 1), (ARP_ALT1, "1/32", 1)]],
        "layer": [lay_none(), lay_wide(), lay_octave(True)],
        "motion": [mv_still(), mv_synced(("1/2", "1/1"), (0.15, 0.3)), mv_pan()],
        "fx": [fx_delay(), fx_chorus_delay(), fx_space(), fx_phaser(), fx_dry()],
    }, words=["source", "filter", "layer", "motion", "fx"]),
    Arch("Sequence", "Sequence", 18, [], lambda p, r: p.amp(0.0, r.choice([0.15, 0.25]), r.choice([0.1, 0.3]), 0.1, vel=0.3), {
        "source": [Opt("Bass", lambda p, o, r: (p.mono(t2=False), p.o1(W_SAW, octave=-1, level=0.8), p.o2(W_SQR, octave=-1, fine=5, level=0.5)), ["Bass"]),
                   src_saws((5, 9)), src_squares(), src_digital("reed"), src_digital("metal"), src_fm((0.15, 0.3))],
        "filter": [flt(None, F_LP24, 300, 0.4, env=0.45, dec=0.12), flt("Bright", F_LP12, 1500, 0.25, env=0.3, dec=0.1), flt("Hollow", F_BP12, 900, 0.4, env=0.3, dec=0.1)],
        "pattern": [Opt(None, lambda p, o, r, st=st: p.arp(rate=r.choice(["1/16", "1/16", "1/8"]), mode=r.choice([ARP_TRIG, ARP_UP, ARP_ALT1]),
                                                          gate=r.uniform(0.35, 0.6), octaves=1, tempo=r.choice([122.0, 124.0, 128.0, 132.0]),
                                                          swing=r.choice([0.0, 0.0, 0.2, 0.35]), steps=st))
                    for st in ["xx.xx.x.", "x.xx.xx.", "x..x..x.", "xxx.xx.x", "x.x.xxx.", "x..xx.x.", "xx.x.xx.", "x.xxx..x"]],
        "motion": [mv_still(), mv_synced(("1/1", "1/2"), (0.15, 0.3))],
        "fx": [fx_delay(), fx_dry(), fx_chorus(), fx_drive()],
    }),
    # ------------------------------------------------------------ BRASS (16) / CHOIR (12)
    Arch("Brass", "Brass", 16, ["Synth Brass"], lambda p, r: (p.spread(0.3), p.amp(r.choice([0.02, 0.05, 0.08]), 0.5, 0.9, 0.35, vel=0.4),
                                                            p.mod(4, S_VEL, D_CUT, 0.15)), {
        "source": [src_saws((5, 9)), src_saw_pulse(), src_digital("reed"), src_digital("analog"), src_pwm(),
                   src_sync((0.1, 0.25)), src_squares()],
        "filter": [flt(None, F_LP24, 900, 0.12, env=0.35, dec=0.6, sus=0.45), flt("Bold", F_LP24, 1300, 0.2, env=0.4, dec=0.4, sus=0.5),
                   flt("Soft", F_LP12, 700, 0.1, env=0.25, dec=0.8, sus=0.5)],
        "layer": [lay_none(), lay_wide(), lay_octave(False), lay_fifth()],
        "fx": [fx_chorus(), fx_room(), fx_space(), fx_tube()],
        "motion": [mv_still(), mv_drift(), mv_morph(), mv_wheel()],
    }),
    Arch("Choir", "Choir", 12, ["Vocal"], lambda p, r: (p.spread(0.3), p.amp(r.choice([0.3, 0.5, 0.8]), 1.0, 1.0, 1.6, vel=0.2),
                                                      p.lfo2(L_SIN, 4.8), p.sh.__setitem__("voice_vib", 0.4)), {
        "source": [Opt(None, lambda p, o, r: (p.o1(W_DIG, digital=dig("vocal", r.randrange(8), r.randrange(2)), level=0.85),
                                              p.o2(W_DIG, digital=dig("vocal", r.randrange(8)), fine=7, level=0.6))),
                   src_digital("vocal", W_SIN), Opt("Breathy", lambda p, o, r: (p.o1(W_DIG, digital=dig("vocal", r.randrange(8)), level=0.85), p.noise(0.1)), ["Breath"])],
        "filter": [flt(None, F_LP24, 3000, 0.1), flt("Formant", F_BP12, 1200, 0.35), flt("Soft", F_LP12, 2000, 0.05)],
        "layer": [lay_wide(), lay_octave(True), lay_fifth()],
        "fx": [fx_space(), fx_chorus(), fx_flanger()],
    }),
    # ------------------------------------------------------------ HIT (18) / SFX (16)
    Arch("Hit", "Stab", 18, ["Stab"], lambda p, r: (p.spread(0.4), p.amp(0.0, r.choice([0.3, 0.45, 0.6]), 0.0, 0.3, vel=0.3)), {
        "source": [src_saws((6, 10)), Opt("Rave", lambda p, o, r: (p.o1(W_SAW, level=0.65), p.o2(W_SAW, pitch=7, level=0.5)), ["Rave"]),
                   Opt("Sus", lambda p, o, r: (p.o1(W_SAW, level=0.65), p.o2(W_SQR, pitch=5, level=0.45)), ["House"]),
                   src_digital("metal"), src_squares(), Opt("Noisy", lambda p, o, r: (p.o1(W_SAW, level=0.65), p.o2(W_SAW, octave=-1, level=0.5), p.noise(0.15)), ["Impact"])],
        "filter": [flt(None, F_LP24, 2200, 0.2, env=0.35, dec=0.25), flt("Bright", F_LP12, 4000, 0.15), flt("Dark", F_LP24, 1100, 0.25, env=0.3, dec=0.3)],
        "layer": [lay_none(), lay_octave(False), lay_wide(), lay_fifth()],
        "fx": [fx_chorus_delay(), fx_space(), fx_drive(), fx_delay()],
    }),
    Arch("SFX", "Effect", 16, [], lambda p, r: None, {
        "design": [Opt(None, lambda p, o, r, k=k: sfx_design(p, k, r)) for k in range(8)],
        "fx": [fx_space(), fx_delay(), fx_phaser()],
    }, words=["design", "fx"]),
]


def rave_design(p, kind, r):
    """Six different rave leads (the archetype's 'design' axis)."""
    if kind == 0:      # classic: pulse + saw an octave down, stacked, a short dive into the note
        p.unison(18.0).o1(W_SQR, shape=0.3, pwm=0.5, level=0.7).o2(W_SAW, octave=-1, level=0.55)
        p.lfo1(L_TRI, hz=4.5, depth=0.7, key=0)
        p.fenv(0.0, 0.08, 0.0, 0.1).mod(2, S_ENV1, D_PITCH, 0.06)
        p.filt(3800, 0.1).amp(0.002, 0.3, 1.0, 0.25, vel=0.15).chorus(0.4, 0.55, 0.3).verb(0.15, 0.55)
        return "Rave Lead", ["Unison"]
    if kind == 1:      # siren: a synced LFO swings the pitch
        p.mono(legato=True).o1(W_SQR, shape=0.15, level=0.75).o2(W_SAW, fine=6, level=0.45)
        p.lfo1(L_TRI, sync="1/8", depth=1.0, key=2).mod(1, S_LFO1, D_PITCH, 0.12)
        p.filt(4200, 0.15).amp(0.004, 0.3, 1.0, 0.3, vel=0.1).delay("3/16", 0.3, 0.16).verb(0.2, 0.6)
        return "Siren", ["Siren"]
    if kind == 2:      # dark: low and thick, timbre 2 an octave down through a resonant filter
        p.unison(14.0).o1(W_SAW, level=0.7).o2(W_SQR, octave=-1, shape=0.2, level=0.6)
        p.filt(1100, 0.35).fenv(0.0, 0.4, 0.4, 0.2, amt=0.25).amp(0.002, 0.3, 1.0, 0.2, vel=0.1)
        p.layer(0.45, osc1_octave=-1, osc2_octave=-2, filter_cutoff=cut(700), amp_pan=0.0)
        p.drive(0.4, 0.45).chorus(0.3, 0.4, 0.25, kind=3)
        return "Dark Rave Lead", ["Dark"]
    if kind == 3:      # sync: a hard-synced saw with an envelope sweep, stacked
        p.unison(16.0).o1(W_SAW, level=0.0).o2(W_SAW, pitch=7, level=0.8)
        p.t1["osc_mod"] = 2
        p.fenv(0.0, 0.5, 0.25, 0.3).mod(2, S_ENV1, D_O2PITCH, 0.55).mod(4, S_WHEEL, D_O2PITCH, 0.3)
        p.filt(5000, 0.1).amp(0.002, 0.3, 1.0, 0.25, vel=0.15).chorus(0.3, 0.45, 0.3).verb(0.15, 0.55)
        return "Sync Rave Lead", ["Sync"]
    if kind == 4:      # fifth: every key plays a root + fifth
        p.mono(legato=True).o1(W_SAW, level=0.65).o2(W_SAW, pitch=7, fine=4, level=0.55)
        p.filt(2600, 0.2).fenv(0.0, 0.3, 0.3, 0.2, amt=0.3).amp(0.002, 0.4, 0.8, 0.25, vel=0.2)
        p.wide(6.0, 0.35).chorus(0.3, 0.4, 0.25).delay("1/8", 0.3, 0.15).verb(0.15, 0.55)
        return "Fifth Lead", ["Fifth"]
    # rise: each note climbs into pitch from below while the filter opens
    p.unison(20.0).o1(W_SQR, shape=0.35, level=0.7).o2(W_SAW, octave=-1, level=0.5)
    p.fenv(0.0, 0.3, 0.0, 0.2, amt=0.3).mod(2, S_ENV1, D_PITCH, -0.1)
    p.filt(1600, 0.25).amp(0.01, 0.3, 1.0, 0.3, vel=0.1).chorus(0.35, 0.5, 0.3).drive(0.25, 0.35).verb(0.2, 0.6)
    return "Rising Lead", ["Rise"]


def sfx_design(p, kind, r):
    if kind == 0:
        p.o1(W_SAW, level=0.6).noise(0.3).filt(400, 0.3, key=0.0)
        p.fenv(r.choice([3.0, 4.0]), 0.5, 1.0, 1.0, amt=0.6).mod(2, S_ENV1, D_PITCH, 0.6).amp(1.5, 1.0, 1.0, 1.0)
        return "Riser", ["Riser"]
    if kind == 1:
        p.o1(W_SQR, shape=0.3, level=0.75).fenv(0.0, r.choice([0.12, 0.2]), 0.0, 0.1)
        p.mod(2, S_ENV1, D_PITCH, r.uniform(0.6, 0.9)).amp(0.0, 0.3, 0.0, 0.2)
        return "Zap", ["Zap"]
    if kind == 2:
        p.o1(W_NOI, level=0.0).noise(0.7).filt(800, 0.55, kind=F_BP12, key=0.0)
        p.lfo1(L_TRI, sync=r.choice(["1/1", "1/2"]), depth=1.0, key=1).mod(1, S_LFO1, D_CUT, 0.6).amp(0.3, 1.0, 1.0, 1.0)
        return "Sweep", ["Sweep"]
    if kind == 3:
        p.o1(W_SAW, octave=-1, level=0.6).o2(W_SAW, octave=-1, fine=12, level=0.5).filt(5000, 0.2, key=0.0)
        p.fenv(0.0, r.choice([2.5, 3.5]), 0.0, 1.0).mod(2, S_ENV1, D_PITCH, 0.5).amp(0.0, 3.0, 0.0, 1.0)
        return "Downlifter", ["Downlifter"]
    if kind == 4:      # laser: fast pitch dive with ring mod
        p.o1(W_SAW, level=0.6).o2(W_SQR, octave=1, level=0.5).t1["osc_mod"] = 1
        p.fenv(0.0, 0.15, 0.0, 0.1).mod(2, S_ENV1, D_PITCH, 0.8).amp(0.0, 0.25, 0.0, 0.2)
        return "Laser", ["Zap"]
    if kind == 5:      # alarm: square pitch swinging on a synced LFO
        p.o1(W_SQR, level=0.7).lfo1(L_SQR, sync="1/8", depth=1.0, key=1).mod(1, S_LFO1, D_PITCH, 0.08).amp(0.0, 0.3, 1.0, 0.3)
        return "Alarm", ["Siren"]
    if kind == 6:      # metallic drone with slow sweep
        p.o1(W_DIG, digital=dig("metal", r.randrange(8), 1), level=0.7).o2(W_SAW, fine=11, level=0.5)
        p.filt(900, 0.5, kind=F_BP12).lfo1(L_SIN, hz=0.1, depth=1.0, key=0).mod(1, S_LFO1, D_CUT, 0.35).amp(1.0, 1.0, 1.0, 2.0)
        return "Drone", ["Drone"]
    # stepped noise texture
    p.o1(W_NOI, level=0.0).noise(0.6).filt(1500, 0.6, kind=F_BP12, key=0.0)
    p.lfo1(L_RND, sync="1/16", depth=1.0, key=1).mod(1, S_LFO1, D_CUT, 0.5).amp(0.0, 1.0, 1.0, 0.5)
    return "Glitch", ["Glitch"]


# ---------------------------------------------------------------- assembly
AXIS_ORDER = ["design", "stack", "source", "filter", "pattern", "motion", "layer", "weight", "grit", "fx"]
BASE_OCT = {"Bass": 0, "Lead": 0, "Pad": 0, "Pluck": 0, "Keys": 0, "Bell": 0, "Arp": 0, "Sequence": 0,
            "Brass": 0, "Choir": 0, "Hit": 0, "SFX": 0}


def make_candidate(arch, v, k):
    """Candidate k of slot v: one option per axis, all seeded so the bank is reproducible."""
    r = random.Random(f"nanofrog-design-{arch.noun}-{v}-{k}")
    choice = {}
    for ax, opts in arch.axes.items():
        if v == 0 and arch.first and ax in arch.first:
            choice[ax] = arch.first[ax]
        elif ax == arch.cycle:
            choice[ax] = v % len(opts)
        else:
            choice[ax] = r.randrange(len(opts))
    p = Patch()
    arch.core(p, r)
    o = BASE_OCT[arch.cat]
    words, tags, own = [], list(arch.tags), None
    for ax in AXIS_ORDER:
        if ax not in arch.axes:
            continue
        opt = arch.axes[ax][choice[ax]]
        res = opt.fn(p, o, r)
        word = opt.word
        if isinstance(res, tuple) and len(res) == 2 and isinstance(res[0], str):
            own, extra = res
            tags += extra
        elif isinstance(res, str):
            word = res          # the option did something else here: name that
        tags += opt.tags
        if word and ax in arch.words:
            words.append(word)
    # quiet sources (sparse digital waves): more level into the mixer
    for t in (p.t1, p.t2):
        if t is not None and arch.boost != 1.0:
            t["mix_o1"] = min(1.0, t["mix_o1"] * arch.boost)
            t["mix_o2"] = min(1.0, t["mix_o2"] * arch.boost)
    # a band-pass on a (near-)sine has nothing to pass: use a low-pass there
    for t in (p.t1, p.t2):
        if t is not None and t["filter_type"] == F_BP12 and t["osc1_wave"] in (W_SIN, W_TRI):
            t["filter_type"] = F_LP12
            t["filter_cutoff"] = min(1.0, t["filter_cutoff"] + 0.11)   # ~2x the frequency
            words = [w for w in words if w not in ("Hollow", "Ringing", "Wah", "Vowel", "Talking", "Formant", "Nasal")]
    tags = [t for i, t in enumerate(tags) if t and t not in tags[:i]]
    return p, tags, words, own, choice


def candidate_name(arch, words, own, used, v):
    noun = own or arch.noun
    tries = []
    for w in words:
        if w and w not in noun:
            tries.append(f"{w} {noun}")
    for a in range(len(words)):
        for b in range(a + 1, len(words)):
            if words[a] and words[b] and words[a] != words[b]:
                tries.append(f"{words[a]} {words[b]} {noun}")
    tries.append(noun)
    for t in tries:
        if t not in used:
            return t
    adj = ADJ[arch.cat]
    for i in range(len(adj) * 4):
        t = f"{adj[(v + i) % len(adj)]} {(words[0] + ' ') if words else ''}{noun}"
        if t not in used:
            return t
    raise AssertionError(noun)


ADJ = {c: w for c, w in {
    "Bass": ["Deep", "Heavy", "Midnight", "Concrete", "Velvet", "Basement", "Low", "Thick", "Iron", "Smoky", "Tight", "Night", "Solid", "Steel", "Liquid", "Shadow"],
    "Lead": ["Silver", "Laser", "Soaring", "Golden", "Crystal", "Electric", "Neon", "Prism", "Comet", "Signal", "Vivid", "Hyper", "Polar", "Sonic"],
    "Pad": ["Endless", "Distant", "Floating", "Frozen", "Golden", "Hazy", "Slow", "Quiet", "Ocean", "Northern", "Dusk", "Dawn", "Misty", "Silk", "Calm"],
    "Pluck": ["Bouncy", "Tiny", "Sunny", "Crisp", "Candy", "Quick", "Spark", "Pastel", "Lucky", "Fresh", "Happy", "Late"],
    "Keys": ["Vintage", "Smooth", "Sunday", "Lounge", "Gospel", "Late Night", "Soulful", "Classic", "Dusty", "Dreamy"],
    "Bell": ["Crystal", "Temple", "Silver", "Winter", "Distant", "Starry", "Clear", "Tiny"],
    "Arp": ["Running", "Spinning", "Rolling", "Cascading", "Twinkling", "Racing", "Bubbling", "Climbing", "Spiral", "Looping", "Flowing", "Dancing"],
    "Sequence": ["Groove", "Motion", "Machine", "Engine", "Tracker", "Shuffle", "Step", "Loop"],
    "Brass": ["Royal", "Big", "Fat", "Golden", "Grand", "Heroic"],
    "Choir": ["Angel", "Ghost", "Distant", "Cathedral", "Glass", "Hollow"],
    "Hit": ["Big", "Sharp", "Hard", "Short", "Loud", "Club", "Massive"],
    "SFX": ["Rocket", "Space", "Static", "Storm", "Orbit", "Signal", "Vapor", "Falling"],
}.items()}

CANDIDATES = 10


def build(choice_map=None, all_candidates=False):
    """The bank (Init + one candidate per slot), or every candidate (for diversify.py)."""
    choice_map = choice_map or {}
    presets = [("Init", ["Init"], init_patch().values(), "init", 0, 0)]
    used = {"Init"}
    assert sum(a.count for a in ARCHES) == 400, sum(a.count for a in ARCHES)
    for arch in ARCHES:
        for v in range(arch.count):
            ks = range(CANDIDATES) if all_candidates else [choice_map.get(f"{arch.noun}/{v}", 0)]
            for k in ks:
                p, tags, words, own, _ = make_candidate(arch, v, k)
                name = candidate_name(arch, words, own, used, v) if not all_candidates else f"{arch.noun} {v} {k}"
                if not all_candidates:
                    used.add(name)
                presets.append((name, [arch.cat] + tags, p.values(), arch.noun, v, k))
    return presets


def init_patch():
    p = Patch()
    p.o1(W_SAW, level=0.8).filt(18000, 0.0).amp(0.002, 0.3, 1.0, 0.25, vel=0.0)
    return p


def main():
    import json
    choice_file = ROOT / "Tools" / "bank_choice.json"
    choice_map = json.loads(choice_file.read_text()) if choice_file.exists() else {}
    presets = build(choice_map)
    assert len(presets) == 401, len(presets)
    levels_file = ROOT / "Tools" / "bank_levels.json"
    levels = {int(k): v for k, v in json.loads(levels_file.read_text()).items()} \
        if levels_file.exists() else {}
    oi = KIDS.index("output_level")
    bank = {"type": "NanoFrogBank", "version": 1, "param_count": len(KIDS),
            "param_ids": KIDS, "presets": []}
    for i, (name, tags, vals, _, _, _) in enumerate(presets):
        if i in levels:
            vals[oi] = round(levels[i], 4)
        bank["presets"].append({"name": name, "tags": tags, "values": vals})
    (ROOT / "FactoryBank").mkdir(exist_ok=True)
    (ROOT / "FactoryBank" / "NanoFrogFactory.json").write_text(json.dumps(bank))
    print(f"wrote {len(presets)} presets", file=sys.stderr)


if __name__ == "__main__":
    main()
