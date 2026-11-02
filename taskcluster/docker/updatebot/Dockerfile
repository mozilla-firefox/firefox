FROM $DOCKER_IMAGE_PARENT
MAINTAINER Tom Ritter <tom@mozilla.com>

VOLUME /builds/worker/checkouts

COPY 	install-node-for-pdfjs.sh 	/setup/install-node-for-pdfjs.sh
COPY    privileged-setup.sh     /setup/privileged-setup.sh
COPY    arcanist_log_invalid_json.patch /setup/arcanist_log_invalid_json.patch
COPY    arcanist_no_gzip.patch  /setup/arcanist_no_gzip.patch
COPY    updatebot-version.sh    /setup/updatebot-version.sh
COPY    evolve-requirements.txt /setup/evolve-requirements.txt
COPY    claude-code             /setup/claude-code
COPY    setup.sh                /builds/worker/setup.sh
COPY    run.py                  /builds/worker/run.py
COPY    hgrc                    /etc/mercurial/hgrc.d/updatebot.rc

RUN     cd /setup && ./privileged-setup.sh

ENV     HOME                /builds/worker
ENV     SHELL               /bin/bash
ENV     USER                worker
ENV     LOGNAME             worker
ENV     PYTHONUNBUFFERED    1
# Pin the Claude Code CLI (installed in privileged-setup.sh) and stop it from
# updating itself at runtime, so the version we execute is reproducible.
ENV     DISABLE_AUTOUPDATER 1
ENV     PATH                "/builds/worker/go/bin:/builds/worker/fetches/rustc/bin:/builds/worker/fetches/cargo-vet:${PATH}"

RUN     cd /builds/worker && ./setup.sh
