# Image for cross-compiling NSS for Android with `build.sh --target-os=android`.
# It adds an Android NDK to the base image, which already has gyp, ninja and
# python3.
FROM $DOCKER_IMAGE_PARENT

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace
VOLUME /builds/worker/.cache

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    unzip \
 && rm -rf /var/lib/apt/lists/* \
 && apt-get autoremove -y && apt-get clean -y

# NDK r30 (30.0.16248370), the one Firefox builds with: keep this in step with
# NDK_VERSION in Firefox's python/mozboot/mozboot/android.py. Google publishes a
# SHA-1 for each NDK archive in
# https://dl.google.com/android/repository/repository2-3.xml; the SHA-256 below
# is of the archive that matched it.
ENV ANDROID_NDK_VERSION r30
ENV ANDROID_NDK_SHA256 753611f410d002cfcd3f3dc2ef49aad532089d3180b436c060a90bf0fcb64df2
RUN curl -sSfL -o /tmp/ndk.zip \
      https://dl.google.com/android/repository/android-ndk-$ANDROID_NDK_VERSION-linux.zip \
 && echo "$ANDROID_NDK_SHA256  /tmp/ndk.zip" | sha256sum -c - \
 && unzip -q /tmp/ndk.zip -d /opt \
 && rm /tmp/ndk.zip
ENV ANDROID_NDK_ROOT /opt/android-ndk-$ANDROID_NDK_VERSION

# Set a default command for debugging.
CMD ["/bin/bash", "--login"]
