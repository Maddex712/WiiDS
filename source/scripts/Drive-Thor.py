"""Send one bounded command to the separate on-device instrumentation driver."""
import argparse
import socket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('command', nargs='+', help='state, probe, probestage 0/1, heldprobe keycode, mapstats [reset], lower on/off, input durationMs stickX stickY [Android button codes], or stop')
args = parser.parse_args()
with socket.create_connection(('127.0.0.1', 32991), timeout=35) as connection:
    connection.sendall((' '.join(args.command) + '\n').encode())
    with connection.makefile('r') as stream:
        response = stream.readline()
        if not response:
            raise SystemExit('Driver disconnected before replying; inspect the game process and runtime log.')
        print(response.strip())
