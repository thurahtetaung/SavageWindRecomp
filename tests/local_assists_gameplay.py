"""Opt-in sprint check on a disposable save in a safe area. Requires a local control endpoint."""
import argparse
import json
import socket
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', type=int, required=True)
parser.add_argument('--sprint-button', choices=('b', 'x'), default='b')
parser.add_argument('--output', type=Path, default=Path('local-test-results/stamina.json'))
args = parser.parse_args()

def command(line):
    with socket.create_connection(('127.0.0.1', args.port), timeout=5) as connection:
        connection.sendall((line + '\n').encode())
        response = b''
        while b'\n' not in response:
            part = connection.recv(65536)
            if not part:
                raise RuntimeError('Control endpoint closed before replying')
            response += part
    reply = json.loads(response.split(b'\n')[0])
    if not reply.get('ok'):
        raise RuntimeError(reply)
    return reply.get('data', {})

samples = {}
try:
    command('pos')  # Require a loaded world before sending input.
    for index, enabled in enumerate(('off', 'on', 'off')):
        command('release all')
        time.sleep(3)
        command('assist stamina ' + enabled)
        command('stick l 0 ' + ('-1' if index % 2 == 0 else '1') + ' 150')
        command('press ' + args.sprint_button + ' 150')
        values = []
        for _ in range(6):
            time.sleep(.3)
            values.append(command('pos')['stamina'])
        samples[f'{enabled}-{index}'] = values
        print(enabled, values, flush=True)
    assert min(samples['off-0']) < samples['off-0'][0] - 100, 'Baseline did not consume stamina'
    assert max(samples['on-1']) - min(samples['on-1']) < .01, 'Assist allowed stamina drain'
    assert samples['on-1'][0] > 0, 'On-state test started without stamina'
    assert min(samples['off-2']) < samples['off-2'][0] - 100, 'Disabling did not restore stamina drain'
    print('PASS: off/on/off sprint behavior')
finally:
    try:
        command('release all')
        command('assist stamina off')
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(samples, indent=2))
