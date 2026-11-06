#! gmake
#
# Creates man pages for the NSS security tools
#
# pk12util, certutil, modutil, ssltap, 
# signver, cmsutil, crlutil,  
# derdump, pp, vfychain, vfyserv
#

.SUFFIXES: .html .txt .1 .xml

COMPILE.1 = xmlto -o nroff man
COMPILE.html = xmlto -o html html

# the name of the tar ball
name = nss-man
date = `date +"%Y%m%d"`

all: prepare all-man all-html

prepare: date-and-version
	mkdir -p html
	mkdir -p nroff

clean:
	rm -f date.xml version.xml *.tar.bz2
	rm -f html/*.proc
	rm -fr $(name) ascii

date-and-version: date.xml version.xml

date.xml:
	date +"%e %B %Y" | tr -d '\n' > $@

version.xml:
	echo -n ${VERSION} > $@

.PHONY : $(MANPAGES)
.PHONY : $(HTMLPAGES)
.PHONY : $(TXTPAGES)

#--------------------------------------------------------
# manpages
#--------------------------------------------------------

nroff/%.1 : %.xml
	$(COMPILE.1) $<

MANPAGES = \
nroff/certutil.1 nroff/cmsutil.1 nroff/crlutil.1 nroff/pk12util.1 \
nroff/modutil.1 nroff/ssltap.1 nroff/derdump.1 nroff/signver.1 \
nroff/pp.1 nroff/vfychain.1 nroff/vfyserv.1 nroff/nss-policy-check.1

all-man: prepare $(MANPAGES)

#--------------------------------------------------------
# html pages
#--------------------------------------------------------

html/%.html : %.xml
	$(COMPILE.html) $<
	mv html/index.html $@

HTMLPAGES = \
html/certutil.html html/cmsutil.html html/crlutil.html html/pk12util.html html/modutil.html \
html/ssltap.html html/derdump.html html/signver.html html/pp.html \
html/vfychain.html html/vfyserv.html html/nss-policy-check.html

all-html: prepare $(HTMLPAGES)
