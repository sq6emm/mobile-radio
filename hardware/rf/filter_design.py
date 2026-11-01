#!/usr/bin/env python3
"""RF filter synthesis and simulation for the mobile-radio PMR446 front end.

Designs and simulates (pure Python + numpy, no scipy needed):

  1. TX harmonic low-pass filter (LPF) in the common antenna path:
     7th-order 0.1 dB Chebyshev, shunt-C first, f_c = 500 MHz by default.
  2. Optional 2-pole capacitively-coupled band-pass filter (BPF) centred on
     446.1 MHz, used for receiver selectivity against the phone's cellular
     transmitter (700-900 MHz) and nearby ISM/LPD (433 MHz) / TETRA (380 MHz).

Component models include finite Q and first-order parasitics so the
stop-band numbers are realistic rather than textbook-ideal.

Usage:
    python3 filter_design.py                 # design + simulate with defaults
    python3 filter_design.py --fc 480e6      # different LPF corner
    python3 filter_design.py --bpf-bw 30e6   # narrower BPF
    python3 filter_design.py --out results   # write CSV sweeps + summary.md

All frequencies in Hz, inductances in H, capacitances in F unless noted.
"""
import argparse
import math
import os
import cmath

import numpy as np

Z0 = 50.0

# PMR446 band and the frequencies we care about (Hz)
F_PMR_LOW = 446.00625e6
F_PMR_HIGH = 446.19375e6
F_PMR_MID = 446.1e6
CHECK_FREQS = {
    "PMR446 ch1 (446.006)": F_PMR_LOW,
    "PMR446 ch16 (446.194)": F_PMR_HIGH,
    "TETRA uplink (380)": 380e6,
    "LPD433 / ISM (433.92)": 433.92e6,
    "LTE B28 UL (703)": 703e6,
    "LTE B20 UL (832)": 832e6,
    "2nd harmonic (892.2)": 2 * F_PMR_MID,
    "GSM900 UL (900)": 900e6,
    "3rd harmonic (1338)": 3 * F_PMR_MID,
    "4th harmonic (1784)": 4 * F_PMR_MID,
    "5th harmonic (2230)": 5 * F_PMR_MID,
    "Wi-Fi 2.4 GHz (2450)": 2450e6,
    "6th harmonic (2677)": 6 * F_PMR_MID,
}

E24 = [1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
       3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1]


# --------------------------------------------------------------------------
# Prototype g-values
# --------------------------------------------------------------------------
def chebyshev_g(n, ripple_db):
    """Return (g[1..n], g_load) for an equal-ripple low-pass prototype."""
    beta = math.log(1.0 / math.tanh(ripple_db / 17.37))
    gamma = math.sinh(beta / (2.0 * n))
    a = [math.sin((2 * k - 1) * math.pi / (2 * n)) for k in range(1, n + 1)]
    b = [gamma ** 2 + math.sin(k * math.pi / n) ** 2 for k in range(1, n + 1)]
    g = [2.0 * a[0] / gamma]
    for k in range(2, n + 1):
        g.append(4.0 * a[k - 2] * a[k - 1] / (b[k - 2] * g[k - 2]))
    g_load = 1.0 if n % 2 else 1.0 / math.tanh(beta / 4.0) ** 2
    return g, g_load


def butterworth_g(n):
    return [2.0 * math.sin((2 * k - 1) * math.pi / (2 * n)) for k in range(1, n + 1)], 1.0


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def nearest_e24(value):
    """Nearest E24 value (geometric distance)."""
    if value <= 0:
        return value
    exp = math.floor(math.log10(value))
    mant = value / 10 ** exp
    candidates = [m * 10 ** exp for m in E24] + [10 ** (exp + 1)]
    return min(candidates, key=lambda c: abs(math.log(c / value)))


def fmt_val(v, unit):
    if unit == "H":
        return f"{v * 1e9:6.2f} nH"
    if unit == "F":
        return f"{v * 1e12:6.2f} pF"
    return str(v)


# --------------------------------------------------------------------------
# Component models (impedances), all with losses and first-order parasitics
# --------------------------------------------------------------------------
class Parts:
    """Default parasitic models for 0402/0603 SMD parts at UHF."""
    def __init__(self, q_l=45.0, q_c=300.0, l_cpar=0.10e-12, c_esl=0.40e-9):
        self.q_l = q_l          # inductor Q at ~450 MHz (wirewound 0603 ~ 40-60)
        self.q_c = q_c          # C0G capacitor Q at ~450 MHz
        self.l_cpar = l_cpar    # inductor parasitic parallel capacitance
        self.c_esl = c_esl      # capacitor series inductance (0402 ~ 0.4 nH)

    def z_inductor(self, L, w):
        if L <= 0:
            return complex(0.0, 0.0)
        r_s = w * L / self.q_l
        z_series = complex(r_s, w * L)
        y = 1.0 / z_series + complex(0.0, w * self.l_cpar)
        return 1.0 / y

    def z_capacitor(self, C, w):
        if C <= 0:
            return complex(1e12, 0.0)
        x_c = -1.0 / (w * C)
        r_s = abs(x_c) / self.q_c
        return complex(r_s, x_c + w * self.c_esl)


def abcd_series(z):
    return np.array([[1.0, z], [0.0, 1.0]], dtype=complex)


def abcd_shunt(z):
    return np.array([[1.0, 0.0], [1.0 / z, 1.0]], dtype=complex)


def s_params(abcd, z0=Z0):
    A, B, C, D = abcd[0, 0], abcd[0, 1], abcd[1, 0], abcd[1, 1]
    denom = A + B / z0 + C * z0 + D
    s21 = 2.0 / denom
    s11 = (A + B / z0 - C * z0 - D) / denom
    return s11, s21


def db(x):
    return 20.0 * math.log10(max(abs(x), 1e-30))


# --------------------------------------------------------------------------
# LPF: shunt-C first ladder  C1 - L2 - C3 - L4 - ... - Cn
# --------------------------------------------------------------------------
def design_lpf(n, fc, ripple_db, z0=Z0):
    g, g_load = chebyshev_g(n, ripple_db)
    wc = 2 * math.pi * fc
    elems = []  # list of (name, kind, value)
    for k, gk in enumerate(g, start=1):
        if k % 2 == 1:
            elems.append((f"C{k}", "F", gk / (wc * z0)))
        else:
            elems.append((f"L{k}", "H", gk * z0 / wc))
    return elems, g, g_load


def lpf_abcd(elems, f, parts):
    w = 2 * math.pi * f
    m = np.eye(2, dtype=complex)
    for name, kind, val in elems:
        if kind == "F":
            m = m @ abcd_shunt(parts.z_capacitor(val, w))
        else:
            m = m @ abcd_series(parts.z_inductor(val, w))
    return m


# --------------------------------------------------------------------------
# BPF: n identical shunt L-C resonators, top-C coupled, series-C end coupling
# --------------------------------------------------------------------------
def design_bpf(f0, bw, L, n=2, z0=Z0):
    """Capacitively coupled shunt-resonator band-pass (Butterworth)."""
    g, g_load = butterworth_g(n)
    w0 = 2 * math.pi * f0
    fbw = bw / f0
    c_node = 1.0 / (w0 ** 2 * L)

    # external Q and end coupling capacitors (series C from 50 ohm to node)
    def end_cap(qe):
        rp_needed = qe * w0 * L
        if rp_needed <= z0:
            raise ValueError("Qe*w0*L must exceed Z0: increase L or narrow BW")
        q_s = math.sqrt(rp_needed / z0 - 1.0)
        c_e = 1.0 / (w0 * z0 * q_s)
        c_p = c_e * q_s ** 2 / (1.0 + q_s ** 2)   # parallel equivalent absorbed by node
        return c_e, c_p

    qe_in = g[0] / fbw               # g0 = 1
    qe_out = g[-1] * g_load / fbw
    ce_in, cp_in = end_cap(qe_in)
    ce_out, cp_out = end_cap(qe_out)

    # inter-resonator coupling
    k = [fbw / math.sqrt(g[i] * g[i + 1]) for i in range(n - 1)]
    c_c = [ki * c_node / (1.0 - ki) for ki in k]

    # resonator shunt caps after absorbing couplings
    c_res = []
    for i in range(n):
        c = c_node
        if i == 0:
            c -= cp_in
        if i == n - 1:
            c -= cp_out
        if i > 0:
            c -= c_c[i - 1]
        if i < n - 1:
            c -= c_c[i]
        c_res.append(c)

    return {
        "n": n, "f0": f0, "bw": bw, "L": L, "fbw": fbw, "c_node": c_node,
        "qe_in": qe_in, "qe_out": qe_out,
        "ce_in": ce_in, "ce_out": ce_out, "k": k, "c_c": c_c, "c_res": c_res,
    }


def bpf_abcd(d, f, parts, L_override=None):
    w = 2 * math.pi * f
    L = d["L"] if L_override is None else L_override
    m = abcd_series(parts.z_capacitor(d["ce_in"], w))
    for i in range(d["n"]):
        z_l = parts.z_inductor(L, w)
        z_c = parts.z_capacitor(d["c_res"][i], w)
        z_par = 1.0 / (1.0 / z_l + 1.0 / z_c)
        m = m @ abcd_shunt(z_par)
        if i < d["n"] - 1:
            m = m @ abcd_series(parts.z_capacitor(d["c_c"][i], w))
    m = m @ abcd_series(parts.z_capacitor(d["ce_out"], w))
    return m


def bpf_insertion_loss_estimate(d, q_u):
    """Cohn's estimate: IL ~ 4.343 * (f0/BW) * sum(g_i) / Qu  [dB]."""
    g, _ = butterworth_g(d["n"])
    return 4.343 * (d["f0"] / d["bw"]) * sum(g) / q_u


# --------------------------------------------------------------------------
# Sweep + reporting
# --------------------------------------------------------------------------
def sweep(abcd_fn, freqs):
    rows = []
    for f in freqs:
        s11, s21 = s_params(abcd_fn(f))
        rows.append((f, db(s21), db(s11)))
    return rows


def write_csv(path, rows):
    with open(path, "w") as fh:
        fh.write("freq_hz,s21_db,s11_db\n")
        for f, s21, s11 in rows:
            fh.write(f"{f:.0f},{s21:.3f},{s11:.3f}\n")


FLOOR_DB = -70.0


def table_at(abcd_fn, label):
    out = [f"\n{label}", "-" * len(label),
           f"{'frequency':<28}{'S21 [dB]':>10}{'S11 [dB]':>10}"]
    for name, f in CHECK_FREQS.items():
        s11, s21 = s_params(abcd_fn(f))
        s21_db = db(s21)
        s21_txt = f"{s21_db:.1f}" if s21_db > FLOOR_DB else f"< {FLOOR_DB:.0f}"
        out.append(f"{name:<28}{s21_txt:>10}{db(s11):>10.1f}")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fc", type=float, default=500e6, help="LPF ripple-edge corner [Hz]")
    ap.add_argument("--order", type=int, default=7, help="LPF order (odd, shunt-C first)")
    ap.add_argument("--ripple", type=float, default=0.1, help="LPF passband ripple [dB]")
    ap.add_argument("--bpf-f0", type=float, default=F_PMR_MID)
    ap.add_argument("--bpf-bw", type=float, default=40e6, help="BPF 3 dB bandwidth [Hz]")
    ap.add_argument("--bpf-L", type=float, default=12e-9, help="BPF resonator inductance [H]")
    ap.add_argument("--bpf-poles", type=int, default=2)
    ap.add_argument("--ql", type=float, default=60.0, help="LPF inductor unloaded Q (wirewound ceramic 0603 ~60-80 at 450 MHz)")
    ap.add_argument("--floor", type=float, default=-70.0,
                    help="layout-limited isolation floor [dB]; deeper simulated values are reported as '< floor'")
    ap.add_argument("--bpf-ql", type=float, default=120.0, help="BPF resonator unloaded Q (air-core/coax)")
    ap.add_argument("--out", default=None, help="directory for CSV sweeps and summary.md")
    args = ap.parse_args()

    global FLOOR_DB
    FLOOR_DB = args.floor
    parts = Parts(q_l=args.ql)
    bpf_parts = Parts(q_l=args.bpf_ql, l_cpar=0.05e-12)
    report = []

    # ---------------- LPF ----------------
    elems, g, g_load = design_lpf(args.order, args.fc, args.ripple)
    e24 = [(n, k, nearest_e24(v)) for n, k, v in elems]
    report.append(f"TX harmonic LPF: {args.order}th-order Chebyshev, {args.ripple} dB ripple, "
                  f"f_c = {args.fc/1e6:.0f} MHz, Z0 = {Z0:.0f} ohm")
    report.append("g-values: " + ", ".join(f"{x:.4f}" for x in g) + f" (g_load = {g_load:.4f})")
    report.append(f"{'element':<8}{'ideal':>12}{'E24':>12}")
    for (n, k, v), (_, _, ve) in zip(elems, e24):
        report.append(f"{n:<8}{fmt_val(v, k):>12}{fmt_val(ve, k):>12}")
    report.append(table_at(lambda f: lpf_abcd(elems, f, parts), "LPF (ideal values, lossy parts)"))
    report.append(table_at(lambda f: lpf_abcd(e24, f, parts), "LPF (E24 values, lossy parts)"))

    # ---------------- BPF ----------------
    d = design_bpf(args.bpf_f0, args.bpf_bw, args.bpf_L, n=args.bpf_poles)
    report.append(f"\nRX/TX band-pass (optional): {d['n']}-pole Butterworth, f0 = {d['f0']/1e6:.1f} MHz, "
                  f"BW3dB = {d['bw']/1e6:.0f} MHz, L = {d['L']*1e9:.1f} nH")
    report.append(f"  node C (bare resonator)        : {d['c_node']*1e12:.2f} pF")
    report.append(f"  Qe in/out                      : {d['qe_in']:.1f} / {d['qe_out']:.1f}")
    report.append(f"  end coupling caps Ce           : {d['ce_in']*1e12:.2f} / {d['ce_out']*1e12:.2f} pF")
    report.append("  coupling coefficient(s) k      : " + ", ".join(f"{x:.4f}" for x in d["k"]))
    report.append("  coupling cap(s) Cc             : " + ", ".join(f"{x*1e12:.2f} pF" for x in d["c_c"]))
    report.append("  resonator shunt caps (trimmed) : " + ", ".join(f"{x*1e12:.2f} pF" for x in d["c_res"]))
    for qu in (45.0, 120.0, 300.0):
        report.append(f"  est. insertion loss @ Qu={qu:>4.0f}  : {bpf_insertion_loss_estimate(d, qu):.2f} dB")
    report.append(table_at(lambda f: bpf_abcd(d, f, bpf_parts), f"BPF alone (Qu = {args.bpf_ql:.0f})"))
    report.append(table_at(lambda f: lpf_abcd(e24, f, parts) @ bpf_abcd(d, f, bpf_parts),
                           "LPF(E24) + BPF cascade"))

    # TX circulating current sanity check for the resonator inductors at 1 W
    p_tx = 1.0
    i_line = math.sqrt(p_tx / Z0)
    q_loaded = d["f0"] / d["bw"]
    report.append(f"\nStop-band values below {FLOOR_DB:.0f} dB are shown as a floor: on a real PCB, coupling "
                  f"around the filter (ground return, pad-to-pad, shield gaps) limits isolation to roughly that level.")
    report.append(f"\nTX current: {i_line*1e3:.0f} mA RMS in the 50-ohm line at {p_tx:.0f} W; "
                  f"resonator circulating current ~ Q_L x that = {q_loaded*i_line*1e3:.0f} mA RMS "
                  f"(Q_L = {q_loaded:.1f}). Use air-core or coaxial ceramic resonators, not 0603 chips.")

    text = "\n".join(report)
    print(text)

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        freqs = np.concatenate([np.linspace(100e6, 1000e6, 901), np.linspace(1001e6, 3000e6, 400)])
        write_csv(os.path.join(args.out, "lpf_e24_sweep.csv"),
                  sweep(lambda f: lpf_abcd(e24, f, parts), freqs))
        write_csv(os.path.join(args.out, "bpf_sweep.csv"),
                  sweep(lambda f: bpf_abcd(d, f, bpf_parts), freqs))
        write_csv(os.path.join(args.out, "lpf_bpf_cascade_sweep.csv"),
                  sweep(lambda f: lpf_abcd(e24, f, parts) @ bpf_abcd(d, f, bpf_parts), freqs))
        with open(os.path.join(args.out, "filter_summary.md"), "w") as fh:
            fh.write("# Filter design summary (generated by filter_design.py)\n\n")
            fh.write("Regenerate with `python3 hardware/rf/filter_design.py --out hardware/rf/results`.\n\n")
            fh.write("```\n" + text + "\n```\n")
        print(f"\nwrote CSV sweeps and filter_summary.md to {args.out}/")


if __name__ == "__main__":
    main()
