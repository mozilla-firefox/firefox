FROM $DOCKER_IMAGE_PARENT

### Add worker user and setup its workspace.
RUN mkdir -p /builds && \
    groupadd -g 1000 worker && \
    useradd -u 1000 -g 1000 -d /builds/worker -s /bin/bash -m worker && \
    mkdir -p /builds/worker/workspace && \
    chown -R worker:worker /builds

# Declare default working folder
WORKDIR /builds/worker

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace

# Set variable normally configured at login, by the shells parent process, these
# are taken from GNU su manual
ENV HOME=/builds/worker \
    SHELL=/bin/bash \
    USER=worker \
    LOGNAME=worker \
    HOSTNAME=taskcluster-worker

CMD ["/bin/bash", "--login"]

# EL8 has no EPEL mercurial, so it comes from PyPI; EL9 and EL10 get it from EPEL.
RUN . /etc/os-release && \
    case "${VERSION_ID%%.*}" in \
        8) \
            dnf install -y git python3 python3.11 python3.11-pip && \
            python3.11 -m pip install --no-cache-dir mercurial==7.2.2 \
            ;; \
        *) \
            dnf install -y epel-release && \
            dnf install -y git python3 mercurial \
            ;; \
    esac && \
    dnf clean all

# %include taskcluster/scripts/robustcheckout.py
COPY topsrcdir/taskcluster/scripts/robustcheckout.py /usr/local/mercurial/robustcheckout.py

# %include taskcluster/docker/recipes/hgrc
COPY topsrcdir/taskcluster/docker/recipes/hgrc /etc/mercurial/hgrc.d/mozilla.rc

# %include taskcluster/scripts/run-task
COPY topsrcdir/taskcluster/scripts/run-task /builds/worker/bin/run-task-hg

# Setup upstream `run-task` and `fetch-content`
# %include-run-task

RUN chown -R worker:worker /builds/worker/bin && chmod 755 /builds/worker/bin/*
