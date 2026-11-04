FROM  $DOCKER_IMAGE_PARENT
LABEL maintainer="Andrew Halberstadt <ahal@mozilla.com>"

VOLUME /builds/worker/artifacts
VOLUME /builds/worker/.task-cache/uv

COPY --from=docker.io/astral/uv:0.13.4 /uv /uvx /bin/

RUN chown -R worker: /builds/worker

ADD system-setup.sh /tmp/system-setup.sh
RUN bash /tmp/system-setup.sh

ADD comm-task-env /builds/worker/bin/comm-task-env

ENV PATH=/builds/worker/bin:$PATH

# Set a default command useful for debugging
CMD ["/bin/bash", "--login"]
