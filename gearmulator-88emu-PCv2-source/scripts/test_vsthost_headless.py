"""Extract and execute VSTHost's original MIDI functions, without MFC or GUI.

The generated functions remain covered by vsthost-main's LGPL license.
This is a source-path test, NOT the newer VSTHost binary's VST3 implementation.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import os
import re
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_smf(path):
    data = path.read_bytes()
    assert data[:4] == b'MThd'
    size, fmt, tracks, ppq = struct.unpack('>IHHH', data[4:14])
    assert ppq < 32768, 'SMPTE division not supported'
    pos, result = 8 + size, []
    for track in range(tracks):
        assert data[pos:pos + 4] == b'MTrk'
        end = pos + 8 + int.from_bytes(data[pos + 4:pos + 8], 'big')
        pos += 8
        tick = running = port = 0

        def vlq():
            nonlocal pos
            value = 0
            while True:
                b = data[pos]
                pos += 1
                value = (value << 7) | (b & 127)
                if not b & 128:
                    return value

        while pos < end:
            tick += vlq()
            status = data[pos]
            if status & 128:
                pos += 1
                if status < 240:
                    running = status
            else:
                status = running
                assert status
            if status == 255:
                kind = data[pos]
                pos += 1
                length = vlq()
                payload = data[pos:pos + length]
                pos += length
                if kind == 33:
                    port = payload[0]
                if kind == 81:
                    result.append((tick, track, port, 'tempo', payload))
            elif status in (240, 247):
                length = vlq()
                payload = data[pos:pos + length]
                pos += length
                # Fail explicitly rather than silently corrupt split/escaped SysEx.
                assert status == 240 and payload[-1] == 247, 'split/escaped SysEx unsupported'
                result.append((tick, track, port, 'sysex', bytes([240]) + payload))
            else:
                length = 1 if status & 240 in (192, 208) else 2
                payload = bytes([status]) + data[pos:pos + length]
                pos += length
                result.append((tick, track, port, 'pc' if status & 240 == 192 else 'midi', payload))
    tempo, previous, usec = 500000, 0, 0.0
    events = []
    for tick, track, port, kind, payload in sorted(result, key=lambda e: (e[0], e[1])):
        usec += (tick - previous) * tempo / ppq
        previous = tick
        if kind == 'tempo':
            tempo = int.from_bytes(payload, 'big')
        else:
            events.append((int(usec / 1000), port, payload))
    return events


def extract(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('midi', type=pathlib.Path)
    parser.add_argument('--vcvars', type=pathlib.Path, required=True)
    parser.add_argument('--vst3-host', type=pathlib.Path)
    parser.add_argument('--plugin', type=pathlib.Path)
    parser.add_argument('--trace', type=pathlib.Path)
    parser.add_argument('--data-folder', type=pathlib.Path)
    parser.add_argument('--observed-log', type=pathlib.Path, help='Replay PC/SysEx bytes from the last real host session')
    args = parser.parse_args()
    out = ROOT / 'temp/vsthost-headless'
    out.mkdir(parents=True, exist_ok=True)
    events = read_smf(args.midi)
    fixture = out / 'input.txt'
    fixture.write_text(''.join(f'{ms} {port} {payload.hex()}\n' for ms, port, payload in events))
    original = (ROOT / 'vsthost-main/SmpVSTHost.cpp').read_text(encoding='latin1')
    functions = '\n\n'.join(extract(original, name) for name in (
        'void CSmpVSTHost::OnMidiIn', 'BYTE * CSmpVSTHost::CreateMIDIEvents',
        'BYTE * CSmpVSTHost::CreateMIDISubset'))
    (out / 'original_midi_functions.h').write_text(functions, encoding='latin1')
    cpp = ROOT / 'source/ronaldo/88emu/88emuplayer/tests/VstHostSource_test.cpp'
    bat = out / 'build.cmd'
    bat.write_text(f'@echo off\ncall "{args.vcvars}" x86\nif errorlevel 1 exit /b 1\n'
                   f'cl /nologo /EHsc /std:c++17 /O2 /I"{out}" "{cpp}" /Fe:"{out / "source_test.exe"}" /Fo:"{out / "source_test.obj"}"\n')
    subprocess.run(['cmd', '/c', str(bat)], cwd=out, check=True)
    run = subprocess.run([str(out / 'source_test.exe'), str(fixture), str(out / 'accepted.txt')],
                         check=True, capture_output=True, text=True)
    (out / 'source-result.txt').write_text(run.stdout)
    print(run.stdout)
    report = {'source_sha256': hashlib.sha256(original.encode('latin1')).hexdigest(),
              'midi_sha256': hashlib.sha256(args.midi.read_bytes()).hexdigest(),
              'events': len(events), 'pc': sum(p[0] & 240 == 192 for _, _, p in events),
              'sysex': sum(p[0] == 240 for _, _, p in events),
              'ports': dict(collections.Counter(port for _, port, _ in events)),
              'scope': 'Original three VST2 MIDI functions; CMidiMsg storage shim; x86. No VST3 code exists in supplied host source.'}
    (out / 'source-report.json').write_text(json.dumps(report, indent=2))
    if args.vst3_host:
        assert args.plugin and args.trace and args.data_folder, 'VST3 test requires plugin, trace, data-folder'
        # Synthetic coverage supplements the real song, which uses only ports A/B.
        four_ports = out / 'four-ports.txt'
        four_ports.write_text(''.join(f'{port} {192 + channel:02x}{(127 if channel == 1 else port * 16 + channel):02x}\n'
                                     for port in range(4) for channel in range(16)) +
                              ''.join(f'{port} f04110421240007f0041f7\n' for port in range(4)))
        results = {}
        cases = [('song', out / 'accepted.txt', 'mapped'), ('four_ports', four_ports, 'mapped'),
                                     ('song_raw', out / 'accepted.txt', 'raw'), ('four_ports_raw', four_ports, 'raw'),
                                     ('four_ports_both', four_ports, 'both')]
        controls = out / 'controls.txt'
        controls.write_text(four_ports.read_text() + ''.join(
            f'{port} {176 + channel:02x}{cc:02x}{value:02x}\n'
            for port in range(4) for channel in range(16)
            for cc, value in [(0, 5), (32, 3), (7, 100), (11, 64), (10, 127), (64, 0),
                              (98, 12), (99, 1), (6, 64), (91, 40), (93, 20)]) +
                            ''.join(f'{port} {224 + channel:02x}0040\n' for port in range(4) for channel in range(16)))
        cases += [('song_standard', out / 'accepted.txt', 'standard'),
                  ('four_ports_standard', four_ports, 'standard'),
                  ('four_ports_multipoint', four_ports, 'multipoint'),
                  ('controls_detached', controls, 'detached')]
        if args.observed_log:
            session = args.observed_log.read_text().rsplit('TRACE_START', 1)[-1]
            observed = out / 'observed-host.txt'
            messages = []
            for port, payload in re.findall(r'RAW_DATA bus=(\d+) subtype=0 size=\d+ bytes=([^\r\n]*)', session):
                data = payload.replace(' ', '')
                if data.startswith('c'):
                    messages.append(f'{port} {data[:4]}\n')
                elif data.startswith('f0'):
                    messages.append(f'{port} {data}\n')
            assert messages, 'No observed PC/SysEx in last session'
            observed.write_text(''.join(messages))
            cases.append(('observed_host_raw', observed, 'raw'))
        for name, accepted, mode in cases:
            start = args.trace.stat().st_size if args.trace.exists() else 0
            env = dict(os.environ, TUS_DATA_FOLDER=str(args.data_folder.resolve()), EMU88_TEST_TRACE_MARKERS=str(args.trace.resolve()))
            run = subprocess.run([str(args.vst3_host.resolve()), str(args.plugin.resolve()), str(accepted), mode],
                                 env=env, capture_output=True, text=True)
            (out / f'{name}-vst3-result.txt').write_text(run.stdout + run.stderr)
            run.check_returncode()
            with args.trace.open('rb') as stream:
                stream.seek(start)
                trace = stream.read().decode()
            (out / f'{name}-received.log').write_text(trace)
            trace = trace.split('TEST_FIXTURE_BEGIN', 1)[1].split('TEST_FIXTURE_END', 1)[0]
            expected = collections.Counter(tuple(line.split()) for line in accepted.read_text().splitlines())
            if mode == 'multipoint':
                for (port, data), n in list(expected.items()):
                    if data.startswith('c'):
                        expected[(port, data[:2] + f'{(int(data[2:4],16) + 1) % 128:02x}')] += n
            actual = collections.Counter()
            for match in re.finditer(r'DELIVER source=3 port=(\d+) offset=\d+ midi=([0-9a-f,]+) sysex=([^\r\n]*)', trace):
                port, short, sysex = match.groups()
                values = short.split(',')
                payload = sysex.replace(' ', '') if sysex else ''.join(values[:2] if values[0][0] in ('c','d') else values)
                actual[(port, payload)] += 1
            assert actual == expected, f'{name}: missing={expected - actual}; unexpected={actual - expected}'
            if mode == 'multipoint':
                assert len(re.findall(r'DELIVER source=3 port=\d+ offset=128 midi=c', trace)) == 64, 'second PC parameter points lost offsets'
            if mode == 'detached':
                assert 'controllerLinked=0' in trace, 'test must actually detach the processing component controller'
            results[name] = {'received': sum(actual.values()), 'byte_and_port_match': True,
                             'pc': sum(n for (_, data), n in actual.items() if data.startswith('c')),
                             'sysex': sum(n for (_, data), n in actual.items() if data.startswith('f0'))}
            print(f'PASS {name}: {results[name]}')
        report['vst3_test_adapter'] = results
        report['plugin_sha256'] = hashlib.sha256(args.plugin.read_bytes()).hexdigest()
        (out / 'report.json').write_text(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
