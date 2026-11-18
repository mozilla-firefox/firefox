(flatpak)=

# Firefox Flatpak Packaging

This page explains interactions between Firefox and Flatpak packaging format.

## Where is the upstream

The code reference itself is the Firefox repository and the repackaging is under {searchfox}`the mach repackage flatpak command <python/mozbuild/mozbuild/repackaging/flatpak.py>`.

## Where to report bugs

All bugs should be reported to Bugzilla in the appropriate component depending on the bug and marked as blocking the `flatpak` meta-bug.

## Build process

Perform a build and then run `mach repackage flatpak`, e.g.:

```shell
$ mach repackage flatpak \
                 --input target.tar.xz \
                 --name org.mozilla.firefox \
                 --arch aarch64 \
                 --version 137.0a1 \
                 --product firefox \
                 --release-type nightly \
                 --flatpak-branch nightly \
                 --template-dir browser/installer/linux/app/flatpak \
                 --langpack-pattern $PWD/langpacks/*.xpi \
                 --output test.flatpak.tar.xz,
```

Where `target.tar.xz` can be a downloaded artifact from try build or built from a local build. You will also want to download [some langpack](https://ftp.mozilla.org/pub/firefox/nightly/latest-mozilla-central-l10n/linux-x86_64/xpi/).

## How to hack on try

Pushing to try is basically just:

```shell
mach try fuzzy --full -q "'repackage 'flatpak !shippable"`
```

This will produce a repackage flatpak task at the end that generates a `target.flatpak.tar.xz`.

## Installing the try build

Download and extract the previously generated `target.flatpak.tar.xz` and it will produce a `./repo` directory that you can directly use with flatpak:

```shell
flatpak --user --no-gpg-verify remote-add firefox-try ./repo/
```

This should add you a user-level firefox-try flatpak remote, you can verify with (`flathub` remote may be a user or a system level remote):

```shell
$ flatpak remotes
Name        Options
firefox-try user
flathub     user
```

Then you can install your local build:

```shell
$ flatpak install firefox-try firefox
Looking for matches…
Found ref ‘app/org.mozilla.firefox/x86_64/nightly’ in remote ‘firefox-try’ (user).
Use this ref? [Y/n]: y

org.mozilla.firefox permissions:
    ipc          network                cups                  fallback-x11            pcsc                         pulseaudio       wayland       x11       devices
    devel        file access [1]        dbus access [2]       bus ownership [3]       system dbus access [4]

    [1] /run/.heim_org.h5l.kcm-socket, xdg-download, xdg-run/speech-dispatcher:ro
    [2] org.a11y.Bus, org.freedesktop.FileManager1, org.gtk.vfs.*
    [3] org.mozilla.firefox.*, org.mozilla.firefox_beta.*, org.mpris.MediaPlayer2.firefox.*
    [4] org.freedesktop.NetworkManager


        ID                                              Branch                 Op            Remote                 Download
 1. [✓] org.freedesktop.Platform.GL.default             24.08                  u             flathub                 67,3 Mo / 156,6 Mo
 2. [✓] org.freedesktop.Platform.GL.default             24.08extra             u             flathub                  3,9 Mo / 156,6 Mo
 3. [✓] org.freedesktop.Platform.Locale                 24.08                  u             flathub                282,1 Ko / 380,3 Mo
 4. [✓] org.freedesktop.Platform                        24.08                  u             flathub                 25,4 Mo / 264,4 Mo
 5. [✓] org.mozilla.firefox.Locale                      nightly                i             firefox-try              1,0 Ko / 1,6 Mo
 6. [✓] org.mozilla.firefox                             nightly                i             firefox-try              1,0 Ko / 111,5 Mo

Changes complete.
```

And after that you can just run `flatpak run org.mozilla.firefox//nightly`.
