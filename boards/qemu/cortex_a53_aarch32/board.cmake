#
# Copyright (c) 2026 Realtek
# SPDX-License-Identifier: Apache-2.0
#

set(SUPPORTED_EMU_PLATFORMS qemu)
set(QEMU_ARCH aarch64)

set(QEMU_CPU_TYPE_${ARCH} cortex-a53,aarch64=off)
set(QEMU_MACH virt,gic-version=3)

set(QEMU_FLAGS_${ARCH}
  -cpu ${QEMU_CPU_TYPE_${ARCH}}
  -machine ${QEMU_MACH}
  )

include(${ZEPHYR_BASE}/boards/common/qemu.board.cmake)
