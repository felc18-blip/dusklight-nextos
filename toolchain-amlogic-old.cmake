# CMake toolchain — cross-compile Dusklight pro NextOS-Elite Amlogic-old (Mali-450, armhf).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TC "/home/felipe/NextOS-Elite-Edition/build.NextOS-Retro-Elite-Edition-Amlogic-old.aarch64-4/toolchain")
set(SYSROOT "${TC}/armv8a-emuelec-linux-gnueabihf/sysroot")

set(CMAKE_C_COMPILER   "${TC}/bin/armv8a-emuelec-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TC}/bin/armv8a-emuelec-linux-gnueabihf-g++")

set(CMAKE_SYSROOT "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${SYSROOT}" "${TC}/armv8a-emuelec-linux-gnueabihf")

# Procura headers/libs no sysroot, programas no host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Mali-450 = ARMv8-A em modo 32-bit (Cortex-A53 do S905).
set(ARCH_FLAGS "-march=armv8-a+crc -mfpu=neon-fp-armv8 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT   "${ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS}")

# pkg-config aponta pro sysroot.
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT}")
