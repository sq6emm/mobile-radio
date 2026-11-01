#!/usr/bin/env python3
"""Hand-laid-out SVG versions of the Markdown mermaid diagrams, for the PDF.

The Markdown keeps mermaid (GitHub renders it); the PDF build cannot rely on
a browser-side library, so the same two diagrams are drawn here with a tiny
box-and-arrow generator. Run directly to write docs/diagrams/*.svg.
"""
import html
import os

FONT = "DejaVu Sans, Liberation Sans, Arial, sans-serif"


class Diagram:
    def __init__(self, width, height):
        self.w, self.h = width, height
        self.boxes = {}
        self.parts = []

    def box(self, key, x, y, w, h, lines, fill="#f4f6fa", shape="rect"):
        self.boxes[key] = (x, y, w, h)
        if shape == "ellipse":
            self.parts.append(f'<ellipse cx="{x + w / 2}" cy="{y + h / 2}" rx="{w / 2}" ry="{h / 2}" fill="{fill}" stroke="#333" stroke-width="1.2"/>')
        else:
            self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="4" fill="{fill}" stroke="#333" stroke-width="1.2"/>')
        n = len(lines)
        for i, line in enumerate(lines):
            size = 11 if i == 0 else 9.5
            weight = "bold" if i == 0 else "normal"
            ty = y + h / 2 + (i - (n - 1) / 2) * 12.5 + 4
            self.parts.append(f'<text x="{x + w / 2}" y="{ty}" font-size="{size}" font-weight="{weight}" text-anchor="middle" font-family="{FONT}">{html.escape(line)}</text>')

    def _port(self, key, side):
        x, y, w, h = self.boxes[key]
        return {"l": (x, y + h / 2), "r": (x + w, y + h / 2), "t": (x + w / 2, y), "b": (x + w / 2, y + h)}[side]

    def edge(self, a, sa, b, sb, label="", both=False, via=None, label_dy=-4, label_seg=0):
        (x1, y1), (x2, y2) = self._port(a, sa), self._port(b, sb)
        pts = [(x1, y1)]
        if via:
            pts += via
        elif sa in "lr" and sb in "lr":
            xm = (x1 + x2) / 2
            pts += [(xm, y1), (xm, y2)]
        elif sa in "tb" and sb in "tb" and abs(x1 - x2) > 1:
            ym = (y1 + y2) / 2
            pts += [(x1, ym), (x2, ym)]
        elif sa in "lr" and sb in "tb":
            pts += [(x2, y1)]
        elif sa in "tb" and sb in "lr":
            pts += [(x1, y2)]
        pts.append((x2, y2))
        d = " ".join(f"{'M' if i == 0 else 'L'}{px},{py}" for i, (px, py) in enumerate(pts))
        markers = 'marker-end="url(#arr)"' + (' marker-start="url(#arrs)"' if both else "")
        self.parts.append(f'<path d="{d}" fill="none" stroke="#444" stroke-width="1.1" {markers}/>')
        if label:
            # label on the first segment, just off the line, so it stays in the gap between boxes
            seg = min(label_seg, len(pts) - 2)
            (ax, ay), (bx, by) = pts[seg], pts[seg + 1]
            lx, ly = (ax + bx) / 2, (ay + by) / 2
            anchor = "middle" if abs(ax - bx) > abs(ay - by) else "start"
            self.parts.append(f'<text x="{lx + (0 if anchor == "middle" else 4)}" y="{ly + label_dy}" font-size="8.5" '
                              f'fill="#222" text-anchor="{anchor}" font-family="{FONT}">{html.escape(label)}</text>')

    def group(self, x, y, w, h, title):
        self.parts.insert(0, f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="#fbfbfb" stroke="#999" stroke-dasharray="5,4"/>'
                          f'<text x="{x + 10}" y="{y + 16}" font-size="11" font-weight="bold" fill="#555" font-family="{FONT}">{html.escape(title)}</text>')

    def svg(self):
        defs = ('<defs><marker id="arr" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">'
                '<path d="M0,0 L8,4 L0,8 z" fill="#444"/></marker>'
                '<marker id="arrs" markerWidth="8" markerHeight="8" refX="1" refY="4" orient="auto">'
                '<path d="M8,0 L0,4 L8,8 z" fill="#444"/></marker></defs>')
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {self.w} {self.h}" width="100%" '
                f'style="max-width:{self.w}px;font-family:{FONT}">{defs}{"".join(self.parts)}</svg>')


def block_diagram():
    d = Diagram(980, 640)
    d.group(10, 10, 210, 70, "Phone")
    d.group(10, 95, 960, 535, "Battery pack")
    d.box("APP", 25, 30, 180, 40, ["Phone app", "PTT, channel, messages, status"], fill="#eef7ee")
    # power column
    d.box("USBC", 25, 120, 180, 48, ["USB-C PHONE port", "VBUS, CC1/2, D+/D-"])
    d.box("PD", 25, 200, 180, 48, ["TPS25751", "PD controller, PHONE"])
    d.box("SRC", 25, 280, 180, 48, ["TPS55288", "5/9/12 V source to PHONE"])
    d.box("CHG", 25, 360, 180, 58, ["BQ25798 charger", "inputs: VAC1 PHONE, VAC2 CHARGE"])
    d.box("USBB", 25, 450, 180, 48, ["USB-C CHARGE port", "VBUS, CC1/2 (power only)"])
    d.box("PDB", 25, 530, 180, 48, ["STUSB4500", "PD sink, CHARGE"])
    # middle column
    d.box("BAT", 285, 360, 220, 58, ["2S Li-ion 7.4 V 4 Ah", "protector, balancer, BQ34Z100 gauge"], fill="#fff6e6")
    d.box("RAILS", 285, 450, 220, 58, ["TPS62130 4.2 V radio rail", "3.3 V LDOs digital / analog"])
    d.box("MCU", 285, 120, 220, 130, ["STM32F405", "USB: MRP / CDC / UAC profiles", "48 kHz ADC/DAC, UART, I2C",
                                     "AFSK, AX.25, APRS, Codec2/FreeDV", "PD policy, interlocks"], fill="#e8eefc")
    d.box("AUDIO", 285, 280, 220, 50, ["Audio front end", "8 kHz filters, speaker amp, MEMS mic"])
    d.box("UI", 285, 540, 220, 40, ["PTT button, mode button, LEDs"])
    # radio column
    d.box("RADIO", 565, 120, 180, 58, ["SA818S-CE module", "430-446 MHz FM, 1 W / 0.5 W"], fill="#fdeeee")
    d.box("MAG", 565, 540, 180, 40, ["Qi2 / MagSafe magnet ring"], fill="#f3f3f3")
    # rf column
    d.box("LPF", 805, 120, 150, 48, ["7-pole LPF", "fc 500 MHz"])
    d.box("BPF", 805, 200, 150, 48, ["2-pole BPF", "439 MHz, 50 MHz wide"])
    d.box("MATCH", 805, 280, 150, 48, ["Match + ESD", "RF test switch"])
    d.box("ANT", 805, 360, 150, 44, ["Helix 439 MHz", "integral, the only antenna"], fill="#e6f2ff", shape="ellipse")
    # edges
    d.edge("APP", "b", "USBC", "t", "USB 2.0 FS, MRP / audio", both=True)
    d.edge("USBC", "b", "PD", "t", "VBUS, CC", both=True)
    d.edge("PD", "b", "SRC", "t", "source path", label_dy=-4)
    d.edge("PD", "r", "CHG", "l", "sink path -> VAC1", via=[(240, 224), (240, 380)], label_dy=-4)
    d.edge("SRC", "r", "RAILS", "l", "VSYS", via=[(255, 304), (255, 470)], label_dy=-4)
    d.edge("CHG", "r", "BAT", "l", both=True)
    d.edge("USBB", "b", "PDB", "t", "CC", both=True)
    d.edge("USBB", "r", "CHG", "r", "VBUS -> VAC2", via=[(270, 474), (270, 395)], label_dy=10)
    d.edge("BAT", "b", "RAILS", "t", "VSYS 6-8.4 V")
    d.edge("USBC", "r", "MCU", "l", "D+/D-")
    d.edge("PD", "r", "MCU", "l", "I2C", both=True, via=[(230, 224), (230, 205)])
    d.edge("RAILS", "t", "MCU", "b", "", via=[(395, 440), (560, 440), (560, 260), (395, 260)])
    d.edge("RAILS", "r", "RADIO", "b", "4.2 V", via=[(655, 479), (655, 190)], label_dy=-4)
    d.edge("MCU", "r", "RADIO", "l", "UART, PTT, PD, H/L, SQ", both=True, label_dy=-6)
    d.edge("MCU", "b", "AUDIO", "t", "ADC / DAC 48 kHz", both=True)
    d.edge("AUDIO", "r", "RADIO", "l", "AF_OUT / MIC_IN", both=True, via=[(540, 305), (540, 165)])
    d.edge("UI", "r", "MCU", "r", "", via=[(545, 560), (545, 215)])
    d.edge("RADIO", "r", "LPF", "l", "ANT")
    d.edge("LPF", "b", "BPF", "t")
    d.edge("BPF", "b", "MATCH", "t")
    d.edge("MATCH", "b", "ANT", "t")
    return d.svg()


def pd_state_diagram():
    d = Diagram(1010, 520)
    d.group(220, 90, 700, 420, "Attached")
    d.box("UN", 30, 30, 150, 44, ["Unattached"], fill="#eee")
    d.box("CHK", 250, 120, 160, 44, ["Check roles"])
    d.box("SINK", 720, 120, 170, 52, ["Sink", "partner not DRP: charger, PC"])
    d.box("PR", 250, 230, 160, 52, ["Request PR_Swap", "we are Sink, partner is a phone"])
    d.box("DR", 500, 230, 160, 52, ["Request DR_Swap", "we are Source and DFP"])
    d.box("GOAL", 720, 230, 170, 52, ["Goal", "Source + UFP"], fill="#e6f7e6")
    d.box("BO", 250, 400, 160, 52, ["Back-off", "retry 30 s, 3 attempts"], fill="#fff2e0")
    d.box("HB", 720, 400, 170, 52, ["Hand back", "SoC < reserve: become Sink,", "draw 100 mA only"], fill="#fff2e0")
    d.edge("UN", "b", "CHK", "l", "CC attach, PD contract", via=[(105, 142)], label_dy=-4)
    d.edge("CHK", "r", "SINK", "l", "partner not DRP")
    d.edge("CHK", "b", "PR", "t", "we are Sink, partner DRP")
    d.edge("CHK", "r", "DR", "t", "Source + DFP", via=[(470, 142), (470, 200), (580, 200)], label_dy=12)
    d.edge("CHK", "r", "GOAL", "t", "already Source + UFP", via=[(455, 142), (455, 210), (805, 210)], label_dy=-4, label_seg=2)
    d.edge("PR", "r", "DR", "l", "accepted, still DFP")
    d.edge("PR", "r", "GOAL", "l", "accepted, already UFP", via=[(455, 256), (455, 310), (700, 310), (700, 256)], label_dy=12)
    d.edge("DR", "r", "GOAL", "l", "accepted")
    d.edge("PR", "b", "BO", "t", "rejected")
    d.edge("DR", "b", "BO", "r", "rejected", via=[(580, 426)])
    d.edge("BO", "l", "PR", "l", "timer", via=[(235, 426), (235, 256)])
    d.edge("GOAL", "b", "HB", "t", "SoC < reserve")
    d.edge("HB", "r", "SINK", "r", "PR_Swap accepted", via=[(905, 426), (905, 146)], label_dy=-4, label_seg=1)
    d.edge("SINK", "b", "PR", "r", "SoC >= reserve + 10 %, partner DRP", via=[(805, 195), (430, 195), (430, 256)], label_dy=-4, label_seg=1)
    d.edge("SINK", "t", "UN", "t", "detach (disable source)", via=[(805, 20), (105, 20)], label_dy=-4, label_seg=1)
    return d.svg()


DIAGRAMS = {"flowchart": block_diagram, "stateDiagram": pd_state_diagram}


def main():
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "diagrams")
    os.makedirs(out, exist_ok=True)
    for name, fn in (("block-diagram", block_diagram), ("pd-roles", pd_state_diagram)):
        with open(os.path.join(out, name + ".svg"), "w") as fh:
            fh.write(fn())
        print("wrote", os.path.join(out, name + ".svg"))


if __name__ == "__main__":
    main()
