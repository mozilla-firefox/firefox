FROM $DOCKER_IMAGE_PARENT
MAINTAINER kshampur <kshampur@mozilla.com>

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace
VOLUME /builds/worker/tooltool-cache

ENV XZ_OPT=-T0

RUN apt-get update && \
    apt-get install \
      bzip2 \
      curl \
      libatomic1 \
      libbz2-dev \
      libglib2.0-dev \
      libtinfo5 \
      pkg-config \
      tar \
      unzip \
      wget \
      zip \
      lsb-release \
      gperf \
      procps \
      libdbus-1-3 \
      libx11-6 \
      libx11-xcb1 \
      libxau6 \
      libxcb1 \
      libxcb-cursor0 \
      libxcomposite1 \
      libxcursor1 \
      libxdamage1 \
      libxdmcp6 \
      libxext6 \
      libxfixes3 \
      libxi6 \
      libxinerama1 \
      libxkbcommon0 \
      libxrandr2 \
      libxrender1 \
      libxss1 \
      libxtst6
