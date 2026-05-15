FROM $DOCKER_IMAGE_PARENT
LABEL maintainer="Alex Hochheiden ahochheiden@mozilla.com"

VOLUME  /builds/worker/checkouts

ADD prepare.sh /setup/prepare-docker.sh
ADD submit_sentry_release.sh /usr/bin/submit_sentry_release.sh
ADD upload_proguard_mapping.sh /usr/bin/upload_proguard_mapping.sh
RUN /bin/bash /setup/prepare-docker.sh && rm -R /setup

# Set a default command useful for debugging
CMD ["/bin/sh", "--login"]
