"""THP movies: the player and THPAudioDecode() read the file header, the
per-frame headers and the audio record headers natively, so those become
little-endian.  Video components (JPEG byte streams) and ADPCM data are byte
oriented and stay as they are.

The file is copied and then patched in place, since movies are hundreds of
megabytes.
"""
import mmap
import shutil
import struct

AUDIO_HEADER_HALFWORDS = 16 + 16 + 4  # lCoef[8][2], rCoef[8][2], lYn1/2, rYn1/2


def cook_thp(src, dst):
    """Writes the little-endian movie to dst; returns the frame count."""
    with open(src, 'rb') as fh:
        head = fh.read(0x30)
    if head[:4] != b'THP\0' or head[4:8] != b'\x00\x01\x10\x00':
        raise ValueError('%s: not a version 1.1 THP file' % src)
    shutil.copyfile(src, dst)
    with open(dst, 'r+b') as fh:
        with mmap.mmap(fh.fileno(), 0) as mm:
            # Swap fields 4..15 (11 uint32s)
            for i in range(11):
                off = 4 + 4 * i
                v = struct.unpack_from('>I', mm, off)[0]
                struct.pack_into('<I', mm, off, v)

            fields = struct.unpack_from('<11I', mm, 4)
            (_version, _buf, _maxSamples, _rate, num_frames, first_size, _data_size,
             comp_off, _offsets_off, movie_off, _final_off) = fields

            ncomp = struct.unpack_from('>I', mm, comp_off)[0]
            struct.pack_into('<I', mm, comp_off, ncomp)

            kinds = list(mm[comp_off + 4:comp_off + 4 + ncomp])
            pos = comp_off + 0x14
            tracks = 1
            for kind in kinds:
                if kind == 0:    # video: xSize, ySize, videoType
                    for k in range(3):
                        v = struct.unpack_from('>I', mm, pos + 4 * k)[0]
                        struct.pack_into('<I', mm, pos + 4 * k, v)
                    pos += 12
                elif kind == 1:  # audio: channels, frequency, samples, tracks
                    for k in range(4):
                        v = struct.unpack_from('>I', mm, pos + 4 * k)[0]
                        struct.pack_into('<I', mm, pos + 4 * k, v)
                    tracks = struct.unpack_from('<I', mm, pos + 12)[0]
                    pos += 16
                else:
                    raise ValueError('%s: unknown THP component %d' % (src, kind))

            off, size = movie_off, first_size
            for _ in range(num_frames):
                if off + 8 + 4 * ncomp > len(mm):
                    break
                next_size = struct.unpack_from('>I', mm, off)[0]
                prev_size = struct.unpack_from('>I', mm, off + 4)[0]
                struct.pack_into('<II', mm, off, next_size, prev_size)

                comp_sizes = []
                for c in range(ncomp):
                    cs = struct.unpack_from('>I', mm, off + 8 + 4 * c)[0]
                    struct.pack_into('<I', mm, off + 8 + 4 * c, cs)
                    comp_sizes.append(cs)

                data = off + 8 + 4 * ncomp
                for kind, comp_size in zip(kinds, comp_sizes):
                    if kind == 1:
                        for t in range(tracks):
                            rec = data + comp_size * t
                            if rec + 8 + AUDIO_HEADER_HALFWORDS * 2 <= len(mm):
                                o1, o2 = struct.unpack_from('>II', mm, rec)
                                struct.pack_into('<II', mm, rec, o1, o2)
                                for h in range(AUDIO_HEADER_HALFWORDS):
                                    hw = struct.unpack_from('>H', mm, rec + 8 + 2 * h)[0]
                                    struct.pack_into('<H', mm, rec + 8 + 2 * h, hw)
                    data += comp_size
                off += size
                size = next_size
    return num_frames
