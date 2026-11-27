ARG BASE_IMAGE
FROM $BASE_IMAGE
MAINTAINER Mike Hommey <mhommey@mozilla.com>

ENV DEBIAN_FRONTEND=noninteractive

# Set a default command useful for debugging
CMD ["/bin/bash", "--login"]

# %include taskcluster/docker/recipes/setup_packages.sh
COPY topsrcdir/taskcluster/docker/recipes/setup_packages.sh /usr/local/sbin/
# %include taskcluster/docker/recipes/clean_packages.sh
COPY topsrcdir/taskcluster/docker/recipes/clean_packages.sh /usr/local/sbin/

COPY snapshot-hack.py /usr/local/sbin
COPY gpgvnoexpkeysig /usr/local/sbin

ARG SNAPSHOT
ARG TASKCLUSTER_ROOT_URL

# Set apt sources list to a snapshot.
# Note: the use of gpgvnoexpkeysig is because the Debian Jessie GPG key expired.
RUN set -ex; \
  DIST="$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release)" ; \
  if [ -z "$DIST" ]; then \
    : Debian jessie does not have VERSION_CODENAME ; \
    DIST="$(sed -n 's/^VERSION=.*(\([^)]*\)).*/\1/p' /etc/os-release)" ; \
  fi; \
  if grep -q ID=debian /etc/os-release; then \
    for s in debian_$DIST-backports debian-debug_$DIST-debug debian-debug_$DIST-proposed-updates-debug debian-debug_$DIST-backports-debug; do \
      case "$s" in \
      debian-debug_jessie*) \
          : No debian-debug/updates archive for Jessie; \
          ;; \
      *) \
          echo "deb http://deb.debian.org/${s%_*} ${s#*_} main"; \
          ;; \
      esac; \
    done > /etc/apt/sources.list.d/debian-extra.list; \
  fi; \
  case "$DIST" in \
  jessie|buster) \
    find /etc/apt -type f | grep ^/etc/apt/sources.list | xargs sed -i '/jessie-updates/d;s/\(deb\|security\)\.debian\.org/archive.debian.org/' ;; \
  *) ;; \
  esac; \
  ( echo 'quiet "true";'; \
    echo 'APT::Get::Assume-Yes "true";'; \
    echo 'APT::Install-Recommends "false";'; \
    echo 'Acquire::Check-Valid-Until "false";'; \
    case "$DIST" in \
    bookworm|trixie|jammy|noble) \
      echo 'Acquire::Retries "5";' ; \
      ;; \
    *) \
      : Work around CVE-2026-109518 ; \
      echo 'Acquire::Retries "0";' ; \
      ;; \
    esac; \
    if [ "$DIST" = "jessie" ]; then \
      echo 'Dir::Bin::gpg "/usr/local/sbin/gpgvnoexpkeysig";'; \
    fi; \
  ) > /etc/apt/apt.conf.d/99taskcluster && \
  ( echo 'Package: *'; \
    echo 'Pin: origin "'$TASKCLUSTER_ROOT_URL'"' | sed 's,https://,,'; \
    echo 'Pin-Priority: 1001'; \
  ) > /etc/apt/preferences.d/99taskcluster

# Update ca-certificates manually on older distros without an update.
RUN set -ex; \
  case "$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release)" in \
  focal|bionic) \
    /usr/lib/apt/apt-helper -o Acquire::https::Verify-Peer=false download-file \
      https://launchpad.net/ubuntu/+archive/primary/+files/ca-certificates_20260816ubuntu0.22.04.2_all.deb /tmp/ca-certificates.deb SHA256:f99e8375d35bdde4800d4842df777be3dafe38bca4d178429fa929de200bf821 ;; \
  buster|bullseye) \
    /usr/lib/apt/apt-helper -o Acquire::https::Verify-Peer=false download-file \
      https://snapshot.debian.org/archive/debian-security/20261020T213255Z/pool/updates/main/c/ca-certificates/ca-certificates_20250419~deb12u2_all.deb /tmp/ca-certificates.deb SHA256:b9f76b5271a7ac373fd5e7e1b5b826a71ddd702b42a0d104955ecb1acfffc94b ;; \
  *) \
    ;; \
  esac; \
  apt-get update; \
  if [ -f /tmp/ca-certificates.deb ]; then \
    ( echo 'Package: ca-certificates'; \
      echo "Pin: version $(dpkg-deb -f /tmp/ca-certificates.deb Version)"; \
      echo 'Pin-Priority: 1002'; \
    ) > /etc/apt/preferences.d/99ca-certificates; \
    apt-get install /tmp/ca-certificates.deb; \
    rm /tmp/ca-certificates.deb; \
  else \
    apt-get install ca-certificates; \
  fi

RUN set -ex; \
  apt-get update; \
  if [ -n "$SNAPSHOT" ] && grep -q ID=debian /etc/os-release; then \
    apt-get install python3-minimal libpython3-stdlib; \
    echo 'dir::bin::methods::https "/usr/local/sbin/snapshot-hack.py";' >> /etc/apt/apt.conf.d/99taskcluster; \
  fi; \
  if [ -n "$SNAPSHOT" ]; then \
    case "$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release)" in \
    bionic) \
      sed -i "s,https\?://\(archive\|security\)\.ubuntu\.com/ubuntu,https://snapshot.ubuntu.com/ubuntu/$SNAPSHOT," /etc/apt/sources.list ;; \
    jammy|focal) \
      sed -i "s/^deb /deb [snapshot=$SNAPSHOT] /" /etc/apt/sources.list ;; \
    bookworm) \
      sed -i "s,https\?://deb\.debian\.org/\([^/ ]*\),https://snapshot.debian.org/archive/\1/$SNAPSHOT," /etc/apt/sources.list.d/* ;; \
    *) \
      echo "APT::Snapshot \"$SNAPSHOT\";" > /etc/apt/apt.conf.d/99snapshot ;; \
    esac; \
    echo 'Acquire::Snapshots::URI::Label::"Debian debug" "https://snapshot.debian.org/archive/debian-debug/@SNAPSHOTID@/";' >> /etc/apt/apt.conf.d/99snapshot; \
    ( echo 'Package: *'; \
      echo 'Pin: origin /snapshot\.(ubuntu\.com|debian\.org)/'; \
      echo 'Pin-Priority: 1001'; \
    ) > /etc/apt/preferences.d/90snapshot; \
    downgrades=--allow-downgrades; \
    apt-get update || exit 1; \
  fi; \
  apt-cache policy | tee /dev/stderr | grep '^ *origin' | { ! grep -E -q -v 'origin (snapshot\.debian\.org|snapshot\.ubuntu\.com|archive.debian.org)'; }; \
  apt-get dist-upgrade ${downgrades}; \
  rm -f /etc/apt/preferences.d/90snapshot
