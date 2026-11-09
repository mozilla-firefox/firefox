# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


# Shared makefile that can be used to easily kick off l10n builds
# of Mozilla applications.
# This makefile should be included, and then assumes that the including
# makefile defines the following targets:
# l10n-%
#   This target should call into the various l10n targets that this
#   application depends on.
# installer-%
#   This target should list all required targets, a typical rule would be
#	installers-%: clobber-% langpack-% repackage-zip-%
#		@echo "repackaging done"
#   to initially clobber the locale staging area, and then to build the
#   language pack and zip package.
#   Other targets like windows installers might be listed, too, and should
#   be defined in the including makefile.
# The including makefile should provide values for the variables
#   MOZ_APP_VERSION and MOZ_LANGPACK_EID.

# This makefile uses variable overrides from the l10n-% target to
# build non-default locales to non-default dist/ locations. Be aware!

# Allows overriding the final destination of the repackaged file
ZIP_OUT ?= $(ABS_DIST)/$(PACKAGE)

# export some global defines for l10n repacks
BASE_MERGE:=$(CURDIR)/merge-dir
export REAL_LOCALE_MERGEDIR=$(BASE_MERGE)/$(AB_CD)
# is an l10n repack step:
export IS_LANGUAGE_REPACK

clobber-%: AB_CD=$*
clobber-%:
	$(RM) -rf $(DIST)/xpi-stage/locale-$*


PACKAGER_NO_LIBS = 1

L10N_STAGE = $(ABS_DIST)/l10n-stage/$(AB_CD)

ifeq (cocoa,$(MOZ_WIDGET_TOOLKIT))
STAGEDIST = $(L10N_STAGE)/$(MOZ_PKG_DIR)/$(MOZ_PKG_RESPATH)
else
STAGEDIST = $(L10N_STAGE)/$(MOZ_PKG_DIR)
endif

include $(MOZILLA_DIR)/toolkit/mozapps/installer/packager.mk

repackage-zip-%: AB_CD=$*
repackage-zip-%:
	$(call py_action,l10n_repackage,--locale=$* --mach=$(topsrcdir)/mach --l10n-stage='$(L10N_STAGE)' --unpack-distdir='$(L10N_STAGE)/$(MOZ_PKG_DIR)' --en-us-package='$(foreach AB_CD,en-US,$(ABS_DIST)/$(PACKAGE))' --stagedist='$(STAGEDIST)' --xpi-stage='$(ABS_DIST)/xpi-stage/locale-$*' --pkg-dir='$(MOZ_PKG_DIR)' --pkg-format=$(MOZ_PKG_FORMAT) --pkg-filename='$(PACKAGE)' --tar=$(TAR) --output='$(ZIP_OUT)' --moz-widget-toolkit=$(MOZ_WIDGET_TOOLKIT) --os-arch=$(OS_ARCH) --installer-dir='$(DEPTH)/$(MOZ_BUILD_APP)/installer/windows' --real-locale-mergedir='$(REAL_LOCALE_MERGEDIR)' $(addprefix --extra-l10n=,$(MOZ_PKG_EXTRAL10N)) $(if $(filter omni,$(MOZ_PACKAGER_FORMAT)),$(addprefix --non-resource=,$(NON_OMNIJAR_FILES))) $(if $(MOZ_PACKAGER_MINIFY),--minify) $(MOZ_PACKAGE_EXTRA_ARGS))

# Dealing with app sub dirs: If DIST_SUBDIRS is defined it contains a
# listing of app sub-dirs we should include in langpack xpis. If not,
# check DIST_SUBDIR, and if that isn't present, just package the default
# chrome directory and top-level localization for Fluent.
PKG_ZIP_DIRS = chrome localization $(or $(DIST_SUBDIRS),$(DIST_SUBDIR))

merge-%: IS_LANGUAGE_REPACK=1
merge-%: AB_CD=$*
merge-%:
	$(call py_action,l10n_merge,--locale=$* --config=$(srcdir)/l10n.toml --l10n-base=$(L10NBASEDIR) --target=$(BASE_MERGE))

LANGPACK_METADATA = $(LOCALE_SRCDIR)/langpack-metadata.ftl

langpack-%: IS_LANGUAGE_REPACK=1
langpack-%: AB_CD=$*
langpack-%: clobber-%
	@echo 'Making langpack $(LANGPACK_FILE)'
	@$(MAKE) l10n-$(AB_CD)
	@$(MAKE) package-langpack-$(AB_CD)

package-langpack-%: LANGPACK_FILE=$(ABS_DIST)/$(PKG_LANGPACK_PATH)$(PKG_LANGPACK_BASENAME).xpi
package-langpack-%: AB_CD=$*
package-langpack-%:
	$(NSINSTALL) -D $(DIST)/$(PKG_LANGPACK_PATH)
	$(call py_action,package_langpack,--locale=$* --xpi-stage=$(DIST)/xpi-stage/locale-$* --metadata=$(LANGPACK_METADATA) --output=$(LANGPACK_FILE) --eid='$(MOZ_LANGPACK_EID)' --app-version=$(MOZ_APP_VERSION) --max-app-ver=$(MOZ_APP_MAXVERSION) --app-name='$(MOZ_APP_DISPLAYNAME)' --l10n-basedir='$(L10NBASEDIR)' $(addprefix --include=,$(PKG_ZIP_DIRS)) --include=manifest.json)
