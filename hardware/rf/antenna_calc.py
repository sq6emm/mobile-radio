#!/usr/bin/env python3
"""Antenna, matching and link-budget calculator for the mobile-radio PMR446 pack.

Sub-commands
  report   (default) print the full design report with project defaults
  helix    size a normal-mode (stubby) helical monopole:   --diameter --length [--k]
  ifa      estimate a meandered inverted-F for a PCB edge: --width --height
  match    L-network from a measured load impedance to 50 ohm: --r --x [--f]
  erp      ERP / EIRP from conducted power, losses and antenna gain
  link     log-distance range table from a link budget

All numbers are first-order starting points for a VNA / simulation session,
not substitutes for measurement. See docs/05-antenna.md.
"""
import argparse
import math

C0 = 299_792_458.0
F0 = 446.1e6
ERP_LIMIT_W = 0.5   # PMR446 (ECC/DEC/(15)05, EN 300 296)


def wavelength(f):
    return C0 / f


# --------------------------------------------------------------------------
# Normal-mode helix (rubber-duck) starting point
# --------------------------------------------------------------------------
def helix(f, diameter_m, axial_len_m, k=0.45):
    """Return dict with turns and pitch for a stubby helical monopole.

    Rule of thumb (Kraus): a normal-mode helix resonates as a quarter-wave
    monopole when the total wire length is roughly k*lambda with k ~ 0.40-0.50
    (self-capacitance of the winding and the dielectric sleeve lower the
    resonance).  Given the axial length we solve pitch and turn count.
    """
    lam = wavelength(f)
    wire = k * lam
    if wire <= axial_len_m:
        raise ValueError("axial length longer than target wire length; shorten the helix")
    circ = math.pi * diameter_m
    pitch = circ / math.sqrt((wire / axial_len_m) ** 2 - 1.0)
    turns = axial_len_m / pitch
    return {"lambda": lam, "wire": wire, "pitch": pitch, "turns": turns}


# --------------------------------------------------------------------------
# Meandered inverted-F estimate for a PCB edge keep-out
# --------------------------------------------------------------------------
def ifa(f, width_m, height_m, shortening=0.90):
    """Estimate the conductor length and meander count for an edge IFA.

    Quarter-wave conductor length scaled by 'shortening' (FR-4 loading and
    proximity to the ground plane typically give 0.85-0.95).  The meander
    count is what fits the width x height keep-out with 1 mm trace/space.
    """
    lam = wavelength(f)
    total = shortening * lam / 4.0
    if total <= width_m:
        return {"lambda": lam, "total": total, "meanders": 0, "pitch": None, "fits": True}
    vertical_runs = math.ceil((total - width_m) / height_m)
    pitch = width_m / max(vertical_runs, 1)
    return {"lambda": lam, "total": total, "meanders": vertical_runs,
            "pitch": pitch, "fits": pitch >= 2.0e-3}


# --------------------------------------------------------------------------
# L-network matching
# --------------------------------------------------------------------------
def l_match(r_l, x_l, f, z0=50.0):
    """All L-network solutions matching (r_l + j x_l) to z0.

    Returns a list of dicts: topology, series element, shunt element (values
    in H or F, with sign conventions resolved into L/C).
    """
    w = 2 * math.pi * f
    sols = []

    def to_part(x):
        if x > 0:
            return ("L", x / w)
        return ("C", -1.0 / (w * x))

    if r_l < z0:
        # series element at the load, shunt element at the source side
        q = math.sqrt(z0 / r_l - 1.0)
        for sign in (+1, -1):
            x_series = sign * q * r_l - x_l          # cancels load reactance and adds +/-Q*R
            x_shunt = -sign * z0 / q
            sols.append({"topology": "series(load)-shunt(source)",
                         "series": to_part(x_series), "shunt": to_part(x_shunt), "q": q})
    else:
        # shunt element at the load, series element at the source side
        g_l = r_l / (r_l ** 2 + x_l ** 2)
        b_l = -x_l / (r_l ** 2 + x_l ** 2)
        r_p = 1.0 / g_l
        q = math.sqrt(r_p / z0 - 1.0)
        for sign in (+1, -1):
            b_shunt = sign * q / r_p - b_l
            x_shunt = -1.0 / b_shunt
            x_series = -sign * q * z0
            sols.append({"topology": "shunt(load)-series(source)",
                         "series": to_part(x_series), "shunt": to_part(x_shunt), "q": q})
    return sols


def fmt_part(p):
    kind, v = p
    return f"{v*1e9:6.2f} nH" if kind == "L" else f"{v*1e12:6.2f} pF"


# --------------------------------------------------------------------------
# ERP and link budget
# --------------------------------------------------------------------------
def dbm(w):
    return 10 * math.log10(w * 1e3)


def erp(p_cond_w, loss_db, gain_dbd):
    erp_dbm = dbm(p_cond_w) - loss_db + gain_dbd
    return erp_dbm, 10 ** (erp_dbm / 10) / 1e3


def link_ranges(eirp_dbm, g_rx_dbi, sens_dbm, margin_db, body_loss_db, f=F0):
    lam = wavelength(f)
    pl_1m = 20 * math.log10(4 * math.pi * 1.0 / lam)
    max_pl = eirp_dbm + g_rx_dbi - sens_dbm - margin_db - body_loss_db
    env = [("free space / line of sight", 2.0), ("open rural, ground reflections", 2.7),
           ("suburban", 3.2), ("dense urban", 3.8), ("indoor, through floors", 4.5)]
    rows = []
    for name, n in env:
        d = 10 ** ((max_pl - pl_1m) / (10 * n))
        rows.append((name, n, d))
    # plane-earth two-ray with both antennas at 1.5 m
    h = 1.5
    d_2ray = 10 ** ((max_pl + 20 * math.log10(h * h)) / 40.0)
    return max_pl, rows, d_2ray


def fmt_dist(d):
    return f"{d/1000:.1f} km" if d >= 1000 else f"{d:.0f} m"


# --------------------------------------------------------------------------
def cmd_report(args):
    lam = wavelength(F0)
    print(f"PMR446 centre {F0/1e6:.1f} MHz: lambda = {lam*1e3:.0f} mm, lambda/4 = {lam*250:.0f} mm, "
          f"lambda/2 = {lam*500:.0f} mm")

    print("\n1. Fixed stubby helical (baseline antenna, integral, in a TPU boot)")
    for d_mm, l_mm in ((6.0, 60.0), (8.0, 50.0), (5.0, 80.0)):
        h = helix(F0, d_mm * 1e-3, l_mm * 1e-3)
        print(f"   D = {d_mm:3.0f} mm, axial = {l_mm:3.0f} mm -> wire ~{h['wire']*1e3:.0f} mm, "
              f"pitch {h['pitch']*1e3:.2f} mm, {h['turns']:.1f} turns")
    print("   Expected gain on the pack: -3 to -6 dBd; feed impedance 15-30 ohm, needs L-match.")

    print("\n2. Internal meandered IFA along the top PCB edge (option, phase 2)")
    for w_mm, h_mm in ((60.0, 10.0), (70.0, 12.0)):
        r = ifa(F0, w_mm * 1e-3, h_mm * 1e-3)
        fit = "ok" if r["fits"] else "too dense - enlarge keep-out"
        print(f"   keep-out {w_mm:.0f} x {h_mm:.0f} mm -> conductor ~{r['total']*1e3:.0f} mm, "
              f"{r['meanders']} vertical runs, pitch {r['pitch']*1e3 if r['pitch'] else 0:.1f} mm ({fit})")
    print("   Expected gain stacked on a phone: -4 to -8 dBd; strongly detuned by the phone body.")

    print("\n3. ERP budget (limit 0.5 W ERP = 27.0 dBm ERP)")
    print(f"   {'P_cond':>8} {'loss':>6} {'G [dBd]':>8} {'ERP [dBm]':>10} {'ERP [W]':>8}  verdict")
    for p in (1.0, 0.5):
        for g in (-2.0, -4.0, -6.0):
            e_dbm, e_w = erp(p, 1.1, g)
            ok = "OK" if e_w <= ERP_LIMIT_W else "OVER LIMIT -> use low power"
            print(f"   {p:>6.1f} W {1.1:>5.1f} dB {g:>8.1f} {e_dbm:>10.1f} {e_w:>8.2f}  {ok}")
    print("   loss = 0.8 dB LPF + 0.3 dB traces/ESD; antenna gain must be measured, firmware then\n"
          "   fixes the module power level so that ERP <= 0.5 W with margin.")

    print("\n4. Range estimate (ERP 0.3 W, RX gain -4 dBi, sensitivity -118 dBm, 10 dB fade, 3 dB body loss/side)")
    eirp = dbm(0.3) + 2.15
    max_pl, rows, d2 = link_ranges(eirp, -4.0, -118.0, 10.0, 6.0)
    print(f"   allowed path loss = {max_pl:.0f} dB")
    for name, n, d in rows:
        print(f"   {name:<32} n = {n:3.1f}  ->  {fmt_dist(d)}")
    print(f"   {'plane-earth two-ray, h = 1.5 m':<32} {'':9}->  {fmt_dist(d2)}")

    print("\n5. Example match: helix measured as 22 - j15 ohm at 446 MHz")
    for s in l_match(22.0, -15.0, F0):
        print(f"   {s['topology']:<28} series {fmt_part(s['series'])}, shunt {fmt_part(s['shunt'])}, Q = {s['q']:.2f}")


def cmd_helix(a):
    h = helix(a.f, a.diameter * 1e-3, a.length * 1e-3, a.k)
    print(f"wire length {h['wire']*1e3:.0f} mm, pitch {h['pitch']*1e3:.2f} mm, turns {h['turns']:.1f}")


def cmd_ifa(a):
    r = ifa(a.f, a.width * 1e-3, a.height * 1e-3, a.shortening)
    print(f"conductor {r['total']*1e3:.0f} mm, {r['meanders']} vertical runs, "
          f"pitch {(r['pitch'] or 0)*1e3:.1f} mm, fits: {r['fits']}")


def cmd_match(a):
    for s in l_match(a.r, a.x, a.f):
        print(f"{s['topology']:<28} series {fmt_part(s['series'])}, shunt {fmt_part(s['shunt'])}, Q = {s['q']:.2f}")


def cmd_erp(a):
    e_dbm, e_w = erp(a.p, a.loss, a.gain)
    print(f"ERP = {e_dbm:.1f} dBm = {e_w:.3f} W  ({'OK' if e_w <= ERP_LIMIT_W else 'OVER 0.5 W LIMIT'})")


def cmd_link(a):
    max_pl, rows, d2 = link_ranges(a.eirp, a.grx, a.sens, a.margin, a.body, a.f)
    print(f"allowed path loss {max_pl:.0f} dB")
    for name, n, d in rows:
        print(f"{name:<32} n = {n:3.1f} -> {fmt_dist(d)}")
    print(f"{'plane-earth two-ray, h = 1.5 m':<32} -> {fmt_dist(d2)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("report")
    p = sub.add_parser("helix"); p.add_argument("--f", type=float, default=F0)
    p.add_argument("--diameter", type=float, required=True, help="mm")
    p.add_argument("--length", type=float, required=True, help="axial length, mm")
    p.add_argument("--k", type=float, default=0.45)
    p = sub.add_parser("ifa"); p.add_argument("--f", type=float, default=F0)
    p.add_argument("--width", type=float, required=True, help="mm")
    p.add_argument("--height", type=float, required=True, help="mm")
    p.add_argument("--shortening", type=float, default=0.90)
    p = sub.add_parser("match"); p.add_argument("--f", type=float, default=F0)
    p.add_argument("--r", type=float, required=True); p.add_argument("--x", type=float, required=True)
    p = sub.add_parser("erp"); p.add_argument("--p", type=float, default=1.0, help="conducted W")
    p.add_argument("--loss", type=float, default=1.1); p.add_argument("--gain", type=float, default=-4.0, help="dBd")
    p = sub.add_parser("link"); p.add_argument("--f", type=float, default=F0)
    p.add_argument("--eirp", type=float, default=dbm(0.3) + 2.15); p.add_argument("--grx", type=float, default=-4.0)
    p.add_argument("--sens", type=float, default=-118.0); p.add_argument("--margin", type=float, default=10.0)
    p.add_argument("--body", type=float, default=6.0)
    a = ap.parse_args()
    {"helix": cmd_helix, "ifa": cmd_ifa, "match": cmd_match, "erp": cmd_erp, "link": cmd_link}.get(a.cmd, cmd_report)(a)


if __name__ == "__main__":
    main()
