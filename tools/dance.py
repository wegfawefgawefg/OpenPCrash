#!/usr/bin/env python3
"""Dance tables for the --dance capture mode (src/mech/dance_pose.hpp).

    tools/dance.py NAME out.txt      write dance NAME's table (offsets from the idle pose)
    tools/dance.py --list            the dances (groove, meme, sexy)

A dance is key poses on beats, eased pose to pose, plus groove layers (bounces, sways, recoils)
added on top. Channels are the Photon 2-leg rig's (build 68, leg class 5): see CHANNELS. The
first and last keys must be the same pose (the root's turn may differ by whole turns) so the
table loops.
"""
import math
import sys

TICKS_PER_SECOND = 60

# the channels the dances write; PART.TRACK.C as dance_pose.hpp reads them
CHANNELS = {
    'hop': 'root.ty',        # the whole machine up (feet leave the ground)
    'spin': 'root.ry',       # the whole machine about its own vertical
    'tilt': 'root.rz',       # the whole machine rolled about its feet
    'pitch': 'root.rx',      # the whole machine tipped forward
    'crouch': 'legs.0.ty',   # hips down, feet planted (IK)
    'sway': 'legs.0.tx',     # hips sideways, feet planted
    'surge': 'legs.0.tz',    # hips forward
    'hip_roll': 'legs.0.rz', # everything over the hips rolled sideways
    'hip_turn': 'legs.4.ry', # the hips' swivel
    'lean': 'legs.2.rx',     # torso forward (+) or back (-)
    'twist': 'legs.2.ry',    # torso turned
    'roll': 'legs.2.rz',     # torso rolled
    'nod': 'legs.3.rx',      # the head: + down, - up
    'head_turn': 'legs.3.ry',
    'b_foot_x': 'legs.6.tx', # foot B (+x side, arm B's side)
    'b_foot_y': 'legs.6.ty',
    'b_foot_z': 'legs.6.tz',
    'a_foot_x': 'legs.10.tx', # foot A (-x side, arm A's side)
    'a_foot_y': 'legs.10.ty',
    'a_foot_z': 'legs.10.tz',
    'a_raise': 'arm_a.1.rx', # arm A: - up, + down (0: the rifle forward)
    'a_out': 'arm_a.1.rz',   # arm A: - out to its side
    'a_turn': 'arm_a.1.ry',
    'b_raise': 'arm_b.1.rx',
    'b_out': 'arm_b.1.rz',   # arm B: + out to its side
    'b_turn': 'arm_b.1.ry',
}


def ease(kind, u):
    if kind == 'linear':
        return u
    if kind == 'snap':  # a hit: fast out, settles
        return 1 - (1 - u) ** 3
    if kind == 'heavy':  # a hit with weight: overshoots a little and settles
        s = 1.8
        v = u - 1
        return 1 + v * v * ((s + 1) * v + s)
    if kind == 'slow':  # wind-up: slow out, fast in
        return u ** 2.2
    return u * u * (3 - 2 * u)  # smooth


def key_value(keys, name, t):
    """keys: [(beat, ease into this key, pose dict)], t in beats"""
    for i in range(len(keys) - 1):
        b0, _, p0 = keys[i]
        b1, kind, p1 = keys[i + 1]
        if b0 <= t <= b1:
            u = 0.0 if b1 == b0 else (t - b0) / (b1 - b0)
            return p0.get(name, 0.0) + (p1.get(name, 0.0) - p0.get(name, 0.0)) * ease(kind, u)
    return keys[-1][2].get(name, 0.0)


def impact(t, at, depth, rate=7.0, ring=2.6):
    """a heavy landing at beat `at`: the body dips and recoils, decaying (t in beats)"""
    d = t - at
    if d < 0:
        return 0.0
    return -depth * 1.6 * math.exp(-rate * d) * math.sin(ring * math.tau * d)


def window(t, a, b, fade=0.25):
    """1 inside beats a..b, eased in and out over `fade` beats"""
    if t < a - fade or t > b + fade:
        return 0.0
    if t < a:
        u = (t - (a - fade)) / fade
    elif t > b:
        u = ((b + fade) - t) / fade
    else:
        return 1.0
    return u * u * (3 - 2 * u)


# ---- the passionate one: groove, stomps, a spin, the disco point, windmill arms ----

# arm poses, arm A's (raise rx, turn ry, out rz); arm B mirrors (turn and out negated)
ARM_UP = (-1.75, 0.0, 0.4)        # straight up beside the head
ARM_T = (0.0, 0.0, -0.8)          # out to the side
ARM_FLARE = (0.25, 0.0, -0.5)     # fists low and out
ARM_POINT = (-1.55, -0.75, 0.0)   # pointing at the sky, out
ARM_ACROSS = (0.2, 0.0, 0.75)     # down across the belly
ARM_FORWARD = (-0.8, 0.0, 0.0)


def arms(a, b=None):
    b = a if b is None else b
    return {'a_raise': a[0], 'a_turn': a[1], 'a_out': a[2],
            'b_raise': b[0], 'b_turn': -b[1], 'b_out': -b[2]}


def pose(base=None, a=None, b=None, **channels):
    out = dict(base or {})
    if a is not None:
        out.update(arms(a, b))
    out.update(channels)
    return out


def groove_keys():
    rest = {'crouch': -0.25}
    roof = pose(rest, ARM_UP, nod=-0.2, lean=-0.08)
    flare = pose(rest, ARM_FLARE, crouch=-0.45, lean=0.2, nod=0.15)
    return [
        (0, 'smooth', roof),
        (4, 'smooth', roof),
        # beats 4-6: fists drop, body squares up for the stomps
        (4.4, 'snap', flare),
        (6.0, 'smooth', flare),
        # 6.5: wound up low for the spin
        (6.5, 'slow', pose(rest, ARM_ACROSS, crouch=-0.85, lean=0.35, twist=0.5, spin=-0.4)),
        # 6.5-8: the hop and a whole turn, arms flung out
        (7.1, 'smooth', pose(rest, ARM_T, crouch=-0.05, hop=1.0, lean=-0.2, nod=-0.3,
                             spin=math.pi)),
        (7.7, 'smooth', pose(rest, ARM_T, crouch=-0.1, hop=0.2, lean=-0.15, nod=-0.25,
                             spin=math.tau * 0.96, b_foot_y=0.5)),
        (8.0, 'heavy', pose(flare, crouch=-0.75, lean=0.3, nod=0.3, spin=math.tau)),
        # 8.5-10: the disco point: arm A to the sky, arm B across, swapping on 9.5
        (8.5, 'snap', pose(rest, ARM_POINT, ARM_ACROSS, crouch=-0.5, spin=math.tau + 0.35,
                           hip_roll=0.12, roll=-0.25, nod=-0.4, head_turn=-0.4, sway=0.25,
                           tilt=0.05)),
        (9.25, 'smooth', pose(rest, ARM_POINT, ARM_ACROSS, crouch=-0.45, spin=math.tau + 0.4,
                              hip_roll=0.14, roll=-0.28, nod=-0.45, head_turn=-0.45, sway=0.3,
                              tilt=0.06)),
        (9.5, 'snap', pose(rest, ARM_ACROSS, ARM_POINT, crouch=-0.55, spin=math.tau - 0.35,
                           hip_roll=-0.12, roll=0.25, nod=-0.4, head_turn=0.4, sway=-0.25,
                           tilt=-0.05)),
        (10.25, 'smooth', pose(rest, ARM_ACROSS, ARM_POINT, crouch=-0.5, spin=math.tau - 0.4,
                               hip_roll=-0.14, roll=0.28, nod=-0.45, head_turn=0.45, sway=-0.3,
                               tilt=-0.06)),
        # 10.5-12: gathered low with crossed arms, rising through a T back to the roof
        (10.75, 'snap', pose(rest, ARM_ACROSS, crouch=-0.8, lean=0.4, nod=0.35,
                             spin=math.tau)),
        (11.4, 'smooth', pose(rest, ARM_T, crouch=-0.3, lean=-0.1, nod=-0.2, spin=math.tau)),
        (12, 'smooth', pose(roof, spin=math.tau)),
    ]


def groove(t):
    """t in beats; the pose as channel -> value"""
    keys = groove_keys()
    pose = {name: key_value(keys, name, t) for name in CHANNELS}
    beat = t % 1.0
    # beats 0-4: the bounce on every beat, the hips swaying over two, step-touch, roof pumps
    g = window(t, 0, 3.8, 0.3) + window(t, 11.8, 12.4, 0.3)
    dip = math.cos(math.tau * beat)  # 1 on the beat
    side = math.sin(math.pi * t)  # over two beats
    pose['crouch'] += g * (-0.25 * (0.5 + 0.5 * dip) ** 2)
    pose['sway'] += g * 0.32 * side
    pose['hip_roll'] += g * -0.10 * side
    pose['hip_turn'] += g * 0.25 * side
    pose['twist'] += g * -0.2 * side
    pose['tilt'] += g * 0.04 * side
    pose['nod'] += g * 0.3 * (0.5 + 0.5 * dip)
    # the roof pumps: arms punch up on the beat, sink between
    pump = 0.5 - 0.5 * dip
    pose['a_raise'] += g * 0.7 * pump
    pose['b_raise'] += g * 0.7 * pump
    # every other bar the arms open to a V
    v = g * (0.5 - 0.5 * math.cos(math.pi * t / 2)) ** 2
    pose['a_turn'] += v * -0.55
    pose['a_out'] += v * -0.4
    pose['b_turn'] += v * 0.55
    pose['b_out'] += v * 0.4
    # the free foot taps: foot B up while the hips are over A, then A
    pose['b_foot_y'] += g * 0.4 * max(0.0, -side) ** 2
    pose['a_foot_y'] += g * 0.4 * max(0.0, side) ** 2
    # beats 4.5-6.5: four stomps, alternating feet, double time, the body slammed down each
    s = window(t, 4.4, 6.4, 0.15)
    for i, at in enumerate((4.5, 5.0, 5.5, 6.0)):
        lift = window(t, at - 0.35, at - 0.2, 0.12) * s
        if i % 2 == 0:
            pose['b_foot_y'] += 0.75 * lift
            pose['b_foot_z'] += 0.25 * lift
        else:
            pose['a_foot_y'] += 0.75 * lift
            pose['a_foot_z'] += 0.25 * lift
        hit = impact(t, at, 0.28) * s
        pose['crouch'] += hit
        pose['nod'] += -0.8 * hit
        pose['tilt'] += (0.07 if i % 2 == 0 else -0.07) * window(t, at - 0.3, at, 0.15) * s
        pose['twist'] += (0.3 if i % 2 == 0 else -0.3) * window(t, at - 0.25, at + 0.2, 0.2) * s
        pose['roll'] += (-0.2 if i % 2 == 0 else 0.2) * window(t, at - 0.25, at + 0.2, 0.2) * s
    # the shimmy over the stomps: shoulders shaking at four a beat
    pose['twist'] += 0.12 * s * math.sin(4 * math.tau * t)
    # the spin's landing and the points' hip pops
    pose['crouch'] += impact(t, 8.0, 0.25)
    for at in (8.5, 9.0, 9.5, 10.0):
        pose['crouch'] += impact(t, at, 0.12, rate=9)
    pose['sway'] += 0.12 * window(t, 8.4, 10.3, 0.15) * math.sin(math.tau * (t - 8.5))
    # windmill: a deep bounce under the arms
    w = window(t, 10.6, 11.9, 0.3)
    pose['crouch'] += w * -0.15 * (0.5 + 0.5 * math.cos(2 * math.tau * t))
    return pose


# ---- the meme one: the stock "silly dancing" loop, keyed by hand from its description ----
# the Cabbage Patch (fists together circling in front of the chest), a shuffle with swinging
# arms, arms crossed over the chest, then the points, one way and the other

ARM_FISTS = (-0.7, 0.55, 0.2)       # forward and in: fists together at the chest
ARM_CROSSED = (-0.9, 1.0, 0.6)       # crossed over the chest
ARM_POINT_OUT = (-0.35, -0.7, 0.0)   # pointing out to its own side
ARM_POINT_OVER = (-0.35, 0.75, 0.0)  # pointing across to the other arm's side


def meme_keys():
    base = {'crouch': -0.3}
    patch = pose(base, ARM_FISTS, lean=0.12)
    crossed = pose(base, ARM_CROSSED, crouch=-0.2, lean=-0.15, nod=-0.3)
    return [
        (0, 'smooth', patch),
        (2, 'smooth', patch),
        # 2-4: the shuffle, arms loose
        (2.35, 'smooth', pose(base, ARM_FORWARD, ARM_FLARE)),
        (3.75, 'smooth', pose(base, ARM_FLARE, ARM_FORWARD)),
        # 4: arms snapped across the chest, chin up
        (4.25, 'snap', crossed),
        (4.9, 'smooth', pose(crossed, crouch=-0.25)),
        # 5: point to the one side (arm A's), leaning in
        (5.15, 'snap', pose(base, ARM_POINT_OUT, ARM_POINT_OVER, crouch=-0.4, lean=0.15,
                            nod=0.1, spin=-0.2, twist=-0.25, roll=-0.15, sway=-0.15)),
        (5.9, 'smooth', pose(base, ARM_POINT_OUT, ARM_POINT_OVER, crouch=-0.4, lean=0.17,
                             nod=0.12, spin=-0.22, twist=-0.28, roll=-0.17, sway=-0.17)),
        # 6: and the other (arm B's)
        (6.15, 'snap', pose(base, ARM_POINT_OVER, ARM_POINT_OUT, crouch=-0.4, lean=0.15,
                            nod=0.1, spin=0.2, twist=0.25, roll=0.15, sway=0.15)),
        (6.9, 'smooth', pose(base, ARM_POINT_OVER, ARM_POINT_OUT, crouch=-0.4, lean=0.17,
                             nod=0.12, spin=0.22, twist=0.28, roll=0.17, sway=0.17)),
        (8, 'smooth', patch),
    ]


def meme(t):
    keys = meme_keys()
    pose = {name: key_value(keys, name, t) for name in CHANNELS}
    dip = math.cos(math.tau * (t % 1.0))
    # the knee bounce under everything, on every beat
    pose['crouch'] += -0.2 * (0.5 + 0.5 * dip) ** 2
    pose['nod'] += 0.18 * (0.5 + 0.5 * dip)
    # the Cabbage Patch: fists circling sideways in front, a circle a beat, the body following
    c = window(t, 0, 1.85, 0.2) + window(t, 7.6, 8.2, 0.3)
    ang = math.tau * t
    pose['a_out'] += c * 0.35 * math.cos(ang)
    pose['b_out'] += c * 0.35 * math.cos(ang)
    pose['a_raise'] += c * 0.3 * math.sin(ang)
    pose['b_raise'] += c * 0.3 * math.sin(ang)
    pose['a_turn'] += c * 0.25 * math.cos(ang)
    pose['b_turn'] += c * 0.25 * math.cos(ang)
    pose['twist'] += c * -0.25 * math.cos(ang)
    pose['sway'] += c * 0.22 * math.cos(ang)
    pose['hip_roll'] += c * -0.07 * math.cos(ang)
    pose['hip_turn'] += c * 0.2 * math.sin(ang)
    # the shuffle: hips swivel, feet kick out in turn, arms swing opposite
    sh = window(t, 2.2, 3.9, 0.2)
    side = math.sin(math.pi * (t - 2))
    pose['hip_turn'] += sh * 0.4 * side
    pose['twist'] += sh * -0.35 * side
    pose['sway'] += sh * 0.25 * side
    pose['a_foot_y'] += sh * 0.35 * max(0.0, side)
    pose['a_foot_z'] += sh * 0.3 * max(0.0, side)
    pose['b_foot_y'] += sh * 0.35 * max(0.0, -side)
    pose['b_foot_z'] += sh * 0.3 * max(0.0, -side)
    # the points jab twice a beat
    for at in (5.15, 5.65, 6.15, 6.65):
        jab = impact(t, at, 0.18, rate=8)
        pose['a_turn'] += -0.5 * jab if at < 6 else 0.5 * jab
        pose['b_turn'] += 0.5 * jab if at < 6 else -0.5 * jab
        pose['crouch'] += 0.4 * jab
    return pose


# ---- the sexy one: a robot that thinks it's hot. hands on hips and hip thrusts, body rolls with
# an arm behind the head, a slow grind, then the reach-and-yank climax thrust

ARM_HIP = (0.6, 1.2, -0.45)          # hung down beside the hip, muzzle turned in: hands on hips
ARM_HEAD = (-3.0, 0.0, 0.4)          # up and curled over behind the head
ARM_REACH = (-0.95, 0.15, -0.1)      # reaching forward to grab
ARM_YANK = (1.35, 0.0, -0.35)        # yanked back past the hips
# (beat, width in beats, strength): two on the beat, then four double time
SEXY_THRUSTS = ((0, 0.6, 1.0), (1, 0.6, 1.0), (2, 0.42, 0.85), (2.5, 0.42, 0.85), (3, 0.42, 0.9),
                (3.5, 0.42, 1.0), (12, 0.6, 1.0))


def thrust_pulse(t, at, width=0.5):
    """a hip thrust at beat `at`: pulled back a little, snapped forward on the beat, eased home.
    1 at the peak, -0.7 in the wind-up"""
    keys = [(-0.6 * width, 'smooth', {'v': 0.0}), (-0.18 * width, 'smooth', {'v': -0.7}),
            (0.0, 'snap', {'v': 1.0}), (0.15 * width, 'smooth', {'v': 0.85}),
            (0.75 * width, 'smooth', {'v': 0.0})]
    d = t - at
    if d < keys[0][0] or d > keys[-1][0]:
        return 0.0
    return key_value(keys, 'v', d)


def sexy_keys():
    base = {'crouch': -0.3}
    hips = pose(base, ARM_HIP)
    roll = pose(base, ARM_HIP, ARM_HEAD, crouch=-0.45, head_turn=0.25)
    # turned toward the camera (spin +) for it
    grind = pose(base, ARM_HIP, ARM_HEAD, crouch=-0.7, spin=0.85, nod=-0.15)
    windup = pose(base, ARM_REACH, crouch=-0.75, surge=-0.6, lean=0.5, nod=0.45)
    climax = pose(base, ARM_YANK, crouch=-0.4, surge=0.85, lean=-0.75, nod=-0.7, pitch=-0.14)
    return [
        (0, 'smooth', hips),
        (3.9, 'smooth', hips),
        # 4-6: body rolls, arm B up behind the head
        (4.5, 'smooth', roll),
        (6.0, 'smooth', roll),
        # 6-8: the slow grind, low, turned to the camera
        (6.7, 'smooth', grind),
        (7.9, 'smooth', grind),
        # 8-9: the reach forward, hips back, then THE thrust, arms yanked back
        (8.6, 'slow', windup),
        (8.85, 'smooth', pose(windup, surge=-0.7, lean=0.55, crouch=-0.8)),
        (9.0, 'heavy', climax),
        (9.9, 'smooth', pose(climax, surge=0.75, lean=-0.65, nod=-0.6)),
        # 10-12: proud, then hands back on hips
        (10.6, 'smooth', pose(base, ARM_HIP, crouch=-0.35, surge=0.2, lean=-0.2, nod=-0.3,
                              head_turn=0.45)),
        (11.5, 'smooth', pose(hips, head_turn=0.1)),
        (12, 'smooth', hips),
    ]


def sexy(t):
    keys = sexy_keys()
    pose = {name: key_value(keys, name, t) for name in CHANNELS}
    # the hip thrusts: hips forward, chest and head back, the hands-on-hips arms swinging
    th = window(t, -1, 3.9, 0.2) + window(t, 11.6, 13, 0.2)
    for at, width, amp in SEXY_THRUSTS:
        p = thrust_pulse(t, at, width) * amp * th
        pose['surge'] += 0.75 * p
        pose['lean'] += -0.55 * p
        pose['nod'] += -0.55 * p
        pose['crouch'] += -0.12 * abs(p)
        pose['pitch'] += -0.06 * p
        pose['a_raise'] += 0.25 * p
        pose['b_raise'] += 0.25 * p
    # 4-6: two body rolls, a beat each: head, then chest, then the hips through
    r = window(t, 4.25, 6.0, 0.3)
    ph = math.tau * (t - 4)
    pose['nod'] += r * 0.35 * math.sin(ph + 0.9)
    pose['lean'] += r * 0.38 * math.sin(ph)
    pose['surge'] += r * 0.45 * math.sin(ph - math.pi / 2)
    pose['crouch'] += r * -0.15 * (0.5 - 0.5 * math.cos(ph))
    # the arm behind the head waves in time
    w = window(t, 4.6, 7.9, 0.3)
    pose['b_raise'] += w * 0.3 * math.sin(math.tau * t)
    pose['b_out'] += w * -0.25 * math.cos(math.tau * t)
    # 6-8: the grind, one slow circle of the hips, the chest against it
    g = window(t, 6.2, 7.9, 0.4)
    th2 = math.pi * (t - 6)
    pose['surge'] += g * 0.4 * math.cos(th2)
    pose['sway'] += g * 0.38 * math.sin(th2)
    pose['hip_roll'] += g * -0.12 * math.sin(th2)
    pose['hip_turn'] += g * 0.22 * math.sin(th2)
    pose['lean'] += g * -0.25 * math.cos(th2)
    pose['roll'] += g * 0.18 * math.sin(th2)
    pose['tilt'] += g * 0.03 * math.sin(th2)
    # the climax lands: the whole machine shudders
    pose['crouch'] += impact(t, 9.0, 0.18, rate=6)
    pose['nod'] += -1.2 * impact(t, 9.0, 0.18, rate=6)
    pose['twist'] += 0.25 * impact(t, 9.05, 0.2, rate=5, ring=4)
    # 10-11.5: a satisfied shimmy
    s = window(t, 10.2, 11.3, 0.2)
    pose['twist'] += s * 0.14 * math.sin(4 * math.tau * t)
    pose['sway'] += s * 0.08 * math.sin(2 * math.tau * t)
    return pose


DANCES = {
    'groove': (groove, 12, 120),  # (pose function, beats, bpm)
    # Mixamo's Silly Dancing loops in 115 frames at 30 fps: 8 beats at 125 bpm
    'meme': (meme, 8, 125),
    'sexy': (sexy, 12, 120),
}


def write(name, path):
    function, beats, bpm = DANCES[name]
    ticks = round(beats * 60 / bpm * TICKS_PER_SECOND)
    names = list(CHANNELS)
    with open(path, 'w') as out:
        out.write(f'# tools/dance.py {name}: {beats} beats at {bpm} bpm\n')
        out.write(f'ticks {ticks}\n')
        out.write('channels ' + ' '.join(CHANNELS[n] for n in names) + '\n')
        for tick in range(ticks + 1):
            pose = function(tick / ticks * beats)
            out.write(' '.join(f'{pose[n]:.5f}' for n in names) + '\n')
    print(f'{path}: {ticks} ticks, {len(names)} channels')


def main():
    if len(sys.argv) == 2 and sys.argv[1] == '--list':
        for name, (_, beats, bpm) in DANCES.items():
            print(f'{name}: {beats} beats at {bpm} bpm')
        return
    if len(sys.argv) != 3 or sys.argv[1] not in DANCES:
        print(__doc__)
        sys.exit(2)
    write(sys.argv[1], sys.argv[2])


if __name__ == '__main__':
    main()
