#!/bin/bash
# Unit tests for pure functions in install-firefox.sh.
# Uses shunit2: https://github.com/kward/shunit2
# Run locally: ./mach shell-test browser/installer/linux/script/install-firefox-test-unit.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_FIREFOX_SCRIPT="$SCRIPT_DIR/install-firefox.sh"

setUp() {
    CHANNEL="release"
    INSTALL_METHOD=""
    LANG_OVERRIDE=""
    VERBOSE=false
    ARCH=""
    DISTRO=""
    DISTRO_VERSION=""
    DETECTED_LOCALE=""
    unset _OS_RELEASE 2>/dev/null || true
    INSTALL_FIREFOX_SOURCED=1 . "$INSTALL_FIREFOX_SCRIPT"
    # install-firefox.sh sets -eu; reset -e so shunit2 can handle non-zero exits.
    set +e
}

tearDown() {
    rm -f "${SHUNIT_TMPDIR}/calls.tmp" "${SHUNIT_TMPDIR}/os-release" 2>/dev/null || true
    unset -f curl wget check_cmd dpkg apt_cache_show_stub uname rpm_pkg_exists_stub flatpak run_sudo retry \
        sleep id sudo _flaky install_apt install_rpm install_flatpak install_tarball \
        download_to_stdout check_url need_cmd find_l10n_package zypper dnf tar gpg snap mktemp \
        _apt_l10n_check _rpm_l10n_check _rpm_l10n_stub apt_verify_gpg_fingerprint 2>/dev/null || true
    rm -f "${SHUNIT_TMPDIR}/flag.tmp" 2>/dev/null || true
    unset _OS_RELEASE 2>/dev/null || true
}

testParseArgsChannelRelease() {
    parse_args --channel release
    assertEquals "channel=release" "release" "$CHANNEL"
}

testParseArgsChannelBeta() {
    parse_args --channel beta
    assertEquals "channel=beta" "beta" "$CHANNEL"
}

testParseArgsChannelDevedition() {
    parse_args --channel devedition
    assertEquals "channel=devedition" "devedition" "$CHANNEL"
}

testParseArgsChannelNightly() {
    parse_args --channel nightly
    assertEquals "channel=nightly" "nightly" "$CHANNEL"
}

testParseArgsChannelEsr() {
    parse_args --channel esr
    assertEquals "channel=esr" "esr" "$CHANNEL"
}

testParseArgsChannelInvalid() {
    assertFalse "invalid channel" 'sh "$INSTALL_FIREFOX_SCRIPT" --channel bogus >/dev/null 2>&1'
}

testParseArgsInstallMethodApt() {
    parse_args --install-method apt
    assertEquals "method=apt" "apt" "$INSTALL_METHOD"
}

testParseArgsInstallMethodInvalid() {
    assertFalse "invalid method" 'sh "$INSTALL_FIREFOX_SCRIPT" --install-method bogus >/dev/null 2>&1'
}

testParseArgsLang() {
    parse_args --lang fr
    assertEquals "--lang sets LANG_OVERRIDE" "fr" "$LANG_OVERRIDE"
}

testParseArgsHelp() {
    assertTrue "--help exits 0" 'sh "$INSTALL_FIREFOX_SCRIPT" --help >/dev/null 2>&1'
}

testChannelToPackageRelease() {
    CHANNEL=release
    assertEquals "pkg release" "firefox" "$(channel_to_package)"
}

testChannelToPackageBeta() {
    CHANNEL=beta
    assertEquals "pkg beta" "firefox-beta" "$(channel_to_package)"
}

testChannelToPackageDevedition() {
    CHANNEL=devedition
    assertEquals "pkg devedition" "firefox-devedition" "$(channel_to_package)"
}

testChannelToPackageNightly() {
    CHANNEL=nightly
    assertEquals "pkg nightly" "firefox-nightly" "$(channel_to_package)"
}

testChannelToPackageEsr() {
    CHANNEL=esr
    assertEquals "pkg esr" "firefox-esr" "$(channel_to_package)"
}

testDetectLocaleOverrideFr() {
    LANG_OVERRIDE="fr"
    detect_locale
    assertEquals "--lang fr" "fr" "$DETECTED_LOCALE"
}

testDetectLocaleOverrideJa() {
    LANG_OVERRIDE="ja"
    detect_locale
    assertEquals "--lang ja" "ja" "$DETECTED_LOCALE"
}

testDetectLocaleFrFr() {
    LANG_OVERRIDE=""
    LANG=fr_FR.UTF-8 LC_ALL= LC_MESSAGES= detect_locale
    assertEquals "fr_FR.UTF-8 -> fr-FR" "fr-FR" "$DETECTED_LOCALE"
}

testDetectLocaleC() {
    LANG_OVERRIDE=""
    LANG=C LC_ALL= LC_MESSAGES= detect_locale
    assertEquals "C -> en-US" "en-US" "$DETECTED_LOCALE"
}

testDetectLocalePosix() {
    LANG_OVERRIDE=""
    LANG=POSIX LC_ALL= LC_MESSAGES= detect_locale
    assertEquals "POSIX -> en-US" "en-US" "$DETECTED_LOCALE"
}

testDetectLocaleBareLang() {
    LANG_OVERRIDE=""
    LANG=de LC_ALL= LC_MESSAGES= detect_locale
    assertEquals "bare lang de -> de" "de" "$DETECTED_LOCALE"
}

testDetectLocaleJaJp() {
    LANG_OVERRIDE=""
    LANG=ja_JP.UTF-8 LC_ALL= LC_MESSAGES= detect_locale
    assertEquals "ja_JP.UTF-8 -> ja-JP" "ja-JP" "$DETECTED_LOCALE"
}

testCheckUrlCurl() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    curl() { echo "curl $*" >> "$_calls"; return 0; }
    wget() { echo "wget $*" >> "$_calls"; return 1; }
    export -f curl wget
    check_url "https://example.com"
    assertTrue "check_url uses curl when available" "grep -q curl '$_calls'"
}

testCheckUrlWget() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    wget() { echo "wget $*" >> "$_calls"; return 0; }
    export -f wget
    check_cmd() { [ "$1" = "wget" ]; }; export -f check_cmd
    check_url "https://example.com"
    assertTrue "check_url falls back to wget" "grep -q wget '$_calls'"
}

testDetectBestMethodApt() {
    check_cmd() { [ "$1" = "apt-get" ]; }
    detect_best_method
    assertEquals "apt-get -> apt" "apt" "$INSTALL_METHOD"
}

testDetectBestMethodForced() {
    INSTALL_METHOD=apt
    detect_best_method
    assertEquals "forced method is preserved" "apt" "$INSTALL_METHOD"
}

testFindL10nPackageAptFrFr() {
    DETECTED_LOCALE="fr-FR"
    apt_cache_show_stub() {
        [ "$1" = "firefox-l10n-fr-fr" ] && return 1
        [ "$1" = "firefox-l10n-fr" ] && return 0
        return 1
    }
    find_l10n_package "apt_cache_show_stub" "firefox"
    assertEquals "fr-FR -> firefox-l10n-fr" "firefox-l10n-fr" "$_l10n_pkg"
}

testFindL10nPackageAptEnUS() {
    DETECTED_LOCALE="en-US"
    apt_cache_show_stub() {
        [ "$1" = "firefox-l10n-fr-fr" ] && return 1
        [ "$1" = "firefox-l10n-fr" ] && return 0
        return 1
    }
    find_l10n_package "apt_cache_show_stub" "firefox"
    assertEquals "en-US -> no l10n pkg" "" "$_l10n_pkg"
}

testAptSourceFormatDeb11() {
    DISTRO=debian; DISTRO_VERSION=11
    dpkg() { return 1; }; export -f dpkg   # 11 < 13 → list
    assertEquals "deb11 -> list" "list" "$(apt_source_format)"
}

testAptSourceFormatDeb12() {
    DISTRO=debian; DISTRO_VERSION=12
    dpkg() { return 1; }; export -f dpkg   # 12 < 13 → list
    assertEquals "deb12 -> list" "list" "$(apt_source_format)"
}

testAptSourceFormatDeb13() {
    DISTRO=debian; DISTRO_VERSION=13
    dpkg() { return 0; }; export -f dpkg   # 13 >= 13 → deb822
    assertEquals "deb13 -> deb822" "deb822" "$(apt_source_format)"
}

testAptSourceFormatUbuntu2404() {
    DISTRO=ubuntu; DISTRO_VERSION=24.04
    dpkg() { return 1; }; export -f dpkg   # 24.04 < 25.10 → list
    assertEquals "ubuntu 24.04 -> list" "list" "$(apt_source_format)"
}

testAptSourceFormatUbuntu2510() {
    DISTRO=ubuntu; DISTRO_VERSION=25.10
    dpkg() { return 0; }; export -f dpkg   # 25.10 >= 25.10 → deb822
    assertEquals "ubuntu 25.10 -> deb822" "deb822" "$(apt_source_format)"
}

testDetectOsLinuxX86_64() {
    printf 'ID=debian\nVERSION_ID=12\nPRETTY_NAME="Debian 12"\n' > "${SHUNIT_TMPDIR}/os-release"
    _OS_RELEASE="${SHUNIT_TMPDIR}/os-release"
    uname() { case "$1" in -s) echo "Linux";; -m) echo "x86_64";; esac; }
    export -f uname
    detect_os
    assertEquals "arch x86_64" "x86_64" "$ARCH"
    assertEquals "distro debian" "debian" "$DISTRO"
    assertEquals "version 12" "12" "$DISTRO_VERSION"
}

testDetectOsLinuxAarch64() {
    printf 'ID=ubuntu\nVERSION_ID=20.04\nPRETTY_NAME="Ubuntu 20.04"\n' > "${SHUNIT_TMPDIR}/os-release"
    _OS_RELEASE="${SHUNIT_TMPDIR}/os-release"
    uname() { case "$1" in -s) echo "Linux";; -m) echo "aarch64";; esac; }
    export -f uname
    detect_os
    assertEquals "arch aarch64" "aarch64" "$ARCH"
    assertEquals "distro ubuntu" "ubuntu" "$DISTRO"
    assertEquals "version 20.04" "20.04" "$DISTRO_VERSION"
}

testDetectOsNonLinuxErrors() {
    uname() { case "$1" in -s) echo "Darwin";; -m) echo "x86_64";; esac; }
    export -f uname
    assertFalse "non-Linux OS errors" '( detect_os >/dev/null 2>&1 )'
}

testDetectOsUnknownArchErrors() {
    printf 'ID=debian\nVERSION_ID=12\n' > "${SHUNIT_TMPDIR}/os-release"
    _OS_RELEASE="${SHUNIT_TMPDIR}/os-release"
    uname() { case "$1" in -s) echo "Linux";; -m) echo "i686";; esac; }
    export -f uname
    assertFalse "unknown arch errors" '( detect_os >/dev/null 2>&1 )'
}

testDetectOsNoOsRelease() {
    _OS_RELEASE="${SHUNIT_TMPDIR}/nonexistent-os-release"
    uname() { case "$1" in -s) echo "Linux";; -m) echo "x86_64";; esac; }
    export -f uname
    detect_os
    assertEquals "distro unknown without os-release" "unknown" "$DISTRO"
}

testNeedCmdFound() {
    assertTrue "need_cmd bash exits 0" 'need_cmd bash >/dev/null 2>&1'
}

testNeedCmdNotFound() {
    assertFalse "need_cmd fails for missing command" '( need_cmd definitely_nonexistent_xyz >/dev/null 2>&1 )'
}

testRetrySucceeds() {
    retry true
    assertTrue "retry succeeds on first attempt" "$?"
}

testRetrySucceedsAfterOneFailure() {
    sleep() { :; }; export -f sleep
    local _flag="${SHUNIT_TMPDIR}/flag.tmp"
    _flaky() { [ -f "$_flag" ] && return 0; touch "$_flag"; return 1; }
    retry _flaky
    assertTrue "retry succeeds after one failure" "$?"
}

testRetryExhaustedErrors() {
    sleep() { :; }; export -f sleep
    assertFalse "retry errors after exhausting attempts" '( retry false >/dev/null 2>&1 )'
}

testDownloadPrefersCurl() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    curl() { echo "curl $*" >> "$_calls"; return 0; }
    export -f curl
    download "https://example.com" "${SHUNIT_TMPDIR}/dl.out"
    assertTrue "download prefers curl" "grep -q curl '$_calls'"
}

testDownloadFallsBackToWget() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    wget() { echo "wget $*" >> "$_calls"; return 0; }
    export -f wget
    check_cmd() { [ "$1" = "wget" ]; }
    download "https://example.com" "${SHUNIT_TMPDIR}/dl.out"
    assertTrue "download falls back to wget" "grep -q wget '$_calls'"
}

testDownloadError() {
    check_cmd() { return 1; }; export -f check_cmd
    assertFalse "download errors when no downloader available" \
        '( download "https://example.com" /dev/null >/dev/null 2>&1 )'
}

testDownloadToStdoutPrefersCurl() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    curl() { echo "curl $*" >> "$_calls"; return 0; }
    export -f curl
    download_to_stdout "https://example.com" >/dev/null
    assertTrue "download_to_stdout prefers curl" "grep -q curl '$_calls'"
}

testDownloadToStdoutWget() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    wget() { echo "wget $*" >> "$_calls"; return 0; }; export -f wget
    check_cmd() { [ "$1" = "wget" ]; }; export -f check_cmd
    download_to_stdout "https://example.com" >/dev/null
    assertTrue "download_to_stdout falls back to wget" "grep -q wget '$_calls'"
}

testDownloadToStdoutError() {
    check_cmd() { return 1; }; export -f check_cmd
    assertFalse "download_to_stdout errors when no downloader available" \
        '( download_to_stdout "https://example.com" >/dev/null 2>&1 )'
}

testRunSudoAsRoot() {
    id() { echo "0"; }; export -f id
    sudo() { fail "sudo should not be called as root"; }; export -f sudo
    run_sudo true
    assertTrue "run_sudo runs directly as root" "$?"
}

testRunSudoAsNonRoot() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    id() { echo "1000"; }; export -f id
    sudo() { echo "sudo $*" >> "$_calls"; "$@"; }; export -f sudo
    run_sudo echo "test" >/dev/null
    assertTrue "run_sudo uses sudo as non-root" "grep -q sudo '$_calls'"
}

testVerifyInstallFindsFirefox() {
    printf '#!/bin/sh\necho "Mozilla Firefox 100.0"\n' > "${SHUNIT_TMPDIR}/firefox"
    chmod +x "${SHUNIT_TMPDIR}/firefox"
    local _old_path="$PATH"
    PATH="${SHUNIT_TMPDIR}:$PATH"
    CHANNEL=release; INSTALL_METHOD=apt
    verify_install 2>/dev/null
    assertTrue "verify_install exits 0 when firefox found" "$?"
    PATH="$_old_path"
}

testVerifyInstallWarnWhenMissing() {
    CHANNEL=release; INSTALL_METHOD=apt
    ( PATH="/nonexistent_dir_xyz123" verify_install 2>/dev/null )
    assertEquals "verify_install exits 0 even when not found" "0" "$?"
}

testInstallFirefoxDispatchApt() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    install_apt() { echo "install_apt" >> "$_calls"; }; export -f install_apt
    INSTALL_METHOD=apt
    install_firefox
    assertTrue "install_firefox dispatches to apt" "grep -q install_apt '$_calls'"
}

testInstallFirefoxUnknownMethod() {
    INSTALL_METHOD=bogus
    assertFalse "unknown install method errors" '( install_firefox >/dev/null 2>&1 )'
}

testAptUseModernKeyringDebian11() {
    DISTRO=debian DISTRO_VERSION=11
    dpkg() { return 1; }; export -f dpkg   # 11 not < 11 → modern
    assertTrue "deb11 -> modern keyring" 'apt_use_modern_keyring'
}

testAptUseModernKeyringDebian9() {
    DISTRO=debian DISTRO_VERSION=9
    dpkg() { return 0; }; export -f dpkg   # 9 < 11 → legacy
    assertFalse "deb9 -> legacy keyring" 'apt_use_modern_keyring'
}

testAptUseModernKeyringUbuntu2204() {
    DISTRO=ubuntu DISTRO_VERSION=22.04
    dpkg() { return 1; }; export -f dpkg   # 22.04 not < 22.04 → modern
    assertTrue "ubuntu 22.04 -> modern keyring" 'apt_use_modern_keyring'
}

testAptUseModernKeyringUbuntu2004() {
    DISTRO=ubuntu DISTRO_VERSION=20.04
    dpkg() { return 0; }; export -f dpkg   # 20.04 < 22.04 → legacy
    assertFalse "ubuntu 20.04 -> legacy keyring" 'apt_use_modern_keyring'
}

testAptUseModernKeyringUnknownDistro() {
    DISTRO=arch DISTRO_VERSION=
    assertTrue "unknown distro -> modern keyring" 'apt_use_modern_keyring'
}

testAptVerifyGpgFingerprintMatch() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    gpg() { printf 'fpr:::::::::35BAA0B33E9EB396F59CA838C0BA5CE6DC6315A3:\n'; }
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }
    export -f gpg run_sudo
    apt_verify_gpg_fingerprint "/fake/keyring.asc"
    assertFalse "matching fingerprint: keyring not removed" \
        "grep -q 'run_sudo rm -f' '$_calls'"
}

testAptVerifyGpgFingerprintMismatch() {
    gpg() { printf 'fpr:::::::::0000000000000000000000000000000000000000:\n'; }
    run_sudo() { :; }
    export -f gpg run_sudo
    assertFalse "wrong fingerprint errors" \
        '( apt_verify_gpg_fingerprint "/fake/keyring.asc" >/dev/null 2>&1 )'
}

testAptCleanupLegacySourcesRemovesKey() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _fake_key="${SHUNIT_TMPDIR}/packages.mozilla.org.gpg"
    touch "$_fake_key"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    _APT_LEGACY_KEY="$_fake_key" apt_cleanup_legacy_sources >/dev/null 2>&1
    assertTrue "legacy key removed" "grep -q 'run_sudo rm -f $_fake_key' '$_calls'"
}

testAptCleanupLegacySourcesRemovesListFile() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _fake_dir="${SHUNIT_TMPDIR}/sources.list.d"
    mkdir -p "$_fake_dir"
    printf 'deb https://packages.mozilla.org/apt mozilla main\n' > "$_fake_dir/mozilla.list"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    _APT_SOURCES_DIR="$_fake_dir" apt_cleanup_legacy_sources >/dev/null 2>&1
    assertTrue "mozilla.list removed" "grep -q 'run_sudo rm -f' '$_calls'"
}

testAptWriteSourceEntryDeb822() {
    DISTRO=debian DISTRO_VERSION=13
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    dpkg() { [ "$3" = "ge" ]; }; export -f dpkg
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    apt_write_source_entry "/etc/apt/keyrings/packages.mozilla.org.asc" "https://packages.mozilla.org/apt"
    assertTrue "deb13: writes mozilla.sources" \
        "grep -q 'run_sudo tee /etc/apt/sources.list.d/mozilla.sources' '$_calls'"
}

testAptWriteSourceEntryList() {
    DISTRO=debian DISTRO_VERSION=12
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    dpkg() { return 1; }; export -f dpkg
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    apt_write_source_entry "/etc/apt/keyrings/packages.mozilla.org.asc" "https://packages.mozilla.org/apt"
    assertTrue "deb12: writes mozilla.list" \
        "grep -q 'run_sudo tee /etc/apt/sources.list.d/mozilla.list' '$_calls'"
}

testAptSetupModernKeyringDownloads() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _fake_tmpkey="${SHUNIT_TMPDIR}/fake.asc"
    apt_cleanup_legacy_sources() { :; }; export -f apt_cleanup_legacy_sources
    download() { echo "download $*" >> "$_calls"; touch "$2"; }; export -f download
    check_cmd() { return 1; }; export -f check_cmd
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    mktemp() { echo "$_fake_tmpkey"; }; export -f mktemp
    apt_write_source_entry() { echo "apt_write_source_entry $*" >> "$_calls"; }
    export -f apt_write_source_entry
    apt_setup_modern_keyring "https://key.url" "https://repo.url"
    assertTrue "modern keyring: downloads key" \
        "grep -q 'download https://key.url' '$_calls'"
    assertTrue "modern keyring: installs to keyrings dir" \
        "grep -q 'run_sudo install -m 644' '$_calls'"
    assertTrue "modern keyring: writes source entry" \
        "grep -q 'apt_write_source_entry' '$_calls'"
}

testAptSetupLegacyKeyring() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _fake_tmpkey="${SHUNIT_TMPDIR}/fake-legacy.asc"
    download() { echo "download $*" >> "$_calls"; touch "$2"; }; export -f download
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    mktemp() { echo "$_fake_tmpkey"; }; export -f mktemp
    apt_verify_gpg_fingerprint() { :; }; export -f apt_verify_gpg_fingerprint
    apt_setup_legacy_keyring "https://key.url" "https://repo.url"
    assertTrue "legacy keyring: downloads key" \
        "grep -q 'download https://key.url' '$_calls'"
    assertTrue "legacy keyring: apt-key add called" \
        "grep -q 'run_sudo apt-key add' '$_calls'"
    assertTrue "legacy keyring: mozilla.list written" \
        "grep -q 'run_sudo tee /etc/apt/sources.list.d/mozilla.list' '$_calls'"
}

testAptAddPriorityPin() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    apt_add_priority_pin
    assertTrue "apt priority pin: writes preferences.d/mozilla" \
        "grep -q 'run_sudo tee /etc/apt/preferences.d/mozilla' '$_calls'"
}

testAptInstallPackages() {
    CHANNEL=release DETECTED_LOCALE=en-US
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    find_l10n_package() { _l10n_pkg=""; }; export -f find_l10n_package
    apt_install_packages "firefox"
    assertTrue "apt install: apt-get update called" \
        "grep -q 'run_sudo apt-get update' '$_calls'"
    assertTrue "apt install: apt-get install called" \
        "grep -q 'run_sudo apt-get install -y firefox$' '$_calls'"
}

testAptInstallPackagesWithL10n() {
    CHANNEL=release DETECTED_LOCALE=fr-FR
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    find_l10n_package() { _l10n_pkg="firefox-l10n-fr"; }; export -f find_l10n_package
    apt_install_packages "firefox"
    assertTrue "apt install with l10n: both packages installed" \
        "grep -q 'run_sudo apt-get install -y firefox firefox-l10n-fr' '$_calls'"
}

testInstallAptModernPath() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    apt_use_modern_keyring() { return 0; }
    apt_setup_modern_keyring() { echo "apt_setup_modern_keyring $*" >> "$_calls"; }
    apt_add_priority_pin() { echo "apt_add_priority_pin" >> "$_calls"; }
    apt_install_packages() { echo "apt_install_packages $*" >> "$_calls"; }
    check_url() { return 0; }; need_cmd() { return 0; }
    export -f apt_use_modern_keyring apt_setup_modern_keyring apt_add_priority_pin \
        apt_install_packages check_url need_cmd
    install_apt
    assertTrue "modern path: setup_modern_keyring called" \
        "grep -q apt_setup_modern_keyring '$_calls'"
}

testInstallAptLegacyPath() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    apt_use_modern_keyring() { return 1; }
    apt_setup_legacy_keyring() { echo "apt_setup_legacy_keyring $*" >> "$_calls"; }
    apt_add_priority_pin() { echo "apt_add_priority_pin" >> "$_calls"; }
    apt_install_packages() { echo "apt_install_packages $*" >> "$_calls"; }
    check_url() { return 0; }; need_cmd() { return 0; }
    export -f apt_use_modern_keyring apt_setup_legacy_keyring apt_add_priority_pin \
        apt_install_packages check_url need_cmd
    install_apt
    assertTrue "legacy path: setup_legacy_keyring called" \
        "grep -q apt_setup_legacy_keyring '$_calls'"
}

testParseArgsInstallMethodRpm() {
    parse_args --install-method rpm
    assertEquals "method=rpm" "rpm" "$INSTALL_METHOD"
}

testDetectBestMethodDnf() {
    check_cmd() { [ "$1" = "dnf" ]; }
    detect_best_method
    assertEquals "dnf -> rpm" "rpm" "$INSTALL_METHOD"
}

testDetectBestMethodYum() {
    check_cmd() { [ "$1" = "yum" ]; }
    detect_best_method
    assertEquals "yum -> tarball (yum can't use rpm method)" "tarball" "$INSTALL_METHOD"
}

testDetectBestMethodZypper() {
    check_cmd() { [ "$1" = "zypper" ]; }
    detect_best_method
    assertEquals "zypper -> rpm" "rpm" "$INSTALL_METHOD"
}

testFindL10nPackageRpm() {
    DETECTED_LOCALE="fr"
    _rpm_l10n_stub() { [ "$1" = "firefox-l10n-fr" ] && return 0; return 1; }
    find_l10n_package _rpm_l10n_stub "firefox"
    assertEquals "fr -> firefox-l10n-fr (dnf)" "firefox-l10n-fr" "$_l10n_pkg"
}

testInstallFirefoxDispatchRpm() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    install_rpm() { echo "install_rpm" >> "$_calls"; }; export -f install_rpm
    INSTALL_METHOD=rpm
    install_firefox
    assertTrue "install_firefox dispatches to rpm" "grep -q install_rpm '$_calls'"
}

testRpmPkgExistsDnf() {
    dnf() { return 0; }; export -f dnf
    assertTrue "rpm_pkg_exists dnf: found" 'rpm_pkg_exists dnf firefox'
}

testRpmPkgExistsDnfMissing() {
    dnf() { return 1; }; export -f dnf
    assertFalse "rpm_pkg_exists dnf: missing" 'rpm_pkg_exists dnf nonexistent'
}

testRpmPkgExistsZypper() {
    zypper() { return 0; }; export -f zypper
    assertTrue "rpm_pkg_exists zypper: found" 'rpm_pkg_exists zypper firefox'
}

testRpmPkgExistsUnknown() {
    assertFalse "rpm_pkg_exists unknown manager: returns 1" 'rpm_pkg_exists snap firefox'
}

testRpmDetectPkgManagerDnf() {
    check_cmd() { [ "$1" = "dnf" ]; }; export -f check_cmd
    assertEquals "dnf detected" "dnf" "$(rpm_detect_pkg_manager)"
}

testRpmDetectPkgManagerYumErrors() {
    check_cmd() { [ "$1" = "yum" ]; }; export -f check_cmd
    assertFalse "yum errors (not supported)" '( rpm_detect_pkg_manager >/dev/null 2>&1 )'
}

testRpmDetectPkgManagerZypper() {
    check_cmd() { [ "$1" = "zypper" ]; }; export -f check_cmd
    assertEquals "zypper detected" "zypper" "$(rpm_detect_pkg_manager)"
}

testRpmDetectPkgManagerNone() {
    check_cmd() { return 1; }; export -f check_cmd
    assertFalse "no rpm manager errors" '( rpm_detect_pkg_manager >/dev/null 2>&1 )'
}

testRpmAddRepositoryDnf() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    rpm_add_repository "dnf" "https://repo.url" "https://gpg.url"
    assertTrue "dnf: repo file written" \
        "grep -q 'run_sudo tee /etc/yum.repos.d/mozilla-firefox.repo' '$_calls'"
}

testRpmAddRepositoryZypper() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    zypper() { echo "zypper $*" >> "$_calls"; return 1; }; export -f zypper
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    rpm_add_repository "zypper" "https://repo.url" "https://gpg.url"
    assertTrue "zypper: zypper ar called" \
        "grep -q 'run_sudo zypper ar' '$_calls'"
    assertTrue "zypper: refresh called" \
        "grep -q 'run_sudo zypper --gpg-auto-import-keys refresh' '$_calls'"
}

testRpmInstallPackagesDnf() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    rpm_install_packages "dnf" "firefox" ""
    assertTrue "dnf install called" \
        "grep -q 'run_sudo dnf install -y firefox' '$_calls'"
}

testRpmInstallPackagesZypper() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    rpm_install_packages "zypper" "firefox" ""
    assertTrue "zypper install --replacefiles called" \
        "grep -q 'run_sudo zypper install -y --replacefiles firefox' '$_calls'"
}

testRpmInstallPackagesWithL10n() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    rpm_install_packages "dnf" "firefox" "firefox-l10n-fr"
    assertTrue "l10n package installed" \
        "grep -q 'run_sudo dnf install -y firefox-l10n-fr' '$_calls'"
}

testRpmInstallPackagesL10nFallback() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; case "$*" in *l10n*) return 1;; esac; }
    export -f run_sudo
    retry() { "$@"; }; export -f retry
    rpm_install_packages "dnf" "firefox" "firefox-l10n-fr"
    assertTrue "main package still installed on l10n failure" \
        "grep -q 'run_sudo dnf install -y firefox' '$_calls'"
}

testInstallRpmOrchestration() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    rpm_detect_pkg_manager() { echo "dnf"; }
    rpm_add_repository() { echo "rpm_add_repository $*" >> "$_calls"; }
    rpm_install_packages() { echo "rpm_install_packages $*" >> "$_calls"; }
    check_url() { return 0; }; find_l10n_package() { _l10n_pkg=""; }
    export -f rpm_detect_pkg_manager rpm_add_repository rpm_install_packages \
        check_url find_l10n_package
    CHANNEL=release
    install_rpm
    assertTrue "rpm_add_repository called" "grep -q rpm_add_repository '$_calls'"
    assertTrue "rpm_install_packages called" "grep -q rpm_install_packages '$_calls'"
}

testParseArgsInstallMethodFlatpak() {
    parse_args --install-method flatpak
    assertEquals "method=flatpak" "flatpak" "$INSTALL_METHOD"
}

testDetectBestMethodFlatpak() {
    check_cmd() { [ "$1" = "flatpak" ]; }
    detect_best_method
    assertEquals "flatpak -> flatpak" "flatpak" "$INSTALL_METHOD"
}

testChannelToFlatpakBranchRelease() {
    CHANNEL=release
    local _result
    _result="$(channel_to_flatpak_branch)"
    assertEquals "flatpak branch release" "" "$_result"
}

testChannelToFlatpakBranchBeta() {
    CHANNEL=beta
    assertEquals "flatpak branch beta" "beta" "$(channel_to_flatpak_branch)"
}

testChannelToFlatpakBranchDeveditionErrors() {
    assertFalse "flatpak devedition errors" 'sh "$INSTALL_FIREFOX_SCRIPT" --channel devedition --install-method flatpak >/dev/null 2>&1'
}

testChannelToFlatpakBranchNightlyErrors() {
    assertFalse "flatpak nightly errors" 'sh "$INSTALL_FIREFOX_SCRIPT" --channel nightly --install-method flatpak >/dev/null 2>&1'
}

testChannelToFlatpakBranchEsrErrors() {
    assertFalse "flatpak esr errors" \
        '( CHANNEL=esr channel_to_flatpak_branch >/dev/null 2>&1 )'
}

testFlatpakAddRemotesRelease() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    flatpak_add_remotes ""
    assertTrue "release: flathub added" \
        "grep -q 'run_sudo flatpak remote-add --if-not-exists --system flathub ' '$_calls'"
    assertFalse "release: flathub-beta not added" \
        "grep -q 'flathub-beta' '$_calls'"
}

testFlatpakAddRemotesBeta() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    retry() { "$@"; }; export -f retry
    flatpak_add_remotes "beta"
    assertTrue "beta: flathub-beta added" \
        "grep -q 'run_sudo flatpak remote-add --if-not-exists --system flathub-beta ' '$_calls'"
}

testFlatpakInstallOrUpdateFresh() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    flatpak() { echo "flatpak $*" >> "$_calls"; [ "$1" = "info" ] && return 1; return 0; }
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }
    retry() { "$@"; }
    export -f flatpak run_sudo retry
    flatpak_install_or_update ""
    assertTrue "fresh install: flatpak install called" \
        "grep -q 'run_sudo flatpak install --system -y flathub org.mozilla.firefox' '$_calls'"
}

testFlatpakInstallOrUpdateExisting() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    flatpak() { echo "flatpak $*" >> "$_calls"; return 0; }
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }
    retry() { "$@"; }
    export -f flatpak run_sudo retry
    flatpak_install_or_update "beta"
    assertTrue "already installed: flatpak update called" \
        "grep -q 'run_sudo flatpak update --system -y org.mozilla.firefox//beta' '$_calls'"
}

testInstallFlatpakOrchestration() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    flatpak_add_remotes() { echo "flatpak_add_remotes $*" >> "$_calls"; }
    flatpak_install_or_update() { echo "flatpak_install_or_update $*" >> "$_calls"; }
    need_cmd() { return 0; }
    export -f flatpak_add_remotes flatpak_install_or_update need_cmd
    CHANNEL=release install_flatpak
    assertTrue "add_remotes called" "grep -q flatpak_add_remotes '$_calls'"
    assertTrue "install_or_update called" "grep -q flatpak_install_or_update '$_calls'"
}

testInstallFirefoxDispatchFlatpak() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    install_flatpak() { echo "install_flatpak" >> "$_calls"; }; export -f install_flatpak
    INSTALL_METHOD=flatpak
    install_firefox
    assertTrue "install_firefox dispatches to flatpak" "grep -q install_flatpak '$_calls'"
}

testParseArgsInstallMethodTarball() {
    parse_args --install-method tarball
    assertEquals "method=tarball" "tarball" "$INSTALL_METHOD"
}

testTarballProductNameRelease() {
    CHANNEL=release
    assertEquals "tarball release" "firefox-latest-ssl" "$(tarball_product_name)"
}

testTarballProductNameBeta() {
    CHANNEL=beta
    assertEquals "tarball beta" "firefox-beta-latest-ssl" "$(tarball_product_name)"
}

testTarballProductNameDevedition() {
    CHANNEL=devedition
    assertEquals "tarball devedition" "firefox-devedition-latest-ssl" "$(tarball_product_name)"
}

testTarballProductNameNightly() {
    CHANNEL=nightly
    assertEquals "tarball nightly" "firefox-nightly-latest-ssl" "$(tarball_product_name)"
}

testTarballProductNameEsr() {
    CHANNEL=esr
    assertEquals "tarball esr" "firefox-esr-latest-ssl" "$(tarball_product_name)"
}

testDetectBestMethodTarball() {
    check_cmd() { return 1; }
    detect_best_method
    assertEquals "no package manager -> tarball" "tarball" "$INSTALL_METHOD"
}

testInstallFirefoxDispatchTarball() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    install_tarball() { echo "install_tarball" >> "$_calls"; }; export -f install_tarball
    INSTALL_METHOD=tarball
    install_firefox
    assertTrue "install_firefox dispatches to tarball" "grep -q install_tarball '$_calls'"
}

testTarballBuildUrlX86_64() {
    ARCH=x86_64 CHANNEL=release DETECTED_LOCALE=en-US
    local _url
    _url="$(tarball_build_url)"
    assertTrue "x86_64: URL contains linux64" \
        "echo '$_url' | grep -q 'os=linux64'"
    assertFalse "x86_64: URL does not contain aarch64" \
        "echo '$_url' | grep -q 'aarch64'"
}

testTarballBuildUrlAarch64() {
    ARCH=aarch64 CHANNEL=release DETECTED_LOCALE=en-US
    local _url
    _url="$(tarball_build_url)"
    assertTrue "aarch64: URL contains linux64-aarch64" \
        "echo '$_url' | grep -q 'os=linux64-aarch64'"
}

testTarballBuildUrlUnsupportedArch() {
    ARCH=i686 CHANNEL=release DETECTED_LOCALE=en-US
    assertFalse "unsupported arch errors" '( tarball_build_url >/dev/null 2>&1 )'
}

testTarballResolvesFinalUrlCurl() {
    curl() { echo "https://final.example.com/firefox.tar"; return 0; }
    export -f curl
    result="$(tarball_resolve_final_url "https://download.example.com/firefox")"
    assertEquals "curl resolves final URL" "https://final.example.com/firefox.tar" "$result"
}

testTarballResolvesFinalUrlWget() {
    wget() {
        printf '  Location: https://final.example.com/firefox.tar\n'
        return 0
    }
    export -f wget
    check_cmd() { [ "$1" = "wget" ]; }
    result="$(tarball_resolve_final_url "https://download.example.com/firefox")"
    assertEquals "wget resolves final URL" "https://final.example.com/firefox.tar" "$result"
}

testTarballKeyUrlRelease() {
    result="$(tarball_key_url \
        "https://releases.mozilla.org/pub/firefox/releases/152.0.1/linux-x86_64/en-US/firefox-152.0.1.tar.xz")"
    assertEquals "release key URL" \
        "https://releases.mozilla.org/pub/firefox/releases/152.0.1/KEY" "$result"
}

testTarballKeyUrlEsr() {
    result="$(tarball_key_url \
        "https://download-installer.cdn.mozilla.net/pub/firefox/releases/153.7.0esr/linux-x86_64/en-US/firefox-153.7.0esr.tar.xz")"
    assertEquals "esr key URL" \
        "https://download-installer.cdn.mozilla.net/pub/firefox/releases/153.7.0esr/KEY" "$result"
}

testTarballKeyUrlNightly() {
    result="$(tarball_key_url \
        "https://archive.mozilla.org/pub/firefox/nightly/2025/06/2025-06-22-09-50-02-mozilla-central/firefox-139.0a1.en-US.linux-x86_64.tar.bz2")"
    assertEquals "nightly key URL" \
        "https://archive.mozilla.org/pub/firefox/nightly/2025/06/2025-06-22-09-50-02-mozilla-central/KEY" "$result"
}

testTarballKeyUrlUnknown() {
    result="$(tarball_key_url "https://example.com/some/path/firefox.tar.xz")"
    assertEquals "unknown URL returns empty" "" "$result"
}

testTarballVerifyGpgSuccess() {
    local _gpgdir="${SHUNIT_TMPDIR}/gpg-ok"
    mkdir -p "$_gpgdir"
    download_to_stdout() { echo "fake-key"; }
    download() { touch "$2"; }
    gpg() {
        case "$*" in
            *--list-keys*) printf 'fpr:::::::::14F26682D0916CDD81E37B6D61B7B526D98F0353:\n' ;;
            *) return 0 ;;
        esac
    }
    export -f download_to_stdout download gpg
    assertTrue "gpg verify succeeds" \
        "( tarball_verify_gpg /dev/null \"https://example.com/firefox.tar.asc\" \"https://example.com/KEY\" \"$_gpgdir\" >/dev/null 2>&1 )"
}

testTarballVerifyGpgFailure() {
    local _gpgdir="${SHUNIT_TMPDIR}/gpg-fail"
    mkdir -p "$_gpgdir"
    download_to_stdout() { echo "fake-key"; }
    download() { touch "$2"; }
    gpg() {
        case "$*" in
            *--list-keys*) printf 'fpr:::::::::14F26682D0916CDD81E37B6D61B7B526D98F0353:\n' ;;
            *--verify*) return 1 ;;
            *) return 0 ;;
        esac
    }
    export -f download_to_stdout download gpg
    assertFalse "gpg verify failure errors" \
        "( tarball_verify_gpg /dev/null \"https://example.com/firefox.tar.asc\" \"https://example.com/KEY\" \"$_gpgdir\" >/dev/null 2>&1 )"
}

testTarballVerifyGpgBadImport() {
    local _gpgdir="${SHUNIT_TMPDIR}/gpg-bad"
    mkdir -p "$_gpgdir"
    download_to_stdout() { echo "bad-key"; }
    download() { touch "$2"; }
    # import succeeds but list-keys returns wrong fingerprint
    gpg() {
        case "$*" in
            *--list-keys*) printf 'fpr:::::::::0000000000000000000000000000000000000000:\n' ;;
            *) return 0 ;;
        esac
    }
    export -f download_to_stdout download gpg
    assertFalse "bad import detected by fingerprint check" \
        "( tarball_verify_gpg /dev/null \"https://example.com/firefox.tar.asc\" \"https://example.com/KEY\" \"$_gpgdir\" >/dev/null 2>&1 )"
}

testTarballDownloadAndExtract() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _tmpdir="${SHUNIT_TMPDIR}/tbtest"
    mkdir -p "$_tmpdir"
    download() { echo "download $*" >> "$_calls"; touch "$2"; }
    tar() { echo "tar $*" >> "$_calls"; }
    tarball_resolve_final_url() { echo "https://releases.mozilla.org/pub/firefox/releases/1.0/linux-x86_64/en-US/firefox-1.0.tar.xz"; }
    tarball_verify_gpg() { echo "tarball_verify_gpg $*" >> "$_calls"; }
    check_cmd() { [ "$1" = "gpg" ]; }
    export -f download tar tarball_resolve_final_url tarball_verify_gpg check_cmd
    tarball_download_and_extract "https://example.com/firefox.tar" "$_tmpdir"
    assertTrue "download uses resolved final URL" "grep -q 'download https://releases.mozilla.org' '$_calls'"
    assertTrue "tar -xf called" "grep -q 'tar -xf' '$_calls'"
    assertTrue "gpg verify called" "grep -q tarball_verify_gpg '$_calls'"
}

testTarballInstallTo() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _tmpdir="${SHUNIT_TMPDIR}/tarball-install"
    mkdir -p "$_tmpdir/firefox"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    tarball_install_to "$_tmpdir" "/opt/firefox" "firefox"
    assertTrue "mv extracted dir to install_dir" \
        "grep -q 'run_sudo mv $_tmpdir/firefox /opt/firefox' '$_calls'"
    assertTrue "symlink created" \
        "grep -q 'run_sudo ln -sf /opt/firefox/firefox /usr/local/bin/firefox' '$_calls'"
}

testTarballInstallToMissingDir() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    local _tmpdir="${SHUNIT_TMPDIR}/tarball-install-missing-dir"
    mkdir -p "$_tmpdir/gpg"
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }; export -f run_sudo
    assertFalse "missing firefox dir is an error" \
        "( tarball_install_to '$_tmpdir' /opt/firefox firefox >/dev/null 2>&1 )"
    assertFalse "nothing installed" "[ -e '$_calls' ]"
}

testInstallTarballOrchestration() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    tarball_build_url() { echo "https://fake.url/firefox.tar"; }
    tarball_download_and_extract() { echo "tarball_download_and_extract $*" >> "$_calls"; }
    tarball_install_to() { echo "tarball_install_to $*" >> "$_calls"; }
    need_cmd() { return 0; }
    export -f tarball_build_url tarball_download_and_extract tarball_install_to need_cmd
    CHANNEL=release install_tarball
    assertTrue "download_and_extract called" \
        "grep -q tarball_download_and_extract '$_calls'"
    assertTrue "install_to called" "grep -q tarball_install_to '$_calls'"
}

testParseArgsInstallMethodSnap() {
    parse_args --install-method snap
    assertEquals "method=snap" "snap" "$INSTALL_METHOD"
}

testChannelToSnapChannelRelease() {
    CHANNEL=release
    assertEquals "snap release -> stable" "stable" "$(channel_to_snap_channel)"
}

testChannelToSnapChannelBeta() {
    CHANNEL=beta
    assertEquals "snap beta -> beta" "beta" "$(channel_to_snap_channel)"
}

testChannelToSnapChannelNightly() {
    CHANNEL=nightly
    assertEquals "snap nightly -> edge" "edge" "$(channel_to_snap_channel)"
}

testChannelToSnapChannelEsr() {
    CHANNEL=esr
    assertEquals "snap esr -> esr/stable" "esr/stable" "$(channel_to_snap_channel)"
}

testChannelToSnapChannelDeveditionErrors() {
    assertFalse "snap devedition errors" \
        '( CHANNEL=devedition channel_to_snap_channel >/dev/null 2>&1 )'
}

testDetectBestMethodSnap() {
    check_cmd() { [ "$1" = "snap" ]; }; export -f check_cmd
    detect_best_method
    assertEquals "snap -> snap" "snap" "$INSTALL_METHOD"
}

testDetectBestMethodSnapOverFlatpak() {
    check_cmd() { [ "$1" = "snap" ] || [ "$1" = "flatpak" ]; }; export -f check_cmd
    detect_best_method
    assertEquals "snap beats flatpak when both present" "snap" "$INSTALL_METHOD"
}

testInstallSnapFreshInstall() {
    CHANNEL=release
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    snap() { echo "snap $*" >> "$_calls"; [ "$1" = "list" ] && return 1; return 0; }
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }
    retry() { "$@"; }
    need_cmd() { return 0; }
    export -f snap run_sudo retry need_cmd
    install_snap
    assertTrue "fresh install: snap install called" \
        "grep -q 'run_sudo snap install firefox --channel=stable' '$_calls'"
}

testInstallSnapAlreadyInstalled() {
    CHANNEL=beta
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    snap() { echo "snap $*" >> "$_calls"; return 0; }
    run_sudo() { echo "run_sudo $*" >> "$_calls"; }
    retry() { "$@"; }
    need_cmd() { return 0; }
    export -f snap run_sudo retry need_cmd
    install_snap
    assertTrue "already installed: snap refresh called" \
        "grep -q 'run_sudo snap refresh firefox --channel=beta' '$_calls'"
}

testInstallFirefoxDispatchSnap() {
    local _calls="${SHUNIT_TMPDIR}/calls.tmp"
    install_snap() { echo "install_snap" >> "$_calls"; }; export -f install_snap
    INSTALL_METHOD=snap
    install_firefox
    assertTrue "install_firefox dispatches to snap" "grep -q install_snap '$_calls'"
}

. shunit2
