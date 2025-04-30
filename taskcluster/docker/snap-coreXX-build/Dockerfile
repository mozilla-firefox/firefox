FROM          $DOCKER_IMAGE_PARENT
MAINTAINER    Gabriele Svelto <gsvelto@mozilla.com>

RUN mkdir -p /builds
RUN id worker || useradd -d /builds/worker -s /bin/bash -m worker
WORKDIR /builds/worker

# We need to declare all potentially cache volumes as caches. Also,
# making high I/O paths volumes increase I/O throughput because of
# AUFS slowness.
VOLUME /builds/worker/checkouts

ARG EXTRA_PACKAGES

RUN apt-get update && \
    apt-get install -y \
        build-essential \
        curl \
        $EXTRA_PACKAGES \
        gnupg \
        jq \
        patch \
        patchelf \
        python3-dev \
        python3-yaml \
        squashfs-tools \
        tar \
        unzip \
        uuid \
        wget \
        zip

ARG SNAP_BASE

COPY install-snap.sh /usr/local/bin/

RUN install-snap.sh core

RUN install-snap.sh snapcraft

RUN install-snap.sh $SNAP_BASE

ARG SNAP_LIST

RUN for snap in $SNAP_LIST; do install-snap.sh "${snap}"; done

# Create a snapcraft runner
RUN mkdir -p /snap/bin
RUN echo "#!/bin/sh" > /snap/bin/snapcraft
RUN snap_version="$(awk '/^version:/{print $2}' /snap/snapcraft/current/meta/snap.yaml)" && echo "export SNAP_VERSION=\"$snap_version\"\nexport PATH=\$SNAP/bin/:\$SNAP/libexec/snapcraft/:\$PATH" >> /snap/bin/snapcraft
RUN echo 'exec "$SNAP/bin/snapcraft" "$@"' >> /snap/bin/snapcraft
RUN chmod +x /snap/bin/snapcraft

# Generate locale
RUN apt update && apt dist-upgrade --yes && apt install --yes sudo snapd locales && locale-gen en_US.UTF-8 && apt-get autoremove -y

# Set the proper environment
ENV LANG="en_US.UTF-8"
ENV LANGUAGE="en_US:en"
ENV LC_ALL="en_US.UTF-8"
ENV PATH="/snap/bin:$PATH"
ENV SNAP="/snap/snapcraft/current"
ENV SNAP_NAME="snapcraft"
ENV SNAP_ARCH="amd64"
ENV SNAP_INSTANCE_KEY=
ENV SNAP_INSTANCE_NAME=snapcraft
ENV SNAPCRAFT_PLATFORM=

RUN snapcraft --version

# Snapcraft pull will need to sudo apt-get update ...
RUN echo "worker ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/worker && \
    chmod 0440 /etc/sudoers.d/worker

COPY system-setup.sh /builds/worker/system-setup.sh

RUN /builds/worker/system-setup.sh "${SNAP_BASE}"

COPY run.sh /builds/worker/run.sh

COPY parse.py /builds/worker/parse.py

COPY patches /builds/worker/patches/

RUN chown -R worker:worker /builds/worker

# Set a default command useful for debugging
CMD ["/bin/bash", "--login"]
