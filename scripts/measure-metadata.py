"""Measure the synthetic metadata test's Windows process peak without fixtures."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('counts', nargs='*', type=int, default=[100000, 1000000])
args = parser.parse_args()
if os.name != 'nt':
    parser.error('This process-memory measurement wrapper supports Windows; run the test executable directly on other systems')
if any(count <= 0 for count in args.counts):
    parser.error('counts must be positive')

class MemoryCounters(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            'PeakWorkingSetSize', 'WorkingSetSize', 'QuotaPeakPagedPoolUsage',
            'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage', 'QuotaNonPagedPoolUsage',
            'PagefileUsage', 'PeakPagefileUsage')]

get_memory = ctypes.WinDLL('psapi').GetProcessMemoryInfo
get_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
results = []
for count in args.counts:
    with subprocess.Popen([str(args.executable.resolve()), str(count)], stdout=subprocess.PIPE,
                          text=True, encoding='utf-8', creationflags=subprocess.CREATE_NO_WINDOW) as process:
        output = process.communicate()[0].strip()
        if process.returncode:
            raise SystemExit(f'metadata test failed ({process.returncode}): {output}')
        counters = MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        # The process handle remains valid after termination; Windows retains
        # its peak counters until the last handle closes.
        if not get_memory(wintypes.HANDLE(int(process._handle)), ctypes.byref(counters), counters.cb):
            raise ctypes.WinError()
        results.append({'entries': count, 'peak_working_set_bytes': counters.PeakWorkingSetSize,
                        'peak_commit_bytes': counters.PeakPagefileUsage, 'output': output})
print(json.dumps(results, indent=2))
