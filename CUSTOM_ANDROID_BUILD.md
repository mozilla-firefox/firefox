# Firewolf Android Build Guide

This repository contains a custom GeckoView build with these changes:

- Islamic Republic of Iran Root CA-G3 added to the NSS built-in trust store.
- NSS built-in certificate library version updated.
- Certificate Transparency configured in telemetry-only mode for the GeckoView Example test application.
- Verified against `https://new.pki.co.ir/`.

> Security notice: CT telemetry-only mode is currently for testing. Before production release, replace the global CT behavior with a narrowly scoped exemption for the approved private PKI root.

## Branch

```text
feature/iran-root-ca-g3
```

## Tested environment

- macOS on Apple Silicon
- Android NDK r29, revision `29.0.14206865`
- Android SDK at `$HOME/Library/Android/sdk`
- Java 17 installed by Mozilla bootstrap
- ARM64 Android device or emulator

## Clone

```bash
git clone --branch feature/iran-root-ca-g3 https://gitlab.pki.co.ir/a.rabiei/firewolf.git
cd firewolf
```

## Bootstrap

```bash
./mach bootstrap
```

Select:

```text
4. GeckoView/Firefox for Android
```

Accept all required Android licenses.

## Environment

Install Android NDK revision `29.0.14206865`, then set paths according to the local machine:

```bash
export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_HOME="/Applications/AndroidNDK14206865.app/Contents/NDK"
```

Verify the NDK:

```bash
cat "$ANDROID_NDK_HOME/source.properties"
```

Expected output includes:

```text
Pkg.Revision = 29.0.14206865
```

## Build

```bash
./mach build
```

## APK

The GeckoView Example debug APK is generated at:

```text
obj-aarch64-unknown-linux-android/gradle/build/mobile/android/geckoview_example/outputs/apk/debug/geckoview_example-debug.apk
```

## Install on a device

Enable Developer Options and USB Debugging, then connect the device:

```bash
ADB="$HOME/Library/Android/sdk/platform-tools/adb"
APK="obj-aarch64-unknown-linux-android/gradle/build/mobile/android/geckoview_example/outputs/apk/debug/geckoview_example-debug.apk"

"$ADB" devices
"$ADB" uninstall org.mozilla.geckoview_example || true
"$ADB" install "$APK"
```

Launch:

```bash
"$ADB" shell monkey -p org.mozilla.geckoview_example -c android.intent.category.LAUNCHER 1
```

Open:

```text
https://new.pki.co.ir/
```

## Modified files

NSS trust store:

```text
security/nss/lib/ckfw/builtins/certdata.txt
```

NSS built-in certificate library version:

```text
security/nss/lib/ckfw/builtins/nssckbi.h
```

GeckoView Example CT configuration:

```text
mobile/android/geckoview_example/src/main/java/org/mozilla/geckoview_example/GeckoViewActivity.java
```

## Verify the embedded root

```bash
APK_CHECK_DIR="$(mktemp -d)"

unzip -p "$APK" lib/arm64-v8a/libxul.so > "$APK_CHECK_DIR/libxul.so"

/usr/bin/strings "$APK_CHECK_DIR/libxul.so" \
  | grep -F "Islamic Republic of Iran Root CA-G3"
```

Root SHA-256 fingerprint:

```text
AA:7F:F6:37:D0:61:29:5A:6A:00:3E:9D:66:21:A5:4F:47:80:BA:E7:AE:F9:60:43:A7:1B:7C:2A:F2:76:D5:56
```

## Certificate Transparency

The GeckoView Example currently uses:

```java
setCertificateTransparencyMode(1)
```

Mode `1` keeps CT telemetry enabled but does not reject a certificate because it has no valid SCT. This setting applies to all sites loaded by the test application and must be reviewed before production use.
