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
    def mono(self, glide=0.0, legato=False, t2=True):
        for pre in ("", "t2_") if t2 else ("",):
            self.sh[pre + "voice_assign"] = 1
            self.sh[pre + "voice_poly"] = 1
            self.sh[pre + "voice_portamento"] = glide
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

def pick(seq, v):
    return seq[v % len(seq)]


def j(rng, x, amt):
    return x + rng.uniform(-amt, amt)


# ============================== BASS
def b_808(v, rng):
    p = Patch().mono(glide=pick([0.0, 0.0, 0.22, 0.3], v), legato=v % 4 >= 2)
    p.o1(W_SIN, octave=-1, level=0.95).filt(2400, key=0.0)
    p.amp(0.0, pick([0.8, 1.1, 0.6, 1.5, 0.9], v), 0.0, pick([0.15, 0.25, 0.12], v), vel=0.15)
    if v == 0:
        return p, ["808", "Sub"]                       # clean: pure sine, nothing else
    p.fenv(0.0, 0.06, 0.0, 0.1).mod(2, S_ENV1, D_PITCH, pick([0.12, 0.08, 0.16, 0.1], v))
    if v % 3 == 1:
        p.sat(pick([0.35, 0.5, 0.25], v), 0.45)
    if v % 3 == 2:
        p.o1(W_TRI, octave=-1, level=0.95).drive(0.25, 0.35)
    tags = ["808", "Sub"] + (["Glide"] if v % 4 >= 2 else [])
    return p, tags


def b_sub(v, rng):
    p = Patch().mono(glide=pick([0.0, 0.12], v), legato=True)
    p.o1(pick([W_SIN, W_TRI, W_SIN, W_SQR], v), octave=-1, level=0.9)
    p.o2(W_SIN, octave=-1, level=0.0)
    if v % 4 == 3:
        p.filt(220, 0.0)
    else:
        p.filt(900, 0.0, key=0.0)
    p.amp(0.004, 0.2, 1.0, 0.12, vel=0.1)
    if v % 2:
        p.o2(W_SQR, octave=0, level=0.18)              # a little edge an octave up
        p.filt(pick([500, 650, 420], v), 0.05)
    return p, ["Sub"]


def b_reese(v, rng):
    det = pick([12.0, 18.0, 9.0, 24.0, 15.0], v)
    p = Patch().mono(glide=pick([0.0, 0.18], v), legato=True)
    p.o1(W_SAW, octave=-1, fine=-det, level=0.75).o2(W_SAW, octave=-1, fine=det, level=0.75)
    p.filt(pick([700, 900, 550, 1200, 800], v), pick([0.12, 0.2, 0.08], v))
    p.fenv(0.0, 0.4, 0.6, 0.2, amt=0.12).amp(0.003, 0.3, 1.0, 0.15, vel=0.1)
    p.lfo1(L_TRI, hz=pick([0.15, 0.3, 0.22], v), depth=1.0, key=0).mod(1, S_LFO1, D_CUT, pick([0.12, 0.18, 0.08], v))
    if v % 2:
        p.chorus(0.25, 0.3, 0.12, kind=pick([3, 2], v // 2))   # phaser / flanger movement
    if v % 3 == 0:
        p.drive(pick([0.3, 0.45], v), 0.4)
    p.eq(low=1.5)
    return p, ["Reese"]


def b_growl(v, rng):
    div = pick(["1/8", "1/16", "1/8", "1/4", "1/12"], v)
    p = Patch().mono(glide=0.08, legato=True)
    if v % 2 == 0:
        p.o1(W_SAW, octave=-1, level=0.7).o2(W_SQR, octave=-1, fine=7, level=0.6, shape=0.3)
        p.t1["fm_amt"] = pick([0.25, 0.35, 0.2], v)
    else:
        p.o1(W_DIG, octave=-1, digital=dig(pick(["metal", "vocal", "bright"], v), v), level=0.8)
        p.o2(W_SAW, octave=-1, fine=-6, level=0.5)
    p.filt(pick([900, 1200, 700], v), pick([0.35, 0.45, 0.3], v), kind=pick([F_LP24, F_BP12, F_LP12], v))
    p.lfo1(pick([L_SIN, L_TRI, L_SAW], v), sync=div, depth=1.0, key=1)
    p.mod(1, S_LFO1, D_CUT, pick([0.55, 0.45, 0.65], v)).mod(3, S_LFO1, D_O1SHAPE, 0.5)
    p.amp(0.002, 0.3, 1.0, 0.12, vel=0.1).drive(pick([0.45, 0.55, 0.4], v), 0.6).eq(low=2.0, high=1.5)
    p.sh["song_tempo"] = pick([140.0, 150.0, 174.0], v)
    return p, ["Growl", "Distorted"]


def b_wobble(v, rng):
    div = pick(["1/4", "1/8", "1/8", "1/6", "3/8", "1/16"], v)
    p = Patch().mono(glide=0.1, legato=True)
    p.o1(W_SAW, octave=-1, level=0.7).o2(pick([W_SQR, W_SAW], v), octave=-1, fine=pick([5, -8], v), level=0.6)
    p.filt(pick([180, 220, 150], v), pick([0.35, 0.45, 0.4], v), key=0.3)
    p.lfo1(pick([L_SIN, L_TRI, L_SIN, L_SQR], v), sync=div, depth=1.0, key=1)
    p.mod(1, S_LFO1, D_CUT, pick([0.75, 0.65, 0.8], v)).mod(4, S_WHEEL, D_L1RATE, 0.5)
    p.amp(0.002, 0.3, 1.0, 0.15, vel=0.1).drive(pick([0.3, 0.4], v), 0.45).eq(low=2.0)
    p.sh["song_tempo"] = pick([140.0, 150.0, 128.0], v)
    return p, ["Wobble"]


def b_pluck(v, rng):
    p = Patch().mono(t2=False) if v % 3 else Patch()
    p.o1(pick([W_SAW, W_SQR, W_SAW], v), octave=-1, shape=pick([0.0, 0.3], v), level=0.8)
    p.o2(pick([W_SQR, W_SAW, W_SIN], v), octave=pick([-1, -2], v), fine=pick([4, -5, 0], v), level=0.5)
    p.filt(pick([180, 240, 300, 150], v), pick([0.2, 0.3, 0.12], v))
    p.fenv(0.0, pick([0.18, 0.25, 0.12, 0.3], v), 0.0, 0.15, amt=pick([0.45, 0.55, 0.4], v))
    p.amp(0.0, pick([0.35, 0.5, 0.28], v), pick([0.15, 0.0, 0.25], v), 0.12, vel=0.4)
    p.mod(4, S_VEL, D_CUT, 0.15)
    if v % 4 == 3:
        p.sat(0.3, 0.55)
    return p, ["Pluck"]


def b_house(v, rng):
    p = Patch().mono(t2=False)
    if v % 2 == 0:                                      # organ-ish: sine + square an octave up
        p.o1(W_SIN, octave=-1, level=0.85).o2(W_SQR, octave=0, level=0.3)
        p.filt(pick([1400, 1800, 1100], v), 0.0)
    else:                                               # rubbery: square through a short env
        p.o1(W_SQR, octave=-1, shape=pick([0.2, 0.35], v), level=0.8).o2(W_SIN, octave=-1, level=0.5)
        p.filt(pick([350, 450], v), 0.25).fenv(0.0, 0.16, 0.1, 0.1, amt=0.35)
    p.amp(0.0, pick([0.25, 0.32, 0.2], v), pick([0.0, 0.3], v), 0.08, vel=0.35)
    p.sh["song_tempo"] = 124.0
    return p, ["House"]


def b_analog(v, rng):
    p = Patch().mono(glide=pick([0.0, 0.2, 0.0, 0.3], v), legato=v % 2 == 1)
    p.o1(W_SAW, octave=-1, level=0.8).o2(pick([W_SQR, W_SAW, W_SQR], v), octave=pick([-1, -2], v),
                                         fine=pick([6, -4, 8], v), level=0.6, shape=pick([0.0, 0.25], v))
    p.filt(pick([320, 450, 260, 600], v), pick([0.25, 0.15, 0.35], v))
    p.fenv(0.0, pick([0.3, 0.45, 0.22], v), pick([0.2, 0.35, 0.1], v), 0.2, amt=pick([0.35, 0.28, 0.42], v))
    p.amp(0.002, 0.4, 0.9, 0.15, vel=0.3)
    if v % 3 == 2:
        p.sat(0.35, 0.5, tube=True)
    tags = ["Analog"] + (["Glide"] if p.sh["voice_portamento"] > 0 else [])
    return p, tags


def b_acid(v, rng):
    p = Patch().mono(glide=pick([0.14, 0.18], v), legato=True)
    p.o1(pick([W_SAW, W_SQR, W_SAW], v), octave=-1, level=0.85).o2(W_SAW, level=0.0)
    p.filt(pick([260, 320, 220], v), pick([0.72, 0.65, 0.78], v), kind=F_LP24, key=0.35)
    p.fenv(0.0, pick([0.18, 0.25, 0.14], v), 0.0, 0.1, amt=pick([0.5, 0.42, 0.58], v))
    p.amp(0.0, 0.3, 0.85, 0.08, vel=0.2).mod(4, S_VEL, D_CUT, 0.2)
    p.drive(pick([0.35, 0.5, 0.25], v), 0.45)
    if v % 2:
        p.delay("3/16", 0.25, 0.12)
    return p, ["Acid", "Glide"]


def b_fm(v, rng):
    p = Patch().mono(t2=False)
    p.o1(W_SIN, octave=-1, level=0.9).o2(W_SIN, octave=pick([0, 1, 0], v), level=0.0)
    p.t1["fm_amt"] = pick([0.35, 0.28, 0.42], v)
    p.filt(pick([1200, 900, 1600], v), 0.1).fenv(0.0, 0.2, 0.25, 0.15, amt=0.3)
    p.amp(0.0, 0.5, pick([0.6, 0.4], v), 0.12, vel=0.35)
    if v % 2:
        p.mod(1, S_ENV1, D_PITCH, 0.03)
    return p, ["FM"]


# ============================== LEAD
def l_supersaw(v, rng):
    p = Patch().unison(pick([24.0, 30.0, 20.0, 35.0, 27.0], v))
    p.o1(W_SAW, level=0.7).o2(W_SAW, octave=pick([0, 1, 0, -1], v), fine=pick([7, 5, 10], v), level=0.55)
    p.filt(pick([5200, 7000, 4200, 8800], v), 0.1).fenv(0.0, 0.5, 0.6, 0.3, amt=0.12)
    p.amp(0.004, 0.4, 1.0, pick([0.35, 0.5, 0.25], v), vel=0.2)
    p.chorus(0.22, 0.3, 0.3).delay(pick(["3/16", "1/8", "1/4"], v), 0.32, 0.18).verb(0.2, 0.6)
    p.sh["voice_portamento"] = p.sh["t2_voice_portamento"] = pick([0.0, 0.12], v)
    return p, ["Supersaw"]


def l_square(v, rng):
    p = Patch().mono(glide=pick([0.0, 0.1, 0.16], v), legato=v % 3 > 0)
    p.o1(W_SQR, shape=pick([0.0, 0.35, 0.6], v), level=0.75).o2(W_SQR, octave=pick([0, 1], v), fine=4, level=0.3)
    p.filt(pick([3800, 5000, 2600], v), 0.1).amp(0.003, 0.3, 1.0, 0.2, vel=0.2)
    p.lfo2(L_TRI, 5.5).sh["voice_vib"] = 0.5
    p.mod(3, S_WHEEL, D_O1SHAPE, 0.0)
    p.delay(pick(["1/8", "3/16"], v), 0.28, 0.15).verb(0.12, 0.45)
    return p, ["Square", "Chip" if v % 2 else "Mono"]


def l_sync(v, rng):
    p = Patch().mono(glide=pick([0.0, 0.12], v), legato=True)
    p.o1(W_SAW, level=0.0).o2(W_SAW, pitch=pick([7, 12, 5, 0], v), level=0.85)
    p.t1["osc_mod"] = 2
    p.fenv(0.0, pick([0.35, 0.6, 0.25], v), pick([0.15, 0.3], v), 0.3)
    p.mod(2, S_ENV1, D_O2PITCH, pick([0.6, 0.45, 0.75], v)).mod(1, S_WHEEL, D_O2PITCH, 0.4)
    p.filt(pick([4500, 6500, 3500], v), 0.12).amp(0.003, 0.3, 1.0, 0.25, vel=0.2)
    p.delay("3/16", 0.3, 0.16).verb(0.14, 0.5)
    if v % 3 == 2:
        p.drive(0.3, 0.4)
    return p, ["Sync"]


def l_glide(v, rng):
    p = Patch().mono(glide=pick([0.28, 0.36, 0.22, 0.42], v), legato=True)
    p.o1(pick([W_SAW, W_SAW, W_SQR], v), level=0.75).o2(W_SAW, fine=pick([-7, 9, -5], v), level=0.6)
    p.filt(pick([2800, 3600, 2200, 4400], v), pick([0.25, 0.18, 0.32], v))
    p.fenv(0.0, 0.5, 0.45, 0.3, amt=0.2).amp(0.004, 0.3, 1.0, 0.3, vel=0.15)
    p.lfo2(L_SIN, 5.2).sh["voice_vib"] = 0.6
    p.delay(pick(["1/4", "3/16", "1/8"], v), 0.38, 0.2).verb(0.18, 0.6)
    if v % 4 == 1:
        p.sat(0.3, 0.6, tube=True)
    return p, ["Glide", "Mono"]


def l_soft(v, rng):
    p = Patch().mono(glide=pick([0.1, 0.0], v), legato=True)
    p.o1(pick([W_SIN, W_TRI, W_TRI], v), level=0.85)
    p.o2(W_SIN, octave=1, level=pick([0.15, 0.0, 0.25], v)).noise(pick([0.04, 0.08, 0.0], v))
    p.filt(pick([3000, 2400, 4200], v), 0.0).amp(pick([0.04, 0.07, 0.02], v), 0.3, 1.0, 0.35, vel=0.3)
    p.lfo2(L_SIN, 5.0).sh["voice_vib"] = 0.7
    p.mod(3, S_LFO2, D_PITCH, 0.0)
    p.chorus(0.15, 0.2, 0.2).delay("1/4", 0.35, 0.18).verb(0.25, 0.65)
    return p, ["Soft"]


def l_digital(v, rng):
    fam = pick(["vocal", "bright", "reed", "vocal", "analog", "metal", "bright"], v)
    p = Patch().mono(glide=pick([0.0, 0.14], v), legato=True)
    p.o1(W_DIG, digital=dig(fam, v, v % 2), level=0.8).o2(W_SAW, fine=6, level=pick([0.25, 0.0, 0.35], v))
    p.filt(pick([4200, 3000, 6000], v), 0.12).amp(0.005, 0.3, 1.0, 0.25, vel=0.2)
    p.lfo1(L_TRI, hz=pick([0.6, 1.2, 0.3], v), depth=0.6).mod(1, S_LFO1, D_CUT, 0.08)
    p.mod(3, S_WHEEL, D_O1SHAPE, 0.5)
    p.lfo2(L_SIN, 5.3).sh["voice_vib"] = 0.5
    p.delay(pick(["3/16", "1/8"], v), 0.3, 0.15).verb(0.15, 0.55)
    return p, ["Digital"] + (["Vocal"] if fam == "vocal" else [])


def l_rave(v, rng):
    p = Patch().unison(pick([35.0, 45.0, 28.0], v))
    p.sh["voice_portamento"] = p.sh["t2_voice_portamento"] = 0.12
    p.o1(W_SQR, shape=0.25, pwm=0.4, level=0.7).o2(W_SAW, octave=-1, fine=-8, level=0.5)
    p.lfo1(L_TRI, hz=pick([3.5, 5.0, 2.5], v), depth=0.8, key=0)
    p.fenv(0.0, 0.25, 0.0, 0.2).mod(2, S_ENV1, D_PITCH, -pick([0.08, 0.12, 0.05], v))
    p.filt(pick([3200, 4500, 2500], v), 0.15).amp(0.003, 0.3, 1.0, 0.2, vel=0.15)
    p.chorus(0.3, 0.45, 0.35).drive(0.3, 0.35).verb(0.15, 0.55)
    return p, ["Rave"]


def l_pluck(v, rng):
    p = Patch()
    p.o1(W_SAW, level=0.7).o2(pick([W_SQR, W_SAW], v), octave=pick([1, 0], v), fine=6, level=0.45)
    p.filt(pick([700, 900, 550], v), 0.2).fenv(0.0, pick([0.22, 0.3, 0.18], v), 0.1, 0.25, amt=0.5)
    p.amp(0.0, 0.45, 0.35, 0.3, vel=0.4).spread(0.4)
    p.delay(pick(["3/16", "1/8"], v), 0.38, 0.22).verb(0.18, 0.55)
    return p, ["Pluck"]


# ============================== PAD
def pd_supersaw(v, rng):
    p = Patch().spread(pick([0.6, 0.75, 0.5, 0.85], v))
    p.o1(W_SAW, level=0.65).o2(W_SAW, octave=pick([0, 1, 0, -1], v), fine=pick([9, 6, 12], v), level=0.55)
    p.filt(pick([3500, 5000, 2600, 6500], v), 0.08).amp(pick([0.35, 0.6, 0.2, 0.9], v), 1.0, 1.0,
                                                        pick([1.6, 2.4, 1.2], v), vel=0.15)
    p.wide(pick([11.0, 8.0, 14.0], v), 0.45)
    p.chorus(0.3, 0.4, 0.2).verb(pick([0.3, 0.35, 0.25], v), 0.75, 0.45)
    if v % 3 == 1:
        p.delay("1/4", 0.35, 0.12)
    return p, ["Supersaw", "Wide"]


def pd_warm(v, rng):
    p = Patch().spread(0.35)
    p.o1(W_SAW, level=0.65).o2(pick([W_SQR, W_SAW, W_TRI], v), octave=pick([0, -1], v), fine=7, level=0.55)
    p.filt(pick([900, 1300, 700, 1700], v), pick([0.15, 0.22, 0.1], v))
    p.fenv(pick([0.6, 1.0, 0.4], v), 1.5, 0.6, 1.5, amt=pick([0.15, 0.2, 0.1], v))
    p.amp(pick([0.5, 0.8, 0.3], v), 1.0, 1.0, pick([1.5, 2.0, 1.2], v), vel=0.15)
    p.lfo1(L_TRI, hz=pick([0.12, 0.2, 0.08], v), depth=1.0, key=0).mod(1, S_LFO1, D_CUT, 0.08)
    p.wide(7.0, 0.3)
    p.chorus(0.3, 0.35, 0.18).verb(0.25, 0.7, 0.6)
    if v % 3 == 0:
        p.sat(0.25, 0.45, tube=True)
    return p, ["Warm", "Analog"]


def pd_airy(v, rng):
    p = Patch().spread(0.5)
    p.o1(pick([W_SAW, W_TRI, W_SAW], v), octave=pick([0, 1], v), level=0.9).o2(W_SAW, octave=1, fine=8, level=0.6)
    p.noise(pick([0.12, 0.2, 0.08], v))
    p.filt(pick([260, 180, 340], v), 0.1, kind=F_HP12).amp(pick([0.8, 1.2, 0.5], v), 1.0, 1.0, 2.5, vel=0.1)
    p.lfo1(L_SIN, hz=0.1, depth=1.0, key=0).mod(1, S_LFO1, D_CUT, 0.1)
    p.wide(10.0, 0.5)
    p.chorus(0.35, 0.45, 0.2).delay("1/4", 0.4, 0.15).verb(0.4, 0.85, 0.35).eq(high=2.0)
    return p, ["Airy"]


def pd_evolving(v, rng):
    fam = pick(["analog", "bright", "vocal", "wood", "detune", "reed"], v)
    p = Patch().spread(0.3)
    p.o1(W_DIG, digital=dig(fam, v), level=0.6).o2(W_SAW, octave=pick([0, -1], v), fine=6, level=0.45)
    p.filt(pick([1500, 2200, 1100], v), 0.2).amp(pick([1.0, 1.5, 0.7], v), 1.0, 1.0, 2.5, vel=0.1)
    p.lfo1(pick([L_TRI, L_SIN, L_RND], v), hz=pick([0.08, 0.15, 0.05], v), depth=1.0, key=0)
    p.mod(1, S_LFO1, D_CUT, 0.15).mod(3, S_LFO1, D_O1SHAPE, 0.5)
    p.layer(0.5, osc1_digital=dig(fam, v + 3, 1), amp_pan=0.35, osc1_fine=-7)
    p.t1["amp_pan"] = -0.35
    p.chorus(0.25, 0.4, 0.15).delay("3/8", 0.45, 0.14).verb(0.35, 0.8, 0.45)
    return p, ["Evolving", "Digital"]


def pd_dark(v, rng):
    p = Patch().spread(0.4)
    p.o1(W_SAW, octave=-1, level=0.65).o2(pick([W_SQR, W_SAW], v), octave=-1, fine=-9, level=0.5)
    p.filt(pick([400, 550, 320, 700], v), pick([0.25, 0.18], v))
    p.amp(pick([0.8, 1.2], v), 1.0, 1.0, 2.5, vel=0.1)
    p.lfo1(L_SIN, hz=pick([0.07, 0.11], v), depth=1.0, key=0).mod(1, S_LFO1, D_CUT, 0.1)
    p.layer(0.45, osc1_octave=0, osc2_octave=0, filter_cutoff=cut(pick([900, 1200], v)), amp_pan=0.25)
    p.chorus(0.2, 0.3, 0.15, kind=3).verb(0.35, 0.85, 0.65)
    return p, ["Dark", "Cinematic"]


def pd_glass(v, rng):
    p = Patch().spread(0.25)
    p.o1(W_DIG, digital=dig(pick(["bell", "bright", "metal"], v), v), level=0.55)
    p.o2(W_SIN, octave=1, level=0.35)
    p.filt(pick([5000, 7000, 3800], v), 0.05).amp(pick([0.4, 0.7], v), 1.2, 0.8, 2.2, vel=0.25)
    p.wide(6.0, 0.4)
    p.chorus(0.25, 0.3, 0.2).delay("3/16", 0.4, 0.16).verb(0.35, 0.75, 0.35).eq(high=1.5)
    return p, ["Glass"]


def pd_strings(v, rng):
    p = Patch().spread(0.3)
    p.o1(W_SQR, shape=0.2, pwm=pick([0.5, 0.35, 0.6], v), level=0.65).o2(W_SAW, fine=6, level=0.45)
    p.lfo1(L_TRI, hz=pick([0.6, 0.9, 0.4], v), depth=1.0, key=0)
    p.filt(pick([2400, 3200, 1800], v), 0.05).amp(pick([0.35, 0.5, 0.25], v), 1.0, 1.0, 1.2, vel=0.3)
    p.lfo2(L_SIN, 5.0).sh["voice_vib"] = 0.4
    p.wide(5.0, 0.35).chorus(0.4, 0.45, 0.22).verb(0.3, 0.7, 0.55)
    return p, ["Strings"]


# ============================== PLUCK
def pl_future(v, rng):
    p = Patch().spread(pick([0.6, 0.75, 0.5], v))
    p.o1(W_SAW, level=0.65).o2(W_SAW, octave=pick([0, 1], v), fine=10, level=0.5)
    p.filt(pick([600, 800, 450], v), 0.15).fenv(0.0, pick([0.25, 0.35, 0.2], v), 0.05, 0.3, amt=0.5)
    p.amp(0.0, pick([0.5, 0.7, 0.4], v), 0.0, 0.4, vel=0.35)
    p.wide(9.0, 0.4).chorus(0.25, 0.35, 0.25).delay(pick(["3/16", "1/8"], v), 0.35, 0.2).verb(0.25, 0.65)
    return p, ["Future", "Chord"]


def pl_mallet(v, rng):
    p = Patch()
    p.o1(pick([W_SIN, W_TRI, W_SIN], v), level=0.85).o2(W_SIN, octave=pick([2, 1, 2], v), level=0.0)
    p.t1["fm_amt"] = pick([0.12, 0.2, 0.08], v)
    p.filt(5000, 0.0).amp(0.0, pick([0.4, 0.6, 0.3], v), 0.0, 0.4, vel=0.5)
    p.chorus(0.15, 0.2, 0.2).verb(0.22, 0.55)
    if v % 2:
        p.delay("1/8", 0.25, 0.12)
    return p, ["Mallet"]


def pl_deep(v, rng):
    p = Patch().spread(0.2)
    p.o1(pick([W_SQR, W_SAW], v), shape=0.2, level=0.7).o2(W_SAW, fine=-7, level=0.5)
    p.filt(pick([450, 650, 350], v), pick([0.3, 0.2], v)).fenv(0.0, pick([0.3, 0.45], v), 0.15, 0.3, amt=0.35)
    p.amp(0.0, 0.6, 0.2, 0.35, vel=0.4)
    p.chorus(0.25, 0.3, 0.2).delay("3/16", 0.4, 0.22).verb(0.25, 0.6, 0.6)
    p.sh["song_tempo"] = 122.0
    return p, ["Deep", "House"]


def pl_kalimba(v, rng):
    p = Patch()
    p.o1(W_DIG, digital=dig(pick(["wood", "bell", "wood"], v), v), level=0.75).o2(W_SIN, octave=1, level=0.3)
    p.filt(pick([3500, 5000, 2600], v), 0.05).amp(0.0, pick([0.35, 0.5, 0.25], v), 0.0, 0.35, vel=0.5)
    p.delay("1/8", 0.25, 0.15).verb(0.2, 0.5)
    return p, ["Wood"]


def pl_harp(v, rng):
    p = Patch()
    p.o1(pick([W_SAW, W_TRI, W_SQR], v), level=0.7).o2(W_SAW, fine=5, level=0.3, octave=pick([0, 1], v))
    p.filt(pick([1500, 2200, 1100], v), 0.1).fenv(0.0, pick([0.4, 0.6], v), 0.0, 0.5, amt=0.35)
    p.amp(0.0, pick([0.9, 1.3, 0.7], v), 0.0, 0.8, vel=0.45)
    p.chorus(0.2, 0.25, 0.2).verb(0.3, 0.7)
    return p, ["Harp" if v % 2 else "String"]


# ============================== KEYS
def k_ep(v, rng):
    p = Patch()
    p.o1(W_SIN, level=0.85).o2(W_SIN, octave=pick([1, 2, 1], v), level=0.0)
    p.t1["fm_amt"] = pick([0.18, 0.24, 0.12, 0.3], v)
    p.o2(W_SIN, octave=pick([1, 2, 1], v), level=pick([0.0, 0.1], v))
    p.filt(pick([3500, 2600, 4500], v), 0.0).amp(0.0, pick([1.5, 2.2, 1.1], v), 0.25, 0.4, vel=0.6)
    p.mod(4, S_VEL, D_CUT, 0.2)
    p.chorus(pick([0.3, 0.2, 0.4], v), 0.35, pick([0.3, 0.45], v), kind=pick([1, 3], v // 3))
    p.verb(0.18, 0.5)
    if v % 3 == 2:
        p.sat(0.3, 0.5, tube=True)
    return p, ["EP"]


def k_organ(v, rng):
    p = Patch()
    p.o1(pick([W_SIN, W_SQR, W_SIN], v), level=0.75).o2(W_SIN, octave=1, level=0.55)
    if v % 3 == 1:
        p.o2(W_SIN, octave=1, pitch=7, level=0.4)
    p.filt(pick([4000, 2800, 5000], v), 0.0).amp(0.002, 0.2, 1.0, 0.08, vel=0.1)
    p.chorus(0.35, 0.4, pick([0.55, 0.35], v), kind=pick([1, 3], v))
    if v % 2:
        p.drive(0.25, 0.35)
    p.verb(0.12, 0.45)
    p.sh["song_tempo"] = 122.0
    return p, ["Organ"]


def k_stab(v, rng):
    p = Patch().spread(0.3)
    p.o1(W_SAW, level=0.85).o2(pick([W_SQR, W_SAW], v), fine=6, level=0.65)
    p.filt(pick([1800, 2600, 1300], v), 0.15).fenv(0.0, 0.25, 0.0, 0.15, amt=0.3)
    p.amp(0.0, pick([0.45, 0.6, 0.35], v), pick([0.0, 0.25], v), 0.2, vel=0.4)
    p.chorus(0.25, 0.3, 0.25).delay("1/8", 0.3, 0.15).verb(0.15, 0.5)
    return p, ["Chord", "Stab"]


def k_clav(v, rng):
    p = Patch()
    p.o1(pick([W_SQR, W_DIG], v), shape=0.55, digital=dig("reed", v), level=0.95).o2(W_SAW, octave=1, level=0.4)
    p.filt(pick([2400, 3200], v), 0.3, kind=F_LP12).fenv(0.0, 0.15, 0.1, 0.1, amt=0.35)
    p.amp(0.0, 0.4, 0.3, 0.1, vel=0.6).chorus(0.2, 0.3, 0.3, kind=3)
    return p, ["Clav"]


# ============================== BELL
def bl_fm(v, rng):
    p = Patch()
    p.o1(W_SIN, level=0.8).o2(W_SIN, octave=pick([2, 1, 2, 1], v), level=pick([0.0, 0.0, 0.12], v))
    p.t1["fm_amt"] = pick([0.34, 0.24, 0.42, 0.3, 0.5], v)
    p.filt(8000, 0.0).amp(0.0, pick([2.5, 3.5, 1.8], v), 0.0, 2.0, vel=0.5)
    p.chorus(0.2, 0.25, 0.15).delay("3/16", 0.35, 0.14).verb(0.3, 0.75, 0.35)
    return p, ["FM"]


def bl_digital(v, rng):
    p = Patch()
    p.o1(W_DIG, digital=dig(pick(["bell", "metal", "bell", "bright"], v), v, v % 2), level=0.75)
    p.o2(W_SIN, octave=2, level=0.2)
    p.filt(9000, 0.0).amp(0.0, pick([2.0, 2.8, 1.5], v), 0.0, 1.6, vel=0.45)
    p.wide(5.0, 0.3).delay("1/4", 0.35, 0.14).verb(0.3, 0.7, 0.35)
    return p, ["Digital"]


# ============================== ARP / SEQUENCE
def a_arp(v, rng):
    src = v % 6
    p = Patch()
    if src == 0:        # supersaw trance arp
        p.spread(0.6).o1(W_SAW, level=0.65).o2(W_SAW, fine=10, level=0.5).filt(2600, 0.15)
        p.fenv(0.0, 0.18, 0.2, 0.1, amt=0.35).amp(0.0, 0.25, 0.5, 0.15)
        p.delay("3/16", 0.35, 0.2).verb(0.2, 0.6)
        tempo, tags = 138.0, ["Trance"]
    elif src == 1:      # square bleeps
        p.o1(W_SQR, shape=0.3, level=0.75).filt(4000, 0.05).amp(0.0, 0.15, 0.3, 0.1)
        p.delay("1/8", 0.3, 0.15)
        tempo, tags = 124.0, ["Square"]
    elif src == 2:      # digital sparkle
        p.o1(W_DIG, digital=dig(pick(["bell", "bright"], v), v), level=0.75).o2(W_SIN, octave=1, level=0.25)
        p.filt(7000).amp(0.0, 0.3, 0.0, 0.25, vel=0.3).delay("3/16", 0.4, 0.2).verb(0.25, 0.65)
        tempo, tags = 120.0, ["Digital"]
    elif src == 3:      # resonant pluck
        p.o1(W_SAW, level=0.75).filt(500, 0.45).fenv(0.0, 0.14, 0.0, 0.1, amt=0.45)
        p.amp(0.0, 0.2, 0.2, 0.1).delay("1/8", 0.3, 0.12)
        tempo, tags = 128.0, ["Pluck"]
    elif src == 4:      # soft sine / FM ripples
        p.o1(W_SIN, level=0.85).o2(W_SIN, octave=1, level=0.0)
        p.t1["fm_amt"] = 0.15
        p.amp(0.0, 0.35, 0.0, 0.3, vel=0.3).chorus(0.2, 0.25, 0.2).delay("1/4", 0.4, 0.2).verb(0.3, 0.7)
        tempo, tags = 110.0, ["Soft"]
    else:               # synthwave saw arp
        p.o1(W_SAW, level=0.7).o2(W_SQR, octave=-1, level=0.4).filt(1400, 0.2)
        p.fenv(0.0, 0.2, 0.2, 0.1, amt=0.3).amp(0.0, 0.25, 0.4, 0.12).chorus(0.25, 0.35, 0.2)
        p.delay("3/16", 0.35, 0.15).verb(0.15, 0.55)
        tempo, tags = 100.0, ["Retro"]
    p.arp(rate=pick(["1/16", "1/16", "1/8", "1/16T", "1/16"], v // 6), mode=pick([ARP_UP, ARP_ALT1, ARP_UP, ARP_DOWN, ARP_ALT2, ARP_RND], v),
          gate=pick([0.5, 0.65, 0.4], v), octaves=pick([1, 2, 2, 1, 3], v // 2), tempo=tempo)
    return p, tags


SEQ_STEPS = ["xx.xx.x.", "x.xx.xx.", "x..x..x.", "xxx.xx.x", "x.x.xxx.", "x..xx.x.", "xx.x.xx.", "x.xxx..x"]


def s_seq(v, rng):
    p = Patch()
    if v % 3 == 0:       # bass sequence
        p.mono(t2=False).o1(W_SAW, octave=-1, level=0.8).o2(W_SQR, octave=-1, fine=5, level=0.5)
        p.filt(280, 0.35).fenv(0.0, 0.15, 0.0, 0.1, amt=0.45).amp(0.0, 0.2, 0.3, 0.08)
        tags = ["Bass"]
    elif v % 3 == 1:     # chord pulse
        p.spread(0.4).o1(W_SAW, level=0.7).o2(W_SAW, fine=8, level=0.5).filt(1200, 0.2)
        p.fenv(0.0, 0.15, 0.1, 0.1, amt=0.3).amp(0.0, 0.2, 0.3, 0.1).chorus(0.25, 0.3, 0.2).verb(0.18, 0.6)
        tags = ["Chord"]
    else:                # plucky bleep
        p.o1(W_SQR, shape=0.25, level=0.75).filt(2200, 0.25).fenv(0.0, 0.1, 0.0, 0.1, amt=0.3)
        p.amp(0.0, 0.15, 0.1, 0.1).delay("3/16", 0.35, 0.18)
        tags = ["Bleep"]
    p.arp(rate=pick(["1/16", "1/16", "1/8"], v), mode=ARP_TRIG if v % 3 != 2 else pick([ARP_UP, ARP_ALT1], v),
          gate=pick([0.45, 0.6, 0.35], v), octaves=1, tempo=pick([124.0, 128.0, 122.0, 132.0], v),
          swing=pick([0.0, 0.2, 0.0, 0.35], v), steps=pick(SEQ_STEPS, v))
    return p, tags


# ============================== BRASS / CHOIR
def br_synth(v, rng):
    p = Patch().spread(0.3)
    p.o1(W_SAW, level=0.7).o2(W_SAW, fine=8, level=0.55)
    p.filt(pick([800, 1100, 650], v), 0.12).fenv(pick([0.04, 0.08, 0.02], v), pick([0.5, 0.8], v), 0.45, 0.3,
                                                  amt=pick([0.35, 0.28, 0.42], v))
    p.amp(pick([0.03, 0.06, 0.015], v), 0.5, 0.9, 0.35, vel=0.4).mod(4, S_VEL, D_CUT, 0.15)
    p.chorus(0.25, 0.3, 0.2).verb(0.18, 0.6)
    if v % 3 == 2:
        p.wide(7.0, 0.3)
    return p, ["Synth Brass"]


def ch_vocal(v, rng):
    p = Patch().spread(0.3)
    p.o1(W_DIG, digital=dig("vocal", v, v % 2), level=0.85).o2(W_DIG, digital=dig("vocal", v + 2), fine=7, level=0.6)
    p.filt(pick([3000, 2200, 4000], v), 0.1).amp(pick([0.4, 0.7, 0.25], v), 1.0, 1.0, 1.6, vel=0.2)
    p.lfo2(L_SIN, 4.8).sh["voice_vib"] = 0.4
    p.wide(8.0, 0.4).chorus(0.35, 0.45, 0.2).verb(0.35, 0.8, 0.5)
    return p, ["Vocal"]


# ============================== HIT / SFX
def h_stab(v, rng):
    p = Patch().spread(pick([0.5, 0.3], v))
    kind = v % 3
    if kind == 0:        # rave chord stab: fifth + octave
        p.o1(W_SAW, level=0.65).o2(W_SAW, pitch=7, level=0.5)
        tags = ["Rave"]
    elif kind == 1:      # sus house stab: a fourth or fifth on top
        p.o1(W_SAW, level=0.65).o2(W_SQR, pitch=pick([5, 7], v), level=0.45)
        tags = ["House"]
    else:                # noise-edged hit
        p.o1(W_SAW, level=0.65).o2(W_SAW, octave=-1, level=0.5).noise(0.15)
        tags = ["Impact"]
    p.filt(pick([2200, 3200, 1600], v), 0.2).fenv(0.0, 0.25, 0.0, 0.2, amt=0.35)
    p.amp(0.0, pick([0.35, 0.5, 0.25], v), 0.0, 0.3, vel=0.3)
    p.chorus(0.2, 0.3, 0.2).delay("1/8", 0.3, 0.14).verb(0.25, 0.65)
    if v % 4 == 3:
        p.drive(0.3, 0.35)
    return p, ["Stab"] + tags


def x_sfx(v, rng):
    p = Patch()
    kind = v % 4
    if kind == 0:        # riser: pitch and filter climb while held
        p.o1(W_SAW, level=0.6).noise(0.3).filt(400, 0.3, key=0.0)
        p.fenv(pick([3.0, 4.0], v), 0.5, 1.0, 1.0, amt=0.6).mod(2, S_ENV1, D_PITCH, 0.6)
        p.amp(1.5, 1.0, 1.0, 1.0).verb(0.3, 0.8)
        tags = ["Riser"]
    elif kind == 1:      # zap / laser
        p.o1(W_SQR, shape=0.3, level=0.75).fenv(0.0, pick([0.12, 0.2], v), 0.0, 0.1)
        p.mod(2, S_ENV1, D_PITCH, pick([0.9, 0.7], v)).amp(0.0, 0.3, 0.0, 0.2).delay("1/8", 0.35, 0.2)
        tags = ["Zap"]
    elif kind == 2:      # noise sweep
        p.o1(W_NOI, level=0.0).noise(0.7).filt(800, 0.55, kind=F_BP12, key=0.0)
        p.lfo1(L_TRI, sync=pick(["1/1", "1/2"], v), depth=1.0, key=1).mod(1, S_LFO1, D_CUT, 0.6)
        p.amp(0.3, 1.0, 1.0, 1.0).verb(0.35, 0.8).delay("1/4", 0.4, 0.2)
        tags = ["Sweep"]
    else:                # downlifter
        # ENV1 falls from full to zero: the pitch starts 6 semitones up and sinks
        p.o1(W_SAW, octave=-1, level=0.6).o2(W_SAW, octave=-1, fine=12, level=0.5).filt(5000, 0.2, key=0.0)
        p.fenv(0.0, pick([2.5, 3.5], v), 0.0, 1.0).mod(2, S_ENV1, D_PITCH, 0.5)
        p.amp(0.0, 3.0, 0.0, 1.0).verb(0.3, 0.8)
        tags = ["Downlifter"]
    return p, tags


# ---------------------------------------------------------------- bank plan
# (category, recipe, count, name words) -> 400 presets
ADJ = {
    "Bass": ["Deep", "Heavy", "Rubber", "Midnight", "Concrete", "Velvet", "Pressure", "Basement", "Low", "Thick",
             "Iron", "Smoky", "Tight", "Round", "Warm", "Gritty", "Neon", "Night", "Dusty", "Solid", "Hollow",
             "Steel", "Liquid", "Shadow", "Bold", "Muddy", "Punchy", "Dark", "Brick", "Underground"],
    "Lead": ["Bright", "Silver", "Laser", "Soaring", "Golden", "Crystal", "Electric", "Neon", "Prism", "Comet",
             "Signal", "Vivid", "Wild", "Hyper", "Shiny", "Clear", "Rising", "Polar", "Sonic", "Turbo"],
    "Pad": ["Endless", "Soft", "Distant", "Floating", "Velvet", "Frozen", "Golden", "Hazy", "Slow", "Wide",
            "Quiet", "Ocean", "Northern", "Dusk", "Dawn", "Misty", "Silk", "Calm", "Deep", "Faded"],
    "Pluck": ["Bouncy", "Glassy", "Bright", "Tiny", "Sunny", "Crisp", "Candy", "Soft", "Quick", "Spark",
              "Wooden", "Pastel", "Lucky", "Fresh", "Happy", "Late"],
    "Keys": ["Warm", "Vintage", "Mellow", "Smooth", "Sunday", "Lounge", "Gospel", "Late Night", "Basement",
             "Soulful", "Classic", "Bright", "Dusty", "Dreamy"],
    "Bell": ["Crystal", "Glass", "Temple", "Silver", "Winter", "Bright", "Distant", "Chime", "Starry", "Clear"],
    "Arp": ["Running", "Spinning", "Rolling", "Cascading", "Twinkling", "Racing", "Bubbling", "Climbing",
            "Spiral", "Looping", "Flowing", "Dancing"],
    "Sequence": ["Groove", "Motion", "Pulse", "Machine", "Engine", "Tracker", "Shuffle", "Step"],
    "Brass": ["Bold", "Royal", "Brassy", "Bright", "Big", "Wide", "Fat", "Golden"],
    "Choir": ["Angel", "Ghost", "Distant", "Soft", "Glass", "Cathedral"],
    "Hit": ["Big", "Sharp", "Hard", "Wide", "Short", "Loud", "Club", "Massive", "Dirty"],
    "SFX": ["Rocket", "Space", "Static", "Storm", "Orbit", "Signal", "Vapor", "Falling"],
}

PLAN = [
    # Bass 84
    ("Bass", b_808, 10, ["808", "Boom", "Kick Sub", "Trap Sub"]),
    ("Bass", b_sub, 8, ["Sub", "Sub Line", "Floor"]),
    ("Bass", b_reese, 10, ["Reese", "Reese Line", "Roller"]),
    ("Bass", b_growl, 8, ["Growl", "Snarl", "Beast"]),
    ("Bass", b_wobble, 8, ["Wobble", "Wub", "Wobble Line"]),
    ("Bass", b_pluck, 10, ["Pluck Bass", "Bounce", "Slap"]),
    ("Bass", b_house, 8, ["House Bass", "Organ Bass", "Groove Bass"]),
    ("Bass", b_analog, 10, ["Mono Bass", "Synth Bass", "Drive Bass"]),
    ("Bass", b_acid, 6, ["Acid", "Squelch", "Acid Line"]),
    ("Bass", b_fm, 6, ["FM Bass", "Tine Bass", "Knock Bass"]),
    # Lead 56
    ("Lead", l_supersaw, 10, ["Supersaw", "Anthem Lead", "Saw Lead"]),
    ("Lead", l_square, 6, ["Square Lead", "Chip Lead", "Pulse Lead"]),
    ("Lead", l_sync, 7, ["Sync Lead", "Sync Scream", "Tear Lead"]),
    ("Lead", l_glide, 8, ["Glide Lead", "Portamento", "Slide Lead"]),
    ("Lead", l_soft, 6, ["Flute", "Whistle", "Soft Lead"]),
    ("Lead", l_digital, 7, ["Digital Lead", "Talk Lead", "Formant Lead"]),
    ("Lead", l_rave, 6, ["Rave Lead", "Hover Lead", "Siren"]),
    ("Lead", l_pluck, 6, ["Pluck Lead", "Hook", "Riff Lead"]),
    # Pad 56
    ("Pad", pd_supersaw, 10, ["Supersaw Pad", "Anthem Pad", "Saw Pad"]),
    ("Pad", pd_warm, 10, ["Warm Pad", "Analog Pad", "Blanket"]),
    ("Pad", pd_airy, 8, ["Air Pad", "Breath", "Atmosphere"]),
    ("Pad", pd_evolving, 8, ["Motion Pad", "Drift", "Tides"]),
    ("Pad", pd_dark, 8, ["Dark Pad", "Abyss", "Undertow"]),
    ("Pad", pd_glass, 6, ["Glass Pad", "Crystal Pad", "Shimmer"]),
    ("Pad", pd_strings, 6, ["Strings", "Synth Strings", "Ensemble"]),
    # Pluck 40
    ("Pluck", pl_future, 10, ["Future Pluck", "Chord Pluck", "Drop Pluck"]),
    ("Pluck", pl_mallet, 8, ["Mallet", "Marimba", "Vibes"]),
    ("Pluck", pl_deep, 8, ["Deep Pluck", "House Pluck", "Dub Chord"]),
    ("Pluck", pl_kalimba, 6, ["Kalimba", "Thumb Piano", "Wood Pluck"]),
    ("Pluck", pl_harp, 8, ["Harp", "Synth Guitar", "Koto Pluck"]),
    # Keys 32
    ("Keys", k_ep, 10, ["EP", "Electric Piano", "Tine Keys"]),
    ("Keys", k_organ, 8, ["Organ", "House Organ", "Drawbar"]),
    ("Keys", k_stab, 8, ["Chord Keys", "Stab Keys", "Poly Keys"]),
    ("Keys", k_clav, 6, ["Clav", "Funk Keys", "Pluck Keys"]),
    # Bell 18
    ("Bell", bl_fm, 10, ["Bell", "FM Bell", "Chime"]),
    ("Bell", bl_digital, 8, ["Digital Bell", "Music Box", "Glockenspiel"]),
    # Arp 34, Sequence 18
    ("Arp", a_arp, 34, ["Arp", "Arpeggio", "Run"]),
    ("Sequence", s_seq, 18, ["Sequence", "Loop", "Pattern"]),
    # Brass 16, Choir 12
    ("Brass", br_synth, 16, ["Brass", "Synth Brass", "Horns"]),
    ("Choir", ch_vocal, 12, ["Choir", "Voices", "Aahs"]),
    # Hit 18, SFX 16
    ("Hit", h_stab, 18, ["Stab", "Hit", "Chord Stab"]),
    ("SFX", x_sfx, 16, ["Riser", "Zap", "Sweep", "Downlifter"]),
]


def build():
    presets = [("Init", ["Init"], init_patch().values())]
    used = {"Init"}
    adj_pos = {c: 0 for c in ADJ}
    for cat, fn, count, nouns in PLAN:
        for v in range(count):
            rng = random.Random(f"nanofrog-2026-10-29-{fn.__name__}-{v}")
            p, tags = fn(v, rng)
            # name: an adjective from the category list + one of the recipe's nouns
            for _ in range(400):
                adj = ADJ[cat][adj_pos[cat] % len(ADJ[cat])]
                adj_pos[cat] += 1
                noun = nouns[(v + adj_pos[cat] // len(ADJ[cat])) % len(nouns)]
                name = f"{adj} {noun}"
                if name not in used:
                    break
            assert name not in used, name
            used.add(name)
            presets.append((name, [cat] + tags, p.values()))
    return presets


def init_patch():
    p = Patch()
    p.o1(W_SAW, level=0.8).filt(18000, 0.0).amp(0.002, 0.3, 1.0, 0.25, vel=0.0)
    return p


def main():
    import json
    presets = build()
    assert len(presets) == 401, len(presets)
    levels_file = ROOT / "Tools" / "bank_levels.json"
    levels = {int(k): v for k, v in json.loads(levels_file.read_text()).items()} \
        if levels_file.exists() else {}
    oi = KIDS.index("output_level")
    bank = {"type": "NanoFrogBank", "version": 1, "param_count": len(KIDS),
            "param_ids": KIDS, "presets": []}
    for i, (name, tags, vals) in enumerate(presets):
        if i in levels:
            vals[oi] = round(levels[i], 4)
        bank["presets"].append({"name": name, "tags": tags, "values": vals})
    (ROOT / "FactoryBank").mkdir(exist_ok=True)
    (ROOT / "FactoryBank" / "NanoFrogFactory.json").write_text(json.dumps(bank))
    print(f"wrote {len(presets)} presets", file=sys.stderr)


if __name__ == "__main__":
    main()
