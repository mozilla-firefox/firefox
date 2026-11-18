/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.mozglue;

import android.annotation.SuppressLint;
import android.os.MemoryFile;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.Parcelable;
import android.util.Log;
import java.io.FileDescriptor;
import java.io.IOException;
import java.lang.reflect.Method;

@SuppressLint("DiscouragedPrivateApi")
public final class SharedMemory implements Parcelable {
  private static final String LOGTAG = "GeckoShmem";
  private static final Method sGetFDMethod;
  private ParcelFileDescriptor mDescriptor;
  private int mSize;
  private int mId;
  private long mHandle; // The native pointer.
  private boolean mIsMapped;
  private MemoryFile mBackedFile;

  // MemoryFile.getFileDescriptor() is hidden. :(
  static {
    Method method = null;
    try {
      method = MemoryFile.class.getDeclaredMethod("getFileDescriptor");
    } catch (final NoSuchMethodException e) {
      e.printStackTrace();
    }
    sGetFDMethod = method;
  }

  private SharedMemory(final Parcel in) {
    mDescriptor = in.readFileDescriptor();
    mSize = in.readInt();
    mId = in.readInt();
  }

  public static final Creator<SharedMemory> CREATOR =
      new Creator<SharedMemory>() {
        @Override
        public SharedMemory createFromParcel(final Parcel in) {
          return new SharedMemory(in);
        }

        @Override
        public SharedMemory[] newArray(final int size) {
          return new SharedMemory[size];
        }
      };

  @Override
  public int describeContents() {
    return CONTENTS_FILE_DESCRIPTOR;
  }

  @Override
  public void writeToParcel(final Parcel dest, final int flags) {
    // We don't want ParcelFileDescriptor.writeToParcel() to close the fd.
    dest.writeFileDescriptor(mDescriptor.getFileDescriptor());
    dest.writeInt(mSize);
    dest.writeInt(mId);
  }

  public SharedMemory(final int id, final int size) throws NoSuchMethodException, IOException {
    if (sGetFDMethod == null) {
      throw new NoSuchMethodException("MemoryFile.getFileDescriptor() doesn't exist.");
    }
    mBackedFile = new MemoryFile(null, size);
    try {
      final FileDescriptor fd = (FileDescriptor) sGetFDMethod.invoke(mBackedFile);
      mDescriptor = ParcelFileDescriptor.dup(fd);
      mSize = size;
      mId = id;
    } catch (final Exception e) {
      e.printStackTrace();
      close();
      throw new IOException(e.getMessage());
    }
  }

  public void flush() {
    if (!mIsMapped) {
      return;
    }

    unmap(mHandle, mSize);
    mHandle = 0;
    mIsMapped = false;
  }

  public void close() {
    flush();

    if (mDescriptor != null) {
      try {
        mDescriptor.close();
      } catch (final IOException e) {
        e.printStackTrace();
      }
      mDescriptor = null;
    }
  }

  // Should only be called by process that allocates shared memory.
  public void dispose() {
    if (!isValid()) {
      return;
    }

    close();

    if (mBackedFile != null) {
      mBackedFile.close();
      mBackedFile = null;
    }
  }

  private native void unmap(long address, int size);

  public boolean isValid() {
    return mDescriptor != null;
  }

  public int getSize() {
    return mSize;
  }

  private int getFD() {
    return isValid() ? mDescriptor.getFd() : -1;
  }

  public long getPointer() {
    if (!isValid()) {
      return 0;
    }

    if (!mIsMapped) {
      try {
        mHandle = map(getFD(), mSize);
      } catch (final NullPointerException e) {
        Log.e(LOGTAG, "SharedMemory#" + mId + " error.", e);
        throw e;
      }
      if (mHandle != 0) {
        mIsMapped = true;
      }
    }
    return mHandle;
  }

  private native long map(int fd, int size);

  @Override
  protected void finalize() throws Throwable {
    if (mBackedFile != null) {
      Log.w(LOGTAG, "dispose() not called before finalizing");
    }
    dispose();

    super.finalize();
  }

  @Override
  public String toString() {
    return "SHM("
        + getSize()
        + " bytes): id="
        + mId
        + ", backing="
        + mBackedFile
        + ",fd="
        + mDescriptor;
  }

  @Override
  public boolean equals(final Object that) {
    return (this == that) || ((that instanceof SharedMemory) && (hashCode() == that.hashCode()));
  }

  @Override
  public int hashCode() {
    return mId;
  }
}
