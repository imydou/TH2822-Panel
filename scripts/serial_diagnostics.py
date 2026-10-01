#!/usr/bin/env python3
"""Bounded UART monitor; only connects to the explicitly selected WCH bridge.
No flash or deliberate reset pulse. Opening the serial bridge can reset the board.
Optional commands are local firmware diagnostics only.
"""
import argparse
import time
import serial
from serial.tools import list_ports

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--seconds', type=int, default=35)
parser.add_argument('--output', required=True)
parser.add_argument('--locale', choices=['en','zh-CN'])
args = parser.parse_args()
ports = [p for p in list_ports.comports() if p.device == args.port]
if len(ports) != 1 or (ports[0].vid, ports[0].pid) != (0x1A86, 0x55D3):
    raise SystemExit('Refusing unverified serial port: expected WCH 1a86:55d3')
port = serial.Serial()
port.port = args.port
port.baudrate = 115200
port.timeout = 0.15
port.dtr = False
port.rts = False
port.open()
start = time.monotonic()
schedule = [(2, b'state\n'), (10, b'state\n')]
if args.locale:
    schedule += [(6, ('locale '+args.locale+'\n').encode()), (10,b'state\n')]
schedule.sort(key=lambda item:item[0])
with open(args.output, 'wb') as log:
    while time.monotonic() - start < args.seconds:
        elapsed = time.monotonic() - start
        while schedule and elapsed >= schedule[0][0]:
            _, command = schedule.pop(0)
            port.write(command)
        data = port.read(8192)
        if data:
            log.write(data)
            log.flush()
            print(data.decode('utf-8', 'replace'), end='', flush=True)
port.close()
