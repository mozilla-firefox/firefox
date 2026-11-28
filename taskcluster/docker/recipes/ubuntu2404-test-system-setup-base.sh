#!/usr/bin/env bash

set -ve

test "$(whoami)" == 'root'

# We do want to install recommended packages.
sed -i /APT::Install-Recommends/d /etc/apt/apt.conf.d/99taskcluster

# To speed up docker image build times as well as number of network/disk I/O
# build a list of packages to be installed and call it in one go.
apt_packages=()

ARCH="${1:-amd64}"

if [[ "$ARCH" == "amd64" ]]; then
    apt_packages+=('g++-multilib')
    apt_packages+=('gcc-multilib')
fi

apt_packages+=('bluez-cups')
apt_packages+=('build-essential')
apt_packages+=('ca-certificates')
apt_packages+=('ccache')
apt_packages+=('curl')
apt_packages+=('dbus-x11')
apt_packages+=('fontconfig')
apt_packages+=('fonts-dejavu')
apt_packages+=('fonts-kacst')
apt_packages+=('fonts-kacst-one')
apt_packages+=('fonts-liberation')
apt_packages+=('fonts-stix')
apt_packages+=('fonts-unfonts-core')
apt_packages+=('fonts-unfonts-extra')
apt_packages+=('fonts-vlgothic')
apt_packages+=('gir1.2-gnomebluetooth-3.0')
apt_packages+=('gnome-icon-theme')
apt_packages+=('gnome-keyring')
apt_packages+=('gstreamer1.0-gtk3')
apt_packages+=('gstreamer1.0-plugins-base')
apt_packages+=('gstreamer1.0-plugins-good')
apt_packages+=('gstreamer1.0-tools')
apt_packages+=('gstreamer1.0-pulseaudio')
apt_packages+=('language-pack-en-base')
apt_packages+=('libc6-dbg')
apt_packages+=('libasound2-dev')
apt_packages+=('libavcodec-extra60')
apt_packages+=('libcanberra-gtk3-module')
apt_packages+=('libcanberra-pulse')
apt_packages+=('libcurl4-openssl-dev')
apt_packages+=('libdbus-1-dev')
apt_packages+=('libfreetype6')
apt_packages+=('libgstreamer-plugins-base1.0-dev')
apt_packages+=('libgstreamer1.0-dev')
apt_packages+=('libgtk-3-0t64')
apt_packages+=('libiw-dev')
apt_packages+=('libx11-xcb1')
apt_packages+=('libxcb1')
apt_packages+=('libxcb-render0')
apt_packages+=('libxcb-shm0')
apt_packages+=('libxcb-glx0')
apt_packages+=('libxcb-shape0')
apt_packages+=('libnotify-dev')
apt_packages+=('libpulse0')
apt_packages+=('libpulse-dev')
apt_packages+=('libxt-dev')
apt_packages+=('libxxf86vm1')
apt_packages+=('llvm')
apt_packages+=('llvm-dev')
apt_packages+=('llvm-runtime')
apt_packages+=('mesa-common-dev')
apt_packages+=('net-tools')
apt_packages+=('pulseaudio-utils')
apt_packages+=('python3-pip')
apt_packages+=('python3-pyatspi')
apt_packages+=('qemu-system-x86')
apt_packages+=('rlwrap')
apt_packages+=('screen')
apt_packages+=('software-properties-common')
apt_packages+=('sudo')
apt_packages+=('ubuntu-desktop')
apt_packages+=('unzip')
apt_packages+=('uuid')
apt_packages+=('wget')
apt_packages+=('xvfb')
apt_packages+=('xwit')
apt_packages+=('yasm')
apt_packages+=('zip')

# Bug 1232407 - this allows the user to start vnc
apt_packages+=('x11vnc')

# Bug 1176031 - need `xset` to disable screensavers
apt_packages+=('x11-xserver-utils')

# Build a list of packages to install from the multiverse repo.
apt_packages+=('ubuntu-restricted-extras')

# Keep mesa packages on an older snapshot until webgl tests pass with
# a newer version
cat > /etc/apt/sources.list.d/extra.list <<EOF
deb [snapshot=no] https://snapshot.ubuntu.com/ubuntu/20250716T072533Z/ noble-updates main universe
EOF

cat > /etc/apt/preferences.d/99extra <<EOF
Package: src:mesa:any
Pin: version 24.2.8-1ubuntu1~24.04.1
Pin-Priority: 1002
EOF

# APT update takes very long on Ubuntu. Run it at the last possible minute.
apt-get update

# Also force the cleanup after installation of packages to reduce image size.
apt-get install --allow-downgrades "${apt_packages[@]}"

# libgallium debug symbols
wget -O /tmp/mesa-libgallium-dbgsym.ddeb "https://launchpad.net/ubuntu/+archive/primary/+files/mesa-libgallium-dbgsym_24.2.8-1ubuntu1~24.04.1_$ARCH.ddeb"
dpkg -i /tmp/mesa-libgallium-dbgsym.ddeb

# gsd-power can't start without logind, but it's marked as required in the
# gnome-session config; remove it so the session doesn't start with the fail
# whale
sed -i 's/org.gnome.SettingsDaemon.Power;//' /usr/share/gnome-session/sessions/ubuntu.session

if [[ "$ARCH" == "amd64" ]]; then
    # Enable i386 packages
    dpkg --add-architecture i386
    apt-get update

    # Make sure we have libraries for 32-bit tests
    apt_packages=()
    apt_packages+=('libavcodec-extra60:i386')
    apt_packages+=('libpulse0:i386')
    apt_packages+=('libxt6t64:i386')
    apt_packages+=('libxtst6:i386')
    apt_packages+=('libsecret-1-0:i386')
    apt_packages+=('libgtk-3-0t64:i386')
    apt_packages+=('libx11-xcb1:i386')
    apt_packages+=('libxcb1:i386')
    apt_packages+=('libasound2t64:i386')
    apt_packages+=('libnotify4:i386')

    apt-get install --allow-downgrades "${apt_packages[@]}"
fi

rm -rf /var/lib/apt/lists/*

# enable audiotestsrc plugin in pipewire config
# used by gecko media tests to create dummy sound sources
install -d -o root -g root -m 755 /etc/pipewire
sed -e '/^context.spa-libs = {/,/^}$/ s/#\(audiotestsrc\)/\1/' /usr/share/pipewire/pipewire.conf > /etc/pipewire/pipewire.conf

# Build a list of packages to purge from the image.
apt_packages=()
apt_packages+=('gnome-calendar')
apt_packages+=('gnome-initial-setup')
apt_packages+=('libx11-doc')
apt_packages+=('manpages-dev')
apt_packages+=('orca')
apt_packages+=('rhythmbox')
apt_packages+=('thunderbird')
apt_packages+=('ubuntu-release-upgrader*')
apt_packages+=('update-manager-core')
apt_packages+=('update-manager')
apt_packages+=('yelp')

# Purge unnecessary packages
apt-get purge "${apt_packages[@]}"

# We don't need no docs!
rm -rf /usr/share/help /usr/share/doc /usr/share/man

# Remove all locale files other than en_US.UTF-8
rm -rf /usr/share/locale/   /usr/share/locale-langpack/     /usr/share/locales/

# Remove systemd tmpfiles that confuse gnome-shell into thinking it should talk to logind
rm -rf /run/systemd/seats

# Further cleanup
apt-get autoremove --purge

# Overwrite Ubuntu's Yaru theme with GTK's default (Adwaita), for consistency
cat > /etc/gtk-3.0/settings.ini <<EOF
[Settings]
gtk-theme-name = Adwaita
gtk-icon-theme-name = Yaru
gtk-sound-theme-name = Yaru
gtk-icon-sizes = panel-menu-bar=24,24
EOF

rm -f "$0"
