/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.net.URLDecoder;
import java.nio.channels.Channels;
import java.nio.channels.FileChannel;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HashMap;
import java.util.zip.GZIPOutputStream;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.gecko.util.ProxySelector;

/**
 * Sends a crash report to the Mozilla <a href="https://wiki.mozilla.org/Socorro">Socorro</a> crash
 * report server.
 */
public class CrashReporter {
  /** Public constructor for CrashReporter. */
  public CrashReporter() {}

  private static final String LOGTAG = "GeckoCrashReporter";
  private static final String MINI_DUMP_PATH_KEY = "upload_file_minidump";
  private static final String PRODUCT_ID = "{eeb82917-e434-4870-8148-5c03d4caa81b}";

  /**
   * Sends a crash report to the Mozilla <a href="https://wiki.mozilla.org/Socorro">Socorro</a>
   * crash report server. <br>
   * The {@code appName} needs to be allowlisted for the server to accept the crash. <a
   * href="https://bugzilla.mozilla.org/enter_bug.cgi?product=Socorro">File a bug</a> if you would
   * like to get your app added to the allowlist.
   *
   * @param context The current Context
   * @param intent The Intent sent to the {@link GeckoRuntime} crash handler
   * @param appName A human-readable app name.
   * @throws IOException This can be thrown if there was a networking error while sending the
   *     report.
   * @throws URISyntaxException This can be thrown if the crash server URI from the extra data was
   *     invalid.
   * @return A GeckoResult containing the crash ID as a String.
   * @see GeckoRuntimeSettings.Builder#crashHandler(Class)
   * @see GeckoRuntime#ACTION_CRASHED
   */
  @AnyThread
  public static @NonNull GeckoResult<String> sendCrashReport(
      @NonNull final Context context, @NonNull final Intent intent, @NonNull final String appName)
      throws IOException, URISyntaxException {
    return sendCrashReport(context, intent.getExtras(), appName);
  }

  /**
   * Sends a crash report to the Mozilla <a href="https://wiki.mozilla.org/Socorro">Socorro</a>
   * crash report server. <br>
   * The {@code appName} needs to be allowlisted for the server to accept the crash. <a
   * href="https://bugzilla.mozilla.org/enter_bug.cgi?product=Socorro">File a bug</a> if you would
   * like to get your app added to the allowlist.
   *
   * @param context The current Context
   * @param intentExtras The Bundle of extras attached to the Intent received by a crash handler.
   * @param appName A human-readable app name.
   * @throws IOException This can be thrown if there was a networking error while sending the
   *     report.
   * @throws URISyntaxException This can be thrown if the crash server URI from the extra data was
   *     invalid.
   * @return A GeckoResult containing the crash ID as a String.
   * @see GeckoRuntimeSettings.Builder#crashHandler(Class)
   * @see GeckoRuntime#ACTION_CRASHED
   */
  @AnyThread
  public static @NonNull GeckoResult<String> sendCrashReport(
      @NonNull final Context context,
      @NonNull final Bundle intentExtras,
      @NonNull final String appName)
      throws IOException, URISyntaxException {
    final File dumpFile = new File(intentExtras.getString(GeckoRuntime.EXTRA_MINIDUMP_PATH));
    final File extrasFile = new File(intentExtras.getString(GeckoRuntime.EXTRA_EXTRAS_PATH));

    return sendCrashReport(context, dumpFile, extrasFile, appName);
  }

  /**
   * Sends a crash report to the Mozilla <a href="https://wiki.mozilla.org/Socorro">Socorro</a>
   * crash report server. <br>
   * The {@code appName} needs to be allowlisted for the server to accept the crash. <a
   * href="https://bugzilla.mozilla.org/enter_bug.cgi?product=Socorro">File a bug</a> if you would
   * like to get your app added to the allowlist.
   *
   * @param context The current {@link Context}
   * @param minidumpFile A {@link File} referring to the minidump.
   * @param extrasFile A {@link File} referring to the extras file.
   * @param appName A human-readable app name.
   * @throws IOException This can be thrown if there was a networking error while sending the
   *     report.
   * @throws URISyntaxException This can be thrown if the crash server URI from the extra data was
   *     invalid.
   * @return A GeckoResult containing the crash ID as a String.
   * @see GeckoRuntimeSettings.Builder#crashHandler(Class)
   * @see GeckoRuntime#ACTION_CRASHED
   */
  @AnyThread
  public static @NonNull GeckoResult<String> sendCrashReport(
      @NonNull final Context context,
      @NonNull final File minidumpFile,
      @NonNull final File extrasFile,
      @NonNull final String appName)
      throws IOException, URISyntaxException {
    final CrashAnnotations annotations =
        getCrashAnnotations(context, minidumpFile, extrasFile, appName);

    final String url = annotations.optString(CrashReport.Annotation.ServerURL);
    if (url == null) {
      return GeckoResult.fromException(new Exception("No server url present"));
    }

    return sendCrashReport(url, minidumpFile, annotations);
  }

  /**
   * Sends a crash report to the Mozilla <a href="https://wiki.mozilla.org/Socorro">Socorro</a>
   * crash report server.
   *
   * @param serverURL The URL used to submit the crash report.
   * @param minidumpFile A {@link File} referring to the minidump.
   * @param extras A {@link JSONObject} holding the parsed JSON from the extra file.
   * @throws IOException This can be thrown if there was a networking error while sending the
   *     report.
   * @throws URISyntaxException This can be thrown if the crash server URI from the extra data was
   *     invalid.
   * @return A GeckoResult containing the crash ID as a String.
   * @see GeckoRuntimeSettings.Builder#crashHandler(Class)
   * @see GeckoRuntime#ACTION_CRASHED
   */
  @AnyThread
  private static @NonNull GeckoResult<String> sendCrashReport(
      @NonNull final String serverURL,
      @NonNull final File minidumpFile,
      @NonNull final CrashAnnotations extras)
      throws IOException, URISyntaxException {
    Log.d(LOGTAG, "Sending crash report: " + minidumpFile.getPath());

    extras.sanitizeForReport();

    HttpURLConnection conn = null;
    try {
      final URL url = new URL(URLDecoder.decode(serverURL, "UTF-8"));
      final URI uri =
          new URI(
              url.getProtocol(),
              url.getUserInfo(),
              url.getHost(),
              url.getPort(),
              url.getPath(),
              url.getQuery(),
              url.getRef());
      conn = (HttpURLConnection) ProxySelector.openConnectionWithProxy(uri);
      conn.setRequestMethod("POST");
      final String boundary = generateBoundary();
      conn.setDoOutput(true);
      conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=" + boundary);
      conn.setRequestProperty("Content-Encoding", "gzip");

      final OutputStream os = new GZIPOutputStream(conn.getOutputStream());
      sendAnnotations(os, boundary, extras);
      sendFile(os, boundary, MINI_DUMP_PATH_KEY, minidumpFile);
      os.write(("\r\n--" + boundary + "--\r\n").getBytes());
      os.flush();
      os.close();

      BufferedReader br = null;
      try {
        br = new BufferedReader(new InputStreamReader(conn.getInputStream()));
        final HashMap<String, String> responseMap = readStringsFromReader(br);

        if (conn.getResponseCode() == HttpURLConnection.HTTP_OK) {
          final String crashid = responseMap.get("CrashID");
          if (crashid != null) {
            Log.i(LOGTAG, "Successfully sent crash report: " + crashid);
            return GeckoResult.fromValue(crashid);
          } else {
            Log.i(LOGTAG, "Server rejected crash report");
          }
        } else {
          Log.w(
              LOGTAG, "Received failure HTTP response code from server: " + conn.getResponseCode());
        }
      } catch (final Exception e) {
        return GeckoResult.fromException(new Exception("Failed to submit crash report", e));
      } finally {
        try {
          if (br != null) {
            br.close();
          }
        } catch (final IOException e) {
          return GeckoResult.fromException(new Exception("Failed to submit crash report", e));
        }
      }
    } catch (final Exception e) {
      return GeckoResult.fromException(new Exception("Failed to submit crash report", e));
    } finally {
      if (conn != null) {
        conn.disconnect();
      }
    }
    return GeckoResult.fromException(new Exception("Failed to submit crash report"));
  }

  private static String computeMinidumpHash(@NonNull final File minidump) throws IOException {
    MessageDigest md = null;
    final FileInputStream stream = new FileInputStream(minidump);
    try {
      md = MessageDigest.getInstance("SHA-256");

      final byte[] buffer = new byte[4096];
      int readBytes;

      while ((readBytes = stream.read(buffer)) != -1) {
        md.update(buffer, 0, readBytes);
      }
    } catch (final NoSuchAlgorithmException e) {
      throw new IOException(e);
    } finally {
      stream.close();
    }

    final byte[] digest = md.digest();
    final StringBuilder hash = new StringBuilder(64);

    for (int i = 0; i < digest.length; i++) {
      hash.append(Integer.toHexString((digest[i] & 0xf0) >> 4));
      hash.append(Integer.toHexString(digest[i] & 0x0f));
    }

    return hash.toString();
  }

  private static HashMap<String, String> readStringsFromReader(final BufferedReader reader)
      throws IOException {
    String line;
    final HashMap<String, String> map = new HashMap<>();
    while ((line = reader.readLine()) != null) {
      int equalsPos = -1;
      if ((equalsPos = line.indexOf('=')) != -1) {
        final String key = line.substring(0, equalsPos);
        final String val = unescape(line.substring(equalsPos + 1));
        map.put(key, val);
      }
    }
    return map;
  }

  private static class CrashAnnotations {
    private final JSONObject mAnnotations;

    CrashAnnotations(final String filePath) throws IOException, JSONException {
      final byte[] buffer = new byte[4096];
      final FileInputStream inputStream = new FileInputStream(filePath);
      final ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
      int bytesRead = 0;

      while ((bytesRead = inputStream.read(buffer)) != -1) {
        outputStream.write(buffer, 0, bytesRead);
      }

      final String contents = new String(outputStream.toByteArray(), "UTF-8");
      mAnnotations = new JSONObject(contents);
    }

    public void put(final CrashReport.Annotation annotation, final Object value)
        throws JSONException {
      mAnnotations.put(annotation.toString(), value);
    }

    /**
     * Returns the string annotation, or null if unset.
     *
     * @param annotation The annotation to read.
     * @return the annotation value, or null if unset.
     */
    public String optString(final CrashReport.Annotation annotation) {
      return mAnnotations.optString(annotation.toString(), null);
    }

    /** Remove annotations that should not be sent in a crash report. */
    public void sanitizeForReport() {
      for (final CrashReport.Annotation annotation : CrashReport.Annotation.values()) {
        if (!annotation.allowedInReport()) {
          mAnnotations.remove(annotation.toString());
        }
      }
    }

    /**
     * Encodes the crash annotations as a compact JSON string.
     *
     * @return The JSON representation of the crash annotations.
     */
    @Override
    public String toString() {
      return mAnnotations.toString();
    }
  }

  private static CrashAnnotations getCrashAnnotations(
      @NonNull final Context context,
      @NonNull final File minidump,
      @NonNull final File extra,
      @NonNull final String appName)
      throws IOException {
    try {
      final CrashAnnotations annotations = new CrashAnnotations(extra.getPath());

      annotations.put(CrashReport.Annotation.ProductName, appName);
      annotations.put(CrashReport.Annotation.ProductID, PRODUCT_ID);
      annotations.put(CrashReport.Annotation.Android_Manufacturer, Build.MANUFACTURER);
      annotations.put(CrashReport.Annotation.Android_Model, Build.MODEL);
      annotations.put(CrashReport.Annotation.Android_Board, Build.BOARD);
      annotations.put(CrashReport.Annotation.Android_Brand, Build.BRAND);
      annotations.put(CrashReport.Annotation.Android_Device, Build.DEVICE);
      annotations.put(CrashReport.Annotation.Android_Display, Build.DISPLAY);
      annotations.put(CrashReport.Annotation.Android_Fingerprint, Build.FINGERPRINT);
      annotations.put(CrashReport.Annotation.Android_CPU_ABI, Build.CPU_ABI);
      annotations.put(CrashReport.Annotation.Android_PackageName, context.getPackageName());
      try {
        annotations.put(CrashReport.Annotation.Android_CPU_ABI2, Build.CPU_ABI2);
        annotations.put(CrashReport.Annotation.Android_Hardware, Build.HARDWARE);
      } catch (final Exception ex) {
        Log.e(LOGTAG, "Exception while sending SDK version 8 keys", ex);
      }
      annotations.put(
          CrashReport.Annotation.Android_Version,
          Build.VERSION.SDK_INT + " (" + Build.VERSION.CODENAME + ")");

      return annotations;
    } catch (final JSONException e) {
      throw new IOException(e);
    }
  }

  private static String generateBoundary() {
    // Generate some random numbers to fill out the boundary
    final int r0 = (int) (Integer.MAX_VALUE * Math.random());
    final int r1 = (int) (Integer.MAX_VALUE * Math.random());
    return String.format("---------------------------%08X%08X", r0, r1);
  }

  private static void sendAnnotations(
      final OutputStream os, final String boundary, final CrashAnnotations extras)
      throws IOException {
    os.write(
        ("--"
                + boundary
                + "\r\n"
                + "Content-Disposition: form-data; name=\"extra\"; "
                + "filename=\"extra.json\"\r\n"
                + "Content-Type: application/json\r\n"
                + "\r\n")
            .getBytes());
    os.write(extras.toString().getBytes("UTF-8"));
    os.write('\n');
  }

  private static void sendFile(
      final OutputStream os, final String boundary, final String name, final File file)
      throws IOException {
    os.write(
        ("--"
                + boundary
                + "\r\n"
                + "Content-Disposition: form-data; name=\""
                + name
                + "\"; "
                + "filename=\""
                + file.getName()
                + "\"\r\n"
                + "Content-Type: application/octet-stream\r\n"
                + "\r\n")
            .getBytes());
    final FileChannel fc = new FileInputStream(file).getChannel();
    fc.transferTo(0, fc.size(), Channels.newChannel(os));
    fc.close();
  }

  private static String unescape(final String string) {
    return string.replaceAll("\\\\\\\\", "\\").replaceAll("\\\\n", "\n").replaceAll("\\\\t", "\t");
  }
}
