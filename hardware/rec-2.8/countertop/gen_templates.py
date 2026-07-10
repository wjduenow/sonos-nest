#!/usr/bin/env python3
"""
1:1 paper validation templates (templates.pdf) — check the model against the REAL
ES3C28P board + the USB-C jack BEFORE 3D printing.

Print at 100% / "Actual size" — NOT "Fit to page". Measure the 100 mm ruler on each
page with a real ruler first; if it isn't 100 mm, fix the print scale and reprint.

Pages:
  1  FRONT  — lay the board SCREEN-UP: mount holes + outline + screen opening
  2  BACK   — flip the board FACE-DOWN (USB edge to the RIGHT): RESET / microSD / USB
             estimated positions to validate
  3  JACK   — hold the JUXINICE female end here: flange + 9x3.5 port + 2 screws @ 17 mm
"""
import warnings, matplotlib; warnings.filterwarnings('ignore'); matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, FancyBboxPatch
from matplotlib.backends.backend_pdf import PdfPages
import stand_params as P

PW, PH = 215.9, 279.4          # US Letter, mm
CX = PW / 2

def new_page(title, sub):
    fig = plt.figure(figsize=(PW / 25.4, PH / 25.4))
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, PW); ax.set_ylim(0, PH)
    ax.set_aspect('equal'); ax.axis('off')
    ax.text(CX, PH - 12, title, ha='center', va='top', fontsize=13, weight='bold')
    ax.text(CX, PH - 19, sub, ha='center', va='top', fontsize=8, color='#444')
    return fig, ax

def ruler(ax, y0):
    x0 = (PW - 100) / 2
    ax.plot([x0, x0 + 100], [y0, y0], 'k-', lw=1)
    for mm in range(0, 101, 10):
        h = 4 if mm % 50 == 0 else 2.5
        ax.plot([x0 + mm, x0 + mm], [y0, y0 - h], 'k-', lw=0.8)
        ax.text(x0 + mm, y0 - 5.5, str(mm), ha='center', va='top', fontsize=6)
    ax.text(CX, y0 + 2.5, 'CALIBRATION: this bar must measure 100 mm', ha='center',
            va='bottom', fontsize=7, color='red')

def cross(ax, x, y, r=3.0, **kw):
    ax.plot([x - r, x + r], [y, y], **kw); ax.plot([x, x], [y - r, y + r], **kw)

def rrect(ax, cx, cy, w, h, r, **kw):
    ax.add_patch(FancyBboxPatch((cx - w / 2 + r, cy - h / 2 + r), w - 2 * r, h - 2 * r,
                                boxstyle=f'round,pad={r}', **kw))

def hole(ax, x, y, d, col='red'):
    ax.add_patch(Circle((x, y), d / 2, fill=False, lw=1.3, color=col)); cross(ax, x, y, d / 2 + 2, color=col, lw=0.7)

def instructions(ax, y, text):
    ax.text(CX, y, text, ha='center', va='top', fontsize=8.5)

# ---- page 1: FRONT (board screen-up) --------------------------------------------
def page_front(pdf):
    fig, ax = new_page('FRONT — board mount + screen check', 'lay your board SCREEN-UP, USB edge to the LEFT')
    ruler(ax, PH - 32); cy = 165
    rrect(ax, CX, cy, P.PCB_W, P.PCB_H, P.PCB_CORNER, fill=False, ec='#159', lw=1.4)   # PCB
    ax.text(CX, cy + P.PCB_H / 2 + 2, 'PCB 86 × 50 (R3.5)', ha='center', fontsize=7, color='#159')
    rrect(ax, CX, cy, P.GLASS_W, P.GLASS_H, 1, fill=False, ec='#2a2', lw=0.8)          # glass
    ax.add_patch(Rectangle((CX - P.VA_W / 2, cy - P.VA_H / 2), P.VA_W, P.VA_H, fill=False, ec='#fa0', lw=1.4, ls='--'))
    ax.text(CX, cy, 'screen\nopening', ha='center', va='center', fontsize=7, color='#c80')
    for sx in (-1, 1):                                                                 # mount holes
        for sy in (-1, 1):
            hole(ax, CX + sx * P.HOLE_DX / 2, cy + sy * P.HOLE_DY / 2, P.HOLE_D)
            ax.add_patch(Circle((CX + sx * P.HOLE_DX / 2, cy + sy * P.HOLE_DY / 2), P.HOLE_KEEPOUT / 2, fill=False, lw=0.4, ls=':', color='#999'))
    for sx in (-1, 1):
        for sy in (-1, 1):
            ax.add_patch(Circle((CX + sx * P.POST_X, cy + sy * P.POST_Y), 1.5, fill=False, lw=0.8, color='#a0f'))
    ax.plot([CX - P.PCB_W / 2, CX - P.PCB_W / 2], [cy - P.USB_SLOT_Y / 2, cy + P.USB_SLOT_Y / 2], color='#c33', lw=3)
    ax.text(CX - P.PCB_W / 2 - 2, cy, 'USB-C\nedge', ha='right', va='center', fontsize=6.5, color='#c33')
    hole(ax, CX + P.MIC_X, cy + P.MIC_Y, P.MIC_CSK_D, col='#0aa')
    ax.text(CX + P.MIC_X + 4, cy + P.MIC_Y, 'mic', ha='left', va='center', fontsize=6.5, color='#088')
    instructions(ax, cy - P.PCB_H / 2 - 12,
        'Align your board outline to the blue rectangle (USB edge left).\n'
        '• the 4 red holes must line up with your board\'s Ø3.2 mount holes\n'
        '• the orange dashed opening should frame your display nicely\n'
        '• purple ○ = bezel screws · dotted ○ = Ø5.6 head keep-out')
    pdf.savefig(fig); plt.close(fig)

# ---- page 2: BACK (board face-down, mirrored) -----------------------------------
def page_back(pdf):
    fig, ax = new_page('BACK — connector position check (ESTIMATED!)', 'flip your board FACE-DOWN, USB edge to the RIGHT')
    ruler(ax, PH - 32); cy = 165
    rrect(ax, CX, cy, P.PCB_W, P.PCB_H, P.PCB_CORNER, fill=False, ec='#888', lw=1.4)
    ax.text(CX, cy + P.PCB_H / 2 + 2, 'board rear', ha='center', fontsize=7, color='#555')
    for sx in (-1, 1):
        for sy in (-1, 1):
            hole(ax, CX + sx * P.HOLE_DX / 2, cy + sy * P.HOLE_DY / 2, P.HOLE_D, col='#bbb')
    # mirror X (face-down): local -X -> page +X
    hole(ax, CX - P.RESET_X, cy + P.RESET_Y, 4.0, col='#c33')                          # RESET button
    ax.text(CX - P.RESET_X, cy + P.RESET_Y - 4, 'RESET', ha='center', va='top', fontsize=6.5, color='#c33')
    # microSD mouth is flush with the -Y long edge -> no case opening, nothing to align
    ax.plot([CX - P.PCB_W / 2 + 8, CX + P.PCB_W / 2 - 8], [cy - P.PCB_H / 2, cy - P.PCB_H / 2], color='#33c', lw=3)
    ax.text(CX, cy - P.PCB_H / 2 + 3, 'microSD mouth (flush with this edge) — no case access',
            ha='center', va='bottom', fontsize=6, color='#33c')
    ax.plot([CX + P.PCB_W / 2, CX + P.PCB_W / 2], [cy - P.USB_SLOT_Y / 2, cy + P.USB_SLOT_Y / 2], color='#c33', lw=3)
    ax.text(CX + P.PCB_W / 2 + 2, cy, 'USB-C / RESET /\nBOOT edge', ha='left', va='center', fontsize=6.5, color='#c33')
    instructions(ax, cy - P.PCB_H / 2 - 12,
        'RESET_Y is measured (7.0 mm centre-to-centre from its corner hole); RESET_X is\n'
        'still a photo estimate. Lay your board face-down on the outline and check that\n'
        'RESET ⊕ lands on the real button. If off: measure it from the nearest corner\n'
        'hole and tell me — RESET_X/Y and MIC_X/Y are one-line params.\n'
        'microSD needs no alignment: its mouth is flush with the bottom edge and the\n'
        'case has no card opening — swap the card with the board out of the case.')
    pdf.savefig(fig); plt.close(fig)

# ---- page 3: USB-C side port -----------------------------------------------------
def page_usb(pdf):
    fig, ax = new_page('USB-C side port — plug fit check', 'hold your USB-C plug against this slot (it is on the -X / left wall)')
    ruler(ax, PH - 32); cy = 170
    rrect(ax, CX, cy, P.USB_SLOT_Y, P.USB_SLOT_Z, 1.5, fill=False, ec='#e07000', lw=1.6)
    ax.text(CX, cy + P.USB_SLOT_Z / 2 + 2,
            f'slot {P.USB_SLOT_Y} (along the board) × {P.USB_SLOT_Z} (into the case)',
            ha='center', fontsize=7, color='#e07000')
    rrect(ax, CX, cy, 8.9, 2.6, 1.3, fill=False, ec='#000', lw=1.0)     # a bare USB-C plug tip
    ax.text(CX, cy - 3.8, 'bare USB-C tip 8.9 × 2.6', ha='center', va='top', fontsize=6.5)
    instructions(ax, cy - 16,
        'The rear panel-mount jack is GONE — a normal USB-C cable now plugs straight\n'
        'into the board through this slot in the left (-X) wall. Hold your actual plug\n'
        'here, overmold and all: it must clear the outer rectangle, not just the tip.\n'
        'A right-angle plug keeps the stand tight to the wall; a straight one sticks\n'
        'out ~18 mm. If it fouls, widen USB_SLOT_Y / USB_SLOT_Z in stand_params.py.')
    pdf.savefig(fig); plt.close(fig)

with PdfPages('templates.pdf') as pdf:
    page_front(pdf)
    page_back(pdf)
    page_usb(pdf)
print('wrote templates.pdf')
