"""Catch dependencies previously found to prevent loading on Windows XP."""
import re
import subprocess
import sys

objdump, executable = sys.argv[1:]
output = subprocess.check_output([objdump, '-p', executable], text=True)
dlls = {name.lower() for name in re.findall(r'DLL Name: (\S+)', output)}
allowed = {'ADVAPI32.dll', 'COMDLG32.dll', 'COMCTL32.dll', 'CRYPT32.dll', 'GDI32.dll',
           'KERNEL32.dll', 'MSIMG32.dll', 'msvcrt.dll', 'SHELL32.dll', 'USER32.dll', 'WS2_32.dll'}
allowed = {name.lower() for name in allowed}
assert dlls <= allowed, f'Unexpected dependencies: {dlls - allowed}'
for function in ('_vsnprintf_s', 'BCryptGenRandom', 'GetTickCount64',
                 'InitializeCriticalSectionEx', 'GetSystemTimePreciseAsFileTime'):
    assert not re.search(r'\b' + re.escape(function) + r'\b', output), function
assert 'file format pei-i386' in output
assert re.search(r'MajorSubsystemVersion\s+5', output)
assert re.search(r'MinorSubsystemVersion\s+1', output)
print('XP import regression checks passed (runtime validation still required)')
