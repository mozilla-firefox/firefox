FROM $DOCKER_IMAGE_PARENT
MAINTAINER Geoff Brown <gbrown@mozilla.com>

# This is the image used for most build and test tasks related to
# android-components, focus, and fenix.

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace
VOLUME /builds/worker/tooltool-cache
VOLUME /builds/worker/gradle-user-home

ENV CURL='curl --location --retry 5' \
    GRADLE_OPTS='-Xmx4096m -Dorg.gradle.parallel=false' \
    LANG='en_US.UTF-8' \
    TERM='dumb'

# less, screen, and vim help debugging interactive tasks in taskcluster
# yaml required by some ui-test tasks
RUN apt-get update && \
    apt-get install \
      build-essential \
      base-files \
      ccache \
      cmake \
      curl \
      file \
      gnupg \
      jq \
      less \
      locales \
      openjdk-25-jdk \
      openssh-client \
      procps \
      python3-dev \
      python3-pip \
      python3-yaml \
      rsync \
      screen \
      sudo \
      tar \
      unzip \
      uuid \
      valgrind \
      vim \
      x11-utils \
      xvfb \
      wget \
      zip \
      zstd

RUN pip3 install --break-system-packages taskcluster

RUN locale-gen "$LANG"

# Back to the workdir, matching desktop-build.
WORKDIR /builds/worker
