#!/usr/bin/env python3
# sample_lr.py <elf> <function> [samples] — who calls <function>? Halts core 0
# briefly (openocd TCL), reads PC and LR, resumes; for samples inside the
# function, tallies the callers. Halting perturbs timing a little; fine for
# finding callers, not for percentages.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
import collections, os, socket, subprocess, sys, time

elf, func = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 300
probe = os.environ.get('PROBE', 'E6616407E335BB29')
khz = os.environ.get('SWD_KHZ', '1000')

nm = subprocess.run(['arm-none-eabi-nm', '-S', elf], capture_output=True, text=True).stdout
start = size = None
for l in nm.splitlines():
    p = l.split()
    if len(p) == 4 and p[3] == func:
        start, size = int(p[0], 16), int(p[1], 16)
if start is None:
    sys.exit(f'{func} not found')

ocd = subprocess.Popen(['openocd', '-f', 'interface/cmsis-dap.cfg', '-f', 'target/rp2350.cfg',
                        '-c', f'adapter serial {probe}', '-c', f'adapter speed {khz}',
                        '-c', 'tcl_port 6666', '-c', 'init'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
s = socket.create_connection(('127.0.0.1', 6666))

def cmd(c):
    s.sendall(c.encode() + b'\x1a')
    buf = b''
    while not buf.endswith(b'\x1a'):
        buf += s.recv(4096)
    return buf[:-1].decode()

cmd('targets rp2350.cm0')
callers = collections.Counter()
inside = 0
for i in range(n):
    cmd('halt')
    pc = int(cmd('reg pc').split()[-1], 16)
    lr = int(cmd('reg lr').split()[-1], 16)
    cmd('resume')
    if start <= pc < start + size:
        inside += 1
        callers[lr & ~1] += 1
    time.sleep(0.02)
s.close()
ocd.terminate()
print(f'{inside} of {n} samples in {func}')
for lr, k in callers.most_common(10):
    f = subprocess.run(['arm-none-eabi-addr2line', '-f', '-C', '-e', elf, hex(lr - 2)],
                       capture_output=True, text=True).stdout.split('\n')
    print(f'{k:4d}  {f[0]}  {f[1].split("/")[-1]}')
