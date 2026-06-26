#!/usr/bin/env python3
"""
Generate 1:1 paper validation templates (templates.pdf) to check the model
against the physical display BEFORE 3D printing.

Print at 100% / "Actual size" -- NOT "Fit to page". Then measure the 100 mm
ruler on each page with a real ruler; if it isn't 100 mm, fix the print scale.
"""
import numpy as np, warnings, matplotlib; warnings.filterwarnings('ignore')
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

PW, PH = 215.9, 279.4   # US Letter, mm

# screw pattern as extracted from reference_mount.stl (display's rear holes)
SCREWS = [(-5.05, -0.04), (3.98, 5.45), (3.98, -5.02)]
SCREW_DIA = 4.0
BC = 12.0               # bolt-circle diameter

def new_page(pdf, title):
    fig = plt.figure(figsize=(PW/25.4, PH/25.4))
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, PW); ax.set_ylim(0, PH)
    ax.set_aspect('equal'); ax.axis('off')
    ax.text(PW/2, PH-12, title, ha='center', va='top', fontsize=13, weight='bold')
    return fig, ax

def ruler(ax, x0, y0):
    ax.plot([x0, x0+100], [y0, y0], 'k-', lw=1)
    for mm in range(0, 101, 10):
        h = 4 if mm % 50 == 0 else 2.5
        ax.plot([x0+mm, x0+mm], [y0, y0-h], 'k-', lw=0.8)
        ax.text(x0+mm, y0-5.5, str(mm), ha='center', va='top', fontsize=6)
    ax.text(x0+50, y0+2.5, 'CALIBRATION: this bar must measure 100 mm',
            ha='center', va='bottom', fontsize=7, color='red')

def cross(ax, x, y, r=3.5, **kw):
    ax.plot([x-r, x+r], [y, y], **kw); ax.plot([x, x], [y-r, y+r], **kw)

def page_calibration(pdf):
    fig, ax = new_page(pdf, 'Page 1 — Calibration & Diameter Check')
    ruler(ax, (PW-100)/2, PH-30)
    cx, cy = PW/2, 150
    rings = [(58, 'Ø58  rear body (sits in cup)'), (61, 'Ø61  mount outer'),
             (79, 'Ø79  front bezel (overhangs)')]
    for d, lbl in rings:
        ax.add_patch(plt.Circle((cx, cy), d/2, fill=False, lw=1.0, ls='--'))
        ax.text(cx, cy+d/2+1, lbl, ha='center', va='bottom', fontsize=7)
    cross(ax, cx, cy, 5, color='k', lw=0.6)
    ax.text(PW/2, 70,
            'Hold the display against these circles:\n'
            '• its round body should match Ø58\n'
            '• its front bezel should match Ø79\n'
            'If they differ, tell me the measured sizes and I will rescale.',
            ha='center', va='top', fontsize=9)
    pdf.savefig(fig); plt.close(fig)

def page_screws(pdf):
    fig, ax = new_page(pdf, 'Page 2 — Display Screw-Hole Template')
    ruler(ax, (PW-100)/2, PH-30)
    cx, cy = PW/2, 150
    ax.add_patch(plt.Circle((cx, cy), BC/2, fill=False, lw=0.8, ls='--', color='green'))
    ax.text(cx, cy+BC/2+1, f'Ø{BC:.0f} bolt circle', ha='center', va='bottom', fontsize=7, color='green')
    ax.add_patch(plt.Circle((cx, cy), 58/2, fill=False, lw=0.8, ls=':'))
    cross(ax, cx, cy, 4, color='gray', lw=0.5)
    for (dx, dy) in SCREWS:
        x, y = cx+dx, cy+dy
        ax.add_patch(plt.Circle((x, y), SCREW_DIA/2, fill=False, lw=1.2, color='red'))
        cross(ax, x, y, 5, color='red', lw=0.7)
    ax.text(PW/2, 95,
            'Cut/punch the 3 red holes, center the dotted Ø58 on the display body,\n'
            'and check the holes land on the unit\'s rear screw holes.\n'
            'Misaligned? Photograph the back with a ruler and send it over.',
            ha='center', va='top', fontsize=9)
    pdf.savefig(fig); plt.close(fig)

def page_wallplate(pdf):
    fig, ax = new_page(pdf, 'Page 3 — Wall-Plate Drill Template')
    ruler(ax, (PW-100)/2, PH-30)
    cx, cy = PW/2, 150
    ax.add_patch(plt.Circle((cx, cy), 61/2, fill=False, lw=1.0))              # outline
    ax.add_patch(plt.Circle((cx, cy), 11/2, fill=False, lw=1.0, color='blue'))  # cable hole
    ax.text(cx, cy-11/2-1, 'Ø11 cable', ha='center', va='top', fontsize=7, color='blue')
    for sx in (-18, 18):
        x = cx+sx
        ax.add_patch(plt.Circle((x, cy), 4.4/2, fill=False, lw=1.2, color='red'))
        cross(ax, x, cy, 6, color='red', lw=0.7)
        ax.text(x, cy+6, 'screw', ha='center', va='bottom', fontsize=7, color='red')
    ax.text(PW/2, 95,
            'Tape to the wall, level the two red screw marks, punch/drill them and\n'
            'the center cable hole. (Screws sit inside the bayonet ring.)',
            ha='center', va='top', fontsize=9)
    pdf.savefig(fig); plt.close(fig)

with PdfPages('templates.pdf') as pdf:
    page_calibration(pdf)
    page_screws(pdf)
    page_wallplate(pdf)
print('wrote templates.pdf')
