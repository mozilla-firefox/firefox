#!/usr/bin/env bash

# This script builds the Rust crate in its directory and generates Kotlin bindings,
# using UniFFI in library mode.

# Ensure script exits on errors or unset variables
set -euo pipefail

THIS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
REPO_ROOT="$( dirname "$( dirname "$THIS_DIR" )" )"
WORKING_DIR=$THIS_DIR
CARGO="$HOME/.cargo/bin/cargo"

case "$(uname -s)" in
    Linux*)               DLL_SUFFIX=".so" ;;
    Darwin*)              DLL_SUFFIX=".dylib" ;;
    CYGWIN*|MINGW*|MSYS*) DLL_SUFFIX=".dll" ;;
    *)                    DLL_SUFFIX=".so" ;;
esac

case "$(uname -s)" in
    CYGWIN*|MINGW*|MSYS*) DLL_PREFIX="" ;;
    *)                    DLL_PREFIX="lib" ;;
esac

# Set the documentation directory
KOTLIN_DIR="$WORKING_DIR/src/main/kotlin"

# Generate Kotlin bindings to use as the documentation source
echo "Generating Kotlin bindings with uniffi-bindgen..."

cd "$REPO_ROOT"

$CARGO build -p megazord
$CARGO uniffi-bindgen generate --language kotlin --no-format --out-dir "$KOTLIN_DIR" "target/debug/${DLL_PREFIX}megazord${DLL_SUFFIX}"

# Generate documentation with increased memory
(cd "$WORKING_DIR" && "$REPO_ROOT/gradlew" --max-workers=2 dokkaGenerate -Dorg.gradle.vfs.watch=false)
echo "Documentation successfully generated"
