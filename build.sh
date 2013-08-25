#!/bin/bash
make distclean
export CROSS_COMPILE=/opt/toolchains/arm-eabi-4.4.3/bin/arm-eabi-
export ARCH=arm

# sed -i s/CONFIG_LOCALVERSION=\".*\"/CONFIG_LOCALVERSION=\"-interloper-${1}\"/ .config
# make gogh_extracted_defconfig
make c5155_extracted_defconfig
make modules
make -j16 zImage 2>&1 | tee ~/logs/c5155_rise.txt

[ -d modules ] || mkdir -p modules
find -name '*.ko' -exec cp -av {} modules/ \;
geany ~/logs/c5155_rise.txt
exit 1


