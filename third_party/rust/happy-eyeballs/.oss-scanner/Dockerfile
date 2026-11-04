# Image for https://github.com/anthropics/oss-scanner. Everything the scanner may need must be fetched and built here;
# the scan itself runs offline.
FROM rust:1-trixie

RUN rustup component add clippy rustfmt

COPY . /src
WORKDIR /src

# Fetch every dependency and prebuild the library and tests, because the agent has only 2 CPUs.
ENV CARGO_NET_OFFLINE=true
RUN CARGO_NET_OFFLINE=false cargo fetch --locked \
    && cargo build --locked --all-targets \
    && cargo test --locked --no-run

# Check the image works, but don't fail the build on test failures.
RUN cargo test --locked || echo "WARNING: cargo test reported failures"
