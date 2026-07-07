FROM debian:13.5

COPY setup.sh /root
RUN cd /root && ./setup.sh

RUN useradd -d /home/worker -s /bin/bash -m worker
USER worker
WORKDIR /home/worker
CMD /bin/bash
