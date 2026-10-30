"""把 toy.ReachMap 写的 Saved/ReachMap/<名字>.csv 画成俯视图，并列出走不到 / 传送不到的地方。

    python Tools/reach_map.py Saved/ReachMap/before.csv [输出.png]

颜色（只看露出来的地板，也就是从上往下第一下打到站立高度的朝上表面）：
    绿 = 走得到也传送得到      黄 = 只能传送（胶囊体站得下，但摇杆走不进去）
    橙 = 只能走（不在能传送的范围里 / 落点不算地板）   红 = 胶囊体站得下，但走不到也传送不了
    粉 = 离墙 / 家具不到一个胶囊体半径（身体站不下，正常的贴边）
    灰 = 地板上压着家具      紫 = 走得到但脚下不是地板（摇杆走路不看脚下，会走到这种地方）
    蓝框 = WalkArea / ExtraWalkAreas，黑点 = 开局站立点
需要 numpy、Pillow。
"""
import collections
import csv
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

PX = 4  # 每格几个像素
COLORS = {
    'green': (60, 190, 90), 'yellow': (240, 210, 40), 'orange': (245, 140, 30), 'red': (220, 30, 30),
    'pink': (245, 185, 185), 'gray': (150, 150, 150), 'purple': (150, 70, 200), 'none': (40, 40, 46),
}


def load(path):
    header, rows = [], []
    with open(path, encoding='utf-8-sig') as f:
        lines = f.read().splitlines()
    body = []
    for line in lines:
        (header if line.startswith('#') else body).append(line)
    for r in csv.DictReader(body):
        rows.append(r)
    meta = {}
    areas = []
    for h in header:
        parts = h[1:].split()
        if parts[0] == 'area':
            areas.append((parts[1], *map(float, parts[2:6])))
        else:
            for k, v in zip(parts[0::2], parts[1::2]):
                meta[k] = float(v)
    return meta, areas, rows


def classify(r):
    floor, tele, free, walk = (int(r[k]) for k in ('floor', 'tele', 'free', 'walk'))
    if floor == 1:
        if walk and tele:
            return 'green'
        if tele:
            return 'yellow'
        if walk:
            return 'orange'
        return 'red' if free else 'pink'
    if walk:
        return 'purple'
    if floor == 2:
        return 'gray'
    return 'none'


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else src.rsplit('.', 1)[0] + '.png'
    meta, areas, rows = load(src)
    nx = max(int(r['ix']) for r in rows) + 1
    ny = max(int(r['iy']) for r in rows) + 1
    cell = meta['cell']
    x0 = min(float(r['x']) for r in rows)
    y0 = min(float(r['y']) for r in rows)
    cat = np.empty((ny, nx), dtype=object)
    grid = {}
    for r in rows:
        ix, iy = int(r['ix']), int(r['iy'])
        cat[iy, ix] = classify(r)
        grid[ix, iy] = r

    # 俯视：工作室 +X（朝桌子）朝上，+Y 朝右
    w, h = ny * PX, nx * PX
    img = Image.new('RGB', (w, h + 60), (255, 255, 255))
    pix = img.load()
    for (ix, iy), r in grid.items():
        c = COLORS[cat[iy, ix]]
        px, py = iy * PX, (nx - 1 - ix) * PX
        for dx in range(PX):
            for dy in range(PX):
                pix[px + dx, py + dy] = c
    draw = ImageDraw.Draw(img)

    def to_px(x, y):
        return (y - y0) / cell * PX + PX / 2, (nx - 1 - (x - x0) / cell) * PX + PX / 2

    for kind, ax0, ay0, ax1, ay1 in areas:
        a, b = to_px(ax1, ay0), to_px(ax0, ay1)
        draw.rectangle([a, b], outline=(40, 120, 255), width=2)
    sx, sy = to_px(0.0, 0.0)
    draw.ellipse([sx - 5, sy - 5, sx + 5, sy + 5], fill=(0, 0, 0))

    # 红 / 黄 / 橙 连成片的地方编号，列出来
    try:
        font = ImageFont.truetype('msyh.ttc', 14)
    except OSError:
        font = ImageFont.load_default()
    seen = set()
    clusters = []
    for (ix, iy) in sorted(grid):
        k = cat[iy, ix]
        if k not in ('red', 'yellow', 'orange', 'purple') or (ix, iy) in seen:
            continue
        stack, cells = [(ix, iy)], []
        seen.add((ix, iy))
        while stack:
            c = stack.pop()
            cells.append(c)
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (c[0] + d[0], c[1] + d[1])
                if n in grid and n not in seen and cat[n[1], n[0]] == k:
                    seen.add(n)
                    stack.append(n)
        if len(cells) < 3:
            continue
        xs = [float(grid[c]['x']) for c in cells]
        ys = [float(grid[c]['y']) for c in cells]
        tops = collections.Counter(grid[c]['top'].rsplit('@', 1)[0] for c in cells)
        # 周围一圈挡路的东西
        near = collections.Counter()
        for c in cells:
            for d in ((2, 0), (-2, 0), (0, 2), (0, -2), (3, 0), (-3, 0), (0, 3), (0, -3)):
                n = (c[0] + d[0], c[1] + d[1])
                if n in grid and grid[n]['blocker']:
                    near[grid[n]['blocker']] += 1
        clusters.append((k, len(cells), (min(xs), min(ys), max(xs), max(ys)), tops.most_common(2), near.most_common(3)))
    clusters.sort(key=lambda c: -c[1])
    for i, (k, n, (ax, ay, bx, by), tops, near) in enumerate(clusters, 1):
        px, py = to_px((ax + bx) / 2, (ay + by) / 2)
        draw.text((px - 6, py - 8), str(i), fill=(0, 0, 0), font=font, stroke_width=2, stroke_fill=(255, 255, 255))
        print(f'#{i} {k} {n} 格 ≈ {n * cell * cell / 1e4:.2f}㎡  X {ax:.0f}..{bx:.0f} Y {ay:.0f}..{by:.0f}  脚下 {tops}  旁边挡路 {near}')

    counts = collections.Counter(cat[iy, ix] for (ix, iy) in grid)
    legend = [('green', '走+传'), ('yellow', '只能传'), ('orange', '只能走'), ('red', '都不行'), ('pink', '贴边站不下'),
              ('gray', '家具下'), ('purple', '走到非地板')]
    x = 8
    for key, text in legend:
        draw.rectangle([x, h + 10, x + 14, h + 24], fill=COLORS[key], outline=(0, 0, 0))
        label = f'{text} {counts[key]}'
        draw.text((x + 18, h + 8), label, fill=(0, 0, 0), font=font)
        x += 26 + int(draw.textlength(label, font=font))
    draw.text((8, h + 34), f'格子 {cell:.0f}cm  胶囊体半径 {meta["radius"]:.0f} 半高 {meta["halfheight"]:.0f} 离地 {meta["lift"]:.0f}'
              '  （上 = 工作室 +X，朝桌子；蓝框 = 能传送的范围；黑点 = 开局站立点）', fill=(0, 0, 0), font=font)
    img.save(out)
    print('合计', dict(counts), '→', out)


if __name__ == '__main__':
    main()
