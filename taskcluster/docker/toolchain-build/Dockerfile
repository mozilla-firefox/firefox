FROM $DOCKER_IMAGE_PARENT
MAINTAINER Mike Hommey <mhommey@mozilla.com>

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace
VOLUME /builds/worker/tooltool-cache

ENV XZ_OPT=-T0

ARG DOCKER_IMAGE_PACKAGES
ARG TASKCLUSTER_ROOT_URL
RUN /usr/local/sbin/setup_packages.sh $TASKCLUSTER_ROOT_URL $DOCKER_IMAGE_PACKAGES

ARG CLANG_VER=13

RUN apt-get update && \
    apt-get install \
      autoconf \
      automake \
      bison \
      build-essential \
      cabextract \
      curl \
      clang-${CLANG_VER} \
      cmake \
      flex \
      gawk \
      gcc-multilib \
      git \
      gnupg \
      gpgv \
      jq \
      libbsd-dev \
      libbz2-dev \
      libcap2-bin \
      libexpat1-dev \
      libffi-dev \
      libncursesw5-dev \
      libsqlite3-dev \
      libssl-dev \
      libtool \
      libucl-dev \
      libxml2-dev \
      lld-${CLANG_VER} \
      msitools \
      ninja-build \
      openssh-client \
      pkg-config \
      procps \
      python3-requests \
      python3-toml \
      scons \
      subversion \
      tar \
      unzip \
      uuid \
      uuid-dev \
      wget \
      zip \
      zlib1g-dev

RUN /usr/local/sbin/clean_packages.sh $DOCKER_IMAGE_PACKAGES

# mmdebstrap is not available in jessie, but we only need it in the buster-based image.
# We also want zstd when available.
RUN apt-get install fakechroot fakeroot mmdebstrap zstd || true
