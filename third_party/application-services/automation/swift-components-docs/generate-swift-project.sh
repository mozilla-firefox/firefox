#!/usr/bin/env bash

# This script builds the Rust crate in its directory and generates Swift bindings,
# headers, and a module map using UniFFI in library mode.

# Ensure script exits on errors or unset variables
set -euo pipefail

FRAMEWORK_NAME="SwiftComponents"
THIS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
WORKING_DIR="$THIS_DIR"

# Create directories for Swift files, headers, and module map
INCLUDE_DIR="$WORKING_DIR/Sources/$FRAMEWORK_NAME/include"
SWIFT_DIR="$WORKING_DIR/Sources/$FRAMEWORK_NAME"
mkdir -p "$INCLUDE_DIR"

# Generate Swift bindings, headers, and module map to use as the documentation source
echo "Generating Swift bindings"
cargo uniffi-bindgen-library-mode -m megazord_ios swift --swift-sources "$SWIFT_DIR"
cargo uniffi-bindgen-library-mode -m megazord_ios swift --headers --modulemap --modulemap-filename module.modulemap "$INCLUDE_DIR"

# Remove the glean-sym Swift bindings, these are only there so the hand-written black-magic in glean-sym
# can use them.  `build_against_ios.py` removes it and we should too.
rm "${SWIFT_DIR}/glean_sym.swift"

# Success message
echo "Successfully generated Swift bindings, headers, and module map."
