"""2x2 preview of the button-v3 shell + lid. conda run -n img23d python render_preview.py"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np, trimesh
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import button_params as P
from build_body import build_body
from build_bezel import build_bezel


def draw(ax, meshes_colors, elev, azim, title):
    for m, c, a in meshes_colors:
        ax.add_collection3d(Poly3DCollection(m.vertices[m.faces], facecolor=c, edgecolor="none",
                                             alpha=a, linewidths=0))
    allv = np.vstack([m.vertices for m, _, _ in meshes_colors])
    c = allv.mean(axis=0); r = (allv.max(axis=0) - allv.min(axis=0)).max() / 2
    ax.set_xlim(c[0]-r, c[0]+r); ax.set_ylim(c[1]-r, c[1]+r); ax.set_zlim(c[2]+r, c[2]-r)
    ax.view_init(elev=elev, azim=azim)
    ax.set_box_aspect((1, 1, 1)); ax.set_axis_off(); ax.set_title(title, fontsize=9)


body, bezel = build_body(), build_bezel()
B, Z = ("#4a6fa5", 1.0), ("#c0703a", 1.0)

fig = plt.figure(figsize=(11, 9))
views = [
    ([(bezel, *Z)], -75, -90, "bezel — front (window + 4 countersinks)"),
    ([(bezel, *Z)], -35, 60, "bezel — inside (3-sided locating rim)"),
    ([(body, *B)], -35, 60, "body — 4 board posts + corner bosses"),
    ([(body, B[0], 0.4), (bezel, Z[0], 0.95)], -25, -140, "assembled"),
]
for i, (mc, elev, azim, title) in enumerate(views, 1):
    draw(fig.add_subplot(2, 2, i, projection="3d"), mc, elev, azim, title)

fig.suptitle(f"sonos-button-v3  —  {P.OUT_X:.1f} x {P.OUT_Y:.1f} x {P.HEIGHT:.1f} mm", fontsize=11)
fig.tight_layout()
fig.savefig("render_preview.png", dpi=130)
print("  render_preview.png")
