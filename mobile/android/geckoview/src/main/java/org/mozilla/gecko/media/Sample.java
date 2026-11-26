/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.os.Build;
import android.os.Parcel;
import android.os.Parcelable;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import org.mozilla.gecko.annotation.WrapForJNI;

// Parcelable carrying input/output sample data and info cross process.
public final class Sample implements Parcelable {
  @WrapForJNI public long session;

  public static final int NO_BUFFER = -1;

  public int bufferId = NO_BUFFER;
  @WrapForJNI public BufferInfo info = new BufferInfo();
  public CryptoInfo cryptoInfo;

  // Simple Linked list for recycling objects.
  // Used to nodify Sample objects. Do not marshal/unmarshal.
  private Sample mNext;
  private static Sample sPool = new Sample();
  private static int sPoolSize = 1;

  private Sample() {}

  private void readInfo(final Parcel in) {
    final int offset = in.readInt();
    final int size = in.readInt();
    final long pts = in.readLong();
    final int flags = in.readInt();

    info.set(offset, size, pts, flags);
  }

  private void readCrypto(final Parcel in) {
    final int hasCryptoInfo = in.readInt();
    if (hasCryptoInfo == 0) {
      cryptoInfo = null;
      return;
    }

    final byte[] iv = in.createByteArray();
    final byte[] key = in.createByteArray();
    final int mode = in.readInt();
    final int[] numBytesOfClearData = in.createIntArray();
    final int[] numBytesOfEncryptedData = in.createIntArray();
    final int numSubSamples = in.readInt();

    if (cryptoInfo == null) {
      cryptoInfo = new CryptoInfo();
    }
    cryptoInfo.set(numSubSamples, numBytesOfClearData, numBytesOfEncryptedData, key, iv, mode);

    final int numEncryptBlocks = in.readInt();
    final int numSkipBlocks = in.readInt();
    cryptoInfo.setPattern(new CryptoInfo.Pattern(numEncryptBlocks, numSkipBlocks));
  }

  public Sample set(final BufferInfo info, final CryptoInfo cryptoInfo) {
    setBufferInfo(info);
    setCryptoInfo(cryptoInfo);
    return this;
  }

  public void setBufferInfo(final BufferInfo info) {
    this.info.set(0, info.size, info.presentationTimeUs, info.flags);
  }

  public void setCryptoInfo(final CryptoInfo crypto) {
    if (crypto == null) {
      cryptoInfo = null;
      return;
    }

    if (cryptoInfo == null) {
      cryptoInfo = new CryptoInfo();
    }
    if (crypto.numSubSamples > 0) {
      cryptoInfo.set(
          crypto.numSubSamples,
          crypto.numBytesOfClearData,
          crypto.numBytesOfEncryptedData,
          crypto.key,
          crypto.iv,
          crypto.mode);
    } else {
      // MediaCodec expects us to provide at least one subsample, and at least one non-null array
      // between numBytesOfClearData and numBytesOfEncryptedData.
      // Whole-block full sample encryption does not carry subsample information, so we need to
      // synthesize it by creating a subsample as big as the sample itself.
      cryptoInfo.set(1, new int[] {0}, new int[] {info.size}, crypto.key, crypto.iv, crypto.mode);
    }
    final CryptoInfo.Pattern pattern = getCryptoPatternCompat(crypto);
    if (pattern == null) {
      return;
    }
    cryptoInfo.setPattern(pattern);
  }

  @WrapForJNI
  public void dispose() {
    if (isEOS()) {
      return;
    }

    bufferId = NO_BUFFER;
    info.set(0, 0, 0, 0);
    if (cryptoInfo != null) {
      cryptoInfo.set(0, null, null, null, null, 0);
    }

    // Recycle it.
    synchronized (CREATOR) {
      this.mNext = sPool;
      sPool = this;
      sPoolSize++;
    }
  }

  public boolean isEOS() {
    return (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
  }

  public static Sample createEOS(final long session) {
    final Sample s = new Sample();
    s.session = session;
    s.info.set(0, 0, Long.MIN_VALUE, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
    return s;
  }

  public static Sample obtain() {
    synchronized (CREATOR) {
      Sample s = null;
      if (sPoolSize > 0) {
        s = sPool;
        sPool = s.mNext;
        s.mNext = null;
        sPoolSize--;
      } else {
        s = new Sample();
      }
      return s;
    }
  }

  public static final Creator<Sample> CREATOR =
      new Creator<Sample>() {
        @Override
        public Sample createFromParcel(final Parcel in) {
          return obtainSample(in);
        }

        @Override
        public Sample[] newArray(final int size) {
          return new Sample[size];
        }

        private Sample obtainSample(final Parcel in) {
          final Sample s = obtain();
          s.session = in.readLong();
          s.bufferId = in.readInt();
          s.readInfo(in);
          s.readCrypto(in);
          return s;
        }
      };

  @Override
  public int describeContents() {
    return 0;
  }

  @Override
  public void writeToParcel(final Parcel dest, final int parcelableFlags) {
    dest.writeLong(session);
    dest.writeInt(bufferId);
    writeInfo(dest);
    writeCrypto(dest);
  }

  private void writeInfo(final Parcel dest) {
    dest.writeInt(info.offset);
    dest.writeInt(info.size);
    dest.writeLong(info.presentationTimeUs);
    dest.writeInt(info.flags);
  }

  private void writeCrypto(final Parcel dest) {
    if (cryptoInfo != null) {
      dest.writeInt(1);
      dest.writeByteArray(cryptoInfo.iv);
      dest.writeByteArray(cryptoInfo.key);
      dest.writeInt(cryptoInfo.mode);
      dest.writeIntArray(cryptoInfo.numBytesOfClearData);
      dest.writeIntArray(cryptoInfo.numBytesOfEncryptedData);
      dest.writeInt(cryptoInfo.numSubSamples);
      final CryptoInfo.Pattern pattern = getCryptoPatternCompat(cryptoInfo);
      if (pattern != null) {
        dest.writeInt(pattern.getEncryptBlocks());
        dest.writeInt(pattern.getSkipBlocks());
      } else {
        // Couldn't get pattern - write default values
        dest.writeInt(0);
        dest.writeInt(0);
      }
    } else {
      dest.writeInt(0);
    }
  }

  public static byte[] byteArrayFromBuffer(
      final ByteBuffer buffer, final int offset, final int size) {
    if (buffer == null || buffer.capacity() == 0 || size == 0) {
      return null;
    }
    if (buffer.hasArray() && offset == 0 && buffer.array().length == size) {
      return buffer.array();
    }
    final int length = Math.min(offset + size, buffer.capacity()) - offset;
    final byte[] bytes = new byte[length];
    buffer.position(offset);
    buffer.get(bytes);
    return bytes;
  }

  @Override
  public String toString() {
    if (isEOS()) {
      return "EOS sample";
    }

    final StringBuilder str = new StringBuilder();
    str.append("{ session#:")
        .append(session)
        .append(", buffer#")
        .append(bufferId)
        .append(", info=")
        .append("{ offset=")
        .append(info.offset)
        .append(", size=")
        .append(info.size)
        .append(", pts=")
        .append(info.presentationTimeUs)
        .append(", flags=")
        .append(Integer.toHexString(info.flags))
        .append(" }")
        .append(" }");
    return str.toString();
  }

  public static CryptoInfo.Pattern getCryptoPatternCompat(final CryptoInfo cryptoInfo) {
    // getPattern() added in API 31:
    // https://developer.android.com/reference/android/media/MediaCodec.CryptoInfo#getPattern()
    if (Build.VERSION.SDK_INT >= 31) {
      return cryptoInfo.getPattern();
    }

    try {
      // Without getPattern(), no way to access the pattern without reflection.
      // https://cs.android.com/android/platform/superproject/+/android-11.0.0_r1:frameworks/base/media/java/android/media/MediaCodec.java;l=2718;drc=3c715d5778e15dc84082e63dc65b382d31fe8e45
      final Field patternField = CryptoInfo.class.getDeclaredField("pattern");
      patternField.setAccessible(true);
      return (CryptoInfo.Pattern) patternField.get(cryptoInfo);
    } catch (final NoSuchFieldException | IllegalAccessException e) {
      return null;
    }
  }
}
