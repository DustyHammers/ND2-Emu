#!/usr/bin/env python3
"""
Program files and SysEx (ProgDump protocol) of the original drum module, reconstructed from
the original editor software and cross-checked against OS v3.00.

.nd2_bank  = zip of 50 files "Program NNN.nd2p" (stored, no compression)
.nd2p      = 201 bytes "CBIN" container:
    0x00 'CBIN'   0x04 u32 0   0x08 'nd2p'   0x0C u16 bank   0x0E u16 program
    0x10 u32 id/timestamp      0x14 u32 format version (3)
    0x18 173-byte program payload (5 global bytes + 6 x 28-byte channel blocks, bit-packed)
    0xC5 u16le CRC-16/CCITT(init 0xFFFF) of the payload
    0xC7 u16le CRC-16/CCITT(init 0xFFFF) of bytes 0x00..0xC6
  The 175 bytes at 0x18..0xC6 (payload + payload CRC) are what goes over MIDI.

SysEx frame (both directions):
    F0 33 <7F> <dev> <protocol> <version> <msg> <header bytes...> <7-bit data...> <chk> F7
    dev      : host -> drum 0x7F ; drum replies with 0x19 (the drum's model id)
    protocol : 7 = Ctrl (version/boot/serial), 8 = ProgDump ; ProgDump version 3
    data     : continuous MSB-first bitstream packed 7 bits per byte
    chk      : (CRC-16/CCITT(init 0xFFFF) over every byte from F0 up to chk) >> 9, & 0x7F
ProgDump messages:
    0 ReqSendCC  1 QryPatch_EditBuffer  2 DtaPatch_EditBuffer  3 QryPatch_File  4 DtaPatch_File
    5 QryPerf_EditBuffer  6 DtaPerf_EditBuffer  7 QryPerf_File  8 DtaPerf_File  9 AckPerf_File
    QryPerf_File (7): [0x40|bank, program]
    DtaPerf_File (8): [0x40|bank, program, format(3)] + 7-bit(175-byte program)
    AckPerf_File (9): 3 bytes back from the drum
The original editor waits for each reply/ack before sending the next program.

usage:
  nd2_sysex.py info  file.nd2_bank|file.nd2p
  nd2_sysex.py tosyx file.nd2_bank --bank N out.syx      (N = 1..8, target bank on the drum)
  nd2_sysex.py query --bank N out.syx                     (50 QryPerf_File requests = "Get Bank")
  nd2_sysex.py parse in.syx                               (decode/verify any ND2 SysEx stream)
"""
import sys, struct, zipfile, argparse, io

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def pack7(data):
    bits = ''.join(f'{b:08b}' for b in data)
    bits += '0' * (-len(bits) % 7)
    return bytes(int(bits[i:i+7], 2) for i in range(0, len(bits), 7))

def unpack7(data, nbytes):
    bits = ''.join(f'{b & 0x7F:07b}' for b in data)
    return bytes(int(bits[i*8:i*8+8], 2) for i in range(nbytes))

def frame(proto, ver, msg, hdr=b'', data=b'', dev=0x7F):
    body = bytes([0xF0, 0x33, 0x7F, dev, proto, ver, msg]) + bytes(h & 0x7F for h in hdr) + pack7(data)
    return body + bytes([(crc16(body) >> 9) & 0x7F, 0xF7])

class Program:
    def __init__(self, raw, name=''):
        assert len(raw) == 201 and raw[:4] == b'CBIN' and raw[8:12] == b'nd2p', 'not an .nd2p'
        self.name = name
        self.bank, self.prog, self.ident, self.fmt = struct.unpack_from('<HHII', raw, 0x0C)
        self.payload = raw[0x18:0xC5]
        self.body = raw[0x18:0xC7]                       # 175 bytes sent over MIDI
        self.crc_ok = (struct.unpack_from('<H', raw, 0xC5)[0] == crc16(self.payload) and
                       struct.unpack_from('<H', raw, 0xC7)[0] == crc16(raw[:0xC7]))
    def sysex(self, bank, prog=None):
        p = self.prog if prog is None else prog
        return frame(8, 3, 8, [0x40 | bank, p, self.fmt], self.body)

def load(path):
    if path.endswith('.nd2p'):
        return [Program(open(path, 'rb').read(), path)]
    z = zipfile.ZipFile(path)
    return [Program(z.read(n), n) for n in sorted(z.namelist())]

def parse(stream):
    out, i = [], 0
    while True:
        s = stream.find(b'\xF0', i)
        if s < 0: break
        e = stream.find(b'\xF7', s); m = stream[s:e+1]; i = e + 1
        ok = len(m) >= 9 and m[1] == 0x33 and ((crc16(m[:-2]) >> 9) & 0x7F) == m[-2]
        d = dict(dev=m[3], proto=m[4], ver=m[5], msg=m[6], chk_ok=ok, raw=m)
        if m[4] == 8 and m[6] == 8:
            d.update(bank=m[7] & 0x3F, prog=m[8], fmt=m[9])
            body = unpack7(m[10:-2], 175)
            d['payload_crc_ok'] = struct.unpack_from('<H', body, 173)[0] == crc16(body[:173])
            d['body'] = body
        elif m[4] == 8 and m[6] in (7, 9):
            d['args'] = list(m[7:-2])
        out.append(d)
    return out

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('cmd'); ap.add_argument('inp', nargs='?'); ap.add_argument('out', nargs='?')
    ap.add_argument('--bank', type=int, default=1)
    a = ap.parse_args()
    if a.cmd == 'info':
        for p in load(a.inp):
            print(f'{p.name:22s} bank {p.bank} prog {p.prog:2d} fmt {p.fmt} crc {"ok" if p.crc_ok else "BAD"}')
    elif a.cmd == 'tosyx':
        progs = load(a.inp); b = a.bank - 1
        blob = b''.join(p.sysex(b) for p in progs)
        open(a.out, 'wb').write(blob); print(f'{len(progs)} programs -> bank {a.bank}, {len(blob)} bytes')
    elif a.cmd == 'query':
        blob = b''.join(frame(8, 3, 7, [0x40 | (a.bank - 1), p]) for p in range(50))
        open(a.inp, 'wb').write(blob); print('wrote', a.inp)
    elif a.cmd == 'parse':
        for d in parse(open(a.inp, 'rb').read()):
            extra = {k: v for k, v in d.items() if k not in ('raw', 'body')}
            print(extra)
