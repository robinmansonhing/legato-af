#*******************************************************************************
# Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
#*******************************************************************************

cmake_minimum_required(VERSION 2.8)

project(LegatoTests C CXX)

# CMake Extension
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include("${CMAKE_MODULE_PATH}/Helpers.cmake")
set(Legato_DIR ${PROJECT_SOURCE_DIR}/cmake)

# Get Legato info.
find_package(Legato REQUIRED)

# Variables changing internal behavior:
# - LEGATO_TARGET: Target device type to build for.
# - TEST_COVERAGE: Provide support for GCov

# Target Platform
if(NOT DEFINED LEGATO_TARGET)
    if(NOT "$ENV{LEGATO_TARGET}" STREQUAL "")
        message("Legato target found in environement: $ENV{LEGATO_TARGET}")
        set(LEGATO_TARGET $ENV{LEGATO_TARGET})
    else()
        message(FATAL_ERROR "Legato Target is not selected")
    endif()
endif()

# Import platform adapter environment variables.
set(LEGATO_AUDIO_PA $ENV{LEGATO_AUDIO_PA})
set(LEGATO_AUDIO_PA_PCM $ENV{LEGATO_AUDIO_PA_PCM})
set(LEGATO_AUDIO_PA_AMR $ENV{LEGATO_AUDIO_PA_AMR})
set(LEGATO_UTIL_PA $ENV{LEGATO_UTIL_PA})
set(LEGATO_MODEM_PA $ENV{LEGATO_MODEM_PA})
set(LEGATO_MODEM_PA_ECALL $ENV{LEGATO_MODEM_PA_ECALL})
set(LEGATO_GNSS_PA $ENV{LEGATO_GNSS_PA})
set(LEGATO_AVC_PA $ENV{LEGATO_AVC_PA})
set(LEGATO_SECSTORE_PA $ENV{LEGATO_SECSTORE_PA})
set(LEGATO_FWUPDATE_PA $ENV{LEGATO_FWUPDATE_PA})

# Default Target configuration
set(LEGATO_COMPONENTS_MODEM "AT" CACHE STRING "Legato Modem Platform-Adapter to build")
set(LEGATO_COMPONENTS_GNSS "AT" CACHE STRING "Legato Gnss Platform-Adapter to build")
set(LEGATO_COMPONENTS_AUDIO "STUB" CACHE STRING "Legato Audio Platform-Adapter to build")
set(LEGATO_COMPONENTS_AVC "AT" CACHE STRING "Legato AirVantage Controller Platform-Adapter to build")
set(LEGATO_COMPONENTS_SECSTORE "AT" CACHE STRING "Legato Secure Storage Controller Platform-Adapter to build")
set(LEGATO_COMPONENTS_FWUPDATE "AT" CACHE STRING "Legato FwUpdate Platform-Adapter to build")

if(INCLUDE_ECALL EQUAL 1)
    add_definitions(-DINCLUDE_ECALL)
    message("Including eCall for target ${LEGATO_TARGET}")
else()
    message("Excluding eCall from target ${LEGATO_TARGET}")
endif()

set(LEGATO_PROPRIETARY_QCT_DIR $ENV{LEGATO_PROPRIETARY_QCT_DIR})

# Target: ar7
if(LEGATO_TARGET MATCHES "ar7")
    set(LEGATO_COMPONENTS_MODEM "QMI")
    set(LEGATO_COMPONENTS_GNSS "QMI")
    set(LEGATO_COMPONENTS_AUDIO "ar7")
    set(LEGATO_COMPONENTS_AVC "QMI")
    set(LEGATO_COMPONENTS_SECSTORE "QMI")
    set(LEGATO_COMPONENTS_FWUPDATE "QMI")

# Target: ar86
elseif(LEGATO_TARGET MATCHES "ar86")
    set(LEGATO_COMPONENTS_MODEM "QMI")
    set(LEGATO_COMPONENTS_GNSS "QMI")
    set(LEGATO_COMPONENTS_AUDIO "ar86")
    set(LEGATO_COMPONENTS_AVC "QMI")
    set(LEGATO_COMPONENTS_SECSTORE "QMI")
    set(LEGATO_COMPONENTS_FWUPDATE "QMI")

# Target: wp85
elseif(LEGATO_TARGET MATCHES "wp85")
    set(LEGATO_COMPONENTS_MODEM "QMI")
    set(LEGATO_COMPONENTS_GNSS "QMI")
    set(LEGATO_COMPONENTS_AVC "QMI")
    set(LEGATO_COMPONENTS_SECSTORE "QMI")
    set(LEGATO_COMPONENTS_FWUPDATE "QMI")

# Target: virt
elseif(LEGATO_TARGET MATCHES "virt")
    set(LEGATO_COMPONENTS_MODEM "simu")
    set(LEGATO_COMPONENTS_GNSS "simu")
    set(LEGATO_COMPONENTS_AUDIO "simu")
    set(LEGATO_COMPONENTS_AVC "simu")
    set(LEGATO_COMPONENTS_SECSTORE "simu")
    set(LEGATO_COMPONENTS_FWUPDATE "simu")

# Target: localhost
elseif(LEGATO_TARGET MATCHES "localhost")

# Target: Unknown
else()
    message(FATAL_ERROR "Invalid Target: ${LEGATO_TARGET}")
endif()

# Target is an automotive platform
if(AUTOMOTIVE_TARGET)
    add_definitions(-DAUTOMOTIVE_TARGET)
endif()

## Output
set(DOCUMENTATION_OUTPUT_PATH   ${LEGATO_ROOT}/build/doc)

## 3rd Party
set(CUNIT_INSTALL "${LEGATO_ROOT}/build/${LEGATO_TARGET}/CUnit")
set(LIBJANSSON_INCLUDE_DIR "${LEGATO_BUILD}/framework/libjansson/include")

# Version
file(READ version LEGATO_VERSION)
string(REPLACE "\n" "" LEGATO_VERSION ${LEGATO_VERSION})
message(STATUS "Version: ${LEGATO_VERSION}")
add_definitions(-DLEGATO_VERSION=\"${LEGATO_VERSION}\")

# Debug/Release
if(CMAKE_BUILD_TYPE MATCHES "Release")
    add_definitions(-O3)
else()
    add_definitions(-g -O0)
endif()

# Turn on all warnings and treat warnings as errors.
add_definitions(-Wall -Werror)

# Coverage
if(TEST_COVERAGE)
    message(STATUS "Coverage reporting (TEST_COVERAGE): Enabled")
    message(STATUS "=> use make coverage_report to produce report")

    # Building part
    add_definitions(--coverage)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} --coverage")

    # Generation part
    add_custom_target(
        coverage_report
        COMMAND lcov --capture --output-file coverage.info -d .
        COMMAND mkdir -p coverage_out
        COMMAND genhtml -o coverage_out coverage.info
        COMMAND echo
        COMMAND echo \"Report in ${LEGATO_BUILD}/coverage_out/\"
        COMMENT "Coverage report will be available in ${LEGATO_BUILD}/coverage_out/"
        WORKING_DIRECTORY ${LEGATO_BUILD}
    )

else()
    message(STATUS "Coverage reporting (TEST_COVERAGE): Disabled")
endif()

set(LEGATO_COMPONENTS_MODEM_TARGET          le_pa)
set(LEGATO_COMPONENTS_GNSS_TARGET           le_pa_gnss)

## Pre-compiled targets
if(${LEGATO_COMPONENTS_MODEM} MATCHES "QMI" AND NOT EXISTS ${LEGATO_COMPONENTS_MODEM_DIR}/qmi/src)
    add_library(${LEGATO_COMPONENTS_MODEM_TARGET} SHARED IMPORTED GLOBAL)
endif()
if(${LEGATO_COMPONENTS_GNSS} MATCHES "QMI" AND NOT EXISTS ${LEGATO_COMPONENTS_GNSS_DIR}/qmi/src)
    add_library(${LEGATO_COMPONENTS_GNSS_TARGET} SHARED IMPORTED GLOBAL)
endif()

## Paths to AT Manager include files needed by various platform adapters that use it.
set (LEGATO_COMPONENTS_AT_MANAGER_INCLUDE_DIRS
        modemServices/platformAdaptor/at/atManager/inc
        modemServices/platformAdaptor/at/atManager/devices/adapter_layer/inc
        modemServices/platformAdaptor/at/atManager/devices/uart/inc
        )

# Documentation
add_custom_target(api_headers)
add_subdirectory(interfaces)
add_subdirectory(framework/doc)

# Tests with ctest
enable_testing()

# Sample and Test Applications
add_subdirectory(apps)
