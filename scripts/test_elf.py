#!/usr/bin/env python3
"""Test that run test.elf works in EponaOS via QEMU monitor keystrokes."""
import subprocess, socket, time, os, sys, tempfile, threading

BUILD = os.path.join(os.path.dirname(__file__), '..', 'build')
IMG = os.path.join(BUILD, 'eponaos.img')
DATA_IMG = os.path.join(BUILD, 'data.img')
MON_SOCK = '/tmp/epona-test-mon.sock'
SERIAL_LOG = '/tmp/epona-test-serial.txt'

# Clean up any leftover files
for f in [MON_SOCK, SERIAL_LOG]:
    try: os.unlink(f)
    except: pass

# Start QEMU
qemu = subprocess.Popen([
    'qemu-system-x86_64',
    '-drive', f'format=raw,file={IMG}',
    '-drive', f'format=raw,file={DATA_IMG}',
    '-serial', f'file:{SERIAL_LOG}',
    '-monitor', f'unix:{MON_SOCK},server,nowait',
    '-display', 'none',
    '-no-reboot',
    '-m', '128M',
])

# Wait for boot to complete
def wait_for_serial(text, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(SERIAL_LOG) as f:
                if text in f.read():
                    return True
        except: pass
        time.sleep(0.1)
    return False

print("Waiting for boot...")
if not wait_for_serial("multitarefa ativa", 20):
    print("FAIL: Boot timeout")
    qemu.kill()
    sys.exit(1)
print("Boot OK")

# Connect to monitor
def monitor_cmd(cmd):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(5)
    s.connect(MON_SOCK)
    s.sendall((cmd + '\n').encode())
    resp = b''
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            resp += chunk
    except socket.timeout: pass
    s.close()
    return resp.decode(errors='replace')

# Send keys to type "run test.elf" and press Enter
def send_key(ch):
    """Send a single key via QEMU monitor."""
    key_map = {
        '\n': 'ret', ' ': 'spc', '.': 'dot',
        'r': 'r', 'u': 'u', 'n': 'n', 't': 't', 'e': 'e', 's': 's',
        'l': 'l', 'f': 'f',
    }
    k = key_map.get(ch)
    if k:
        monitor_cmd(f'sendkey {k}')
        time.sleep(0.05)

print("Sending keys: 'run test.elf'")
for ch in "run test.elf\n":
    send_key(ch)
    time.sleep(0.03)

print("Waiting for test.elf output...")
if wait_for_serial("Hello from C in ring 3", 10):
    print("SUCCESS: test.elf ran and produced output!")
    with open(SERIAL_LOG) as f:
        log = f.read()
        # Print relevant lines
        for line in log.split('\n'):
            if any(x in line for x in ['Hello from', 'argc', 'argv', 'Content', 'ring 3', 'Reading', 'syscall exit']):
                print(f"  {line}")
else:
    print("FAIL: Did not see test.elf output")
    with open(SERIAL_LOG) as f:
        print("Serial log (last 30 lines):")
        lines = f.read().split('\n')
        for l in lines[-30:]:
            print(f"  {l}")

qemu.kill()
time.sleep(0.5)
for f in [MON_SOCK, SERIAL_LOG]:
    try: os.unlink(f)
    except: pass
