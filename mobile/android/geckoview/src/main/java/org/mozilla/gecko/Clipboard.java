/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.ClipboardManager.OnPrimaryClipChangedListener;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.Build;
import android.os.PersistableBundle;
import android.text.TextUtils;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.atomic.AtomicLong;
import org.mozilla.gecko.annotation.WrapForJNI;

public final class Clipboard {
  private static final String HTML_MIME = "text/html";
  private static final String PLAINTEXT_MIME = "text/plain";
  private static final String LOGTAG = "GeckoClipboard";
  private static final int DEFAULT_BUFFER_SIZE = 8192;

  // W3C clipboard-apis spec, Appendix A. Android shares the Linux/ChromeOS
  // convention: the JSON essence->slot map is held under
  // "application/web;type=\"custom/formatmap\"", and each essence's bytes
  // live under "application/web;type=\"custom/formatN\"".
  private static final String WEB_CUSTOM_FORMAT_MAP_MIME =
      "application/web;type=\"custom/formatmap\"";

  private static OnPrimaryClipChangedListener sClipboardChangedListener = null;
  private static final AtomicLong sClipboardSequenceNumber = new AtomicLong();
  private static final AtomicLong sClipboardTimestamp = new AtomicLong();

  private Clipboard() {}

  /**
   * Get the text on the primary clip on Android clipboard
   *
   * @param context application context.
   * @return a plain text string of clipboard data.
   */
  public static String getText(final Context context) {
    return getTextData(context, PLAINTEXT_MIME);
  }

  /**
   * Get the text data on the primary clip on clipboard
   *
   * @param context application context
   * @param mimeType the mime type we want. This supports text/html and text/plain only. If other
   *     type, we do nothing.
   * @return a string into clipboard.
   */
  @WrapForJNI(calledFrom = "gecko")
  private static String getTextData(final Context context, final String mimeType) {
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    if (cm.hasPrimaryClip()) {
      final ClipData clip = cm.getPrimaryClip();
      if (clip == null || clip.getItemCount() == 0) {
        return null;
      }

      final ClipDescription description = clip.getDescription();
      if (HTML_MIME.equals(mimeType)
          && description.hasMimeType(ClipDescription.MIMETYPE_TEXT_HTML)) {
        final CharSequence data = clip.getItemAt(0).getHtmlText();
        if (data == null) {
          return null;
        }
        return data.toString();
      }
      if (PLAINTEXT_MIME.equals(mimeType)) {
        try {
          return clip.getItemAt(0).coerceToText(context).toString();
        } catch (final SecurityException e) {
          Log.e(LOGTAG, "Couldn't get clip data from clipboard", e);
        }
      }
    }
    return null;
  }

  /**
   * Get the blob data on the primary clip on clipboard
   *
   * @param mimeType the mime type we want.
   * @return a byte array into clipboard.
   */
  @WrapForJNI(calledFrom = "gecko", exceptionMode = "nsresult")
  private static byte[] getRawData(final String mimeType) {
    final Context context = GeckoAppShell.getApplicationContext();
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    if (cm.hasPrimaryClip()) {
      final ClipData clip = cm.getPrimaryClip();
      if (clip == null || clip.getItemCount() == 0) {
        return null;
      }

      final ClipDescription description = clip.getDescription();
      if (description.hasMimeType(mimeType)) {
        return getRawDataFromClipData(context, clip);
      }
    }
    return null;
  }

  private static byte[] getRawDataFromClipData(final Context context, final ClipData clipData) {
    try (final AssetFileDescriptor descriptor =
            context
                .getContentResolver()
                .openAssetFileDescriptor(clipData.getItemAt(0).getUri(), "r");
        final InputStream inputStream = new FileInputStream(descriptor.getFileDescriptor());
        final ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
      final byte[] data = new byte[DEFAULT_BUFFER_SIZE];
      int readed;
      while ((readed = inputStream.read(data)) != -1) {
        outputStream.write(data, 0, readed);
      }
      return outputStream.toByteArray();
    } catch (final IOException e) {
      Log.e(LOGTAG, "Couldn't get clip data from clipboard due to I/O error", e);
    } catch (final OutOfMemoryError e) {
      Log.e(LOGTAG, "Couldn't get clip data from clipboard due to OOM", e);
    }
    return null;
  }

  /**
   * Set plain text to clipboard
   *
   * @param context application context
   * @param text a plain text to set to clipboard
   * @param isPrivateData whether to mark the clip as sensitive
   * @return true if copy is successful.
   */
  @WrapForJNI(calledFrom = "gecko")
  public static boolean setText(
      final Context context, final CharSequence text, final boolean isPrivateData) {
    return setData(context, ClipData.newPlainText("text", text), isPrivateData);
  }

  /**
   * Store HTML to clipboard
   *
   * @param context application context
   * @param text a plain text to set to clipboard
   * @param html a html text to set to clipboard
   * @return true if copy is successful.
   */
  @WrapForJNI(calledFrom = "gecko")
  private static boolean setHTML(
      final Context context,
      final CharSequence text,
      final String htmlText,
      final boolean isPrivateData) {
    return setData(context, ClipData.newHtmlText("html", text, htmlText), isPrivateData);
  }

  /**
   * Store {@link android.content.ClipData} to clipboard
   *
   * @param context application context
   * @param clipData a {@link android.content.ClipData} to set to clipboard
   * @return true if copy is successful.
   */
  /* package */ static boolean setData(
      final Context context, final ClipData clipData, final boolean isPrivateData) {
    // In API Level 11 and above, CLIPBOARD_SERVICE returns android.content.ClipboardManager,
    // which is a subclass of android.text.ClipboardManager.
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    if (isPrivateData) {
      final PersistableBundle extras = new PersistableBundle();
      extras.putBoolean("android.content.extra.IS_SENSITIVE", true);
      clipData.getDescription().setExtras(extras);
    }
    try {
      cm.setPrimaryClip(clipData);
    } catch (final NullPointerException e) {
      // Bug 776223: This is a Samsung clipboard bug. setPrimaryClip() can throw
      // a NullPointerException if Samsung's /data/clipboard directory is full.
      // Fortunately, the text is still successfully copied to the clipboard.
    } catch (final RuntimeException e) {
      // If clipData is too large, TransactionTooLargeException occurs.
      Log.e(LOGTAG, "Couldn't set clip data to clipboard", e);
      return false;
    }
    updateSequenceNumber(context);
    return true;
  }

  /**
   * Check whether primary clipboard has given MIME type.
   *
   * @param context application context
   * @param mimeType MIME type
   * @return true if the clipboard is nonempty, false otherwise.
   */
  @WrapForJNI(calledFrom = "gecko")
  private static boolean hasData(final Context context, final String mimeType) {
    if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
      if (HTML_MIME.equals(mimeType) || PLAINTEXT_MIME.equals(mimeType)) {
        return !TextUtils.isEmpty(getTextData(context, mimeType));
      }
    }

    // Calling getPrimaryClip causes a toast message from Android 12.
    // https://developer.android.com/about/versions/12/behavior-changes-all#clipboard-access-notifications

    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);

    if (!cm.hasPrimaryClip()) {
      return false;
    }

    final ClipDescription description = cm.getPrimaryClipDescription();
    if (description == null) {
      return false;
    }

    if (HTML_MIME.equals(mimeType)) {
      return description.hasMimeType(ClipDescription.MIMETYPE_TEXT_HTML);
    }

    if (PLAINTEXT_MIME.equals(mimeType)) {
      // We cannot check content in data at this time to avoid toast message.
      return description.hasMimeType(ClipDescription.MIMETYPE_TEXT_HTML)
          || description.hasMimeType(ClipDescription.MIMETYPE_TEXT_PLAIN);
    }

    return description.hasMimeType(mimeType);
  }

  /**
   * Deletes all data from the clipboard.
   *
   * @param context application context
   */
  @WrapForJNI(calledFrom = "gecko")
  private static void clear(final Context context) {
    if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
      setText(context, null, false);
      return;
    }
    // Although we don't know more details of https://crbug.com/1203377, Blink doesn't use
    // clearPrimaryClip on Android P since this may throw an exception, even if it is supported
    // on Android P.
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    cm.clearPrimaryClip();
  }

  /**
   * Start monitor clipboard sequence number.
   *
   * @param context application context
   */
  @WrapForJNI(calledFrom = "gecko")
  private static void startTrackingClipboardData(final Context context) {
    if (sClipboardChangedListener != null) {
      return;
    }

    sClipboardChangedListener =
        new OnPrimaryClipChangedListener() {
          @Override
          public void onPrimaryClipChanged() {
            final Context appContext = GeckoAppShell.getApplicationContext();
            Clipboard.updateSequenceNumber(appContext);
            // Reap our in-memory clipboard bytes if the primary clip is no
            // longer ours; another app writing text/image/etc. makes our
            // URIs unreachable.
            GeckoClipboardContentProvider.onPrimaryClipChanged(appContext);
          }
        };

    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    cm.addPrimaryClipChangedListener(sClipboardChangedListener);
  }

  /** Stop monitor clipboard sequence number. */
  @WrapForJNI(calledFrom = "gecko")
  private static void stopTrackingClipboardData(final Context context) {
    if (sClipboardChangedListener == null) {
      return;
    }

    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    cm.removePrimaryClipChangedListener(sClipboardChangedListener);
    sClipboardChangedListener = null;
  }

  private static long getClipboardTimestamp(final Context context) {
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    final ClipDescription description = cm.getPrimaryClipDescription();
    if (description == null) {
      return 0;
    }
    return description.getTimestamp();
  }

  public static void updateSequenceNumber(final Context context) {
    final long timestamp = getClipboardTimestamp(context);
    if (timestamp != 0) {
      if (timestamp == sClipboardTimestamp.get()) {
        return;
      }
      sClipboardTimestamp.set(timestamp);
    }

    sClipboardSequenceNumber.incrementAndGet();
  }

  /** Get clipboard sequence number. */
  @WrapForJNI(calledFrom = "gecko")
  private static long getSequenceNumber(final Context context) {
    return sClipboardSequenceNumber.get();
  }

  // ---- Multi-MIME clipboard write support ----
  //
  // Writes use a session model so we don't need to pass byte[][] across JNI.
  // The session is MIME-agnostic: any number of (MIME, bytes) payloads can be
  // pushed in, and the commit step builds a single multi-item ClipData backed
  // by GeckoClipboardContentProvider URIs. Web custom format is the first consumer;
  // future callers can reuse the same session to publish arbitrary
  // (MIME, bytes) sets alongside standard text/HTML.
  //   1. openWriteSession() returns a session id.
  //   2. addPayload() appends one (MIME, bytes) pair. Payload order matters:
  //      the first added payload becomes ClipData.Item[0]'s URI (bundled with
  //      the standard text/HTML), subsequent payloads become items 1..N.
  //   3. commitWriteSession() finalises the ClipData and installs it as the
  //      primary clip.

  @WrapForJNI(calledFrom = "gecko")
  private static long openWriteSession() {
    return GeckoClipboardContentProvider.openSession();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void addPayload(
      final long sessionId, final String mimeType, final byte[] payload) {
    GeckoClipboardContentProvider.addPayload(sessionId, mimeType, payload);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static boolean commitWriteSession(
      final long sessionId,
      final CharSequence text,
      final String htmlText,
      final boolean isPrivateData) {
    return GeckoClipboardContentProvider.commitSessionToClipboard(
        GeckoAppShell.getApplicationContext(), sessionId, text, htmlText, isPrivateData);
  }

  /**
   * Returns the {@code application/web;type="custom/formatmap"} JSON for the primary clip, or null.
   */
  @WrapForJNI(calledFrom = "gecko")
  private static String getWebCustomFormatMapJson(final Context context) {
    final byte[] bytes = readWebClipBytes(context, WEB_CUSTOM_FORMAT_MAP_MIME);
    if (bytes == null) {
      return null;
    }
    return new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
  }

  /**
   * Returns the bytes for a per-essence {@code application/web;type="custom/formatN"} MIME, or
   * null.
   */
  @WrapForJNI(calledFrom = "gecko")
  private static byte[] getWebCustomFormatPayload(final Context context, final String mimeType) {
    return readWebClipBytes(context, mimeType);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static boolean hasWebCustomFormatMap(final Context context) {
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    if (!cm.hasPrimaryClip()) {
      return false;
    }
    final ClipDescription description = cm.getPrimaryClipDescription();
    return description != null && description.hasMimeType(WEB_CUSTOM_FORMAT_MAP_MIME);
  }

  // Walks the primary clip's items looking for one whose Uri the
  // ContentResolver reports as serving aMimeType, and reads it.
  private static byte[] readWebClipBytes(final Context context, final String mimeType) {
    final ClipboardManager cm =
        (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
    if (!cm.hasPrimaryClip()) {
      return null;
    }
    // ClipboardManager.getPrimaryClip() can throw when the clip's owning
    // app process has died and Android can't rehydrate the ClipData.
    final ClipData clip;
    try {
      clip = cm.getPrimaryClip();
    } catch (final RuntimeException e) {
      Log.w(LOGTAG, "readWebClipBytes: getPrimaryClip failed", e);
      return null;
    }
    if (clip == null) {
      return null;
    }
    final ClipDescription description = clip.getDescription();
    if (description == null || !description.hasMimeType(mimeType)) {
      return null;
    }
    for (int i = 0; i < clip.getItemCount(); ++i) {
      final Uri uri = clip.getItemAt(i).getUri();
      if (uri == null) {
        continue;
      }
      final String[] supportedTypes = context.getContentResolver().getStreamTypes(uri, mimeType);
      if (supportedTypes == null) {
        continue;
      }
      boolean matches = false;
      for (final String t : supportedTypes) {
        if (mimeType.equals(t)) {
          matches = true;
          break;
        }
      }
      if (!matches) {
        continue;
      }
      try (final AssetFileDescriptor descriptor =
              context.getContentResolver().openAssetFileDescriptor(uri, "r");
          final InputStream inputStream = new FileInputStream(descriptor.getFileDescriptor());
          final ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
        final byte[] buf = new byte[DEFAULT_BUFFER_SIZE];
        int n;
        while ((n = inputStream.read(buf)) != -1) {
          outputStream.write(buf, 0, n);
        }
        return outputStream.toByteArray();
      } catch (final IOException e) {
        Log.e(LOGTAG, "Couldn't read web custom format payload from clipboard", e);
        return null;
      } catch (final OutOfMemoryError e) {
        Log.e(LOGTAG, "Web custom format payload too large to read", e);
        return null;
      }
    }
    return null;
  }
}
