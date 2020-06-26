# Script to build dav1d with CMake
# I, Miku AuahDark grant you permission to use this script under
# BSD-2 Clause license
# This CMake also depends on TargetArch.cmake which is shipped along with this
# repository, modified to detect AArch64. To get the unmodified version, visit
# https://github.com/axr/solar-cmake/blob/73cfea0/TargetArch.cmake

cmake_minimum_required(VERSION 3.8.0)

set(CMAKE_C_STANDARD 99)

project(dav1d VERSION "0.7.1" LANGUAGES C)
set(PROJECT_VERSION_REVISION ${PROJECT_VERSION_PATCH})

if(POLICY CMP0025)
    cmake_policy(SET CMP0025 NEW)
endif()

set(DAV1D_API_VERSION_MAJOR 4)
set(DAV1D_API_VERSION_MINOR 0)
set(DAV1D_API_VERSION_REVISION 2)

set(API_VERSION_MAJOR 4)
set(API_VERSION_MINOR 0)
set(API_VERSION_REVISION 2)

###########
# Options #
###########

# bitdepths
option(bitdepth_8 "Enable specified bitdepths" ON)
option(bitdepth_16 "Enable specified bitdepths" OFF)

# enable_asm
option(enable_asm "Build asm files, if available" ON)

# enable_avx512
option(enable_avx512 "Build AVX-512 asm files, requires nasm 2.14" ON)

# enable_tools
option(enable_tools "Build dav1d cli tools" ON)

# enable_examples
option(enable_examples "Build dav1d examples" OFF)

# TODO enable_tests

# logging
option(logging "Print error log messages using the provided callback function" ON)

# TODO testdata_tests
# TODO fuzzing_engine
# TODO fuzzer_ldflags

# stack_alignment
set(stack_alignment "0" CACHE STRING "")

##############
# End Option #
##############

# compatibility
set(bitdepths "")

if(bitdepth_8)
    list(APPEND bitdepths "8")
endif()

if(bitdepth_16)
    list(APPEND bitdepths "16")
endif()

foreach(BITS IN LISTS bitdepths)
    set(CONFIG_${BITS}BPC 1)
endforeach()

include(TargetArch.cmake)
target_architecture(PROCESSOR)

if(${PROCESSOR} STREQUAL "i386" OR ${PROCESSOR} STREQUAL "x86_64")
    set(ARCH_X86 1)
endif()

if(enable_asm)
    if(ARCH_X86 OR ${PROCESSOR} STREQUAL "aarch64" OR ${PROCESSOR} MATCHES "^arm" OR ${PROCESSOR} STREQUAL "ppc64")
        set(HAVE_ASM 1)
    endif()
endif()

set(CONFIG_LOG ${logging})

set(TEMP_COMPILE_DEFS "")
set(TEMP_COMPILE_FLAGS "")
set(TEMP_LINK_LIBRARIES "")
set(TEMP_COMPAT_FILES "")
set(RT_DEPENDENCY "")

include(CheckSymbolExists)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    list(APPEND TEMP_COMPILE_DEFS -D_GNU_SOURCE)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND TEMP_COMPILE_DEFS -D_DARWIN_C_SOURCE)
else()
    list(APPEND TEMP_COMPILE_DEFS -D_POSIX_C_SOURCE=200112L)
endif()

set(CMAKE_REQUIRED_DEFINITIONS ${TEMP_COMPILE_DEFS})

if(WIN32)
    set(_TARGETTING_WINDOWS 1)

    check_symbol_exists(fseeko "stdio.h" HAS_FSEEKO)

    if(HAS_FSEEKO)
        set(_FILE_OFFSET_BITS 64)
    else()
        set(_FSEEKO_FUNCTION _fseeki64)
        set(_FTELLO_FUNCTION _ftelli64)
    endif()

    if (${PROCESSOR} STREQUAL "x86_64" AND ${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
        list(APPEND TEMP_COMPILE_FLAGS -mcmodel=small)
    endif()

    # On Windows, we use a compatibility layer to emulate pthread
    add_library(thread_compat_dep STATIC dav1d/src/win32/thread.c)
    target_include_directories(thread_compat_dep PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dav1d
        ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/include
    )
    set(THREAD_DEPENDENCY thread_compat_dep)
else()
    set(THREAD_DEPENDENCY pthread)

    check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)

    if(NOT HAVE_CLOCK_GETTIME)
        message(FATAL_ERROR "clock_gettime not found")
    endif()
endif()

# check for fseeko on android. It is not always available if _FILE_OFFSET_BITS is defined to 64
if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    check_symbol_exists(fseeko "stdio.h" HAS_FSEEKO)

    if(NOT HAS_FSEEKO)
        set(CMAKE_REQUIRED_FLAGS "-U_FILE_OFFSET_BITS")
        check_symbol_exists(fseeko "stdio.h" HAS_FSEEKO)
        unset(CMAKE_REQUIRED_FLAGS)

        if(HAS_FSEEKO)
            message(WARNING "Files larger than 2 gigabytes might not be supported in the dav1d CLI tool.")
            list(APPEND TEMP_COMPILE_FLAGS -U_FILE_OFFSET_BITS)
        elseif(enable_tools)
            message(FATAL_ERROR "dav1d CLI tool needs fseeko()")
        else()
            unset(HAS_FSEEKO)
        endif()
    endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_DL_LIBS})
    check_symbol_exists(dlsym "dlfcn.h" HAVE_DLSYM)
    unset(CMAKE_REQUIRED_LIBRARIES)

    if(HAVE_DLSYM)
        list(APPEND TEMP_LINK_LIBRARIES ${CMAKE_DL_LIBS})
    endif()
endif()

#
# Header checks
#
include(CheckIncludeFile)

check_include_file(stdatomic.h HAS_STDATOMIC)

if(NOT HAS_STDATOMIC)
    if(MSVC)
        # we have a custom replacement for MSVC
        add_library(stdatomic_dependency INTERFACE)
        target_include_directories(stdatomic_dependency INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/include/compat/msvc)
    else()
        try_compile(_GCC_STYLE_ATOMICS ${CMAKE_CURRENT_BINARY_DIR} stdatomic_gcc_test.c
            COMPILE_DEFINITIONS ${TEMP_COMPILE_DEFS}
        )

        if(_GCC_STYLE_ATOMICS)
            add_library(stdatomic_dependency INTERFACE)
            target_include_directories(stdatomic_dependency INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/include/compat/gcc)
        else()
            message(FATAL_ERROR "Atomics not supported")
        endif()
    endif()
endif()

check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(io.h HAVE_IO_H)

#
# Function checks
#
set(CMAKE_REQUIRED_DEFINITIONS ${TEMP_COMPILE_DEFS})
check_symbol_exists(getopt_long "getopt.h" HAS_GETOPT_LONG)

if(NOT HAS_GETOPT_LONG)
    add_library(getopt_dependency STATIC dav1d/tools/compat/getopt.c)
    target_include_directories(getopt_dependency PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/include/compat)
endif()

check_symbol_exists(_aligned_malloc "malloc.h" HAVE_ALIGNED_MALLOC)
check_symbol_exists(posix_memalign "stdlib.h" HAVE_POSIX_MEMALIGN)
check_symbol_exists(memalign "malloc.h" HAVE_MEMALIGN)

if(
    ${PROCESSOR} STREQUAL "aarch64" OR
    ${PROCESSOR} MATCHES "^arm" OR
    ${PROCESSOR} STREQUAL "ppc64"
)
    check_symbol_exists(getauxval "sys/auxv.h" HAVE_GETAUXVAL)
    check_symbol_exists(elf_aux_info "sys/auxv.h" HAVE_ELF_AUX_INFO)
endif()

#
# Compiler flag tests
#
include(CheckCCompilerFlag)

if(NOT MSVC)
    check_c_compiler_flag("-fvisibility=hidden" HAS_VISIBILITY_HIDDEN)
    if(HAS_VISIBILITY_HIDDEN)
        list(APPEND TEMP_COMPILE_FLAGS -fvisibility=hidden)
    else()
        message(WARNING "Compiler does not support -fvisibility=hidden, all symbols will be public!")
    endif()
endif()

# Compiler flags that should be set
# But when the compiler does not supports them
# it is not an error and silently tolerated
set(OPTIONAL_FLAGS "")

if(MSVC)
    list(APPEND OPTIONAL_FLAGS -wd4028) # parameter different from declaration
    list(APPEND OPTIONAL_FLAGS -wd4996) # use of POSIX functions
else()
    list(APPEND OPTIONAL_FLAGS
      -Wundef
      -Werror=vla
      -Wno-maybe-uninitialized
      -Wno-missing-field-initializers
      -Wno-unused-parameter
      -Werror=missing-prototypes
      -Wshorten-64-to-32
    )

    if(${CMAKE_SYSTEM_NAME} MATCHES "^i{d}86$")
        list(APPEND OPTIONAL_FLAGS
            -msse2
            -mfpmath=sse
        )
    endif()
endif()

if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    list(APPEND OPTIONAL_FLAGS
        -fomit-frame-pointer
        -ffast-math
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND "${XCODE_VERSION}" MATCHES "^11")
    # Workaround for Xcode 11 -fstack-check bug, see #301
    list(APPEND OPTIONAL_FLAGS -fno-stack-check)
endif()

foreach(FLAG IN LISTS OPTIONAL_FLAGS)
    check_c_compiler_flag(${FLAG} _COMPILE_FLAG_${FLAG})
    if(_COMPILE_FLAG_${FLAG})
        list(APPEND TEMP_COMPILE_FLAGS ${FLAG})
    endif()
endforeach()

# TODO libFuzzer related things

#
# Stack alignments flags
#
include(TestBigEndian)
set(STACKALIGN_FLAG "")
set(STACKREALIGN_FLAG "")
test_big_endian(ENDIANNESS_BIG)

if(ARCH_X86)
    if(${stack_alignment} GREATER 0)
        set(STACK_ALIGNMENT ${stack_alignment})
    elseif(${PROCESSOR} STREQUAL "x86_64")
        check_c_compiler_flag("-mpreferred-stack-boundary=6" _HAS_PREFER_STACK_BOUNDARY)

        if(_HAS_PREFER_STACK_BOUNDARY)
            list(APPEND STACKALIGN_FLAG "-mpreferred-stack-boundary=6")
            list(APPEND STACKREALIGN_FLAG "-mincoming-stack-boundary=4")
            set(STACK_ALIGNMENT 32)
        else()
            check_c_compiler_flag("-mstack-alignment=64" _HAS_STACK_ALIGN)

            if(_HAS_STACK_ALIGN)
                list(APPEND STACKALIGN_FLAG "-mstack-alignment=64")
                list(APPEND STACKREALIGN_FLAG "-mstackrealign")
                set(STACK_ALIGNMENT 32)
            else()
                set(STACK_ALIGNMENT 16)
            endif()
        endif()
    else()
        if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" OR ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            set(STACK_ALIGNMENT 16)
        else()
            check_c_compiler_flag("-mpreferred-stack-boundary=4" "_HAS_PREFER_STACK_BOUNDARY")

            if(_HAS_PREFER_STACK_BOUNDARY)
                list(APPEND STACKALIGN_FLAG "-mpreferred-stack-boundary=4")
                list(APPEND STACKREALIGN_FLAG "-mincoming-stack-boundary=2")
                set(STACK_ALIGNMENT 16)
            else()
                check_c_compiler_flag("-mstack-alignment=16" _HAS_STACK_ALIGN)

                if(_HAS_STACK_ALIGN)
                    list(APPEND STACKALIGN_FLAG "-mstack-alignment=16")
                    list(APPEND STACKREALIGN_FLAG "-mstackrealign")
                    set(STACK_ALIGNMENT 16)
                else()
                    set(STACK_ALIGNMENT 4)
                endif()
            endif()
        endif()
    endif()
endif()

if(${PROCESSOR} STREQUAL "aarch64")
    set(ARCH_AARCH64 1)
    set(ARCH_ARM 1)
elseif(${PROCESSOR} MATCHES "^arm")
    set(ARCH_ASM 1)
endif()

if(HAVE_ASM AND ARCH_ARM)
    enable_language(ASM)

    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(HAVE_AS_FUNC ${CMAKE_CURRENT_BINARY_DIR} test_as.c)

    # fedora package build infrastructure uses a gcc specs file to enable
    # '-fPIE' by default. The chosen way only adds '-fPIE' to the C compiler
    # with integrated preprocessor. It is not added to the standalone
    # preprocessor or the preprocessing stage of '.S' files. So we have to
    # compile code to check if we have to define PIC for the arm asm to
    # avoid absolute relocations when building for example checkasm.
    try_compile(_HAS_PIC ${CMAKE_CURRENT_BINARY_DIR} check_pic_code.c)
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)

    if(_HAS_PIC)
        set(PIC 3)
    endif()
endif()

if(ARCH_X86)
    if(${PROCESSOR} STREQUAL "x86_64")
        set(ARCH_X86_64 1)
    else()
        set(ARCH_X86_32 1)
    endif()

    set(PIC 1)
endif()

if(${PROCESSOR} STREQUAL "ppc64")
    set(ARCH_PPC64LE 1)
endif()

# https://mesonbuild.com/Release-notes-for-0-37-0.html#new-compiler-function-symbols_have_underscore_prefix
# For example, Windows 32-bit prefixes underscore, but 64-bit does not.
# Linux does not prefix an underscore but OS X does.
if(WIN32 AND NOT WIN64 OR ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    #set(UNDERSCORE_PREFIXED 1)
endif()

#
# ASM specific stuff
#
if(HAVE_ASM AND ARCH_X86)
    # NASM compiler support
    enable_language(ASM_NASM)

    # check NASM version
    execute_process(COMMAND "${CMAKE_ASM_NASM_COMPILER}" "--version" OUTPUT_VARIABLE NASM_VERSION_FULL)

    if("${NASM_VERSION_FULL}" MATCHES "^NASM version")
        string(REPLACE " " ";" NASM_VERSION_FULL "${NASM_VERSION_FULL}")
        list(GET NASM_VERSION_FULL 2 NASM_VERSION)

        if(enable_avx512 AND ${NASM_VERSION} VERSION_LESS "2.14")
            message(FATAL_ERROR "nasm 2.14 or later is required for AVX-512 asm. AVX-512 asm can be disabled with '-Denable_avx512=false'")
        elseif(${NASM_VERSION} VERSION_LESS "2.13.02")
            message(FATAL_ERROR "nasm 2.13.02 or later is required, found nasm ${NASM_VERSION}")
        endif()

        set(HAVE_AVX512ICL ${enable_avx512})
    else()
        message(FATAL_ERROR "unexpected nasm version string: ${NASM_VERSION_FULL}")
    endif()

    # Generate config.asm
    configure_file(config.asm.in ${CMAKE_CURRENT_BINARY_DIR}/config.asm.in2)
    file(READ ${CMAKE_CURRENT_BINARY_DIR}/config.asm.in2 CONFIG_ASM_CONTENTS_TEMP)
    string(REPLACE "#" "%" CONFIG_ASM_CONTENTS_TEMP2 ${CONFIG_ASM_CONTENTS_TEMP})
    string(REPLACE "/*" ";" CONFIG_ASM_CONTENTS ${CONFIG_ASM_CONTENTS_TEMP2})
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/config.asm CONTENT "${CONFIG_ASM_CONTENTS}")
endif()

configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# Revision file (vcs_version.h) generation
set(DAV1D_GIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dav1d/.git")

find_package(Git)

if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} --git-dir "${DAV1D_GIT_DIR}" describe --tags --long --match ?.*.* --always
        OUTPUT_VARIABLE VCS_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(VCS_TAG "unknown")
endif()

message(STATUS "Git revision: ${VCS_TAG}")
configure_file(dav1d/include/vcs_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/vcs_version.h)
unset(VCS_TAG)

configure_file(dav1d/include/dav1d/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/dav1d/version.h)

# libdav1d source files
set(LIBDAV1D_SOURCES
    dav1d/src/cdf.c
    dav1d/src/cpu.c
    dav1d/src/data.c
    dav1d/src/decode.c
    dav1d/src/dequant_tables.c
    dav1d/src/getbits.c
    dav1d/src/intra_edge.c
    dav1d/src/itx_1d.c
    dav1d/src/lf_mask.c
    dav1d/src/log.c
    dav1d/src/msac.c
    dav1d/src/obu.c
    dav1d/src/picture.c
    dav1d/src/qm.c
    dav1d/src/ref.c
    dav1d/src/refmvs.c
    dav1d/src/scan.c
    dav1d/src/tables.c
    dav1d/src/warpmv.c
    dav1d/src/wedge.c
)

# libdav1d bitdepth source files
# These files are compiled for each bitdepth with
# `BITDEPTH` defined to the currently built bitdepth.
set(LIBDAV1D_TMPL_SOURCES
    dav1d/src/cdef_apply_tmpl.c
    dav1d/src/cdef_tmpl.c
    dav1d/src/fg_apply_tmpl.c
    dav1d/src/film_grain_tmpl.c
    dav1d/src/ipred_prepare_tmpl.c
    dav1d/src/ipred_tmpl.c
    dav1d/src/itx_tmpl.c
    dav1d/src/lf_apply_tmpl.c
    dav1d/src/loopfilter_tmpl.c
    dav1d/src/looprestoration_tmpl.c
    dav1d/src/lr_apply_tmpl.c
    dav1d/src/mc_tmpl.c
    dav1d/src/recon_tmpl.c
)

set(LIBDAV1D_ARCH_TMPL_SOURCES "")
set(LIBDAV1D_BITDEPTH_OBJS "")

# libdav1d entrypoint source files
# These source files contain library entry points and are
# built with the stack-realign flag set, where necessary.
set(LIBDAV1D_ENTRYPOINT_SOURCES
    dav1d/src/lib.c
    dav1d/src/thread_task.c
)

# ASM specific sources
set(LIBDAV1D_SOURCES_ASM "")
# Arch-specific flags
set(ARCH_SPECIFIC_FLAGS "")

if(HAVE_ASM)
    if(ARCH_ARM)
        list(APPEND LIBDAV1D_SOURCES dav1d/src/arm/cpu.c)
        list(APPEND LIBDAV1D_TMPL_SOURCES
            dav1d/src/arm/cdef_init_tmpl.c
            dav1d/src/arm/ipred_init_tmpl.c
            dav1d/src/arm/itx_init_tmpl.c
            dav1d/src/arm/loopfilter_init_tmpl.c
            dav1d/src/arm/looprestoration_init_tmpl.c
            dav1d/src/arm/mc_init_tmpl.c
        )

        if(ARCH_AARCH64)
            list(APPEND LIBDAV1D_SOURCES
                # itx.S is used for both 8 and 16 bpc.
                dav1d/src/arm/64/itx.S
                dav1d/src/arm/64/looprestoration_common.S
                dav1d/src/arm/64/msac.S
            )

            if(CONFIG_8BPC)
                list(APPEND LIBDAV1D_SOURCES
                    dav1d/src/arm/64/cdef.S
                    dav1d/src/arm/64/ipred.S
                    dav1d/src/arm/64/loopfilter.S
                    dav1d/src/arm/64/looprestoration.S
                    dav1d/src/arm/64/mc.S
                )
            endif()

            if(CONFIG_16BPC)
                list(APPEND LIBDAVID_SOURCES
                    dav1d/src/arm/64/cdef16.S
                    dav1d/src/arm/64/ipred16.S
                    dav1d/src/arm/64/itx16.S
                    dav1d/src/arm/64/loopfilter16.S
                    dav1d/src/arm/64/looprestoration16.S
                    dav1d/src/arm/64/mc16.S
                )
            endif()
        else()
            list(APPEND LIBDAV1D_SOURCES dav1d/src/arm/32/msac.S)

            if(CONFIG_8BPC)
                list(APPEND LIBDAV1D_SOURCES
                    dav1d/src/arm/32/cdef.S
                    dav1d/src/arm/32/ipred.S
                    dav1d/src/arm/32/itx.S
                    dav1d/src/arm/32/loopfilter.S
                    dav1d/src/arm/32/looprestoration.S
                    dav1d/src/arm/32/mc.S
                )
            endif()
        endif()
    elseif(ARCH_X86)
        list(APPEND LIBDAV1D_SOURCES
            dav1d/src/x86/cpu.c
            dav1d/src/x86/msac_init.c
        )
        list(APPEND LIBDAV1D_TMPL_SOURCES
            dav1d/src/x86/cdef_init_tmpl.c
            dav1d/src/x86/film_grain_init_tmpl.c
            dav1d/src/x86/ipred_init_tmpl.c
            dav1d/src/x86/itx_init_tmpl.c
            dav1d/src/x86/loopfilter_init_tmpl.c
            dav1d/src/x86/looprestoration_init_tmpl.c
            dav1d/src/x86/mc_init_tmpl.c
        )

        # NASM source files
        list(APPEND LIBDAV1D_SOURCES_ASM
            dav1d/src/x86/cpuid.asm
            dav1d/src/x86/msac.asm
        )

        if(CONFIG_8BPC)
            list(APPEND LIBDAV1D_SOURCES_ASM
                dav1d/src/x86/cdef_avx512.asm
                dav1d/src/x86/cdef_avx2.asm
                dav1d/src/x86/film_grain.asm
                dav1d/src/x86/ipred.asm
                dav1d/src/x86/itx.asm
                dav1d/src/x86/loopfilter.asm
                dav1d/src/x86/looprestoration.asm
                dav1d/src/x86/mc.asm
                dav1d/src/x86/cdef_sse.asm
                dav1d/src/x86/film_grain_ssse3.asm
                dav1d/src/x86/ipred_ssse3.asm
                dav1d/src/x86/itx_ssse3.asm
                dav1d/src/x86/loopfilter_ssse3.asm
                dav1d/src/x86/looprestoration_ssse3.asm
                dav1d/src/x86/mc_sse.asm
            )
        endif()
    elseif(ARCH_PPC64LE)
        list(APPEND ARCH_SPECIFIC_FLAGS -maltivec -mvsx)
        list(APPEND LIBDAV1D_SOURCES
            dav1d/src/ppc/cpu.c
        )
        list(APPEND LIBDAV1D_ARCH_TMPL_SOURCES
            dav1d/src/ppc/cdef_init_tmpl.c
            dav1d/src/ppc/looprestoration_init_tmpl.c
        )
    endif()
endif()

#
# Windows .rc file and API export flags
#
set(API_EXPORT_DEFS "")

if(WIN32 AND BUILD_SHARED_LIBS)
    set(COPYRIGHT_YEARS 2019)
    configure_file(dav1d/src/dav1d.rc.in ${CMAKE_CURRENT_BINARY_DIR}/dav1d.rc)
    list(APPEND LIBDAV1D_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dav1d.rc)
    list(APPEND API_EXPORT_DEFS DAV1D_BUILDING_DLL)
endif()

#
# Library definitions
#

set(LIBDAV1D_INCLUDE_DIRS_PRIV
    ${CMAKE_CURRENT_SOURCE_DIR}/dav1d
    ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/include/dav1d
    ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include/dav1d
)
set(LIBDAV1D_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# The final dav1d library
add_library(dav1d ${TEMP_COMPAT_FILES} ${LIBDAV1D_SOURCES} ${LIBDAV1D_SOURCES_ASM} ${LIBDAV1D_ENTRYPOINT_SOURCES})
target_include_directories(dav1d PRIVATE ${LIBDAV1D_INCLUDE_DIRS_PRIV})
target_include_directories(dav1d PUBLIC ${LIBDAV1D_INCLUDE_DIRS})
target_compile_options(dav1d PRIVATE ${STACKALIGN_FLAG} ${STACKREALIGN_FLAG})
target_compile_definitions(dav1d PRIVATE ${TEMP_COMPILE_DEFS} ${API_EXPORT_DEFS})

if(STACKREALIGN_FLAG)
    set_source_files_properties(${LIBDAV1D_ENTRYPOINT_SOURCES} PROPERTIES COMPILE_FLAGS ${STACKREALIGN_FLAG})
endif()

if(NOT WIN32)
    set_target_properties(dav1d SOVERSION "${DAV1D_API_VERSION_MAJOR}")
else()
    set_target_properties(dav1d PROPERTIES PREFIX "lib")
endif()

if(NOT HAS_STDATOMIC)
    target_link_libraries(dav1d stdatomic_dependency)
endif()

# Helper library for each bitdepth (and architecture-specific flags)
foreach(BITS IN LISTS bitdepths)
    add_library(dav1d_bitdepth_${BITS} STATIC ${LIBDAV1D_TMPL_SOURCES})
    target_include_directories(dav1d_bitdepth_${BITS} PRIVATE ${LIBDAV1D_INCLUDE_DIRS_PRIV})
    target_include_directories(dav1d_bitdepth_${BITS} PUBLIC ${LIBDAV1D_INCLUDE_DIRS})
    target_compile_options(dav1d_bitdepth_${BITS} PRIVATE ${TEMP_COMPILE_FLAGS} ${STACKALIGN_FLAG})
    target_compile_definitions(dav1d_bitdepth_${BITS} PRIVATE ${TEMP_COMPILE_DEFS} BITDEPTH=${BITS})
    target_link_libraries(dav1d dav1d_bitdepth_${BITS})

    if(NOT HAS_STDATOMIC)
        target_link_libraries(dav1d_bitdepth_${BITS} stdatomic_dependency)
    endif()

    if(LIBDAV1D_ARCH_TMPL_SOURCES)
        add_library(dav1d_arch_bitdepth_${BITS} STATIC ${LIBDAV1D_ARCH_TMPL_SOURCES})
        target_include_directories(dav1d_arch_bitdepth_${BITS} PRIVATE ${LIBDAV1D_INCLUDE_DIRS_PRIV})
        target_include_directories(dav1d_arch_bitdepth_${BITS} PUBLIC ${LIBDAV1D_INCLUDE_DIRS})
        target_compile_options(dav1d_arch_bitdepth_${BITS} PRIVATE ${TEMP_COMPILE_FLAGS} ${STACKALIGN_FLAG} ${ARCH_SPECIFIC_FLAGS})
        target_compile_definitions(dav1d_arch_bitdepth_${BITS} PRIVATE ${TEMP_COMPILE_DEFS} BITDEPTH=${BITS})
        target_link_libraries(dav1d dav1d_arch_bitdepth_${BITS})

        if(NOT HAS_STDATOMIC)
            target_link_libraries(dav1d_arch_bitdepth_${BITS} stdatomic_dependency)
        endif()
    endif()
endforeach()

if(TEMP_LINK_LIBRARIES)
    target_link_directories(dav1d ${TEMP_LINK_LIBRARIES})
endif()

if(THREAD_DEPENDENCY)
    target_link_libraries(dav1d ${THREAD_DEPENDENCY})
endif()

if(enable_tools)
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cli_config.h CONTENT " ")

    add_executable(dav1d_exe
        dav1d/tools/input/input.c
        dav1d/tools/input/annexb.c
        dav1d/tools/input/ivf.c
        dav1d/tools/input/section5.c
        dav1d/tools/output/md5.c
        dav1d/tools/output/null.c
        dav1d/tools/output/output.c
        dav1d/tools/output/y4m2.c
        dav1d/tools/output/yuv.c
        dav1d/tools/dav1d.c
        dav1d/tools/dav1d_cli_parse.c
    )
    target_include_directories(dav1d_exe PRIVATE
        ${LIBDAV1D_INCLUDE_DIRS_PRIV}
        ${LIBDAV1D_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/dav1d/tools
    )
    target_link_libraries(dav1d_exe dav1d)
    set_target_properties(dav1d_exe PROPERTIES OUTPUT_NAME "dav1d")

    if(NOT HAS_GETOPT_LONG)
        target_link_libraries(dav1d_exe getopt_dependency)
    endif()

    if(THREAD_DEPENDENCY)
        target_link_libraries(dav1d ${THREAD_DEPENDENCY})
    endif()
endif()

# TODO tests
