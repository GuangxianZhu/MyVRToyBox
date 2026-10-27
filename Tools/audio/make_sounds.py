"""合成玩具箱的全部音效（不用录音素材）：写到 Tools/audio/out/*.wav，48 kHz、16 位、单声道。

用法：python Tools/audio/make_sounds.py        （只要 numpy）
然后跑根目录的 导入音效.bat 导入成 UE 资产（/Game/Audio）。

合成方法：
- 敲击（材料声）= 模态合成：几个按材料定好频率比和衰减时间的正弦叠加，再加一小段噪声当"咔"的瞬态。
  重金属（钢铁铜）亮、长；轻金属（铝）更高、更短；硬塑料短而闷；橡胶低、几乎没有余音；脆性（玻璃陶瓷硅片）很高、清脆。
- 抓取器的锁定声、触屏点击 = 很短的高频衰减正弦 + 噪声瞬态，偏"科幻"。
- 伺服电机循环 = 整数周期的锯齿 + 齿轮啸叫 + 用 FFT 圆周滤波的噪声，所以首尾无缝（UE 里设成循环播放）。
- 每种声音出 3 个变体（频率、衰减、噪声随机一点），游戏里轮着放、再加 ±3% 音高、±2 dB 音量。
随机数种子固定，每次生成结果一样。
"""
import os
import wave

import numpy as np

SR = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
rng = np.random.default_rng(20261027)


def t_axis(duration):
    return np.arange(int(SR * duration)) / SR


def band(x, lo, hi, soft=0.15):
    """FFT 带通（圆周滤波：对循环素材首尾也连续）。lo / hi 单位 Hz，边缘按 soft 比例做余弦过渡。"""
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / SR)
    mask = np.ones_like(f)
    if lo > 0:
        w = lo * soft
        mask *= np.clip((f - (lo - w)) / (2 * w), 0, 1)
    if hi < SR / 2:
        w = hi * soft
        mask *= np.clip(((hi + w) - f) / (2 * w), 0, 1)
    return np.fft.irfft(spec * mask, len(x))


def noise(duration, lo=0, hi=SR / 2):
    return band(rng.standard_normal(int(SR * duration)), lo, hi)


def decay_env(duration, tau, attack=0.0005):
    t = t_axis(duration)
    env = np.exp(-t / tau)
    if attack > 0:
        env *= np.clip(t / attack, 0, 1)
    return env


def modal(f0, ratios, amps, taus, duration, jitter=0.01):
    t = t_axis(duration)
    out = np.zeros_like(t)
    for ratio, amp, tau in zip(ratios, amps, taus):
        f = f0 * ratio * (1 + rng.uniform(-jitter, jitter))
        if f >= SR / 2 * 0.9:
            continue
        out += amp * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi)) * np.exp(-t / tau)
    return out * np.clip(t / 0.0004, 0, 1)


def pad(x, duration):
    n = int(SR * duration)
    return np.pad(x, (0, max(0, n - len(x))))[:n]


def mix(*parts):
    n = max(len(p) for p in parts)
    return sum(np.pad(p, (0, n - len(p))) for p in parts)


def delayed(x, seconds):
    return np.concatenate([np.zeros(int(SR * seconds)), x])


def fade_tail(x, seconds=0.01):
    n = min(len(x), int(SR * seconds))
    x = x.copy()
    x[-n:] *= np.linspace(1, 0, n)
    return x


def write(name, x, peak=0.89, fade=True):
    x = np.asarray(x, dtype=np.float64)
    if fade:
        x = fade_tail(x)
    m = np.max(np.abs(x))
    if m > 0:
        x = x / m * peak
    data = (np.clip(x, -1, 1) * 32767).astype('<i2')
    os.makedirs(OUT, exist_ok=True)
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())
    print(f'{name}.wav  {len(x) / SR * 1000:.0f} ms')


# ---------------------------------------------------------------- 材料敲击（手指碰到零件、抓稳、吸回原位都用它）
MATERIALS = {
    # 名字: (基频范围, 频率比, 振幅, 衰减时间, 瞬态噪声 (时长, 低频, 高频, 音量))
    'Heavy':   ((1100, 1600), [1, 2.76, 5.40, 8.93], [1, .6, .35, .2], [.35, .2, .1, .06], (.003, 3000, 16000, .5)),
    'Light':   ((1500, 2100), [1, 2.32, 4.25, 6.63], [1, .55, .3, .15], [.18, .1, .06, .04], (.003, 3000, 16000, .45)),
    'Plastic': ((500, 800),   [1, 1.58, 2.40, 3.60], [1, .5, .3, .15], [.04, .03, .02, .015], (.006, 1000, 5000, .8)),
    'Rubber':  ((160, 260),   [1, 1.9, 3.1],         [1, .4, .15],     [.03, .02, .012],      (.008, 80, 800, .7)),
    'Brittle': ((2400, 3400), [1, 2.32, 4.25, 6.63], [1, .5, .3, .2],  [.25, .15, .08, .05],  (.002, 5000, 20000, .4)),
}


def tap(material):
    (lo, hi), ratios, amps, taus, (nd, nlo, nhi, nvol) = MATERIALS[material]
    taus = [tau * rng.uniform(0.85, 1.15) for tau in taus]
    duration = min(1.2, max(taus) * 5 + 0.02)
    body = modal(rng.uniform(lo, hi), ratios, amps, taus, duration)
    click = noise(nd + 0.01, nlo, nhi) * decay_env(nd + 0.01, nd / 3) * nvol
    return mix(body, click)


# ---------------------------------------------------------------- 抓取器自己的声音（偏科幻）
def lock_click():
    """抓稳：两声"咔嗒"（锁销落位），第二声轻一点、低一点。"""
    def one(f, vol):
        tick = modal(f, [1, 1.5], [1, .4], [.008, .005], .06)
        thunk = modal(rng.uniform(160, 200), [1, 2.1], [1, .3], [.04, .02], .15)
        snap = noise(.02, 2500, 12000) * decay_env(.02, .002) * .6
        return mix(tick * .8, thunk * .6, snap) * vol
    return mix(one(rng.uniform(3500, 4500), 1.0), delayed(one(rng.uniform(2800, 3400), .6), rng.uniform(.022, .03)))


def servo_loop():
    """伺服电机的循环声：1 秒，所有成分在 1 秒里都是整数周期，首尾无缝。"""
    t = t_axis(1.0)
    base = 96
    saw = sum(np.sin(2 * np.pi * base * k * t) / k for k in range(1, 24))
    whine = np.sin(2 * np.pi * 1920 * t) * .12 + np.sin(2 * np.pi * 2880 * t) * .05
    hiss = band(rng.standard_normal(SR), 600, 5000) * .25
    wobble = 1 + .15 * np.sin(2 * np.pi * 8 * t)
    return band(saw * .25 * wobble + whine + hiss, 60, 9000)


def hover_tick():
    f = rng.uniform(2000, 2600)
    return pad(modal(f, [1], [1], [.004], .03), .03)


def release_hiss():
    d = rng.uniform(.24, .32)
    return noise(d, 2000, 8000) * decay_env(d, d / 3.5, attack=.006)


def whoosh():
    """飞回原位：低频噪声里叠一段高一点的，响度先升后降。"""
    d = rng.uniform(.4, .5)
    t = t_axis(d)
    bell = np.sin(np.pi * t / d) ** 2
    low = noise(d, 250, 1200)
    high = noise(d, 1200, 3500)
    x = low * bell + high * bell ** 3 * .6
    return x


def seat():
    """落回原位的"咔哒"：低频敦实 + 中频的边沿声。"""
    thunk = modal(rng.uniform(120, 160), [1, 2.3, 3.9], [1, .4, .2], [.06, .03, .015], .3)
    edge = modal(rng.uniform(850, 1000), [1, 1.7], [1, .5], [.015, .01], .08)
    grit = noise(.03, 800, 6000) * decay_env(.03, .005) * .5
    return mix(thunk, edge * .5, grit)


def ready_tone():
    """拿着零件靠近原位、虚影出现：一声很轻的"叮"（大三度 + 五度）。"""
    t = t_axis(.4)
    env = np.clip(t / .015, 0, 1) * np.exp(-t / .16)
    return (np.sin(2 * np.pi * 1046.5 * t) + .6 * np.sin(2 * np.pi * 1568 * t) + .2 * np.sin(2 * np.pi * 2093 * t)) * env


# ---------------------------------------------------------------- 触屏 / 菜单
def ui_hover():
    return pad(modal(rng.uniform(1700, 2000), [1, 2], [1, .2], [.005, .003], .03), .03)


def ui_press():
    tick = modal(rng.uniform(2300, 2600), [1, 1.5], [1, .3], [.012, .008], .08)
    body = modal(rng.uniform(1150, 1300), [1, 2.1], [1, .25], [.025, .015], .12)
    transient = noise(.01, 3000, 14000) * decay_env(.01, .001) * .6
    return mix(tick * .7, body * .8, transient)


def ui_release():
    tick = modal(rng.uniform(2900, 3300), [1], [1], [.008], .05)
    transient = noise(.008, 4000, 16000) * decay_env(.008, .0008) * .4
    return mix(tick, transient)


def chirp(f_from, f_to, d=.35):
    t = t_axis(d)
    f = f_from + (f_to - f_from) * (t / d) ** .7
    phase = 2 * np.pi * np.cumsum(f) / SR
    env = np.clip(t / .02, 0, 1) * np.clip((d - t) / .12, 0, 1)
    shimmer = noise(d, 4000, 9000) * env * .08
    return (np.sin(phase) + .35 * np.sin(2 * phase) + .15 * np.sin(3 * phase)) * env + shimmer


def main():
    for material in MATERIALS:
        for k in range(1, 4):
            write(f'Tap_{material}_{k}', tap(material))
    for k in range(1, 4):
        write(f'Claw_Lock_{k}', lock_click())
        write(f'Claw_HoverTick_{k}', hover_tick())
        write(f'Claw_Release_{k}', release_hiss())
        write(f'Part_Whoosh_{k}', whoosh())
        write(f'Part_Seat_{k}', seat())
        write(f'UI_Hover_{k}', ui_hover())
        write(f'UI_Press_{k}', ui_press())
        write(f'UI_Release_{k}', ui_release())
    write('Claw_ServoLoop', servo_loop(), peak=.7, fade=False)
    write('Part_Ready', ready_tone())
    write('UI_MenuOpen', chirp(600, 1250))
    write('UI_MenuClose', chirp(1250, 600))


if __name__ == '__main__':
    main()
