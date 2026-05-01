#
# Copyright (c) 2026 Realtek
# SPDX-License-Identifier: Apache-2.0
#

set(SUPPORTED_EMU_PLATFORMS qemu)
set(QEMU_ARCH aarch64)

set(QEMU_CPU_TYPE_${ARCH} cortex-a53,aarch64=off)
set(QEMU_MACH virt,gic-version=3)

if(CONFIG_ENTROPY_VIRTIO)
  set(QEMU_VIRTIO_ENTROPY_FLAGS -device virtio-rng-device,bus=virtio-mmio-bus.0)
endif()

if(CONFIG_BUILD_WITH_TFA)
  set(QEMU_CPU_TYPE_${ARCH} cortex-a53)
  set(QEMU_MACH virt,secure=on,gic-version=3)

  set(TFA_PLAT "qemu")
  set(TFA_EXTRA_ARGS
    "QEMU_USE_GIC_DRIVER=QEMU_GICV3;QEMU_BL33_IN_AARCH32=1;QEMU_BL33_LOAD_BASE=${CONFIG_SRAM_BASE_ADDRESS}")

  if(CONFIG_TFA_MAKE_BUILD_TYPE_DEBUG)
    set(TFA_BUILD_FOLDER "debug")
  else()
    set(TFA_BUILD_FOLDER "release")
  endif()

  set(QEMU_KERNEL_OPTION
    -bios ${CMAKE_BINARY_DIR}/tfa/qemu/${TFA_BUILD_FOLDER}/qemu_fw.bios
    )
endif()

set(QEMU_FLAGS_${ARCH}
  -global virtio-mmio.force-legacy=false
  -cpu ${QEMU_CPU_TYPE_${ARCH}}
  ${QEMU_VIRTIO_ENTROPY_FLAGS}
  -nographic
  -machine ${QEMU_MACH}
  )

include(${ZEPHYR_BASE}/boards/common/qemu.board.cmake)
