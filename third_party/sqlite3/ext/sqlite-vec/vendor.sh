#!/bin/bash

# IMPORTANT: use `./mach vendor third_party/sqlite3/ext-sqlite-vec.yaml`,
#            don't invoke this script directly.

# Script to download header from sqlite-vec extension amalgamation.

set -e

# Retrieve latest version value.
echo ""
echo "Get extension version."
version=`cat VERSION.txt`
echo "Github version: $version";

# Retrieve files and update sources.

echo ""
echo "Retrieving amalgamation..."
amalgamation_url=""https://github.com/asg017/sqlite-vec/releases/download/v$version/sqlite-vec-$version-amalgamation.zip""
wget -t 3 --retry-connrefused -w 5 --random-wait $amalgamation_url -qO amalgamation.zip
echo "Unpacking source files..."
unzip -p "amalgamation.zip" "sqlite-vec.h" > "sqlite-vec.h"
rm -f "amalgamation.zip"

echo ""
echo "Update complete, please commit and check in your changes."
echo ""
