# Retrieve the native architecture of the target.

import sys

sys_mpy = getattr(sys.implementation, "_mpy", 0)
arch = [
    None,
    "x86",
    "x64",
    "armv6",
    "armv6m",
    "armv7m",
    "armv7em",
    "armv7emsp",
    "armv7emdp",
    "xtensa",
    "xtensawin",
    "rv32imc",
][sys_mpy >> 10]
print(arch)
