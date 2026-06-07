# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

PACKAGE       = $(PKG_PATH)$(PKG_BASENAME)$(PKG_SUFFIX)

ifneq (,$(PGO_JARLOG_PATH))
  # The backslash subst is to work around an issue with our version of mozmake,
  # where backslashes get slurped in command-line arguments if a command is run
  # with a double-quote character. The command to packager.py happens to be one
  # of these commands, where double-quotes appear in certain ACDEFINES values.
  # This turns a jarlog path like "Z:\task..." into "Z:task", which fails.
  # Switching the backslashes for forward slashes works around the issue.
  JARLOG_FILE_AB_CD = $(subst \,/,$(PGO_JARLOG_PATH))
else
  JARLOG_FILE_AB_CD = $(topobjdir)/jarlog/$(AB_CD).log
endif

TAR_CREATE_FLAGS := --exclude=.mkdir.done $(TAR_CREATE_FLAGS)

MAKE_PACKAGE = $(call py_action,package $(MOZ_PKG_FORMAT), \
  --format $(MOZ_PKG_FORMAT) \
  --cwd '$(1)' \
  --pkg-dir '$(MOZ_PKG_DIR)' \
  --output-dir '$(PKG_PATH)' \
  --basename '$(PKG_BASENAME)' \
  --tar '$(TAR)' \
  $(MOZ_PACKAGE_EXTRA_ARGS))

ifeq ($(MOZ_PKG_FORMAT),APK)
MAKE_PACKAGE = true
endif

INNER_MAKE_PACKAGE = $(MAKE_PACKAGE)

NO_PKG_FILES += \
	core \
	bsdecho \
	js \
	js-config \
	jscpucfg \
	nsinstall \
	viewer \
	TestGtkEmbed \
	elf-dynstr-gc \
	mangle* \
	maptsv* \
	mfc* \
	msdump* \
	msmap* \
	nm2tsv* \
	nsinstall* \
	res/samples \
	res/throbber \
	shlibsign* \
	certutil* \
	pk12util* \
	BadCertAndPinningServer* \
	DelegatedCredentialsServer* \
	EncryptedClientHelloServer* \
	FaultyServer* \
	OCSPStaplingServer* \
	SanctionsTestServer* \
	GenerateOCSPResponse* \
	chrome/chrome.rdf \
	chrome/app-chrome.manifest \
	chrome/overlayinfo \
	components/compreg.dat \
	components/xpti.dat \
	content_unit_tests \
	necko_unit_tests \
	*.dSYM \
	$(NULL)

# If a manifest has not been supplied, the following
# files should be excluded from the package too
ifndef MOZ_PKG_MANIFEST
  NO_PKG_FILES += ssltunnel*
endif

ifdef MOZ_DMD
  NO_PKG_FILES += SmokeDMD
endif

DEFINES += -DDLL_PREFIX=$(DLL_PREFIX) -DDLL_SUFFIX=$(DLL_SUFFIX) -DBIN_SUFFIX=$(BIN_SUFFIX)

ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
  DEFINES += -DDIR_MACOS=Contents/MacOS/ -DDIR_RESOURCES=Contents/Resources/
else
  DEFINES += -DDIR_MACOS= -DDIR_RESOURCES=
endif

ifdef MOZ_FOLD_LIBS
  DEFINES += -DMOZ_FOLD_LIBS=1
endif

# The following target stages files into two directories: one directory for
# core files, and one for optional extensions based on the information in
# the MOZ_PKG_MANIFEST file.

PKG_ARG = , '$(pkg)'

ifndef MOZ_PACKAGER_FORMAT
  MOZ_PACKAGER_FORMAT = $(error MOZ_PACKAGER_FORMAT is not set)
endif

ifneq (android,$(MOZ_WIDGET_TOOLKIT))
  JAR_COMPRESSION ?= none
endif

ifeq ($(OS_TARGET), WINNT)
  INSTALLER_PACKAGE = $(DIST)/$(PKG_INST_PATH)$(PKG_INST_BASENAME).exe
endif

# These are necessary because some of our packages/installers contain spaces
# in their filenames and GNU Make's $(wildcard) function doesn't properly
# deal with them.
empty :=
space = $(empty) $(empty)
QUOTED_WILDCARD = $(if $(wildcard $(subst $(space),?,$(1))),'$(1)')
ESCAPE_SPACE = $(subst $(space),\$(space),$(1))
ESCAPE_WILDCARD = $(subst $(space),?,$(1))

# This variable defines which OpenSSL algorithm to use to
# generate checksums for files that we upload
CHECKSUM_ALGORITHM_PARAM = -d sha512 -d md5 -d sha1

# This variable defines where the checksum file will be located
CHECKSUM_FILE = '$(DIST)/$(PKG_PATH)/$(CHECKSUMS_FILE_BASENAME).checksums'
CHECKSUM_FILES = $(CHECKSUM_FILE)

# Upload MAR tools only if AB_CD is unset or en_US
ifeq (,$(AB_CD:en-US=))
  ifeq (WINNT,$(OS_TARGET))
    UPLOAD_EXTRA_FILES += host/bin/mar.exe
    UPLOAD_EXTRA_FILES += host/bin/mbsdiff.exe
  else
    UPLOAD_EXTRA_FILES += host/bin/mar
    UPLOAD_EXTRA_FILES += host/bin/mbsdiff
  endif
endif

UPLOAD_FILES= \
  $(call QUOTED_WILDCARD,$(DIST)/$(PACKAGE)) \
  $(call QUOTED_WILDCARD,$(INSTALLER_PACKAGE)) \
  $(call QUOTED_WILDCARD,$(DIST)/$(LANGPACK)) \
  $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(MOZHARNESS_PACKAGE)) \
  $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(SYMBOL_ARCHIVE_BASENAME).zip) \
  $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(GENERATED_SOURCE_FILE_PACKAGE)) \
  $(call QUOTED_WILDCARD,$(MOZ_SOURCESTAMP_FILE)) \
  $(call QUOTED_WILDCARD,$(MOZ_BUILDINFO_FILE)) \
  $(call QUOTED_WILDCARD,$(MOZ_BUILDHUB_JSON)) \
  $(call QUOTED_WILDCARD,$(MOZ_BUILDID_INFO_TXT_FILE)) \
  $(call QUOTED_WILDCARD,$(MOZ_MOZINFO_FILE)) \
  $(call QUOTED_WILDCARD,$(MOZ_TEST_PACKAGES_FILE)) \
  $(call QUOTED_WILDCARD,$(PKG_JSSHELL)) \
  $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(SYMBOL_FULL_ARCHIVE_BASENAME).tar.zst) \
  $(call QUOTED_WILDCARD,$(topobjdir)/$(MOZ_BUILD_APP)/installer/windows/instgen/setup.exe) \
  $(call QUOTED_WILDCARD,$(topobjdir)/$(MOZ_BUILD_APP)/installer/windows/instgen/setup-stub.exe) \
  $(call QUOTED_WILDCARD,$(topsrcdir)/toolchains.json) \
  $(call QUOTED_WILDCARD,$(topobjdir)/config.status) \
  $(if $(UPLOAD_EXTRA_FILES), $(foreach f, $(UPLOAD_EXTRA_FILES), $(wildcard $(DIST)/$(f))))

ifneq ($(filter-out en-US,$(AB_CD)),)
  UPLOAD_FILES += \
    $(call QUOTED_WILDCARD,$(topobjdir)/$(MOZ_BUILD_APP)/installer/windows/l10ngen/setup.exe) \
    $(call QUOTED_WILDCARD,$(topobjdir)/$(MOZ_BUILD_APP)/installer/windows/l10ngen/setup-stub.exe)
endif

ifdef MOZ_CODE_COVERAGE
  UPLOAD_FILES += \
    $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(CODE_COVERAGE_ARCHIVE_BASENAME).zip) \
    $(call QUOTED_WILDCARD,$(topobjdir)/chrome-map.json) \
    $(NULL)
endif


ifdef ENABLE_MOZSEARCH_PLUGIN
  UPLOAD_FILES += $(call QUOTED_WILDCARD,$(topobjdir)/chrome-map.json)
  UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(MOZSEARCH_ARCHIVE_BASENAME).zip)
  UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(MOZSEARCH_SCIP_INDEX_BASENAME).zip)
  UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(MOZSEARCH_INCLUDEMAP_BASENAME).map)
ifeq ($(MOZ_BUILD_APP),mobile/android)
  UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(MOZSEARCH_JAVA_INDEX_BASENAME).zip)
endif
endif

ifdef MOZ_STUB_INSTALLER
  UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_INST_PATH)$(PKG_STUB_BASENAME).exe)
endif

# Upload `.xpt` artifacts for use in artifact builds.
UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(XPT_ARTIFACTS_ARCHIVE_BASENAME).zip)
# Upload update-related macOS framework artifacts for use in artifact builds.
ifeq ($(OS_ARCH),Darwin)
UPLOAD_FILES += $(call QUOTED_WILDCARD,$(DIST)/$(PKG_PATH)$(UPDATE_FRAMEWORK_ARTIFACTS_ARCHIVE_BASENAME).zip)
endif # Darwin

HG_BUNDLE_FILE = $(DIST)/$(PKG_SRCPACK_PATH)$(PKG_BUNDLE_BASENAME).bundle

HG ?= hg
CREATE_HG_BUNDLE_CMD  = $(HG) -v -R $(topsrcdir) bundle --base null
ifdef HG_BUNDLE_REVISION
  CREATE_HG_BUNDLE_CMD += -r $(HG_BUNDLE_REVISION)
endif
CREATE_HG_BUNDLE_CMD += $(HG_BUNDLE_FILE)
ifdef UPLOAD_HG_BUNDLE
  SOURCE_UPLOAD_FILES  += $(HG_BUNDLE_FILE)
endif
