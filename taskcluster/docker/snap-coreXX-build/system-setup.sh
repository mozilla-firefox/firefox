#!/bin/bash

set -ex

SNAP_BASE=$1

# Limit cross compilation support to core24 because it's not working before
if [ "${SNAP_BASE}" != "core24" ]; then
    echo "Cross-compilation not supported before core24 base"
    exit 0
fi;

# GPG: we need to do it here otherwhise snapcraft tries to do it as user and fails
CROSS_GPG_KEYID="F6ECB3762474EDA9D21B7022871920D1991BC93C"
KEYRING_FILENAME="/etc/apt/keyrings/craft-$(echo "${CROSS_GPG_KEYID}" | rev | cut -c 1-8 | rev | tr -d '\n').gpg"
gpg --batch --no-default-keyring --with-colons --keyring gnupg-ring:"${KEYRING_FILENAME}" --homedir /tmp --keyserver keyserver.ubuntu.com --recv-keys "${CROSS_GPG_KEYID}"
chmod 0444 "${KEYRING_FILENAME}"

sed -ri 's/^deb http/deb [arch=amd64] http/g' /etc/apt/sources.list

# snapcraft 8.8+ looks for ports.ubuntu.com
# > 2025-04-15 06:31:53.558 Reading sources in '/etc/apt/sources.list.d/ubuntu.sources' looking for 'ports.ubuntu.com/'
sed -ri "s|URIs: http://archive.ubuntu.com/ubuntu/|URIs: http://archive.ubuntu.com/ubuntu/\nArchitectures: amd64|g" /etc/apt/sources.list.d/ubuntu.sources
sed -ri "s|URIs: http://security.ubuntu.com/ubuntu/|URIs: http://security.ubuntu.com/ubuntu/\nArchitectures: amd64|g" /etc/apt/sources.list.d/ubuntu.sources

# This needs to exactly match what snapcraft will produce at https://github.com/canonical/craft-archives/blob/015bbe59d53cad5e262ab7fddba3a6080cb90391/craft_archives/repo/apt_sources_manager.py#L161-L165
# If it's not the case then the file will be re-generated, and snapcraft will try to do things with .sources files and dpkg architecture that will badly break
cat <<EOF > /etc/apt/sources.list.d/craft-http_ports_ubuntu_com.sources
Types: deb
URIs: http://ports.ubuntu.com
Suites: noble noble-updates noble-security noble-backports
Components: main multiverse universe
Architectures: armhf arm64
Signed-By: ${KEYRING_FILENAME}
EOF

# snapcraft 8.8+ will want both and will rewrite ubuntu.sources
dpkg --add-architecture "arm64"
dpkg --add-architecture "armhf"
apt-get update
apt-get --fix-broken install -y
apt-get upgrade -y
