# --------------------------------------------------------------------------------------------------
# Makefile used to build the Legato framework.
#
# See the TARGETS variable definition for a list of supported build targets.  Each of these
# is a valid goal for this Makefile.  For example, to build for the Sierra Wireless AR7xxx family
# of modules, run "make ar7".
#
# Building a target will build the following:
#  - host tools (needed to build the framework for the target)
#  - liblegato
#  - framework daemons and libs they require
#  - on-target tools
#  - a basic working system (see system.sdef)
#
# To build sample apps and tests for a target X, build tests_X (e.g., 'make tests_wp85').
#
# Other goals supported by this Makefile are:
#
#    clean = delete all build output.
#
#    docs = build the documentation.
#
#    tools = just build the host tools.
#
#    release = build and package a release.
#
#    sdk = build and package a "software development kit" containing the build tools.
#
# To enable coverage testing, run make with "TEST_COVERAGE=1" on the command-line.
#
# To get more details from the build as it progresses, run make with "VERBOSE=1".
#
# Targets to be built for release can be selected with RELEASE_TARGETS.
#
# Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
# --------------------------------------------------------------------------------------------------

# List of target devices needed by the release process:
ifndef RELEASE_TARGETS
  RELEASE_TARGETS := ar7 ar86 wp85
endif

# List of target devices supported:
TARGETS := localhost $(RELEASE_TARGETS) virt

# By default, build for the localhost and build the documentation.
.PHONY: default
default:
	$(MAKE) localhost
	$(MAKE) docs

# Define the LEGATO_ROOT environment variable.
export LEGATO_ROOT := $(CURDIR)

# Add the framework's bin directory to the PATH environment variable.
export PATH := $(PATH):$(LEGATO_ROOT)/bin

# Some framework on-target runtime parameters.
export LEGATO_FRAMEWORK_NICE_LEVEL := -19
export LE_RUNTIME_DIR := /tmp/legato/
export LE_SVCDIR_SERVER_SOCKET_NAME := $(LE_RUNTIME_DIR)serviceDirectoryServer
export LE_SVCDIR_CLIENT_SOCKET_NAME := $(LE_RUNTIME_DIR)serviceDirectoryClient

# Define paths to various platform adaptors' directories.
AT_MANAGER_DIR := $(LEGATO_ROOT)/components/atManager
AUDIO_PA_DIR := $(LEGATO_ROOT)/components/audio/platformAdaptor
MODEM_PA_DIR := $(LEGATO_ROOT)/components/modemServices/platformAdaptor
GNSS_PA_DIR := $(LEGATO_ROOT)/components/positioning/platformAdaptor
AVC_PA_DIR := $(LEGATO_ROOT)/components/airVantage/platformAdaptor
SECSTORE_PA_DIR := $(LEGATO_ROOT)/components/secStore/platformAdaptor
FWUPDATE_PA_DIR := $(LEGATO_ROOT)/components/fwupdate/platformAdaptor

# Define the default platform adaptors to use if not otherwise specified for a given target.
export LEGATO_AUDIO_PA = $(AUDIO_PA_DIR)/stub/le_pa_audio
export LEGATO_UTIL_PA = $(AT_MANAGER_DIR)
export LEGATO_MODEM_PA = $(MODEM_PA_DIR)/at/le_pa
export LEGATO_MODEM_PA_ECALL = $(MODEM_PA_DIR)/stub/le_pa_ecall
export LEGATO_GNSS_PA = $(GNSS_PA_DIR)/at/le_pa_gnss
export LEGATO_AVC_PA = $(AVC_PA_DIR)/at/le_pa_avc
export LEGATO_SECSTORE_PA = $(SECSTORE_PA_DIR)/at/le_pa_secStore
export LEGATO_FWUPDATE_PA = $(FWUPDATE_PA_DIR)/at/le_pa_fwupdate

# Do not use clang by default.
USE_CLANG ?= 0

# Default eCall build to be ON
INCLUDE_ECALL ?= 1

# Do not be verbose by default.
export VERBOSE ?= 0

# In case of release, override parameters
ifeq ($(MAKECMDGOALS),release)
  # We never build for coverage testing when building a release.
  override TEST_COVERAGE := 0
endif

# ========== TARGET-SPECIFIC VARIABLES ============

# If the user specified a goal other than "clean", ensure that all required target-specific vars
# are defined.
ifneq ($(MAKECMDGOALS),clean)

  HOST_ARCH := $(shell uname -m)
  TOOLS_ARCH ?= $(HOST_ARCH)
  FINDTOOLCHAIN := framework/tools/scripts/findtoolchain

  include targetDefs

  ifeq ($(USE_CLANG),1)
    export TARGET_CC = $(COMPILER_DIR)/$(TOOLCHAIN_PREFIX)clang
  else
    export TARGET_CC = $(COMPILER_DIR)/$(TOOLCHAIN_PREFIX)gcc
  endif

endif


# ========== GENERIC BUILD RULES ============

# Tell make that the targets are not actual files.
.PHONY: $(TARGETS)

# The rule to make each target is: build the system and stage for cwe image generation
# in build/$TARGET/staging.
# This will also cause the host tools, framework, and target tools to be built, since those things
# depend on them.
$(TARGETS): %: system_% stage_%

# Cleaning rule.
.PHONY: clean
clean:
	rm -rf build Documentation* bin doxygen.*.log doxygen.*.err
	rm -f framework/doc/toolsHost.dox framework/doc/toolsHost_*.dox
	rm -f sources.md5

# Version related rules.
ifndef LEGATO_VERSION
  export LEGATO_VERSION := $(shell git describe --tags 2> /dev/null)
endif

# Source code directories and files to include in the MD5 sum in the version and package.properties.
FRAMEWORK_SOURCES = framework \
					components \
					interfaces \
					apps/platformServices \
					apps/tools \
					targetFiles \
					$(wildcard Makefile*) \
					$(wildcard targetDefs*) \
					CMakeLists.txt

.PHONY: sources.md5
sources.md5:
	# Generate an MD5 hash of everything in the source directories.
	find $(FRAMEWORK_SOURCES) -type f | sort | while read filePath ; \
	do \
	  echo "$$filePath" && \
	  cat "$$filePath" ; \
	done | md5sum | awk '{ print $$1 }' > sources.md5

.PHONY: version
version:
	@if [ -n "$(LEGATO_VERSION)" ] ; then \
		echo "$(LEGATO_VERSION)" > $@  ; \
	elif ! [ -e version ] ; then \
		echo "unknown" > $@ ; \
	fi

package.properties: version sources.md5
	@echo "version=`cat version`" > $@
	@echo "md5=`cat sources.md5`" >> $@
	@cat $@

# Goal for building all documentation.
.PHONY: docs user_docs implementation_docs
docs: user_docs implementation_docs

# Docs for people who don't want to be distracted by the internal implementation details.
user_docs: localhost build/localhost/Makefile
	$(MAKE) -C build/localhost user_docs
	ln -sf build/doc/user/html Documentation

user_pdf: localhost build/localhost/Makefile
	$(MAKE) -C build/localhost user_pdf
	ln -sf build/localhost/bin/doc/user/legato-user.pdf Documentation.pdf

# Docs for people who want or need to know the internal implementation details.
implementation_docs: localhost build/localhost/Makefile
	$(MAKE) -C build/localhost implementation_docs

implementation_pdf: localhost build/localhost/Makefile
	$(MAKE) -C build/localhost implementation_pdf

# Rule building the unit test covergae report
coverage_report:
	$(MAKE) -C build/localhost coverage_report

# Rule for how to build the build tools.
.PHONY: tools
tools: version
	$(MAKE) -f Makefile.hostTools

# Rule to create a "software development kit" from the tools.
.PHONY: sdk
sdk: tools
	createsdk

# Rule building the framework for a given target.
FRAMEWORK_TARGETS = $(foreach target,$(TARGETS),framework_$(target))
.PHONY: $(FRAMEWORK_TARGETS)
$(FRAMEWORK_TARGETS): tools package.properties
	$(MAKE) -f Makefile.framework

# Rule building the tests for a given target.
TESTS_TARGETS = $(foreach target,$(TARGETS),tests_$(target))
.PHONY: $(TESTS_TARGETS)
$(TESTS_TARGETS):tests_%: % framework_% build/%/Makefile
	$(MAKE) -C build/$(TARGET)

# Rule for invoking CMake to generate the Makefiles inside the build directory.
# Depends on the build directory being there.
# NOTE: CMake is only used to build tests and samples.
$(foreach target,$(TARGETS),build/$(target)/Makefile):
	export PATH=$(COMPILER_DIR):$(PATH) && \
		cd `dirname $@` && \
		cmake ../.. \
			-DLEGATO_ROOT=$(LEGATO_ROOT) \
			-DLEGATO_TARGET=$(TARGET) \
			-DTEST_COVERAGE=$(TEST_COVERAGE) \
			-DINCLUDE_ECALL=$(INCLUDE_ECALL) \
			-DUSE_CLANG=$(USE_CLANG) \
			-DPLATFORM_SIMULATION=$(PLATFORM_SIMULATION) \
			-DTOOLCHAIN_PREFIX=$(TOOLCHAIN_PREFIX) \
			-DCOMPILER_DIR=$(COMPILER_DIR) \
			-DCMAKE_TOOLCHAIN_FILE=$(LEGATO_ROOT)/cmake/toolchain.yocto.cmake

# Construct the staging directory with common files for embedded targets.
# Staging directory will become /mnt/legato/ in cwe
.PHONY: stage_embedded
stage_embedded:
	# Make sure the TARGET environment variable is set.
	if [ -z "$(TARGET)" ]; then exit -1; fi
	# Prep the staging area to make sure there are no leftover files from last build.
	rm -rf build/$(TARGET)/staging
	# Create the directory.
	mkdir -p build/$(TARGET)/staging/apps
	outputDir=build/$(TARGET)/staging && \
	stagingDir="build/$(TARGET)/system/staging" && \
	echo "Copying system staging directory '$$stagingDir' to '$$outputDir/system'." && \
	cp -r -P $$stagingDir "$$outputDir/system" && \
	echo "Adding apps to '$$outputDir'." && \
	for appName in `ls "$$outputDir/system/apps/"` ; \
	do \
		md5=`readlink "$$outputDir/system/apps/$$appName" | sed 's#^/legato/apps/##'` && \
		if grep -e `printf '"md5":"%s"' $$md5` build/$(TARGET)/system.$(TARGET).update > /dev/null ; \
		then \
			echo "  $$appName ($$md5)" && \
			cp -r -P build/$(TARGET)/system/app/$$appName/staging "$$outputDir"/apps/$$md5 ; \
		else \
			echo "  $$appName ($$md5) <-- EXCLUDED FROM .CWE (must be preloaded on target)" ; \
		fi \
	done
	cp -r targetFiles/shared/bin build/$(TARGET)/staging/system/
	# Print some diagnostic messages.
	@echo "== built system's info.properties: =="
	cat build/$(TARGET)/staging/system/info.properties

.PHONY: stage_mklegatoimg
stage_mklegatoimg:
	mklegatoimg -t $(TARGET) -d build/$(TARGET)/staging -o build/$(TARGET)

# ==== localhost needs no staging. Just a blank rule ====

.PHONY: stage_localhost
stage_localhost:

# ==== AR7, AR86 and WP85 (9x15-based Sierra Wireless modules) ====

.PHONY: stage_9x15
stage_9x15:
	install targetFiles/mdm9x15/startup/start build/$(TARGET)/staging
	install targetFiles/mdm9x15/startup/startupScript build/$(TARGET)/staging

.PHONY: stage_ar7 stage_ar86 stage_wp85
stage_ar7 stage_ar86 stage_wp85: stage_embedded stage_9x15 stage_mklegatoimg

# ==== Virtual ====

.PHONY: stage_virt
stage_virt: stage_embedded
	# Install default startup scripts.
	install -d build/$(TARGET)/staging/mnt/flash/startupDefaults
	install targetFiles/virt/startup/* -t build/$(TARGET)/staging/mnt/flash/startupDefaults

# ========== RELEASE ============

# Clean first, then build for localhost and selected embedded targets and generate the documentation
# before packaging it all up into a compressed tarball.
# Partition images for relevant devices are provided as well in the releases/ folder.
.PHONY: release
release: clean
	for target in localhost $(RELEASE_TARGETS) docs; do set -e; make $$target; done
	releaselegato -t "$(shell echo ${RELEASE_TARGETS} | tr ' ' ',')"

# ========== PROTOTYPICAL SYSTEM ============

MKSYS_FLAGS =

ifeq ($(VERBOSE),1)
  MKSYS_FLAGS += -v
endif

# Define the default sdef file to use
SDEF_TO_USE ?= default.sdef

SYSTEM_TARGETS = $(foreach target,$(TARGETS),system_$(target))
.PHONY: $(SYSTEM_TARGETS)
$(SYSTEM_TARGETS):system_%: framework_%
	rm -f system.sdef
	ln -s $(SDEF_TO_USE) system.sdef
	mksys -t $(TARGET) -w build/$(TARGET)/system -o build/$(TARGET) system.sdef \
			-i interfaces/modemServices \
			-i interfaces/positioning \
			$(MKSYS_FLAGS)
