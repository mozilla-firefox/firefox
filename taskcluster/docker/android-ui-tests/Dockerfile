ARG DOCKER_IMAGE_PARENT
FROM $DOCKER_IMAGE_PARENT

LABEL authors="Richard Pappalardo <rpappalax@gmail.com>, Aaron Train <atrain@mozilla.com>"
LABEL maintainer="Richard Pappalardo <rpappalax@gmail.com>"

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace
VOLUME /builds/worker/gradle-user-home

ENV CURL='curl --location --retry 5' \
    GRADLE_OPTS='-Xmx4096m -Dorg.gradle.parallel=false' \
    LANG='en_US.UTF-8' \
    TERM='dumb'

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
      wget \
      x11-utils \
      xvfb \
      zip \
      zstd

RUN pip3 install taskcluster==102.0.1

RUN locale-gen "$LANG"

#----------------------------------------------------------------------------------------------------------------------
#-- Test tools --------------------------------------------------------------------------------------------------------
#----------------------------------------------------------------------------------------------------------------------

RUN chown -R worker:worker ${HOME}/.config

USER worker:worker

ENV GOOGLE_SDK_DOWNLOAD ./gcloud.tar.gz
ENV GOOGLE_SDK_VERSION 577

ENV TEST_TOOLS /builds/worker/test-tools
ENV PATH ${PATH}:${TEST_TOOLS}:${TEST_TOOLS}/google-cloud-sdk/bin

RUN mkdir -p ${TEST_TOOLS} && \
    mkdir -p ${HOME}/.config/gcloud

RUN curl https://dl.google.com/dl/cloudsdk/channels/rapid/downloads/google-cloud-sdk-${GOOGLE_SDK_VERSION}.0.0-linux-x86_64.tar.gz --output ${GOOGLE_SDK_DOWNLOAD} \
    && tar -xvf ${GOOGLE_SDK_DOWNLOAD} -C ${TEST_TOOLS} \
    && rm -f ${GOOGLE_SDK_DOWNLOAD} \
    && ${TEST_TOOLS}/google-cloud-sdk/install.sh --quiet \
    && ${TEST_TOOLS}/google-cloud-sdk/bin/gcloud --quiet components update

ENV FLANK_DOWNLOAD ${TEST_TOOLS}/flank.jar
ENV FLANK_VERSION 23.10.1
RUN curl -L https://github.com/Flank/flank/releases/download/v${FLANK_VERSION}/flank.jar --output ${FLANK_DOWNLOAD} \
    && chmod +x ${FLANK_DOWNLOAD}

# Used in taskcluster/scripts/tests/parse-junit-results.py for parsing and displaying JUnit XML results
# junitparser 5.x requires Python >=3.10 (satisfied by ubuntu2204-base)
RUN pip3 install junitparser==5.0.1 beautifultable==1.1.0

# Used for env var ingest in testrail.py
RUN pip3 install python-dotenv==1.2.2

# run-task expects to run as root
USER root
