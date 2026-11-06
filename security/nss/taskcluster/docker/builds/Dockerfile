# Dockerfile for building extra builds. This includes more tools than the
# default image, so it's a fair bit bigger. Only use this for builds where the
# smaller docker image is missing something. These builds will run on the
# leaner configuration.
#
# This carries the modern toolchain (noble's clang, plus a newer clang from
# apt.llvm.org). The legacy/RHEL-floor compilers live in the builds-legacy
# image instead.
FROM $DOCKER_IMAGE_PARENT

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace
VOLUME /builds/worker/.cache

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    clang \
    libclang-rt-18-dev \
    cmake \
    libelf-dev \
    libdw-dev \
    libssl-dev \
    libssl-dev:i386 \
    valgrind \
    libabigail-dev \
    abigail-tools \
 && rm -rf /var/lib/apt/lists/* \
 && apt-get autoremove -y && apt-get clean -y

# Keep the version in sync with llvm.sources and EXTRA_COMPILERS in
# taskcluster/nss_taskgraph/transforms/build.py.
COPY apt.llvm.org.asc /etc/apt/trusted.gpg.d/
COPY llvm.sources /etc/apt/sources.list.d/
RUN apt-get update \
 && apt-get install -y --no-install-recommends clang-23 \
 && rm -rf /var/lib/apt/lists/* \
 && apt-get autoremove -y && apt-get clean -y

# Install golang for bogo tests.
RUN curl https://dl.google.com/go/go1.23.1.linux-amd64.tar.gz -sLf | tar xzf - -C $HOME
ENV PATH "$PATH:$HOME/go/bin"

# Set a default command for debugging.
CMD ["/bin/bash", "--login"]
