export ARCH=arm
export CROSS_COMPILE=arm-linux-androideabi-
export PATH=$PWD/arm-linux-androideabi-4.9/bin:$PATH 
make ARCH=arm O=out CROSS_COMPILE=arm-linux-androideabi- mt8167s_ref_debug_defconfig
make ARCH=arm O=out CROSS_COMPILE=arm-linux-androideabi- KCFLAGS=-mno-android -j32
