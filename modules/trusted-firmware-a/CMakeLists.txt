# Copyright (c) 2022 Arm Limited (or its affiliates). All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BUILD_WITH_TFA)
  set(TFA_BINARY_DIR ${CMAKE_BINARY_DIR}/tfa)
  find_program(TFA_MAKE_PROGRAM NAMES gmake make REQUIRED)

  file(MAKE_DIRECTORY ${TFA_BINARY_DIR})

  include(ExternalProject)

  set(tfa_image_info MAP "name: tfa, source-dir: ${ZEPHYR_TRUSTED_FIRMWARE_A_MODULE_DIR}")
  build_info(images VALUE ${tfa_image_info})

  ExternalProject_Add(
    tfa
    SOURCE_DIR ${ZEPHYR_TRUSTED_FIRMWARE_A_MODULE_DIR}
    BINARY_DIR ${TFA_BINARY_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_ALWAYS True
    USES_TERMINAL_BUILD True
  )

  if(CONFIG_TFA_MAKE_BUILD_TYPE_DEBUG)
    set(TFA_BUILD_DEBUG "1")
  else()
    set(TFA_BUILD_DEBUG "0")
  endif()

  # ARMv9-A is AArch64-only, disable AArch32 register context
  if(CONFIG_ARMV9_A)
    set(TFA_AARCH32_REGS "CTX_INCLUDE_AARCH32_REGS=0")
  else()
    set(TFA_AARCH32_REGS "")
  endif()

  # Set default TF-A build parameters that can be overridden by boards
  if(NOT DEFINED TFA_EXTRA_ARGS)
    set(TFA_EXTRA_ARGS "")
  endif()

  set(TFA_CROSS_COMPILE "" CACHE STRING
      "Toolchain prefix for building TF-A independently from the Zephyr image")
  set(TFA_OPENSSL_DIR "" CACHE PATH
      "OpenSSL prefix used when building TF-A host tools such as fiptool")

  if(TFA_CROSS_COMPILE STREQUAL "")
    if("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "zephyr")
      set(TFA_CROSS_COMPILE
          "${ZEPHYR_SDK_INSTALL_DIR}/gnu/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-")
    else()
      message(FATAL_ERROR
              "CONFIG_BUILD_WITH_TFA requires TFA_CROSS_COMPILE to point to an AArch64 TF-A "
              "toolchain prefix")
    endif()
  endif()

  set(TFA_CROSS_COMPILE_FOR_MAKE "${TFA_CROSS_COMPILE}")
  set(TFA_MAKE_ENV_ARGS "")
  set(TFA_OPTIONAL_ARGS "")

  if(TFA_CROSS_COMPILE MATCHES "/")
    get_filename_component(TFA_TOOLCHAIN_BIN_DIR "${TFA_CROSS_COMPILE}" DIRECTORY)
    get_filename_component(TFA_CROSS_COMPILE_FOR_MAKE "${TFA_CROSS_COMPILE}" NAME)
    list(APPEND TFA_MAKE_ENV_ARGS "PATH=${TFA_TOOLCHAIN_BIN_DIR}:$ENV{PATH}")
  endif()

  if(NOT TFA_OPENSSL_DIR STREQUAL "")
    list(APPEND TFA_OPTIONAL_ARGS "OPENSSL_DIR=${TFA_OPENSSL_DIR}")
  endif()

  set(TFA_CC "${TFA_CROSS_COMPILE_FOR_MAKE}gcc")
  set(TFA_CPP "${TFA_CROSS_COMPILE_FOR_MAKE}gcc")
  set(TFA_AS "${TFA_CROSS_COMPILE_FOR_MAKE}gcc")
  set(TFA_LD "${TFA_CROSS_COMPILE_FOR_MAKE}gcc")
  set(TFA_OC "${TFA_CROSS_COMPILE_FOR_MAKE}objcopy")
  set(TFA_OD "${TFA_CROSS_COMPILE_FOR_MAKE}objdump")
  set(TFA_AR "${TFA_CROSS_COMPILE_FOR_MAKE}gcc-ar")

  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
      COMMAND ${CMAKE_COMMAND} -E env
            ${TFA_MAKE_ENV_ARGS}
            ${TFA_MAKE_PROGRAM} -C ${ZEPHYR_TRUSTED_FIRMWARE_A_MODULE_DIR}
            DEBUG=${TFA_BUILD_DEBUG}
            CROSS_COMPILE=${TFA_CROSS_COMPILE_FOR_MAKE}
            CC=${TFA_CC}
            CPP=${TFA_CPP}
            AS=${TFA_AS}
            LD=${TFA_LD}
            OC=${TFA_OC}
            OD=${TFA_OD}
            AR=${TFA_AR}
            ${TFA_OPTIONAL_ARGS}
            BUILD_BASE=${TFA_BINARY_DIR} PLAT=${TFA_PLAT}
            BL33=${CMAKE_BINARY_DIR}/zephyr/${KERNEL_BIN_NAME}
            ${TFA_AARCH32_REGS}
            ${TFA_EXTRA_ARGS}
            all fip
  )
endif()
