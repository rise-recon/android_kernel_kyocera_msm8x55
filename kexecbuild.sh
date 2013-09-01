#/bin/bash
make clean
make risekexec_defconfig
make zImage -j4 CONFIG_XIP_KERNEL=n
