FROM $DOCKER_IMAGE_PARENT
MAINTAINER Mozilla Releng <release@mozilla.com>

VOLUME /builds/worker/workspace

ARG TASKCLUSTER_ROOT_URL
ARG DOCKER_IMAGE_PACKAGES
RUN /usr/local/sbin/setup_packages.sh $TASKCLUSTER_ROOT_URL $DOCKER_IMAGE_PACKAGES && \
    apt-get update && \
    apt-get install -y rpm
