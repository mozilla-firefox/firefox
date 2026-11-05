# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Package up the tests and test harnesses
include $(topsrcdir)/toolkit/mozapps/installer/package-name.mk

PKG_STAGE = $(DIST)/test-stage

stage-all: $(TEST_PKG_STAGE_TARGETS)

PKG_ARG = --$(1) '$(PKG_BASENAME).$(1).tests.$(2)'

test-packages-manifest:
	@rm -f $(MOZ_TEST_PACKAGES_FILE)
	$(NSINSTALL) -D $(dir $(MOZ_TEST_PACKAGES_FILE))
	$(PYTHON3) $(topsrcdir)/build/gen_test_packages_manifest.py \
      --jsshell $(JSSHELL_NAME) \
      --dest-file '$(MOZ_TEST_PACKAGES_FILE)' \
      $(call PKG_ARG,common,zip) \
      $(foreach pkg,$(TEST_PKGS_TARZST),$(call PKG_ARG,$(pkg),tar.zst))

ifdef UPLOAD_PATH
test_archive_dir = $(UPLOAD_PATH)
else
test_archive_dir = $(DIST)/$(PKG_PATH)
endif

package-tests-prepare-dest:
	$(NSINSTALL) -D $(test_archive_dir)

download-wpt-manifest:
	$(call py_action,download_wpt_manifest)

define package_archive
package-tests-$(1): stage-all package-tests-prepare-dest download-wpt-manifest
	$$(call py_action,test_archive, \
		$(1) \
		'$$(abspath $$(test_archive_dir))/$$(PKG_BASENAME).$(1).tests.$(2)')
package-tests: package-tests-$(1)
endef

$(foreach name,$(TEST_PKGS_TARZST),$(eval $(call package_archive,$(name),tar.zst)))

# Prepare _tests before any of the other staging/packaging steps.
# make-stage-dir is a prerequisite to all the stage-* targets in testsuite-targets.mk.
make-stage-dir: install-test-files
	rm -rf $(PKG_STAGE)
	$(NSINSTALL) -D $(PKG_STAGE)
	$(NSINSTALL) -D $(PKG_STAGE)/bin
	$(NSINSTALL) -D $(PKG_STAGE)/bin/components
	$(NSINSTALL) -D $(PKG_STAGE)/certs
	$(NSINSTALL) -D $(PKG_STAGE)/config
	$(NSINSTALL) -D $(PKG_STAGE)/modules
	$(NSINSTALL) -D $(PKG_STAGE)/tools/mach

stage-config: make-stage-dir
	$(NSINSTALL) -D $(PKG_STAGE)/config
	@(cd $(topsrcdir)/testing/config && tar $(TAR_CREATE_FLAGS) - *) | (cd $(PKG_STAGE)/config && tar -xf -)

stage-mach: make-stage-dir
	@(cd $(topsrcdir)/python/mach && tar $(TAR_CREATE_FLAGS) - *) | (cd $(PKG_STAGE)/tools/mach && tar -xf -)
	cp $(topsrcdir)/testing/tools/mach_test_package_initialize.py $(PKG_STAGE)/tools/mach_initialize.py
	cp $(topsrcdir)/mach $(PKG_STAGE)

stage-jstests: make-stage-dir
	$(MAKE) -C $(DEPTH)/js/src/tests stage-package

ifdef OBJCOPY
ifneq ($(OBJCOPY), :) # see build/autoconf/toolchain.m4:102 for why this is necessary
ifdef PKG_STRIP
STRIP_COMPILED_TESTS := 1
endif
endif
endif

stage-gtest: make-stage-dir
	$(NSINSTALL) -D $(PKG_STAGE)/gtest/gtest_bin/gtest
ifdef STRIP_COMPILED_TESTS
# The libxul file basename will vary per platform. Fortunately
# dependentlibs.list always lists the library name as its final line, so we
# can get the value from there.
	LIBXUL_BASE=`tail -1 $(DIST)/bin/dependentlibs.list` && \
        $(OBJCOPY) $(or $(STRIP_FLAGS),--strip-unneeded) \
        $(DIST)/bin/gtest/$$LIBXUL_BASE $(PKG_STAGE)/gtest/gtest_bin/gtest/$$LIBXUL_BASE
else
	cp -RL $(DIST)/bin/gtest $(PKG_STAGE)/gtest/gtest_bin
endif
	cp -RL $(DIST)/bin/gtest/*buildid.* $(PKG_STAGE)/gtest/gtest_bin/gtest
	cp -RL $(DEPTH)/_tests/gtest $(PKG_STAGE)
	cp $(topsrcdir)/testing/gtest/gtest_filter_sets.py $(PKG_STAGE)/gtest
	cp $(topsrcdir)/testing/gtest/gtest_filter_sets.yml $(PKG_STAGE)/gtest
	cp $(topsrcdir)/testing/gtest/rungtests.py $(PKG_STAGE)/gtest
	cp $(topsrcdir)/testing/gtest/remotegtests.py $(PKG_STAGE)/gtest
	cp $(topsrcdir)/testing/gtest/suites.py $(PKG_STAGE)/gtest
	cp $(topsrcdir)/testing/gtest/mach_test_package_commands.py $(PKG_STAGE)/gtest
	cp $(DIST)/bin/dependentlibs.list.gtest $(PKG_STAGE)/gtest
	cp $(DEPTH)/mozinfo.json $(PKG_STAGE)/gtest

stage-android: make-stage-dir
	$(NSINSTALL) $(topsrcdir)/mobile/android/fonts $(DEPTH)/_tests/reftest
	$(NSINSTALL) $(topsrcdir)/mobile/android/fonts $(DEPTH)/_tests/testing/mochitest

ifdef MOZ_COPY_PDBS
CPP_UNIT_TEST_BINS=$(filter-out $(wildcard $(DIST)/cppunittests/*.pdb), $(wildcard $(DIST)/cppunittests/*))
else
CPP_UNIT_TEST_BINS=$(wildcard $(DIST)/cppunittests/*)
endif

stage-cppunittests: make-stage-dir
	$(NSINSTALL) -D $(PKG_STAGE)/cppunittest
ifdef STRIP_COMPILED_TESTS
	$(foreach bin,$(CPP_UNIT_TEST_BINS),$(OBJCOPY) $(or $(STRIP_FLAGS),--strip-unneeded) $(bin) $(bin:$(DIST)/cppunittests/%=$(PKG_STAGE)/cppunittest/%);)
else
	cp -RL $(CPP_UNIT_TEST_BINS) $(PKG_STAGE)/cppunittest
endif
ifdef MOZ_COPY_PDBS
	cp -RL $(addsuffix .pdb,$(basename $(CPP_UNIT_TEST_BINS))) $(PKG_STAGE)/cppunittest
endif
ifdef STRIP_COMPILED_TESTS
	$(OBJCOPY) $(or $(STRIP_FLAGS),--strip-unneeded) $(DIST)/bin/jsapi-tests$(BIN_SUFFIX) $(PKG_STAGE)/cppunittest/jsapi-tests$(BIN_SUFFIX)
else
	cp -RL $(DIST)/bin/jsapi-tests$(BIN_SUFFIX) $(PKG_STAGE)/cppunittest
endif
ifdef MOZ_COPY_PDBS
	cp -RL $(DIST)/bin/jsapi-tests.pdb $(PKG_STAGE)/cppunittest
endif

TEST_EXTENSIONS := \
    specialpowers@mozilla.org.xpi \
	$(NULL)

stage-extensions: make-stage-dir
	$(NSINSTALL) -D $(PKG_STAGE)/extensions/
	@$(foreach ext,$(TEST_EXTENSIONS), cp -RL $(DIST)/xpi-stage/$(ext) $(PKG_STAGE)/extensions;)


.PHONY: \
  reftest \
  crashtest \
  xpcshell-tests \
  jstestbrowser \
  package-tests \
  download-wpt-manifest \
  package-tests-prepare-dest \
  package-tests-common \
  make-stage-dir \
  stage-all \
  stage-config \
  stage-jstests \
  stage-android \
  test-packages-manifest \
  check \
  $(NULL)
