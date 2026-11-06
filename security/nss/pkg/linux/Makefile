#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

CORE_DEPTH = ../../..

NAME        = sun-nss
ifndef RPM_RELEASE
RPM_RELEASE = 1
endif
VERSION     = `grep NSS_VERSION $(CORE_DEPTH)/../dist/public/nss/nss.h \
		| head -1 \
		| sed -e 's/[^"]*"//' -e 's/".*//' -e 's/ .*//'`
PWD         = `pwd`
BUILDROOT   = $(PWD)\/$(NAME)-root

include $(CORE_DEPTH)/coreconf/config.mk

# Force i386 for non 64 bit build
ifneq ($(USE_64),1)
	RPMTARGET = "--target=i386"
	RPMLIBDIR = lib
else
	RPMLIBDIR = lib64
endif


publish:
	$(MAKE) clean
	mkdir -p SOURCES SRPMS RPMS BUILD
	mkdir -p opt/sun/private/$(RPMLIBDIR) 
	find $(CORE_DEPTH)/../dist/$(OBJDIR)/lib -type l \
		\( -name "*.so" -o -name "*.chk" \) \
	-exec cp {} opt/sun/private/$(RPMLIBDIR) \;
	rm -f opt/sun/private/$(RPMLIBDIR)/libnspr4.so \
	   opt/sun/private/$(RPMLIBDIR)/libplc4.so \
	   opt/sun/private/$(RPMLIBDIR)/libplds4.so \
	   opt/sun/private/$(RPMLIBDIR)/libjss*.so
	mkdir -p opt/sun/private/bin
	(cd $(CORE_DEPTH)/../dist/$(OBJDIR)/bin && tar cphf - \
		certutil cmsutil crlutil modutil pk12util \
		signver ssltap addbuiltin ) | (cd opt/sun/private/bin && tar xvfBp -)
	(cd $(CORE_DEPTH)/../dist/public && tar cphf - .) \
		| (mkdir -p opt/sun/private/include && cd opt/sun/private/include && tar xvfBp -)
	rm -rf opt/sun/private/include/seccmd
	rm -rf opt/sun/private/include/dbm

	tar czvf $(NAME)-$(VERSION).tar.gz opt 
	echo "%define _topdir `pwd`" >temp.spec
	sed -e "s/NAME_REPLACE/$(NAME)/" \
		-e "s/VERSION_REPLACE/$(VERSION)/" \
		-e "s/RELEASE_REPLACE/$(RPM_RELEASE)/" \
	<$(NAME).spec >>temp.spec
	echo "" >>temp.spec
	echo "%files" >>temp.spec
	echo "%defattr(-,root,root)" >>temp.spec
	echo "%dir /opt" >>temp.spec
	echo "%dir /opt/sun" >>temp.spec
	echo "%dir /opt/sun/private" >>temp.spec
	echo "%dir /opt/sun/private/$(RPMLIBDIR)" >>temp.spec
	echo "%dir /opt/sun/private/bin" >>temp.spec
	find opt \( -name "*.so" -o -name "*.chk" -o -type f \
		-perm u=rwx,g=rx,o=rx \) | sed -e "s-^-/-" >>temp.spec
	echo "" >>temp.spec
	echo "%files devel" >>temp.spec
	echo "%defattr(-,root,root)" >>temp.spec
	echo "%dir /opt" >>temp.spec
	echo "%dir /opt/sun" >>temp.spec
	echo "%dir /opt/sun/private" >>temp.spec
	echo "%dir /opt/sun/private/include" >>temp.spec
	echo "%dir /opt/sun/private/include/nss" >>temp.spec
	find opt -type f \( -name "*.h" \) \
		| sed -e "s-^-/-" >>temp.spec
	cp $(NAME)-$(VERSION).tar.gz SOURCES
	rpmbuild $(RPMTARGET) -bb temp.spec

clean::
	rm -rf SOURCES SRPMS RPMS BUILD
	rm -rf opt 
	rm -f temp.spec
	rm -f $(NAME)-$(VERSION).tar.gz

include $(CORE_DEPTH)/coreconf/rules.mk
