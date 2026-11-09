"""Pure-python Yaz0 + RARC + JMap(BCSV) readers (no DLL, read-only)."""
import struct


def yaz0(data):
    if data[:4] != b'Yaz0':
        return data
    size = struct.unpack_from('>I', data, 4)[0]
    out = bytearray(size)
    src = 16
    dst = 0
    while dst < size:
        code = data[src]
        src += 1
        for bit in range(8):
            if dst >= size:
                break
            if code & (0x80 >> bit):
                out[dst] = data[src]
                dst += 1
                src += 1
            else:
                b1 = data[src]
                b2 = data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8) | b2
                cs = dst - dist - 1
                n = b1 >> 4
                if n == 0:
                    n = data[src] + 0x12
                    src += 1
                else:
                    n += 2
                if cs + n <= dst:
                    out[dst:dst + n] = out[cs:cs + n]
                    dst += n
                else:
                    for _ in range(n):
                        out[dst] = out[cs]
                        dst += 1
                        cs += 1
    return bytes(out)


def rarc(data):
    data = yaz0(data)
    assert data[:4] == b'RARC', data[:4]
    data_off = struct.unpack_from('>I', data, 0x0C)[0] + 0x20
    nn, noff, ne, eoff, ssz, soff = struct.unpack_from('>IIIIII', data, 0x20)
    noff += 0x20
    eoff += 0x20
    soff += 0x20

    def cstr(o):
        e = data.index(b'\0', soff + o)
        return data[soff + o:e].decode('shift_jis', 'replace')

    nodes = []
    for i in range(nn):
        ident, nofs, h, n, first = struct.unpack_from('>IIHHI', data, noff + i * 0x10)
        nodes.append((cstr(nofs), n, first))
    out = {}

    def walk(ni, prefix):
        name, n, first = nodes[ni]
        for e in range(first, first + n):
            fid, h, ft, nofs, doff, dsz = struct.unpack_from('>HHHHII', data, eoff + e * 0x14)
            en = cstr(nofs)
            if (ft >> 8) & 2:
                if en in ('.', '..'):
                    continue
                walk(doff, prefix + en + '/')
            else:
                out[prefix + en] = data[data_off + doff:data_off + doff + dsz]

    walk(0, nodes[0][0] + '/')
    return out


def jhash(s):
    h = 0
    for c in s.encode('shift_jis'):
        if c >= 0x80:
            c -= 0x100
        h = (h * 31 + c) & 0xffffffff
    return h


KNOWN = """name l_id obj_arg0 obj_arg1 obj_arg2 obj_arg3 obj_arg4 obj_arg5 obj_arg6 obj_arg7
pos_x pos_y pos_z dir_x dir_y dir_z scale_x scale_y scale_z SW_APPEAR SW_DEAD SW_A SW_B
SW_SLEEP SW_AWAKE SW_PARAM CommonPath_ID FollowId ClippingGroupId GroupId DemoGroupId MapParts_ID
Obj_ID ChildObjId CameraSetId MessageId ShapeModelNo ViewGroupId ParentID Range Distance Priority
Inverse Power Gravity_type ParamScale LinkInfo ZoneName ScenarioNo ScenarioName PowerStarId
Comet LuigiModeTimer PowerStarType ZoneID MarioNo AppearPowerStarObj Camera_id CastId
ParentId AreaShapeNo RotateSpeed RotateAngle RotateAxis RotateAccelType RotateStopTime
RotateType ShadowType SignMotionType PressType MoveConditionType FarClip SoundLookAt ParentCategory
GeneratorID ClippingID Message_ID SW_APPEAR_TYPE DemoSkip GalaxyName Type Scale Count
ZoneId LayerA LayerB LayerC LayerD LayerE LayerF LayerG LayerH LayerI LayerJ LayerK
LayerL LayerM LayerN LayerO LayerP Common Pos_x Pos_y Pos_z no WorldNo PlanetName RangeA
EnableFarCulling BigDistance WaterLevel IsNormal IsWater IsFar StageName ObjName
PowerStar Hidden ID Hide IsHidden ErrorCheck IsRequestScenario PlanetCount ScenarioPlanetMax PathPointNo
point_arg0 point_arg1 point_arg2 point_arg3 point_arg4 point_arg5 point_arg6 point_arg7
pnt0_x pnt0_y pnt0_z pnt1_x pnt1_y pnt1_z pnt2_x pnt2_y pnt2_z closed num_pnt path_arg0 path_arg1
path_arg2 path_arg3 path_arg4 path_arg5 path_arg6 path_arg7 usage Group Flag StageType
LowFlag MiddleFlag HighFlag Data FileName Id type UniqueName FarClip GameFlag Name
ZoneSw ZoneSwitch""".split()
HNAMES = {jhash(n): n for n in KNOWN}


def bcsv(data):
    ne, nf, doff, esz = struct.unpack_from('>IIII', data, 0)
    fields = []
    for i in range(nf):
        h, mask, off, shift, typ = struct.unpack_from('>IIHBB', data, 16 + i * 12)
        fields.append((HNAMES.get(h, '%08X' % h), mask, off, shift, typ))
    strtab = doff + ne * esz
    rows = []
    for r in range(ne):
        base = doff + r * esz
        row = {}
        for name, mask, off, shift, typ in fields:
            if typ == 0:
                v = (struct.unpack_from('>I', data, base + off)[0] & mask) >> shift
                if v & 0x80000000:
                    v -= 1 << 32
            elif typ == 1:
                v = data[base + off:base + off + 32].split(b'\0')[0].decode('shift_jis', 'replace')
            elif typ == 2:
                v = struct.unpack_from('>f', data, base + off)[0]
            elif typ == 3:
                v = (struct.unpack_from('>I', data, base + off)[0] & mask) >> shift
            elif typ == 4:
                v = (struct.unpack_from('>H', data, base + off)[0] & mask) >> shift
                if v & 0x8000:
                    v -= 0x10000
            elif typ == 5:
                v = (data[base + off] & mask) >> shift
            elif typ == 6:
                so = struct.unpack_from('>I', data, base + off)[0]
                e = data.index(b'\0', strtab + so)
                v = data[strtab + so:e].decode('shift_jis', 'replace')
            else:
                v = None
            row[name] = v
        rows.append(row)
    return [f[0] for f in fields], rows
