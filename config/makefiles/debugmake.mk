#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
#

###########################################################################
## Intent: Helper targets for displaying variables and state information
###########################################################################

# Support usage outside of config/rules.mk
ifndef INCLUDED_DEBUGMAKE_MK #{

define CR


endef

define shell_quote
'$(subst $(CR),\$(CR),$(subst ','\'',$(1)))'
endef

echo-variable-%:
	@echo $(call shell_quote,$($*))

echo-dirs:
	@echo $(call shell_quote,$(DIRS))

define print_var
@printf '%20s = %s\n' $1 $(call shell_quote,$($1))

endef

define print_vars
$(foreach var,$1,$(call print_var,$(var)))
endef

showtargs:
ifneq (,$(filter $(PROGRAM) $(HOST_PROGRAM) $(SIMPLE_PROGRAMS) $(LIBRARY) $(SHARED_LIBRARY),$(TARGETS)))
	@echo --------------------------------------------------------------------------------
	$(call print_vars,\
		PROGRAM \
		SIMPLE_PROGRAMS \
		LIBRARY \
		SHARED_LIBRARY \
		LIBS \
		DEF_FILE \
		IMPORT_LIBRARY \
		STATIC_LIBS \
		SHARED_LIBS \
		EXTRA_DSO_LDOPTS \
		DEPENDENT_LIBS \
	)
	@echo --------------------------------------------------------------------------------
endif
	$(LOOP_OVER_DIRS)

showbuild showhost: _DEPEND_CFLAGS=
showbuild:
	$(call print_vars,\
		MOZ_WIDGET_TOOLKIT \
		CC \
		CXX \
		CCC \
		CPP \
		LD \
		AR \
		MKSHLIB \
		MKCSHLIB \
		RC \
		CFLAGS \
		OS_CFLAGS \
		COMPILE_CFLAGS \
		CXXFLAGS \
		OS_CXXFLAGS \
		COMPILE_CXXFLAGS \
		COMPILE_CMFLAGS \
		COMPILE_CMMFLAGS \
		LDFLAGS \
		OS_LDFLAGS \
		DSO_LDOPTS \
		OS_INCLUDES \
		OS_LIBS \
		BIN_FLAGS \
		INCLUDES \
		DEFINES \
		ACDEFINES \
		BIN_SUFFIX \
		LIB_SUFFIX \
		DLL_SUFFIX \
		IMPORT_LIB_SUFFIX \
		INSTALL \
		VPATH \
	)

showhost:
	$(call print_vars,\
		HOST_CC \
		HOST_CXX \
		HOST_CFLAGS \
		HOST_LDFLAGS \
		HOST_LIBS \
		HOST_EXTRA_LIBS \
		HOST_PROGRAM \
		HOST_OBJS \
		HOST_PROGOBJS \
	)

INCLUDED_DEBUGMAKE_MK = 1
endif #}
