#!/bin/sh
# shellcheck shell=dash
# shellcheck disable=SC3043  # local is non-POSIX but supported by all target shells
# Firefox installer script
# Usage: curl --proto '=https' --tlsv1.2 -sSf <url>/install-firefox.sh -o install-firefox.sh && sh install-firefox.sh [OPTIONS]
#    or: sh install-firefox.sh [OPTIONS]
#
# Options:
#   --channel <release|beta|devedition|nightly>   Choose Firefox channel (default: release)
#   --install-method <method>          Force install method: apt, rpm, flatpak, tarball, snap
#   --lang <locale>                    Force locale (e.g. fr, de, ja)
#   -v, --verbose                      Enable verbose output
#   -h, --help                         Show this help

set -eu

CHANNEL="release"
INSTALL_METHOD=""
LANG_OVERRIDE=""
VERBOSE=false
ARCH=""
DISTRO=""
DISTRO_VERSION=""
DETECTED_LOCALE=""

info()  { printf "[info] %s\n" "$*"; }
ok()    { printf "[ok] %s\n" "$*"; }
warn()  { printf "[warn] %s\n" "$*" >&2; }
error() { printf "[error] %s\n" "$*" >&2; exit 1; }

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        error "Required command not found: $1"
    fi
}

check_cmd() {
    command -v "$1" >/dev/null 2>&1
}

run_sudo() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        [ "$VERBOSE" = true ] && info "Running with sudo: $*"
        sudo "$@"
    fi
}

retry() {
    local _n=3 _delay=5 _i=0
    while true; do
        if "$@"; then
            return 0
        fi
        _i=$((_i + 1))
        if [ "$_i" -ge "$_n" ]; then
            error "Command failed after $_n attempts: $*"
        fi
        warn "Attempt $_i/$_n failed; retrying in ${_delay}s..."
        sleep "$_delay"
        _delay=$((_delay * 2))
    done
}

# Finds the l10n package for $DETECTED_LOCALE given a base package name and a
# package existence check command. Tries the full locale code first (fr-FR),
# then the language-only code (fr). Sets _l10n_pkg to the found name or "".
find_l10n_package() {
    local _check_cmd="$1"
    local _base_pkg="$2"
    _l10n_pkg=""
    if [ "$DETECTED_LOCALE" = "en-US" ]; then
        return
    fi
    local _full _lang _cand
    _full=$(echo "$DETECTED_LOCALE" | tr '[:upper:]_' '[:lower:]-')
    _lang="${_full%%-*}"
    for _cand in "$_full" "$_lang"; do
        if "$_check_cmd" "${_base_pkg}-l10n-${_cand}" >/dev/null 2>&1; then
            _l10n_pkg="${_base_pkg}-l10n-${_cand}"
            return
        fi
    done
    warn "No language pack available for $DETECTED_LOCALE; installing en-US"
}

# Downloader abstraction: prefer curl, fall back to wget
download() {
    local _url="$1"
    local _output="$2"

    if check_cmd curl; then
        curl --proto '=https' --tlsv1.2 -sSf --retry 3 -C - -L -o "$_output" "$_url"
    elif check_cmd wget; then
        wget --https-only --quiet --continue -O "$_output" "$_url"
    else
        error "Neither curl nor wget found. Install one and retry."
    fi
}

# Downloader for piping to stdout (e.g. GPG keys)
download_to_stdout() {
    local _url="$1"

    if check_cmd curl; then
        curl --proto '=https' --tlsv1.2 -sSf --retry 3 "$_url"
    elif check_cmd wget; then
        wget --https-only --quiet -O - "$_url"
    else
        error "Neither curl nor wget found. Install one and retry."
    fi
}

rpm_pkg_exists() {
    local _mgr="$1"
    local _name="$2"
    case "$_mgr" in
        dnf|yum)  "$_mgr" info "$_name" >/dev/null 2>&1 ;;
        zypper)   zypper --non-interactive search --match-exact --type package "$_name" >/dev/null 2>&1 ;;
        *)        return 1 ;;
    esac
}

# Check that a URL is reachable before relying on it
check_url() {
    local _url="$1"

    if check_cmd curl; then
        curl --proto '=https' --tlsv1.2 -sSf --head --retry 2 "$_url" >/dev/null 2>&1
    elif check_cmd wget; then
        wget --https-only --spider --quiet "$_url" 2>/dev/null
    else
        # Unreachable in practice: main() checks for curl/wget before calling
        # any install function that invokes check_url.
        return 1
    fi
}

usage() {
    cat <<'EOF'
Firefox installer script
Usage: curl --proto '=https' --tlsv1.2 -sSf <url>/install-firefox.sh -o install-firefox.sh && sh install-firefox.sh [OPTIONS]
   or: sh install-firefox.sh [OPTIONS]

Options:
  --channel <release|beta|devedition|nightly>   Choose Firefox channel (default: release)
  --install-method <method>          Force install method: apt, rpm, flatpak, tarball, snap
  --lang <locale>                    Force locale (e.g. fr, de, ja)
  -v, --verbose                      Enable verbose output (shell trace + sudo command logging)
  -h, --help                         Show this help
EOF
    exit 0
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --channel)
                shift
                CHANNEL="${1:?Missing channel value}"
                case "$CHANNEL" in
                    release|beta|devedition|nightly) ;;
                    *) error "Invalid channel '$CHANNEL'. Must be: release, beta, devedition, nightly" ;;
                esac
                ;;
            --install-method)
                shift
                INSTALL_METHOD="${1:?Missing install method value}"
                case "$INSTALL_METHOD" in
                    apt|rpm|flatpak|snap|tarball) ;;
                    *) error "Invalid install method '$INSTALL_METHOD'. Must be: apt, rpm, flatpak, snap, tarball" ;;
                esac
                ;;
            --lang)
                shift
                LANG_OVERRIDE="${1:?Missing lang value}"
                ;;
            -v|--verbose)
                VERBOSE=true
                ;;
            -h|--help)
                usage
                ;;
            *)
                error "Unknown option: $1"
                ;;
        esac
        shift
    done
}

detect_os() {
    need_cmd uname

    case "$(uname -s)" in
        Linux) ;;
        *)
            warn "Diagnostic info for bug report:"
            warn "  uname -a: $(uname -a)"
            [ -f /etc/os-release ] && warn "  os-release: $(cat /etc/os-release)"
            error "Unsupported operating system: $(uname -s) (this script only supports Linux)"
            ;;
    esac

    case "$(uname -m)" in
        x86_64|amd64)  ARCH="x86_64" ;;
        aarch64|arm64) ARCH="aarch64" ;;
        *)
            warn "Diagnostic info for bug report:"
            warn "  uname -a: $(uname -a)"
            error "Unsupported architecture: $(uname -m)"
            ;;
    esac

    local _os_release="${_OS_RELEASE:-/etc/os-release}"
    if [ -f "$_os_release" ]; then
        # shellcheck source=/dev/null
        . "$_os_release"
        DISTRO="${ID:-unknown}"
        DISTRO_VERSION="${VERSION_ID:-}"
        info "Detected: ${PRETTY_NAME:-$DISTRO} ($ARCH)"
    else
        DISTRO="unknown"
        warn "Could not detect Linux distribution"
    fi
}

detect_locale() {
    if [ -n "$LANG_OVERRIDE" ]; then
        DETECTED_LOCALE="$LANG_OVERRIDE"
        info "Using forced locale: $DETECTED_LOCALE"
        return
    fi

    local _raw_locale=""
    _raw_locale="${LC_ALL:-${LC_MESSAGES:-${LANG:-en_US.UTF-8}}}"
    _raw_locale="${_raw_locale%%.*}"  # strip .UTF-8
    _raw_locale="${_raw_locale%%@*}"  # strip @modifier

    case "$_raw_locale" in
        C|POSIX|"") DETECTED_LOCALE="en-US" ;;
        *_*)
            local _lang="${_raw_locale%%_*}"
            local _region="${_raw_locale##*_}"
            DETECTED_LOCALE="${_lang}-${_region}"
            ;;
        *)
            DETECTED_LOCALE="$_raw_locale"
            ;;
    esac
    info "Detected locale: $DETECTED_LOCALE"
}

channel_to_package() {
    case "$CHANNEL" in
        release)    echo "firefox" ;;
        beta)       echo "firefox-beta" ;;
        devedition) echo "firefox-devedition" ;;
        nightly)    echo "firefox-nightly" ;;
        *)          error "Unsupported channel: $CHANNEL" ;;
    esac
}

apt_source_format() {
    case "$DISTRO" in
        debian)
            if [ -n "$DISTRO_VERSION" ] && \
               dpkg --compare-versions "$DISTRO_VERSION" ge "13" 2>/dev/null; then
                echo "deb822"; return
            fi
            ;;
        ubuntu)
            if [ -n "$DISTRO_VERSION" ] && \
               dpkg --compare-versions "$DISTRO_VERSION" ge "25.10" 2>/dev/null; then
                echo "deb822"; return
            fi
            ;;
    esac
    echo "list"
}

# Returns 0 if the system supports the modern signed-by keyring (Debian 11+,
# Ubuntu 22.04+), 1 if it requires the legacy apt-key path.
apt_use_modern_keyring() {
    case "$DISTRO" in
        debian)
            if [ -n "$DISTRO_VERSION" ] && \
               dpkg --compare-versions "$DISTRO_VERSION" lt "11" 2>/dev/null; then
                return 1
            fi
            ;;
        ubuntu)
            if [ -n "$DISTRO_VERSION" ] && \
               dpkg --compare-versions "$DISTRO_VERSION" lt "22.04" 2>/dev/null; then
                return 1
            fi
            ;;
    esac
    return 0
}

apt_verify_gpg_fingerprint() {
    local _keyring="$1"
    local _expected="35BAA0B33E9EB396F59CA838C0BA5CE6DC6315A3"
    local _actual
    _actual="$(gpg --with-colons --show-keys "$_keyring" 2>/dev/null \
        | awk -F: '/^fpr:/{print $10; exit}')"
    if [ "$_actual" != "$_expected" ]; then
        run_sudo rm -f "$_keyring"
        error "GPG key fingerprint mismatch: expected $_expected, got $_actual"
    fi
    info "GPG key fingerprint verified: $_actual"
}

apt_write_source_entry() {
    local _keyring="$1"
    local _repo_url="$2"
    if [ "$(apt_source_format)" = "deb822" ]; then
        # ${_repo_url} and ${_keyring} are interpolated — safe because both are
        # hardcoded Mozilla URLs set by the caller, never user-controlled input.
        run_sudo tee /etc/apt/sources.list.d/mozilla.sources >/dev/null <<SRCEOF
Types: deb
URIs: ${_repo_url}
Suites: mozilla
Components: main
Signed-By: ${_keyring}
SRCEOF
    else
        echo "deb [signed-by=$_keyring] $_repo_url mozilla main" \
            | run_sudo tee /etc/apt/sources.list.d/mozilla.list >/dev/null
    fi
}

# Removes any conflicting legacy Mozilla apt sources that would prevent the
# modern signed-by keyring from working correctly.
# Path variables are overrideable for testing.
apt_cleanup_legacy_sources() {
    local _legacy_key="${_APT_LEGACY_KEY:-/etc/apt/trusted.gpg.d/packages.mozilla.org.gpg}"
    local _sources_dir="${_APT_SOURCES_DIR:-/etc/apt/sources.list.d}"
    local _sources_list="${_APT_SOURCES_LIST:-/etc/apt/sources.list}"
    if [ -e "$_legacy_key" ]; then
        info "Removing legacy Mozilla keyring: $_legacy_key"
        run_sudo rm -f "$_legacy_key"
    fi
    local _f
    for _f in "$_sources_dir"/*.list "$_sources_dir"/*.sources; do
        [ -e "$_f" ] || continue
        if grep -q 'packages\.mozilla\.org' "$_f" 2>/dev/null; then
            info "Removing existing Mozilla source: $_f"
            run_sudo rm -f "$_f"
        fi
    done
    if [ -e "$_sources_list" ] \
        && grep -q 'packages\.mozilla\.org' "$_sources_list" 2>/dev/null; then
        info "Stripping Mozilla entries from $_sources_list"
        run_sudo sed -i '/packages\.mozilla\.org/d' "$_sources_list"
    fi
}

apt_setup_modern_keyring() {
    local _key_url="$1"
    local _repo_url="$2"
    apt_cleanup_legacy_sources
    run_sudo install -d -m 0755 /etc/apt/keyrings
    local _keyring="/etc/apt/keyrings/packages.mozilla.org.asc"
    if [ ! -f "$_keyring" ]; then
        local _tmpkey
        _tmpkey="$(mktemp)"
        download "$_key_url" "$_tmpkey"
        run_sudo install -m 644 "$_tmpkey" "$_keyring"
        rm -f "$_tmpkey"
        if check_cmd gpg; then
            apt_verify_gpg_fingerprint "$_keyring"
        else
            warn "gpg not found, skipping key fingerprint verification"
        fi
    fi
    apt_write_source_entry "$_keyring" "$_repo_url"
}

apt_setup_legacy_keyring() {
    local _key_url="$1"
    local _repo_url="$2"
    info "Using legacy apt-key for older system"
    local _tmpkey
    _tmpkey="$(mktemp)"
    download "$_key_url" "$_tmpkey"
    if check_cmd gpg; then
        apt_verify_gpg_fingerprint "$_tmpkey"
    else
        warn "gpg not found, skipping key fingerprint verification"
    fi
    run_sudo apt-key add "$_tmpkey"
    rm -f "$_tmpkey"
    echo "deb $_repo_url mozilla main" \
        | run_sudo tee /etc/apt/sources.list.d/mozilla.list >/dev/null
}

apt_add_priority_pin() {
    # Pin Mozilla repo higher than distro packages
    run_sudo tee /etc/apt/preferences.d/mozilla >/dev/null <<'PINEOF'
Package: *
Pin: origin packages.mozilla.org
Pin-Priority: 1000
PINEOF
}

apt_install_packages() {
    local _pkg="$1"
    local _l10n_pkg
    retry run_sudo apt-get update -qq
    # Mozilla packages most languages without a region (firefox-l10n-fr),
    # but some require it (firefox-l10n-pt-br, firefox-l10n-en-gb).
    _apt_l10n_check() { apt-cache show "$1"; }
    find_l10n_package _apt_l10n_check "$_pkg"
    if [ -n "$_l10n_pkg" ]; then
        retry run_sudo apt-get install -y "$_pkg" "$_l10n_pkg"
    else
        retry run_sudo apt-get install -y "$_pkg"
    fi
}

install_apt() {
    info "Installing Firefox ($CHANNEL) via apt..."
    need_cmd apt-get
    export DEBIAN_FRONTEND=noninteractive
    local _repo_url="https://packages.mozilla.org/apt"
    local _key_url="https://packages.mozilla.org/apt/repo-signing-key.gpg"
    if apt_use_modern_keyring; then
        apt_setup_modern_keyring "$_key_url" "$_repo_url"
    else
        apt_setup_legacy_keyring "$_key_url" "$_repo_url"
    fi
    check_url "$_repo_url/dists/mozilla/Release" || \
        error "Mozilla APT repository is not reachable at $_repo_url"
    apt_add_priority_pin
    apt_install_packages "$(channel_to_package)"
    ok "Firefox ($CHANNEL, $ARCH, $DETECTED_LOCALE) installed via apt"
}

rpm_detect_pkg_manager() {
    if check_cmd dnf; then
        echo "dnf"
    elif check_cmd yum; then
        error "yum is not supported: verifying Mozilla's package signatures requires RPM 4.18+ (GPG subkey support), but yum-based systems ship older RPM versions that cannot verify the signing key. Use --install-method flatpak or --install-method tarball instead, or upgrade to a distribution with dnf (RHEL 8+, Fedora, etc.)."
    elif check_cmd zypper; then
        echo "zypper"
    else
        error "No supported RPM package manager found (dnf, yum, zypper)"
    fi
}

rpm_add_repository() {
    local _pkg_manager="$1"
    local _repo_url="$2"
    local _gpg_key="$3"
    if [ "$_pkg_manager" = "zypper" ]; then
        # --gpgcheck-allow-unsigned-repo: Mozilla's repo metadata is not GPG-signed;
        # individual .rpm packages are verified via gpgcheck=1 at install time.
        zypper lr mozilla-firefox >/dev/null 2>&1 || \
            run_sudo zypper ar -f -p 10 --gpgcheck-allow-unsigned-repo \
                "$_repo_url" mozilla-firefox
        # Note: unlike the apt path, the downloaded key is not verified against a
        # hardcoded fingerprint — packages are verified by gpgcheck=1 instead.
        retry run_sudo zypper --gpg-auto-import-keys refresh mozilla-firefox
    else
        local _repo_file="/etc/yum.repos.d/mozilla-firefox.repo"
        if [ ! -f "$_repo_file" ]; then
            run_sudo tee "$_repo_file" >/dev/null <<REPOEOF
[mozilla-firefox]
name=Mozilla Firefox
baseurl=${_repo_url}
enabled=1
gpgcheck=1
repo_gpgcheck=0
gpgkey=${_gpg_key}
priority=10
REPOEOF
        fi
    fi
}

rpm_install_packages() {
    local _pkg_manager="$1"
    local _pkg="$2"
    local _l10n_pkg="$3"
    if [ "$_pkg_manager" = "zypper" ]; then
        # --replacefiles: on openSUSE the distro ships MozillaFirefox which owns
        # /usr/bin/firefox; zypper blocks the install without this flag.
        retry run_sudo zypper install -y --replacefiles "$_pkg"
    else
        retry run_sudo "$_pkg_manager" install -y "$_pkg"
    fi
    if [ -n "$_l10n_pkg" ]; then
        # Soft fallback: Mozilla RPM repos have fewer l10n packages than apt.
        # One attempt only — retry would exit 1 on failure, defeating the fallback.
        run_sudo "$_pkg_manager" install -y "$_l10n_pkg" 2>/dev/null || \
            warn "Could not install language pack $_l10n_pkg; Firefox is installed without it"
    fi
}

install_rpm() {
    local _pkg_manager _pkg _repo_url _gpg_key _l10n_pkg
    _pkg_manager="$(rpm_detect_pkg_manager)"
    info "Installing Firefox ($CHANNEL) via $_pkg_manager..."
    _pkg="$(channel_to_package)"
    _repo_url="https://packages.mozilla.org/rpm/firefox"
    _gpg_key="https://packages.mozilla.org/rpm/firefox/signing-key.gpg"
    check_url "$_repo_url" || error "Mozilla RPM repository is not reachable at $_repo_url"
    rpm_add_repository "$_pkg_manager" "$_repo_url" "$_gpg_key"
    _rpm_l10n_check() { rpm_pkg_exists "$_pkg_manager" "$1"; }
    find_l10n_package _rpm_l10n_check "$_pkg"
    rpm_install_packages "$_pkg_manager" "$_pkg" "$_l10n_pkg"
    ok "Firefox ($CHANNEL, $ARCH, $DETECTED_LOCALE) installed via $_pkg_manager"
}

channel_to_flatpak_branch() {
    case "$CHANNEL" in
        release)    echo "" ;;
        beta)       echo "beta" ;;
        devedition) error "Firefox Developer Edition is not available as a flatpak" ;;
        nightly)    error "Firefox Nightly is not available as a flatpak" ;;
        *)          error "Unsupported channel: $CHANNEL" ;;
    esac
}

channel_to_snap_channel() {
    case "$CHANNEL" in
        release)    echo "stable" ;;
        beta)       echo "beta" ;;
        nightly)    echo "edge" ;;
        devedition) error "Firefox Developer Edition is not available as a snap" ;;
        *)          error "Unsupported channel: $CHANNEL" ;;
    esac
}

# flathub is always added: runtimes (e.g. org.freedesktop.Platform) live only
# on flathub, not flathub-beta.
flatpak_add_remotes() {
    local _branch="$1"
    retry run_sudo flatpak remote-add --if-not-exists --system flathub \
        "https://flathub.org/repo/flathub.flatpakrepo"
    if [ -n "$_branch" ]; then
        retry run_sudo flatpak remote-add --if-not-exists --system flathub-beta \
            "https://flathub.org/beta-repo/flathub-beta.flatpakrepo"
    fi
}

flatpak_install_or_update() {
    local _branch="$1"
    local _remote _ref
    _remote="${_branch:+flathub-beta}"
    _remote="${_remote:-flathub}"
    _ref="org.mozilla.firefox${_branch:+//$_branch}"
    if flatpak info --system "$_ref" >/dev/null 2>&1; then
        retry run_sudo flatpak update --system -y "$_ref"
    else
        retry run_sudo flatpak install --system -y "$_remote" "$_ref"
    fi
}

install_flatpak() {
    info "Installing Firefox ($CHANNEL) via flatpak..."
    need_cmd flatpak
    local _branch
    _branch="$(channel_to_flatpak_branch)"
    flatpak_add_remotes "$_branch"
    flatpak_install_or_update "$_branch"
    ok "Firefox ($CHANNEL, $ARCH, $DETECTED_LOCALE) installed via flatpak"
}

install_snap() {
    info "Installing Firefox ($CHANNEL) via snap..."
    need_cmd snap
    local _channel
    _channel="$(channel_to_snap_channel)"
    if snap list firefox >/dev/null 2>&1; then
        retry run_sudo snap refresh firefox --channel="$_channel"
    else
        retry run_sudo snap install firefox --channel="$_channel"
    fi
    ok "Firefox ($CHANNEL, $ARCH, $DETECTED_LOCALE) installed via snap"
}

tarball_product_name() {
    case "$CHANNEL" in
        release)    echo "firefox-latest-ssl" ;;
        beta)       echo "firefox-beta-latest-ssl" ;;
        devedition) echo "firefox-devedition-latest-ssl" ;;
        nightly)    echo "firefox-nightly-latest-ssl" ;;
        *)          error "Unsupported channel: $CHANNEL" ;;
    esac
}

tarball_build_url() {
    local _product _os
    _product="$(tarball_product_name)"
    case "$ARCH" in
        x86_64)  _os="linux64" ;;
        aarch64) _os="linux64-aarch64" ;;
        *)       error "Unsupported architecture for tarball: $ARCH" ;;
    esac
    echo "https://download.mozilla.org/?product=${_product}&os=${_os}&lang=${DETECTED_LOCALE}"
}

tarball_resolve_final_url() {
    local _url="$1"
    if check_cmd curl; then
        curl --proto '=https' --tlsv1.2 -sSfIL -o /dev/null -w '%{url_effective}' "$_url"
    elif check_cmd wget; then
        wget --https-only --server-response --spider "$_url" 2>&1 \
            | awk '/^  Location: /{url=$2} END{print url}'
    fi
    # No else: if neither downloader is available, returns empty string.
    # Caller uses ${_final_url:-$_url} and skips GPG verification with a warning.
}

tarball_key_url() {
    local _final_url="$1"
    case "$_final_url" in
        */pub/firefox/releases/*)
            echo "$_final_url" | sed 's|\(/pub/firefox/releases/[^/]*\)/.*|\1/KEY|'
            ;;
        */pub/firefox/nightly/*)
            echo "${_final_url%/*}/KEY"
            ;;
    esac
    # Unknown path: return empty; caller will skip GPG verification with a warning.
}

tarball_verify_gpg() {
    local _tarball="$1"
    local _sig_url="$2"
    local _key_url="$3"
    local _gpgdir="$4"  # Created and cleaned up by install_tarball's EXIT trap
    local _expected="14F26682D0916CDD81E37B6D61B7B526D98F0353"
    local _actual
    chmod 700 "$_gpgdir"
    download_to_stdout "$_key_url" \
        | gpg --homedir "$_gpgdir" --import >/dev/null 2>&1
    _actual="$(gpg --homedir "$_gpgdir" --with-colons --list-keys 2>/dev/null \
        | awk -F: '/^fpr:/{print $10; exit}')"
    if [ "$_actual" != "$_expected" ]; then
        error "GPG key fingerprint mismatch for tarball: expected $_expected, got ${_actual:-<none>}"
    fi
    download "$_sig_url" "$_gpgdir/firefox.tar.asc"
    if ! gpg --homedir "$_gpgdir" --verify "$_gpgdir/firefox.tar.asc" "$_tarball" >/dev/null 2>&1; then
        error "GPG signature verification of tarball failed"
    fi
    info "Tarball GPG signature verified"
}

tarball_download_and_extract() {
    local _url="$1"
    local _tmpdir="$2"
    local _final_url
    _final_url="$(tarball_resolve_final_url "$_url")"
    download "${_final_url:-$_url}" "$_tmpdir/firefox.tar"
    if [ -n "$_final_url" ] && check_cmd gpg; then
        local _key_url
        _key_url="$(tarball_key_url "$_final_url")"
        if [ -n "$_key_url" ]; then
            local _gpgdir="$_tmpdir/gpg"
            mkdir -p "$_gpgdir"
            tarball_verify_gpg "$_tmpdir/firefox.tar" "${_final_url}.asc" "$_key_url" "$_gpgdir"
        else
            warn "Skipping GPG verification (unrecognized download URL path: $_final_url)"
        fi
    else
        warn "Skipping GPG verification ($([ -z "$_final_url" ] && echo 'could not resolve download URL' || echo 'gpg not found'))"
    fi
    tar -xf "$_tmpdir/firefox.tar" -C "$_tmpdir"
    rm -f "$_tmpdir/firefox.tar"
}

tarball_install_to() {
    local _tmpdir="$1"
    local _install_dir="$2"
    local _pkg_bin="$3"
    local _extracted_dir="$_tmpdir/firefox"
    if [ ! -d "$_extracted_dir" ]; then
        error "Tarball extraction produced no firefox directory in $_tmpdir"
    fi
    # Non-atomic: backup old dir, move new dir in, then remove backup.
    # A crash mid-upgrade leaves no Firefox; acceptable for an installer script.
    run_sudo mv "$_install_dir" "${_install_dir}.old" 2>/dev/null || true
    run_sudo mv "$_extracted_dir" "$_install_dir"
    run_sudo rm -rf "${_install_dir}.old"
    run_sudo ln -sf "$_install_dir/firefox" "/usr/local/bin/${_pkg_bin}"
}

install_tarball() {
    info "Installing Firefox ($CHANNEL) via tarball..."
    need_cmd tar
    local _tmpdir _pkg_bin _install_dir
    _tmpdir="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf '$_tmpdir'" EXIT
    _pkg_bin="$(channel_to_package)"
    _install_dir="/opt/${_pkg_bin}"
    info "Downloading Firefox ($CHANNEL)..."
    tarball_download_and_extract "$(tarball_build_url)" "$_tmpdir"
    tarball_install_to "$_tmpdir" "$_install_dir" "$_pkg_bin"
    rm -rf "$_tmpdir"
    trap - EXIT
    ok "Firefox ($CHANNEL, $ARCH, $DETECTED_LOCALE) installed via tarball"
}

detect_best_method() {
    if [ -n "$INSTALL_METHOD" ]; then
        info "Using forced install method: $INSTALL_METHOD"
        return
    fi

    if check_cmd apt-get; then
        INSTALL_METHOD="apt"
    elif check_cmd dnf || check_cmd zypper; then
        INSTALL_METHOD="rpm"
    elif check_cmd snap; then
        # snap preferred over flatpak: better system integration on Ubuntu/Debian.
        INSTALL_METHOD="snap"
    elif check_cmd flatpak; then
        INSTALL_METHOD="flatpak"
    else
        INSTALL_METHOD="tarball"
    fi

    info "Selected install method: $INSTALL_METHOD"
}

install_firefox() {
    if [ "$VERBOSE" = true ]; then set -x; fi
    case "$INSTALL_METHOD" in
        apt)     install_apt ;;
        rpm)     install_rpm ;;
        flatpak) install_flatpak ;;
        snap)    install_snap ;;
        tarball) install_tarball ;;
        *)       error "Unknown install method: $INSTALL_METHOD" ;;
    esac
    if [ "$VERBOSE" = true ]; then set +x; fi
}

verify_install() {
    info "Verifying installation..."
    if [ "$INSTALL_METHOD" = "flatpak" ]; then
        local _branch _ref
        _branch="$(channel_to_flatpak_branch)"
        _ref="org.mozilla.firefox${_branch:+//$_branch}"
        if flatpak info --system "$_ref" >/dev/null 2>&1; then
            ok "Verified: Firefox flatpak ($CHANNEL) is installed"
        else
            warn "Could not verify Firefox flatpak installation"
        fi
        return
    fi
    local _pkg_bin _bin _seen _candidate
    _pkg_bin="$(channel_to_package)"
    _seen=""
    for _candidate in "firefox" "$_pkg_bin"; do
        [ "$_candidate" = "$_seen" ] && continue
        _seen="$_candidate"
        _bin="$(command -v "$_candidate" 2>/dev/null || true)"
        if [ -n "$_bin" ]; then
            local _version
            _version="$("$_bin" --version 2>/dev/null || echo "unknown version")"
            ok "$_version (at $_bin)"
            return
        fi
    done
    warn "Could not verify Firefox installation"
}

main() {
    parse_args "$@"
    info "Firefox installer - channel: $CHANNEL"

    # Verify we have a downloader before doing anything
    if ! check_cmd curl && ! check_cmd wget; then
        error "Either curl or wget is required. Install one and retry."
    fi
    need_cmd uname
    need_cmd mktemp

    detect_os
    detect_locale
    detect_best_method
    install_firefox
    verify_install
}

if [ "${INSTALL_FIREFOX_SOURCED:-}" != "1" ]; then
    main "$@"
fi
