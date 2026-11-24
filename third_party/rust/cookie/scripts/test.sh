#!/bin/bash

set -e

cargo build --verbose

cargo test --verbose --features percent-encode
cargo test --verbose --features private
cargo test --verbose --features signed
cargo test --verbose --features secure
cargo test --verbose --features 'private,key-expansion'
cargo test --verbose --features 'signed,key-expansion'
cargo test --verbose --features 'secure,percent-encode'

cargo test --verbose
cargo test --verbose --no-default-features
cargo test --verbose --all-features

rustdoc --test README.md -L target
