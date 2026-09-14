#!/usr/bin/env python3
# Copyright (c) 2026 ApertureFox Technology. MIT License.
"""Flipcraft key art renderer.

Draws the promo image the same way the game draws a frame: this is a port of
src/game/render.cpp -- the same 8x8 textures from assets/textures.inc, the same
quad templates and block meshes from src/game/data.cpp, the same perspective
rasterizer, the same fixed-sun shader with its traced per-texel dither. Nothing
here is painted by hand except the scene layout and the logo, so the result is
a real screenshot of a scene that the engine itself could produce.

    python3 tools/keyart.py [-o .catalog/keyart.png] [--scale 4]

Output is 128x64 1-bit upscaled by --scale, in the Flipper catalog palette.
"""

import argparse
import math
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------- constants
# (flipcraft.h)
SCREEN_W, SCREEN_H = 128, 64
LENS = 56
CLIP = 3
BLOCKSIZE = 16
WORLD_SY = 16
PERSP_STEP = 8
SHADOW_MAX_STEPS = 24

SUN = (0.60402, 0.76604, 0.21985)   # 50 deg up, 20 deg off +X

TS_CULLBACK, TS_TRANSPARENT, TS_INVERTED, TS_OVERLAY = 0b1000, 0b0100, 0b0010, 0b0001

# Blocks
AIR, GRASS, DIRT, STONE, COBBLE, LOG, LEAVES, PLANK = 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7
COALORE, IRONORE, SAND, GLASS, SAPLING, TABLE, FURNACE, CHEST = 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF
DYNAMITE = 0x10

TRANSPARENT_BLOCKS = {AIR, LEAVES, SAPLING, GLASS, CHEST}
NOT_FULL_BLOCKS = {AIR, SAPLING, CHEST}

# Textures
(TEX_EMPTY, TEX_COALITEMLIGHT, TEX_GRASSSIDE, TEX_DIRT, TEX_STONE, TEX_COBBLE,
 TEX_LOGTOP, TEX_LOGSIDE, TEX_LEAVES, TEX_PLANK, TEX_COALORE, TEX_IRONORE,
 TEX_GLASS, TEX_SAPLINGLIGHT, TEX_SAPLINGDARK, TEX_TABLESIDE, TEX_TABLETOP,
 TEX_FURNACESIDE, TEX_FURNACETOP, TEX_FURNACEFRONTOFF) = range(0x00, 0x14)
TEX_CHESTSIDE, TEX_CHESTTOP, TEX_CHESTFRONT = 0x15, 0x16, 0x17
TEX_COALITEMDARK, TEX_STICKITEMLIGHT, TEX_STICKITEMDARK = 0x18, 0x19, 0x1A
TEX_APPLEITEMLIGHT, TEX_APPLEITEMDARK, TEX_SHADOW = 0x1B, 0x1C, 0x1D
TEX_CREEPERFRONT, TEX_CREEPERSIDE, TEX_CREEPERTOP = 0x96, 0x97, 0x98

# Quads
(QUAD_FULL_NEGX, QUAD_FULL_POSX, QUAD_FULL_NEGZ, QUAD_FULL_POSZ, QUAD_FULL_NEGY,
 QUAD_FULL_POSY, QUAD_CROSS1, QUAD_CROSS2, QUAD_SMALL_NEGX, QUAD_SMALL_POSX,
 QUAD_SMALL_NEGZ, QUAD_SMALL_POSZ, QUAD_SMALL_NEGY, QUAD_SMALL_POSY,
 QUAD_ITEMSHADOW, QUAD_BLOCKITEM_NEGX, QUAD_BLOCKITEM_POSX, QUAD_BLOCKITEM_NEGZ,
 QUAD_BLOCKITEM_POSZ, QUAD_BLOCKITEM_NEGY, QUAD_BLOCKITEM_POSY, QUAD_CROSSITEM1,
 QUAD_CROSSITEM2, QUAD_CROSSITEM3, QUAD_CROSSITEM4, QUAD_BEDROCK) = range(26)
QUAD_COUNT = 26

# src/game/data.cpp QUADS[]
QUADS = [
    [[0,0,16],[0,16,16],[0,16,0],[0,0,0]],
    [[16,0,0],[16,16,0],[16,16,16],[16,0,16]],
    [[0,0,0],[0,16,0],[16,16,0],[16,0,0]],
    [[16,0,16],[16,16,16],[0,16,16],[0,0,16]],
    [[0,0,16],[0,0,0],[16,0,0],[16,0,16]],
    [[0,16,0],[0,16,16],[16,16,16],[16,16,0]],
    [[2,0,2],[2,16,2],[14,16,14],[14,0,14]],
    [[2,0,14],[2,16,14],[14,16,2],[14,0,2]],
    [[1,0,15],[1,14,15],[1,14,1],[1,0,1]],
    [[15,0,1],[15,14,1],[15,14,15],[15,0,15]],
    [[1,0,1],[1,14,1],[15,14,1],[15,0,1]],
    [[15,0,15],[15,14,15],[1,14,15],[1,0,15]],
    [[1,0,15],[1,0,1],[15,0,1],[15,0,15]],
    [[1,14,1],[1,14,15],[15,14,15],[15,14,1]],
    [[0,0,0],[0,0,8],[8,0,8],[8,0,0]],
    [[1,1,7],[1,7,7],[1,7,1],[1,1,1]],
    [[7,1,1],[7,7,1],[7,7,7],[7,1,7]],
    [[1,1,1],[1,7,1],[7,7,1],[7,1,1]],
    [[7,1,7],[7,7,7],[1,7,7],[1,1,7]],
    [[1,1,7],[1,1,1],[7,1,1],[7,1,7]],
    [[1,7,1],[1,7,7],[7,7,7],[7,7,1]],
    [[2,1,2],[2,6,2],[6,6,6],[6,1,6]],
    [[2,1,6],[2,6,6],[6,6,2],[6,1,2]],
    [[6,1,2],[6,6,2],[2,6,6],[2,1,6]],
    [[6,1,6],[6,6,6],[2,6,2],[2,1,2]],
    [[0,0,0],[0,0,16],[16,0,16],[16,0,0]],
]
QUAD_UVS = [(0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0)]

# tex[6] corner picks: bit0 x1, bit1 y1, bit2 z1 (render.cpp kCorner)
CORNER = [
    (4,6,2,0), (1,3,7,5), (0,2,3,1), (5,7,6,4), (4,0,1,5), (2,6,7,3),
]

# ------------------------------------------------------------------ meshes
# src/game/data.cpp initMesh(), only the blocks this scene uses.
def _cube(top, tops, bot, bots, side, sides, front=None, fronts=0):
    tx = [(top, tops), (bot, bots), (side, sides)]
    if front is not None:
        tx.append((front, fronts))
    return {"tex": tx, "quads": []}

MESH = {
    GRASS:   {"tex": [(TEX_EMPTY, 0b1010), (TEX_DIRT, 0b1000), (TEX_GRASSSIDE, 0b1000)], "quads": []},
    DIRT:    _cube(TEX_DIRT, 0b1000, TEX_DIRT, 0b1000, TEX_DIRT, 0b1000, TEX_DIRT, 0b1000),
    STONE:   _cube(TEX_STONE, 0b1000, TEX_STONE, 0b1000, TEX_STONE, 0b1000),
    COBBLE:  _cube(TEX_COBBLE, 0b1000, TEX_COBBLE, 0b1000, TEX_COBBLE, 0b1000, TEX_COBBLE, 0b1000),
    LOG:     _cube(TEX_LOGTOP, 0b1000, TEX_LOGTOP, 0b1000, TEX_LOGSIDE, 0b1000, TEX_LOGSIDE, 0b1000),
    LEAVES:  _cube(TEX_LEAVES, 0b1000, TEX_LEAVES, 0b1000, TEX_LEAVES, 0b1000, TEX_LEAVES, 0b1000),
    PLANK:   _cube(TEX_PLANK, 0b1000, TEX_PLANK, 0b1000, TEX_PLANK, 0b1000, TEX_PLANK, 0b1000),
    COALORE: _cube(TEX_COALORE, 0b1000, TEX_COALORE, 0b1000, TEX_COALORE, 0b1000),
    IRONORE: _cube(TEX_IRONORE, 0b1000, TEX_IRONORE, 0b1000, TEX_IRONORE, 0b1000, TEX_IRONORE, 0b1000),
    SAND:    _cube(TEX_DIRT, 0b1010, TEX_DIRT, 0b1010, TEX_DIRT, 0b1010, TEX_DIRT, 0b1010),
    GLASS:   _cube(TEX_GLASS, 0b1100, TEX_GLASS, 0b1100, TEX_GLASS, 0b1100),
    TABLE:   _cube(TEX_TABLETOP, 0b1000, TEX_PLANK, 0b1000, TEX_TABLESIDE, 0b1000, TEX_TABLESIDE, 0b1000),
    FURNACE: _cube(TEX_FURNACETOP, 0b1000, TEX_FURNACETOP, 0b1000, TEX_FURNACESIDE, 0b1000,
                   TEX_FURNACEFRONTOFF, 0b1000),
    SAPLING: {"tex": [(TEX_SAPLINGLIGHT, 0b0100), (TEX_SAPLINGDARK, 0b0110)],
              "quads": [(QUAD_CROSS1, 0), (QUAD_CROSS1, 1), (QUAD_CROSS2, 0), (QUAD_CROSS2, 1)]},
    CHEST:   {"tex": [(TEX_CHESTTOP, 0b1000), (TEX_CHESTTOP, 0b1000),
                      (TEX_CHESTSIDE, 0b1000), (TEX_CHESTFRONT, 0b1000)],
              "quads": [(QUAD_SMALL_NEGX, 2), (QUAD_SMALL_POSX, 2), (QUAD_SMALL_NEGZ, 2),
                        (QUAD_SMALL_POSZ, 2), (QUAD_SMALL_NEGY, 0), (QUAD_SMALL_POSY, 0)]},
}

# src/game/data.cpp CREEPER_BOXES: ox, oy, oz, sx, sy, sz, flags
CREEPER_BOXES = [
    (-4, 0,  2, 8, 5, 4, 0),
    (-4, 0, -6, 8, 5, 4, 0),
    (-3, 5, -3, 6, 12, 6, 0),
    (-4, 17, -4, 8, 9, 8, 1),
]
CREEPER_TEX = (TEX_CREEPERFRONT, TEX_CREEPERSIDE, TEX_CREEPERTOP)

# src/game/gui.cpp kToolShape / kToolFill -- MSB is the left column, row 0 the
# top one. The rasterizer wants bit u of byte v with v == 0 at the bottom, so
# these are re-packed into spare atlas slots at load time.
TOOL_SHAPE = {
    "pickaxe": (0x78, 0x86, 0x72, 0x19, 0x2D, 0x55, 0xA5, 0xC2),
    "axe":     (0xF8, 0x88, 0xF8, 0x18, 0x18, 0x18, 0x18, 0x18),
    "shovel":  (0x3C, 0x24, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18),
    "sword":   (0x03, 0x07, 0x0E, 0x1C, 0xB8, 0x70, 0xD0, 0x00),
}
TOOL_FILL = {
    "pickaxe": (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    "axe":     (0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    "shovel":  (0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    "sword":   (0x00, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00),
}
TOOL_SLOT = {"pickaxe": 0xA0, "axe": 0xA1, "shovel": 0xA2, "sword": 0xA3}


def load_textures(path):
    """assets/textures.inc -> [256][8] rows, bit u of byte v is texel (u, v)."""
    with open(path, "r", encoding="utf-8") as fh:
        src = fh.read()
    tex = [[0] * 8 for _ in range(256)]
    for idx, body in re.findall(r"/\*\s*(\d+)\s*\*/\s*\{([^}]*)\}", src):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
        if len(vals) == 8:
            tex[int(idx)] = vals
    return tex


def bake_tools(tex, tier=2):
    """Register the four tool icons as textures. tier: 0 wood, 1 stone, 2 iron."""
    for name, shape in TOOL_SHAPE.items():
        fill = TOOL_FILL[name]
        rows = []
        for r in range(8):
            bits = shape[r]
            if tier == 2:
                bits |= fill[r]
            elif tier == 1:
                bits |= fill[r] & (0x55 if (r & 1) else 0xAA)
            # MSB-left, top-down  ->  LSB-left, bottom-up
            flipped = 0
            for c in range(8):
                if (bits >> (7 - c)) & 1:
                    flipped |= 1 << c
            rows.append(flipped)
        tex[TOOL_SLOT[name]] = rows[::-1]


# ------------------------------------------------------------------- world
class Scene:
    """Sparse voxel grid with the same lookup contract as flipcraft::World."""

    def __init__(self):
        self.cells = {}

    def set(self, x, y, z, block):
        if block == AIR:
            self.cells.pop((x, y, z), None)
        else:
            self.cells[(x, y, z)] = block

    def get(self, x, y, z):
        return self.cells.get((x, y, z), AIR)

    def fill(self, x0, y0, z0, x1, y1, z1, block):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.set(x, y, z, block)

    def column(self, x, z, top, surface, sub=DIRT, depth=3, floor=STONE):
        """Surface block at `top`, `depth` layers of `sub` under it, `floor` below."""
        self.set(x, top, z, surface)
        for y in range(top - depth, top):
            if y >= 0:
                self.set(x, y, z, sub)
        for y in range(0, max(0, top - depth)):
            self.set(x, y, z, floor)


# ---------------------------------------------------------------- renderer
class Renderer:
    def __init__(self, tex, shaders=True):
        self.tex = tex
        self.shaders = shaders
        self.zbuf = [bytearray(SCREEN_W) for _ in range(SCREEN_H)]
        self.cam = [0.0, 0.0, 0.0]
        self.m = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
        self.texture = 0
        self.cull = True
        self.transparent = False
        self.inverted = False
        self.overlay = False
        self.lit_level = 0
        self.lit_mask = None
        self.quad_normal = []
        self.quad_lit = []
        for q in range(QUAD_COUNT):
            t = QUADS[q]
            e0 = [t[1][k] - t[0][k] for k in range(3)]
            e1 = [t[2][k] - t[1][k] for k in range(3)]
            n = [e0[1]*e1[2] - e0[2]*e1[1],
                 e0[2]*e1[0] - e0[0]*e1[2],
                 e0[0]*e1[1] - e0[1]*e1[0]]
            ln = math.sqrt(sum(c * c for c in n)) or 1.0
            n = [c / ln for c in n]
            self.quad_normal.append(n)
            dot = sum(n[k] * SUN[k] for k in range(3))
            self.quad_lit.append(2 if dot >= 0.65 else (1 if dot > 0.05 else 0))

    # -- camera ------------------------------------------------------------
    def set_cam_rot(self, pitch_deg, yaw_deg):
        sc, cc = math.sin(math.radians(yaw_deg)), math.cos(math.radians(yaw_deg))
        sb, cb = math.sin(math.radians(pitch_deg)), math.cos(math.radians(pitch_deg))
        self.m = [[cc, 0.0, sc],
                  [sb * sc, cb, -sb * cc],
                  [-cb * sc, sb, cb * cc]]

    def world_to_cam(self, v):
        m = self.m
        ox, oy, oz = v[0] - self.cam[0], v[1] - self.cam[1], v[2] - self.cam[2]
        return (m[0][0]*ox + m[0][2]*oz,
                m[1][0]*ox + m[1][1]*oy + m[1][2]*oz,
                m[2][0]*ox + m[2][1]*oy + m[2][2]*oz,
                v[3], v[4])

    @staticmethod
    def cam_to_screen(v):
        inv_z = 1.0 / v[2]
        persp = LENS * inv_z
        x = min(255.0, max(-255.0, v[0] * persp + SCREEN_W / 2))
        y = min(255.0, max(-255.0, (SCREEN_H - 1 - SCREEN_H / 2) - v[1] * persp))
        return (x, y, inv_z, v[3] * inv_z, v[4] * inv_z)

    # -- rasterizer --------------------------------------------------------
    def raster_tri(self, A, B, C):
        area = (B[0]-A[0]) * (C[1]-A[1]) - (B[1]-A[1]) * (C[0]-A[0])
        if abs(area) < 1e-9:
            return
        min_y = max(math.floor(min(A[1], B[1], C[1])), 0)
        max_y = min(math.ceil(max(A[1], B[1], C[1])), SCREEN_H - 1)
        min_x = max(math.floor(min(A[0], B[0], C[0])), 0)
        max_x = min(math.ceil(max(A[0], B[0], C[0])), SCREEN_W - 1)
        if min_x > max_x or min_y > max_y:
            return

        trow = self.tex[self.texture]
        skip_zero = self.transparent
        inv_mask = 1 if self.inverted else 0
        use_overlay = self.overlay
        lit = 0 if self.transparent else self.lit_level
        lmask = self.lit_mask

        e0dx, e0dy = B[1]-C[1], C[0]-B[0]
        e0c = B[0]*C[1] - B[1]*C[0]
        e1dx, e1dy = C[1]-A[1], A[0]-C[0]
        e1c = C[0]*A[1] - C[1]*A[0]

        ia = 1.0 / area
        zA, zB, zC = A[2]*ia, B[2]*ia, C[2]*ia
        uA, uB, uC = A[3]*ia, B[3]*ia, C[3]*ia
        vA, vB, vC = A[4]*ia, B[4]*ia, C[4]*ia
        pos_area = area > 0.0

        de2dx = -(e0dx + e1dx)
        d_inv_z = e0dx*zA + e1dx*zB + de2dx*zC
        dS = e0dx*uA + e1dx*uB + de2dx*uC
        dT = e0dx*vA + e1dx*vB + de2dx*vC
        d_depth = 512.0 * d_inv_z

        for y in range(min_y, max_y + 1):
            py, px0 = y + 0.5, min_x + 0.5
            e0 = e0c + e0dx*px0 + e0dy*py
            e1 = e1c + e1dx*px0 + e1dy*py
            e2 = area - e0 - e1
            inv_z = e0*zA + e1*zB + e2*zC
            S = e0*uA + e1*uB + e2*uC
            T = e0*vA + e1*vB + e2*vC
            depth_acc = 512.0 * inv_z
            row = self.zbuf[y]
            was_in = False
            sub = 0
            fu = fv = dfu = dfv = 0.0

            for x in range(min_x, max_x + 1):
                outside = (e0 < 0 or e1 < 0 or e2 < 0) if pos_area else (e0 > 0 or e1 > 0 or e2 > 0)
                if outside or inv_z <= 0:
                    if was_in:
                        break
                    sub = 0
                    e0 += e0dx; e1 += e1dx; e2 += de2dx
                    inv_z += d_inv_z; S += dS; T += dT; depth_acc += d_depth
                    continue
                was_in = True

                depth = int(depth_acc)
                if depth > 127:
                    depth = 127
                if (row[x] >> 1) > depth:
                    if sub > 0:
                        fu += dfu; fv += dfv; sub -= 1
                    e0 += e0dx; e1 += e1dx; e2 += de2dx
                    inv_z += d_inv_z; S += dS; T += dT; depth_acc += d_depth
                    continue

                if sub == 0:
                    rz = 1.0 / inv_z
                    fu = 8.0 * S * rz
                    fv = 8.0 * T * rz
                    dfu = 8.0 * rz * (dS - (S * rz) * d_inv_z)
                    dfv = 8.0 * rz * (dT - (T * rz) * d_inv_z)
                    sub = PERSP_STEP

                a = min(7, max(0, int(fu)))
                b = min(7, max(0, int(fv)))
                color = (trow[b] >> a) & 1
                fu += dfu; fv += dfv; sub -= 1

                if not (skip_zero and color == 0):
                    color ^= inv_mask
                    if lit and (lmask is None or ((lmask[b] >> a) & 1)):
                        color &= ((x ^ y) & 1) if lit == 2 else ((x | y) & 1)
                    if use_overlay:
                        color ^= row[x] & 1
                    row[x] = (depth << 1) | color

                e0 += e0dx; e1 += e1dx; e2 += de2dx
                inv_z += d_inv_z; S += dS; T += dT; depth_acc += d_depth

    @staticmethod
    def clip_near(poly):
        out = []
        n = len(poly)
        for i in range(n):
            cur, nxt = poly[i], poly[(i + 1) % n]
            cur_in, nxt_in = cur[2] >= CLIP, nxt[2] >= CLIP
            if cur_in:
                out.append(cur)
            if cur_in != nxt_in:
                t = (CLIP - cur[2]) / (nxt[2] - cur[2])
                out.append((cur[0] + t*(nxt[0]-cur[0]),
                            cur[1] + t*(nxt[1]-cur[1]),
                            float(CLIP),
                            cur[3] + t*(nxt[3]-cur[3]),
                            cur[4] + t*(nxt[4]-cur[4])))
        return out

    @staticmethod
    def backfacing(v1, v2, v3):
        return ((v3[0]-v1[0]) * (v1[1]-v2[1]) - (v1[1]-v3[1]) * (v2[0]-v1[0])) < 0.0

    def draw_quad_cam(self, quad):
        if all(v[2] < CLIP for v in quad):
            return
        clipped = self.clip_near(quad)
        if len(clipped) < 3:
            return
        scr = [self.cam_to_screen(v) for v in clipped]
        if self.backfacing(scr[0], scr[1], scr[2]):
            if self.cull:
                return
            scr.reverse()
        for i in range(1, len(scr) - 1):
            self.raster_tri(scr[0], scr[i], scr[i + 1])

    def apply_settings(self, s):
        self.cull = bool(s & TS_CULLBACK)
        self.transparent = bool(s & TS_TRANSPARENT)
        self.inverted = bool(s & TS_INVERTED)
        self.overlay = bool(s & TS_OVERLAY)

    def draw_quad_world(self, corners, tex_id, settings, lit=0, mask=None):
        """corners: 4 (x, y, z, u, v) in world sub-pixels."""
        self.texture = tex_id
        self.apply_settings(settings)
        self.lit_level = lit
        self.lit_mask = mask
        self.draw_quad_cam([self.world_to_cam(c) for c in corners])

    def draw_block_quad(self, x, y, z, quad_id, tex_id, settings, lit=0, mask=None):
        t = QUADS[quad_id]
        bx, by, bz = x << 4, y << 4, z << 4
        corners = [(bx + t[i][0], by + t[i][1], bz + t[i][2], QUAD_UVS[i][0], QUAD_UVS[i][1])
                   for i in range(4)]
        self.draw_quad_world(corners, tex_id, settings, lit, mask)

    def render_quad(self, x, y, z, quad_id, tex_id, settings):
        """Float block position; used for item entities and ground sprites."""
        t = QUADS[quad_id]
        corners = [(x*16.0 + t[i][0], y*16.0 + t[i][1], z*16.0 + t[i][2],
                    QUAD_UVS[i][0], QUAD_UVS[i][1]) for i in range(4)]
        self.draw_quad_world(corners, tex_id, settings, 0, None)

    # -- shaders -----------------------------------------------------------
    def face_tex(self, block_id, slot):
        mesh = MESH.get(block_id)
        if mesh is None or slot >= len(mesh["tex"]):
            return None
        return mesh["tex"][slot]

    def sun_visible(self, scene, px, py, pz, own):
        """One shadow ray towards the sun. Returns (lit, crossed_a_texture)."""
        via_tex = False
        vx, vy, vz = math.floor(px), math.floor(py), math.floor(pz)
        if (vx, vy, vz) != own:
            id0 = scene.get(vx, vy, vz)
            if id0 != AIR:
                ft = self.face_tex(id0, 1)
                if ft is None or not (ft[1] & TS_TRANSPARENT):
                    return False, via_tex
                via_tex = True
                tu = min(7, max(0, int((px - vx) * 8.0)))
                tv = min(7, max(0, int((pz - vz) * 8.0)))
                ink = (self.tex[ft[0]][tv] >> tu) & 1
                if ft[1] & TS_INVERTED:
                    ink ^= 1
                if ink:
                    return False, via_tex

        FAR = 1e9
        sx = 1 if SUN[0] > 0 else -1
        sz = 1 if SUN[2] > 0 else -1
        tdx = abs(1.0 / SUN[0]) if SUN[0] else FAR
        tdy = abs(1.0 / SUN[1])
        tdz = abs(1.0 / SUN[2]) if SUN[2] else FAR
        tmx = ((vx + 1 - px) if SUN[0] > 0 else (px - vx)) * tdx if SUN[0] else FAR
        tmy = (vy + 1 - py) * tdy
        tmz = ((vz + 1 - pz) if SUN[2] > 0 else (pz - vz)) * tdz if SUN[2] else FAR

        for _ in range(SHADOW_MAX_STEPS):
            if tmx <= tmy and tmx <= tmz:
                t = tmx; tmx += tdx; vx += sx; axis = 0
            elif tmy <= tmz:
                t = tmy; tmy += tdy; vy += 1; axis = 1
            else:
                t = tmz; tmz += tdz; vz += sz; axis = 2
            if vy >= WORLD_SY:
                return True, via_tex
            bid = scene.get(vx, vy, vz)
            if bid == AIR:
                continue
            ft = self.face_tex(bid, 1 if axis == 1 else 2)
            if ft is None or not (ft[1] & TS_TRANSPARENT):
                return False, via_tex
            via_tex = True
            hx, hy, hz = px + SUN[0]*t, py + SUN[1]*t, pz + SUN[2]*t
            if axis == 0:
                fu, fv = hz - math.floor(hz), hy - math.floor(hy)
            elif axis == 1:
                fu, fv = hx - math.floor(hx), hz - math.floor(hz)
            else:
                fu, fv = hx - math.floor(hx), hy - math.floor(hy)
            tu = min(7, max(0, int(fu * 8.0)))
            tv = min(7, max(0, int(fv * 8.0)))
            ink = (self.tex[ft[0]][tv] >> tu) & 1
            if ft[1] & TS_INVERTED:
                ink ^= 1
            if ink:
                return False, via_tex
        return True, via_tex

    def bake_face(self, scene, gx, y, gz, quad):
        """0 lit, 1 shadowed, (2, mask) mixed."""
        if not self.quad_lit[quad]:
            return 1, None
        t = QUADS[quad]
        n = self.quad_normal[quad]
        base, du, dv = [0.0]*3, [0.0]*3, [0.0]*3
        for k in range(3):
            org = float(gx if k == 0 else (y if k == 1 else gz))
            base[k] = org + t[0][k] / 16.0 + n[k] * 0.03
            du[k] = (t[3][k] - t[0][k]) / 16.0
            dv[k] = (t[1][k] - t[0][k]) / 16.0
        own = (gx, y, gz)

        def ray(u, v):
            fu, fv = (u + 0.5) / 8.0, (v + 0.5) / 8.0
            return self.sun_visible(scene,
                                    base[0] + du[0]*fu + dv[0]*fv,
                                    base[1] + du[1]*fu + dv[1]*fv,
                                    base[2] + du[2]*fu + dv[2]*fv, own)

        via_tex = False
        lit_probes = 0
        for pu, pv in ((0, 0), (7, 0), (0, 7), (7, 7), (4, 4)):
            ok, vt = ray(pu, pv)
            via_tex = via_tex or vt
            lit_probes += ok
        if lit_probes in (0, 5) and not via_tex:
            return (0 if lit_probes else 1), None

        mask = bytearray(8)
        for v in range(8):
            bits = 0
            for u in range(8):
                ok, vt = ray(u, v)
                if ok:
                    bits |= 1 << u
            mask[v] = bits
        return 2, mask

    # -- world pass --------------------------------------------------------
    def render_scene(self, scene):
        SIDE_QUADS = ((QUAD_FULL_NEGX, (-1, 0, 0)), (QUAD_FULL_POSX, (1, 0, 0)),
                      (QUAD_FULL_NEGZ, (0, 0, -1)), (QUAD_FULL_POSZ, (0, 0, 1)))
        for (x, y, z), bid in sorted(scene.cells.items()):
            mesh = MESH.get(bid)
            if mesh is None:
                continue
            if bid in NOT_FULL_BLOCKS:
                for quad_id, ti in mesh["quads"]:
                    tex_id, settings = mesh["tex"][ti]
                    self.emit(scene, x, y, z, quad_id, tex_id, settings)
                continue
            side = self.face_tex(bid, 2)
            if side:
                for quad_id, (dx, dy, dz) in SIDE_QUADS:
                    n = scene.get(x + dx, y + dy, z + dz)
                    if n != bid and n in TRANSPARENT_BLOCKS:
                        self.emit(scene, x, y, z, quad_id, side[0], side[1])
            bot = self.face_tex(bid, 1)
            if y > 0 and bot:
                n = scene.get(x, y - 1, z)
                if n != bid and n in TRANSPARENT_BLOCKS:
                    self.emit(scene, x, y, z, QUAD_FULL_NEGY, bot[0], bot[1])
            top = self.face_tex(bid, 0)
            if top:
                n = scene.get(x, y + 1, z)
                if n != bid and n in TRANSPARENT_BLOCKS:
                    self.emit(scene, x, y, z, QUAD_FULL_POSY, top[0], top[1])

    def emit(self, scene, x, y, z, quad_id, tex_id, settings):
        lit, mask = 0, None
        if self.shaders:
            state, mask = self.bake_face(scene, x, y, z, quad_id)
            if state == 1:
                lit = 0
            elif state == 0:
                lit, mask = self.quad_lit[quad_id], None
            else:
                lit = self.quad_lit[quad_id]
        self.draw_block_quad(x, y, z, quad_id, tex_id, settings, lit, mask)

    # -- entities ----------------------------------------------------------
    def render_mob(self, x, y, z, boxes, tex, yaw_deg, scale=1.0, shadow=True):
        """(x, y, z) feet centre in world sub-pixels; port of Renderer::renderMob."""
        sn, cs = math.sin(math.radians(yaw_deg)), math.cos(math.radians(yaw_deg))
        self.cull = True
        self.transparent = False
        self.inverted = False
        self.overlay = False
        self.lit_mask = None
        tex_front, tex_side, tex_top = tex
        for bx in boxes:
            ox, oy, oz, sxb, syb, szb, flags = bx
            lx = (ox * scale, (ox + sxb) * scale)
            ly = (y + oy * scale, y + (oy + syb) * scale)
            lz = (oz * scale, (oz + szb) * scale)
            for f in range(6):
                cam = []
                for j in range(4):
                    c = CORNER[f][j]
                    pxl, pzl = lx[c & 1], lz[(c >> 2) & 1]
                    wx = x + cs * pxl - sn * pzl
                    wy = ly[(c >> 1) & 1]
                    wz = z + sn * pxl + cs * pzl
                    if f >= 4:
                        u, v = float(c & 1), 1.0 - float((c >> 2) & 1)
                    else:
                        u, v = QUAD_UVS[j]
                    cam.append(self.world_to_cam((wx, wy, wz, u, v)))
                self.texture = (tex_front if (f == 3 and (flags & 1))
                                else tex_top if f >= 4 else tex_side)
                self.lit_level = self.quad_lit[f] if (self.shaders and f >= 4) else 0
                self.draw_quad_cam(cam)
        if shadow:
            self.render_quad((x - 4.0) / 16.0, y / 16.0, (z - 4.0) / 16.0,
                             QUAD_ITEMSHADOW, TEX_SHADOW,
                             TS_CULLBACK | TS_TRANSPARENT | TS_INVERTED)

    def render_ground_sprite(self, x, y, z, tex_id, size=10.0, yaw_deg=0.0):
        """A tool dropped flat on a block top: the icon lit up out of the dark
        surface, the way TEX_SHADOW is punched into it."""
        sn, cs = math.sin(math.radians(yaw_deg)), math.cos(math.radians(yaw_deg))
        h = size / 2.0
        local = ((-h, -h), (-h, h), (h, h), (h, -h))
        corners = []
        for i, (lx, lz) in enumerate(local):
            corners.append((x + cs*lx - sn*lz, y, z + sn*lx + cs*lz,
                            QUAD_UVS[i][0], QUAD_UVS[i][1]))
        self.draw_quad_world(corners, tex_id, TS_TRANSPARENT | TS_INVERTED)


# -------------------------------------------------------------------- logo
GLYPHS = {
    "F": ["#######",
          "#######",
          "##.....",
          "##.....",
          "######.",
          "######.",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "##....."],
    "L": ["##.....",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "#######",
          "#######"],
    "I": ["####",
          "####",
          ".##.",
          ".##.",
          ".##.",
          ".##.",
          ".##.",
          ".##.",
          ".##.",
          "####",
          "####"],
    "P": ["#####..",
          "######.",
          "##..##.",
          "##..##.",
          "##..##.",
          "######.",
          "#####..",
          "##.....",
          "##.....",
          "##.....",
          "##....."],
    "C": [".#####.",
          "#######",
          "##...##",
          "##.....",
          "##.....",
          "##.....",
          "##.....",
          "##...##",
          "##...##",
          "#######",
          ".#####."],
    "R": ["#####..",
          "######.",
          "##..##.",
          "##..##.",
          "######.",
          "#####..",
          "##.##..",
          "##..##.",
          "##..##.",
          "##...##",
          "##...##"],
    "A": ["..###..",
          ".#####.",
          "##...##",
          "##...##",
          "##...##",
          "#######",
          "#######",
          "##...##",
          "##...##",
          "##...##",
          "##...##"],
    "T": ["########",
          "########",
          "...##...",
          "...##...",
          "...##...",
          "...##...",
          "...##...",
          "...##...",
          "...##...",
          "...##...",
          "...##..."],
}


def draw_logo(fb, text, top=1, gap=2, depth=2):
    """Extruded block letters: solid face, dithered side, hard outline."""
    glyphs = [GLYPHS[c] for c in text]
    width = sum(len(g[0]) for g in glyphs) + gap * (len(glyphs) - 1)
    x0 = (SCREEN_W - (width + depth)) // 2

    face = set()
    pen = x0
    for g in glyphs:
        for r, row in enumerate(g):
            for c, ch in enumerate(row):
                if ch == "#":
                    face.add((pen + c, top + r))
        pen += len(g[0]) + gap

    # extruded side, offset down-right, only where the face does not cover it
    side = set()
    for d in range(1, depth + 1):
        for (x, y) in face:
            p = (x + d, y + d)
            if p not in face:
                side.add(p)

    # the sky behind the logo is cleared so it never fights the scene
    halo = set()
    for (x, y) in face | side:
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                halo.add((x + dx, y + dy))
    for (x, y) in halo:
        if 0 <= x < SCREEN_W and 0 <= y < SCREEN_H:
            fb[y][x] = 0
    for (x, y) in side:
        if 0 <= x < SCREEN_W and 0 <= y < SCREEN_H:
            fb[y][x] = (x ^ y) & 1
    for (x, y) in face:
        if 0 <= x < SCREEN_W and 0 <= y < SCREEN_H:
            fb[y][x] = 1
    return x0, width


# --------------------------------------------------------------------- png
PALETTE_INK = (0, 0, 0)
PALETTE_BG = (254, 138, 44)


def write_png(path, fb, scale):
    w, h = SCREEN_W * scale, SCREEN_H * scale
    rows = []
    for y in range(SCREEN_H):
        line = bytearray()
        for x in range(SCREEN_W):
            rgb = PALETTE_INK if fb[y][x] else PALETTE_BG
            line += bytes(rgb) * scale
        for _ in range(scale):
            rows.append(bytes(line))
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(png)
    return w, h


# ------------------------------------------------------------------- scene
# The camera is the composition tool: everything is placed in camera-relative
# (forward, right) block units and only then rotated into the world, so the
# framing survives a change of yaw. Forward is where the camera looks, right is
# screen-right; both in blocks.
CAM_X, CAM_Z = 12.6, 0.2
CAM_YAW = 22.5          # a whole step of the game's own 16-step yaw
CAM_PITCH = -7.0
GROUND = 4              # surface block y of the foreground
EYE = 24                # PLAYERCAMHEIGHT, sub-pixels above the block below

_A = math.radians(CAM_YAW)
_FX, _FZ = -math.sin(_A), math.cos(_A)
_RX, _RZ = math.cos(_A), math.sin(_A)


def place(fwd, right):
    """Camera-relative block position -> world (x, z)."""
    return CAM_X + fwd * _FX + right * _RX, CAM_Z + fwd * _FZ + right * _RZ


def cam_rel(x, z):
    """World (x, z) -> camera-relative (forward, right)."""
    dx, dz = x - CAM_X, z - CAM_Z
    return dx * _FX + dz * _FZ, dx * _RX + dz * _RZ


# The game only ever draws RENDER_RADIUS_BLOCKS around the player, so the
# world really does stop at a clean edge with open sky behind it. The art keeps
# that: it is the horizon, and it is what the creeper's head is read against.
DRAW_RADIUS = 9.6


def in_world(x, z):
    f, r = cam_rel(x, z)
    if f < -3.0 or abs(r) > f * 1.25 + 7.5:
        return False
    return math.hypot(f, r) <= DRAW_RADIUS + 1.0 * math.sin(r * 0.52 + 0.7)


# A block top is solid ink and a block side is nearly bare, so height is the
# only thing that separates one tone from another here: the ground is kept flat
# and dark, and everything that has to read gets lifted off it into the sky.
PIT = ((4.8, 7.1), (0.5, 2.9))      # (forward, right) span of the dug hole


def surface(x, z):
    f, r = cam_rel(x, z)
    if PIT[0][0] < f < PIT[0][1] and PIT[1][0] < r < PIT[1][1]:
        return GROUND - 1
    return GROUND


def build_scene():
    """The clearing. Laid out by hand, but only out of blocks the game has."""
    s = Scene()

    # Ground, clipped to the drawn radius so nothing off-camera is ever built.
    for x in range(-8, 26):
        for z in range(-11, 24):
            if in_world(x, z):
                s.column(x, z, surface(x, z), GRASS, DIRT, 2)

    # Ore at the bottom of the dug hole -- the reason the tools are out.
    px, pz = place(5.7, 1.3)
    s.set(int(round(px)), GROUND - 1, int(round(pz)), COALORE)
    px, pz = place(6.4, 2.1)
    s.set(int(round(px)), GROUND - 1, int(round(pz)), IRONORE)
    px, pz = place(5.2, 2.0)
    s.set(int(round(px)), GROUND, int(round(pz)), COBBLE)   # spoil on the lip

    # -- the chest, right of frame, lifted onto a cobble pedestal so its lid
    # -- and lock band read against open sky instead of the dark ground -----
    cx, cz = place(3.9, 3.0)
    bx, bz = int(round(cx)), int(round(cz))
    s.set(bx, GROUND + 1, bz, COBBLE)
    s.set(bx, GROUND + 2, bz, CHEST)
    s.set(bx + 1, GROUND + 1, bz + 1, PLANK)

    # -- the two stations, back and left, half against the sky --------------
    wx, wz = place(7.0, -0.9)
    s.set(int(round(wx)), GROUND + 1, int(round(wz)), TABLE)
    fx, fz = place(7.6, -1.9)
    s.set(int(round(fx)), GROUND + 1, int(round(fz)), FURNACE)

    # -- one tree, cropped by the left edge, framing the logo ---------------
    def tree(fwd, right, trunk_h=2):
        tx, tz = place(fwd, right)
        tbx, tbz = int(round(tx)), int(round(tz))
        base = surface(tbx, tbz) + 1
        for y in range(base, base + trunk_h):
            s.set(tbx, y, tbz, LOG)
        crown = base + trunk_h - 1
        for dx in range(-2, 3):
            for dz in range(-2, 3):
                d = abs(dx) + abs(dz)
                if d <= 3:
                    s.set(tbx + dx, crown, tbz + dz, LEAVES)
                if d <= 2:
                    s.set(tbx + dx, crown + 1, tbz + dz, LEAVES)
        s.set(tbx, crown + 2, tbz, LEAVES)

    tree(6.4, -6.6)

    # saplings, so the eye has something small to land on out there
    for f, r in ((6.2, -3.6), (8.2, 3.4)):
        sx, sz = place(f, r)
        s.set(int(round(sx)), GROUND + 1, int(round(sz)), SAPLING)
    return s


def render_keyart(tex, shaders=False):
    r = Renderer(tex, shaders=shaders)
    r.cam = [CAM_X * 16, (GROUND + 1) * 16 + EYE, CAM_Z * 16]
    r.set_cam_rot(CAM_PITCH, CAM_YAW)

    scene = build_scene()
    r.render_scene(scene)

    # -- the creeper, left of centre, turned dead on to the camera ---------
    # renderMob's forward is (-sin yaw, cos yaw), the same convention as the
    # camera's, so a mob at the camera's own yaw faces away from it: +180 is
    # what puts the head's front quad parallel to the image plane, which is
    # what makes the 8x8 face texture arrive undistorted.
    mx, mz = place(2.65, -1.55)
    r.render_mob(mx * 16, (GROUND + 1) * 16, mz * 16, CREEPER_BOXES, CREEPER_TEX,
                 yaw_deg=CAM_YAW + 180.0)

    # -- tools dropped in the grass, near foreground -----------------------
    for name, fwd, right, yaw, size in (
        ("pickaxe", 2.30, 0.55, 26.0, 11.0),
        ("axe",     2.60, 1.85, -46.0, 10.0),
        ("shovel",  1.80, -1.35, 68.0, 10.0),
        ("sword",   3.10, 1.25, 8.0, 11.5),
    ):
        tx, tz = place(fwd, right)
        r.render_ground_sprite(tx * 16, (GROUND + 1) * 16 + 0.4, tz * 16,
                               TOOL_SLOT[name], size=size, yaw_deg=yaw)

    fb = [[b & 1 for b in row] for row in r.zbuf]
    draw_logo(fb, "FLIPCRAFT")
    return fb


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render the Flipcraft key art.")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, ".catalog", "keyart.png"))
    ap.add_argument("--scale", type=int, default=4, help="pixel scale (catalog uses 4)")
    ap.add_argument("--shaders", action="store_true",
                    help="render with the sun (the art ships without it: a lit "
                         "grass top is a 50%% dither and the creeper would sink "
                         "into it)")
    ap.add_argument("--tool-tier", type=int, default=2, choices=(0, 1, 2),
                    help="0 wood, 1 stone, 2 iron")
    args = ap.parse_args(argv)

    tex = load_textures(os.path.join(ROOT, "assets", "textures.inc"))
    bake_tools(tex, tier=args.tool_tier)
    fb = render_keyart(tex, shaders=args.shaders)
    w, h = write_png(args.out, fb, args.scale)
    print("%s  %dx%d" % (args.out, w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
