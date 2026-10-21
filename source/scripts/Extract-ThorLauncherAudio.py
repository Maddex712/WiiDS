"""Extract launcher music and UI sounds from the owner's own Mario Kart Wii disc data.

Reads files/sound/revo_kart.brsar from an extracted dump (DiscData/files) and writes
16-bit PCM WAV files plus audio.json into the ignored private folder
build/thor-map-data/launcher-audio. Nothing is downloaded; nothing is written into the
APK or public source. Stage to a device with --stage --serial DEVICE (debuggable build).

Music: W_BGM_TITLE, the title-screen theme, stored as a looping stereo DSP-ADPCM wave.
Its native loop points are kept so playback can loop seamlessly.
UI sounds: the game's own SE_UI_* sequences are short note sequences that play
instruments from a sound bank. They are rendered here with a small sequence player
(notes, waits, program, volume, pan, transpose, ADSR, tempo, sub-tracks). LFO,
portamento and filters are not modelled; the result is a close rendering, not a
bit-exact capture of the console mixer.
"""
import argparse
import hashlib
import io
import json
import math
import struct
import subprocess
import tarfile
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
FOLDER = ROOT / 'build/thor-map-data'
ASSETS = FOLDER / 'launcher-audio'
DEFAULT_DISC = ROOT / 'reference/driftdroid/DiscData/files'
RATE = 32000
MUSIC = ('music', 'W_BGM_TITLE')
# Launcher role -> original Mario Kart Wii UI sound.
SOUNDS = {
    'focus': 'SE_UI_BTN_IN',          # cursor onto a game case
    'open': 'SE_UI_CTRL_WIN_POPUP',   # case opens
    'close': 'SE_UI_CTRL_WIN_POPDOWN',  # case closes / back out
    'select': 'SE_UI_BTN_OK',         # disc chosen
    'launch': 'SE_UI_TITLE_OUT',      # title -> game transition
    'reject': 'SE_UI_CTRL_REJECT',    # launch blocked
}


class Rsar:
    def __init__(self, data):
        self.b = data
        if data[:4] != b'RSAR':
            raise ValueError('Not a sound archive')
        symb, info = self.u32(0x10), self.u32(0x18)
        s = symb + 8
        strtab = s + self.u32(s)
        self.names = []
        for i in range(self.u32(strtab)):
            o = s + self.u32(strtab + 4 + 4 * i)
            self.names.append(data[o:data.index(b'\0', o)].decode())
        self.i = info + 8
        self.sounds = self.table(self.ref(self.i))
        self.banks = self.table(self.ref(self.i + 8))
        self.files = self.table(self.ref(self.i + 24))
        self.groups = self.table(self.ref(self.i + 32))
        self.by_name = {}
        for k, o in enumerate(self.sounds):
            sid = self.u32(self.i + o)
            if sid < len(self.names):
                self.by_name[self.names[sid]] = k

    def u8(self, o): return self.b[o]
    def u16(self, o): return struct.unpack_from('>H', self.b, o)[0]
    def u32(self, o): return struct.unpack_from('>I', self.b, o)[0]
    def ref(self, o): return self.u32(o + 4)

    def table(self, offset):
        o = self.i + offset
        return [self.u32(o + 4 + 8 * k + 4) for k in range(self.u32(o))]

    def file(self, file_id):
        """(file bytes, wave-data bytes) for a file, from its first group position."""
        fo = self.i + self.files[file_id]
        positions = self.table(self.ref(fo + 20))
        if not positions:
            raise ValueError(f'File {file_id} is not in any group')
        gid, index = self.u32(self.i + positions[0]), self.u32(self.i + positions[0] + 4)
        go = self.i + self.groups[gid]
        goff, gwoff = self.u32(go + 16), self.u32(go + 24)
        it = self.i + self.table(self.ref(go + 32))[index]
        _, off, size, woff, wsize = (self.u32(it + 4 * k) for k in range(5))
        return self.b[goff + off:goff + off + size], self.b[gwoff + woff:gwoff + woff + wsize]

    def sound(self, name):
        o = self.i + self.sounds[self.by_name[name]]
        file_id, kind = self.u32(o + 4), self.u8(o + 22)
        detail = self.i + self.ref(o + 24)
        return file_id, kind, detail


def be(b, o, fmt): return struct.unpack_from('>' + fmt, b, o)[0]


def rwar_waves(data):
    if data[:4] != b'RWAR':
        raise ValueError('Expected a wave archive')
    tabl, body = be(data, 0x10, 'I'), be(data, 0x18, 'I')
    return [rwav(data[body + be(data, tabl + 16 + 12 * k, 'I'):]) for k in range(be(data, tabl + 8, 'I'))]


def rwav(data):
    """Decode an RWAV into (float32 [channels, samples], rate, loop, loop_start, loop_end)."""
    if data[:4] != b'RWAV':
        raise ValueError('Expected a wave')
    info = be(data, 0x10, 'I') + 8
    body = be(data, 0x18, 'I') + 8
    fmt, loop, channels = data[info], data[info + 1], data[info + 2]
    rate = (data[info + 3] << 16) | be(data, info + 4, 'H')
    loop_start, loop_end = be(data, info + 8, 'I'), be(data, info + 12, 'I')
    table = info + be(data, info + 16, 'I')
    out = []
    for c in range(channels):
        ch = info + be(data, table + 4 * c, 'I')
        start = body + be(data, ch, 'I')
        if fmt == 2:
            adpcm = info + be(data, ch + 4, 'I')
            coefs = struct.unpack_from('>16h', data, adpcm)
            out.append(dsp_decode(data, start, loop_end, coefs))
        elif fmt == 1:
            out.append(np.frombuffer(data, '>i2', loop_end + 1, start).astype(np.float32) / 32768)
        else:
            out.append(np.frombuffer(data, 'i1', loop_end + 1, start).astype(np.float32) / 128)
    if fmt == 2:  # ADPCM loop points are nibble addresses.
        loop_start, loop_end = nibble_sample(loop_start), nibble_sample(loop_end)
    n = min(len(x) for x in out)
    return np.stack([x[:n] for x in out]), rate, bool(loop), loop_start, min(loop_end, n - 1)


def nibble_sample(n): return (n // 16) * 14 + max(0, n % 16 - 2)


def dsp_decode(data, start, end_nibble, coefs):
    samples = nibble_sample(end_nibble) + 1
    frames = (samples + 13) // 14
    raw = np.frombuffer(data, np.uint8, frames * 8, start)
    out = np.empty(frames * 14, np.int32)
    h1 = h2 = 0
    for f in range(frames):
        header = int(raw[f * 8]); scale = 1 << (header & 15); c1, c2 = coefs[(header >> 4) * 2], coefs[(header >> 4) * 2 + 1]
        for k in range(14):
            byte = int(raw[f * 8 + 1 + k // 2]); nib = (byte >> 4) if k % 2 == 0 else (byte & 15)
            if nib >= 8: nib -= 16
            v = ((nib * scale) << 11) + 1024 + c1 * h1 + c2 * h2
            v = max(-32768, min(32767, v >> 11))
            out[f * 14 + k] = v; h2, h1 = h1, v
    return out[:samples].astype(np.float32) / 32768


class Bank:
    def __init__(self, data, waves):
        if data[:4] != b'RBNK':
            raise ValueError('Expected a bank')
        self.b, self.waves = data, waves
        self.d = be(data, 16, 'I') + 8

    def lookup(self, prg, key, vel):
        """Resolve program -> key region -> velocity region -> instrument parameters."""
        b, d = self.b, self.d
        if prg >= be(b, d, 'I'):
            return None
        kind, off = b[d + 4 + 8 * prg + 1], be(b, d + 4 + 8 * prg + 4, 'I')
        for depth in range(4):
            p = d + off
            if kind == 1:
                return dict(wave=be(b, p, 'i'), a=b[p + 4], d=b[p + 5], s=b[p + 6], r=b[p + 7],
                            key=b[p + 12], vol=b[p + 13], pan=b[p + 14], tune=be(b, p + 16, 'f'))
            probe = key if depth == 0 else vel
            if kind == 2:  # range table: count, upper bounds, refs at the next 4-byte boundary
                n = b[p]; bounds = b[p + 1:p + 1 + n]; refs = p + ((1 + n + 3) & ~3)
                i = next((k for k, hi in enumerate(bounds) if probe <= hi), n - 1)
                kind, off = b[refs + 8 * i + 1], be(b, refs + 8 * i + 4, 'I')
            elif kind == 3:  # index table: min, max, refs
                lo, hi = b[p], b[p + 1]; k = min(max(probe, lo), hi) - lo
                kind, off = b[p + 4 + 8 * k + 1], be(b, p + 4 + 8 * k + 4, 'I')
            else:
                return None
        return None

def env_seconds(v, longest):
    return 0.0 if v >= 127 else ((127 - v) / 127.0) ** 2 * longest


def render_sequence(rsar, name):
    file_id, kind, detail = rsar.sound(name)
    if kind != 1:
        raise ValueError(f'{name} is not a sequence')
    seq, _ = rsar.file(file_id)
    if seq[:4] != b'RSEQ':
        raise ValueError(f'{name}: unexpected sequence file')
    start = rsar.u32(detail)
    bank_info = rsar.i + rsar.banks[rsar.u32(detail + 4)]
    bank_bytes, bank_waves = rsar.file(rsar.u32(bank_info + 4))
    bank = Bank(bank_bytes, rwar_waves(bank_waves))
    code = seq[be(seq, 16, 'I') + be(seq, be(seq, 16, 'I') + 8, 'I'):]
    notes = []  # (time s, duration s, key, velocity, volume, pan, prg, adsr)

    def varlen(pc):
        v = 0
        while True:
            c = code[pc]; pc += 1; v = (v << 7) | (c & 0x7f)
            if c < 0x80: return v, pc

    tempo = [120]
    tracks = [dict(pc=start, t=0.0, prg=0, vol=127, pan=64, trans=0, wait=True, adsr=None, stack=[], loops=[])]
    ticks = lambda n: n * 60.0 / (tempo[0] * 48)
    guard = 0
    while tracks:
        tr = tracks.pop(0)
        while guard < 20000 and tr['t'] < 4.0:
            guard += 1
            pc = tr['pc']; c = code[pc]; pc += 1
            if c < 0x80:
                vel = code[pc]; length, pc = varlen(pc + 1)
                adsr = tr['adsr']
                notes.append((tr['t'], ticks(length), c + tr['trans'], vel, tr['vol'], tr['pan'], tr['prg'], adsr))
                if tr['wait']: tr['t'] += ticks(length)
            elif c == 0x80: n, pc = varlen(pc); tr['t'] += ticks(n)
            elif c == 0x81: tr['prg'], pc = varlen(pc)
            elif c == 0x88:
                o = int.from_bytes(code[pc + 1:pc + 4], 'big'); pc += 4
                tracks.append(dict(tr, pc=o, stack=[], loops=[], adsr=tr['adsr']))
            elif c == 0x89: pc = int.from_bytes(code[pc:pc + 3], 'big')
            elif c == 0x8a: tr['stack'].append(pc + 3); pc = int.from_bytes(code[pc:pc + 3], 'big')
            elif c == 0xfd:
                if not tr['stack']: break
                pc = tr['stack'].pop()
            elif c == 0xff: break
            elif c == 0xfe: pc += 2
            elif c == 0xc1: tr['vol'] = code[pc]; pc += 1
            elif c == 0xc0: tr['pan'] = code[pc]; pc += 1
            elif c == 0xc3: tr['trans'] = struct.unpack('b', code[pc:pc + 1])[0]; pc += 1
            elif c == 0xc7: tr['wait'] = code[pc] != 0; pc += 1
            elif 0xd0 <= c <= 0xd3:
                a = list(tr['adsr'] or (None, None, None, None)); a[c - 0xd0] = code[pc]; tr['adsr'] = tuple(a); pc += 1
            elif c == 0xd4: tr['loops'].append([pc + 1, code[pc]]); pc += 1
            elif c == 0xfc:
                if tr['loops']:
                    loop = tr['loops'][-1]
                    if loop[1] == 0 or loop[1] > 1:
                        if loop[1] > 1: loop[1] -= 1
                        pc = loop[0]
                    else: tr['loops'].pop()
            elif c == 0xe1: tempo[0] = be(code, pc, 'H') or 120; pc += 2
            elif c in (0xe0, 0xe3): pc += 2
            elif 0xc2 <= c <= 0xdf: pc += 1
            elif 0xb0 <= c <= 0xbf: pc += 3
            elif c == 0xa0: pc += 5 if code[pc] in (0xb0,) else 4  # random arg: treat as wait/arg skip
            elif c in (0xa1, 0xa2, 0xa3): pc += 0  # prefix modifiers: execute the following command plainly
            elif c == 0xf0:
                sub = code[pc]; pc += 1
                pc += 3 if 0x80 <= sub <= 0x8f or 0x90 <= sub <= 0x9f else 2
            else:
                raise ValueError(f'{name}: unsupported sequence command 0x{c:02x} at {pc - 1}')
            tr['pc'] = pc
    if not notes:
        raise ValueError(f'{name}: sequence produced no notes')
    end = max(t + d for t, d, *_ in notes) + 1.5
    mix = np.zeros((2, int(end * RATE) + 1), np.float32)
    for t, dur, key, vel, vol, pan, prg, adsr in notes:
        inst = bank.lookup(prg, key, vel)
        if inst is None or not 0 <= inst['wave'] < len(bank.waves):
            continue
        pcm, wrate, loop, ls, le = bank.waves[inst['wave']]
        a, d_, s, r = (x if x is not None else y for x, y in zip(adsr or (None,) * 4, (inst['a'], inst['d'], inst['s'], inst['r'])))
        step = 2 ** ((key - inst['key']) / 12.0) * inst['tune'] * wrate / RATE
        held = dur + env_seconds(r, 2.5) + 0.02
        n = int(held * RATE)
        pos = np.arange(n) * step
        if loop and le > ls:
            span = le - ls
            pos = np.where(pos > le, ls + np.mod(pos - ls, span), pos)
        else:
            n = min(n, int((pcm.shape[1] - 1) / step)); pos = pos[:n]
        if n <= 0:
            continue
        i0 = pos.astype(np.int64); frac = (pos - i0).astype(np.float32); i1 = np.minimum(i0 + 1, pcm.shape[1] - 1)
        voice = pcm[:, i0] * (1 - frac) + pcm[:, i1] * frac
        if voice.shape[0] == 1: voice = np.repeat(voice, 2, axis=0)
        tt = np.arange(n) / RATE
        at, dt, rt = env_seconds(a, 1.2), env_seconds(d_, 3.0) + 0.005, env_seconds(r, 2.5) + 0.005
        sus = (s / 127.0) ** 2
        env = np.where(tt < at, tt / max(at, 1e-4), 1.0)
        env = np.where(tt >= at, sus + (1 - sus) * np.exp(-(tt - at) / dt * 3), env)
        off = tt >= dur
        env = np.where(off, env[min(int(dur * RATE), n - 1)] * np.exp(-np.minimum((tt - dur) / rt * 4, 60)), env)
        gain = (vel / 127) ** 2 * (vol / 127) ** 2 * (inst['vol'] / 127) ** 2
        p = ((pan - 64) + (inst['pan'] - 64)) / 64.0; p = max(-1, min(1, p))
        lr = np.array([[math.cos((p + 1) * math.pi / 4)], [math.sin((p + 1) * math.pi / 4)]], np.float32) * math.sqrt(2)
        s0 = int(t * RATE)
        mix[:, s0:s0 + n] += voice * env * gain * lr
    peak = float(np.abs(mix).max())
    if peak <= 1e-4:
        raise ValueError(f'{name}: rendered silence')
    mix *= 0.7 / peak  # UI sounds are normalised together; the app sets their final level.
    nz = np.nonzero(np.abs(mix).max(0) > 4e-3)[0]
    mix = mix[:, :nz[-1] + RATE // 50] if len(nz) else mix
    # Looping sequence tails (stopped by the game itself) are capped for launcher use.
    cap = int(1.6 * RATE)
    if mix.shape[1] > cap:
        mix = mix[:, :cap].copy(); fade = int(0.15 * RATE); mix[:, -fade:] *= np.linspace(1, 0, fade, dtype=np.float32)
    return mix


def write_wav(path, pcm, rate):
    data = (np.clip(pcm.T, -1, 1) * 32767).astype('<i2')
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(pcm.shape[0]); w.setsampwidth(2); w.setframerate(rate); w.writeframes(data.tobytes())


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(disc):
    brsar = disc / 'sound/revo_kart.brsar'
    if not brsar.is_file():
        raise FileNotFoundError(f'Missing local input: {brsar} (extract your own RMCP01 dump first)')
    rsar = Rsar(brsar.read_bytes())
    ASSETS.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 1, 'kind': 'private-launcher-audio', 'source': 'owner Mario Kart Wii dump: sound/revo_kart.brsar',
                'sounds': {}}
    file_id, kind, _ = rsar.sound(MUSIC[1])
    if kind != 3:
        raise ValueError('Title music is not a wave sound')
    _, waves = rsar.file(file_id)
    pcm, rate, loop, ls, le = rwar_waves(waves)[0]
    pcm = pcm[:, :le + 1]
    target = ASSETS / 'music.wav'
    write_wav(target, pcm, rate)
    manifest['music'] = {'file': target.name, 'sha256': sha(target), 'source': MUSIC[1], 'rate': rate,
                         'channels': int(pcm.shape[0]), 'frames': int(pcm.shape[1]),
                         'loopStart': int(ls) if loop else 0, 'loopEnd': int(le)}
    print(f'music {MUSIC[1]}: {pcm.shape[1] / rate:.1f}s, loop {ls}..{le}')
    for role, name in SOUNDS.items():
        pcm = render_sequence(rsar, name)
        target = ASSETS / f'{role}.wav'
        write_wav(target, pcm, RATE)
        manifest['sounds'][role] = {'file': target.name, 'sha256': sha(target), 'source': name, 'frames': int(pcm.shape[1])}
        print(f'{role} <- {name}: {pcm.shape[1] / RATE * 1000:.0f} ms')
    (ASSETS / 'audio.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')


def find_loop(pcm, rate):
    """Repeat period of a soundtrack rip that plays its loop twice then fades: returns
    (loop_start, loop_end_inclusive) in frames, or None when no clean repeat exists."""
    mono = pcm.mean(0)
    n = len(mono)
    def ncc(a, b, length):
        u, v = mono[a:a + length], mono[b:b + length]
        return float((u * v).sum() / np.sqrt((u * u).sum() * (v * v).sum() + 1e-9))
    win, a, tail = 8 * rate, 2 * rate, 6 * rate
    coarse = [(ncc(a, a + L, win), L) for L in range(15 * rate, n - win - tail - a, rate // 100)]
    if not coarse:
        return None
    _, L0 = max(coarse)
    fine_win = min(20 * rate, n - L0 - tail - a)
    score, L = max((ncc(a, a + L, fine_win), L) for L in range(L0 - rate // 40, L0 + rate // 40))
    if score < 0.9:
        return None
    # Seam where the two passes agree most closely, a few seconds in (past any intro).
    lo, hi = min(8 * rate, n - L - tail), min(14 * rate, n - L - tail)
    _, start = min((float(np.abs(pcm[:, s:s + 64] - pcm[:, s + L:s + L + 64]).sum()), s) for s in range(lo, max(lo + 1, hi)))
    return start, start + L - 1


def custom_music(path):
    """Owner-supplied menu music (any format ffmpeg reads), looped seamlessly where possible."""
    if not path.is_file():
        raise FileNotFoundError(path)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:a:0', '-f', 's16le', '-ac', '2',
                          '-ar', str(RATE), '-'], check=True, capture_output=True).stdout
    pcm = np.frombuffer(raw, '<i2').reshape(-1, 2).T.astype(np.float32) / 32768
    loop = find_loop(pcm, RATE)
    if loop:
        start, end = loop
        pcm = pcm[:, :end + 1]
    else:
        start, end = 0, pcm.shape[1] - 1  # no clean repeat: plays through and restarts
    ASSETS.mkdir(parents=True, exist_ok=True)
    target = ASSETS / 'music.wav'
    write_wav(target, pcm, RATE)
    manifest_path = ASSETS / 'audio.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf8'))
    manifest['music'] = {'file': target.name, 'sha256': sha(target), 'source': 'owner-supplied file: ' + path.name,
                         'rate': RATE, 'channels': 2, 'frames': int(pcm.shape[1]), 'loopStart': int(start), 'loopEnd': int(end)}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
    print(f'music {path.name}: loop {start / RATE:.2f}s..{end / RATE:.2f}s' if loop else f'music {path.name}: no clean repeat; plays through')


def validate():
    manifest = ASSETS / 'audio.json'
    if not manifest.exists():
        return []  # The launcher runs silently without the optional private audio set.
    data = json.loads(manifest.read_text(encoding='utf8'))
    if data.get('schema') != 1 or data.get('kind') != 'private-launcher-audio' or set(data.get('sounds', {})) != set(SOUNDS):
        raise ValueError('Unexpected launcher audio manifest')
    files = [manifest]
    for entry in [data['music']] + list(data['sounds'].values()):
        path = ASSETS / entry['file']
        if path.is_symlink() or not path.is_file() or path.resolve().parent != ASSETS.resolve() or sha(path) != entry['sha256']:
            raise ValueError(f'Launcher audio identity mismatch: {entry["file"]}')
        files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--disc', type=Path, default=DEFAULT_DISC, help='extracted DiscData/files folder')
    parser.add_argument('--extract', action='store_true')
    parser.add_argument('--music', type=Path, help='use your own audio file (e.g. a Wii Menu soundtrack rip) as the menu music')
    parser.add_argument('--stage', action='store_true')
    parser.add_argument('--serial')
    args = parser.parse_args()
    if args.extract:
        extract(args.disc)
    if args.music:
        custom_music(args.music)
    files = validate()
    print(f'Validated {len(files)} optional private launcher audio files')
    if not args.stage:
        return
    if not files or not args.serial:
        parser.error('--stage needs extracted audio and --serial')
    target = 'files/WiiCompiled/ThorMap'
    archive = ROOT / 'build/thor-launcher-audio.tar'
    with tarfile.open(archive, 'w') as tar:
        for path in files:
            tar.add(path, arcname=path.relative_to(FOLDER).as_posix(), recursive=False)
        checks = ''.join(f'{sha(p)}  {p.relative_to(FOLDER).as_posix()}\n' for p in files).encode()
        entry = tarfile.TarInfo('.thor-launcher-audio.sha256'); entry.size = len(checks)
        tar.addfile(entry, io.BytesIO(checks))
    adb = [str(ROOT / '.tools/platform-tools/adb.exe'), '-s', args.serial]
    subprocess.run(adb + ['shell', 'run-as', 'com.driftdroid.android.thor', 'mkdir', '-p', target + '/launcher-audio'], check=True)
    with archive.open('rb') as stream:
        subprocess.run(adb + ['exec-in', 'run-as', 'com.driftdroid.android.thor', 'tar', '-xf', '-', '-C', target],
                       stdin=stream, check=True, timeout=120)
    subprocess.run(adb + ['shell', 'run-as', 'com.driftdroid.android.thor', 'sh', '-c',
                         f"'cd {target} && sha256sum -cs .thor-launcher-audio.sha256'"], check=True, timeout=120)
    print('Staged launcher audio; all device hashes match')


if __name__ == '__main__':
    main()
