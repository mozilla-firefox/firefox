#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# functions.mk
#
# Defines functions that are needed by various Makefiles throughout the build
# system, which are needed before config.mk can be included.
#

# Define an include-at-most-once flag
ifdef INCLUDED_FUNCTIONS_MK
$(error Do not include functions.mk twice!)
endif
INCLUDED_FUNCTIONS_MK = 1

ifdef MACH
ifndef NO_BUILDSTATUS_MESSAGES
define BUILDSTATUS
@echo 'BUILDSTATUS@$(relativesrcdir) $1'

endef
endif
endif

core_abspath = $(error core_abspath is unsupported, use $$(abspath) instead)
core_realpath = $(error core_realpath is unsupported)

core_winabspath = $(error core_winabspath is unsupported)

# Run a named Python build action. The first argument is the name of the build
# action. The second argument are the arguments to pass to the action (space
# delimited arguments). e.g.
#
#   libs::
#       $(call py_action,purge_manifests,_build_manifests/purge/foo.manifest)
#
# The first argument can optionally contain the name of the file being created
# or processed. e.g.
#   libs::
#       $(call py_action,purge_manifests foo.manifest,_build_manifests/purge/foo.manifest)
# This optional name will be displayed in build profiles.
#
# The optional third argument changes the working directory before running.
#
# The optional fourth argument is a command wrapper (e.g. $(CCACHE)) that will
# be prepended to the Python invocation.
define py_action
$(call BUILDSTATUS,START_$(firstword $(1)) $(or $(word 2,$(1)),$(2)))
$(if $(3),cd $(3) && )$(4) $(PYTHON3) -m mozbuild.action.$(firstword $(1)) $(2)
$(call BUILDSTATUS,END_$(firstword $(1)) $(or $(word 2,$(1)),$(2)))

endef
