FROM $DOCKER_IMAGE_PARENT
MAINTAINER Mozilla Releng <release@mozilla.com>
 
VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace

ARG TASKCLUSTER_ROOT_URL
ARG DOCKER_IMAGE_PACKAGES

RUN /usr/local/sbin/setup_packages.sh $TASKCLUSTER_ROOT_URL $DOCKER_IMAGE_PACKAGES && \
    apt-get update && \
    apt-get install librsvg2-common flatpak dbus ostree appstream-util appstream-compose && \
    /usr/local/sbin/clean_packages.sh $DOCKER_IMAGE_PACKAGES

# flatpak as non-root wants to connect to the system bus
COPY dbus.sh /usr/local/bin/dbus.sh
RUN chmod +x /usr/local/bin/dbus.sh
ENTRYPOINT ["/usr/local/bin/dbus.sh"]
