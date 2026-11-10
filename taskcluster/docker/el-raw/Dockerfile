ARG BASE_IMAGE
FROM $BASE_IMAGE

# Some EL base images set no PATH, and the base stage needs /usr/sbin for groupadd.
ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

CMD ["/bin/bash", "--login"]

RUN dnf install -y \
      ca-certificates \
    && dnf clean all
