/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/**
 * {@code PdfFileSaver} instances returned by {@link GeckoSession#getPdfFileSaver()} performs save
 * operation.
 */
@AnyThread
public final class SessionPdfFileSaver {
  private static final String LOGTAG = "GeckoPdfFileSaver";

  private final GeckoSession mSession;

  /* package */ SessionPdfFileSaver(@NonNull final GeckoSession session) {
    mSession = session;
  }

  /**
   * Save the current PDF.
   *
   * @return Result of the save operation as a {@link GeckoResult} object.
   */
  @NonNull
  public GeckoResult<WebResponse> save() {
    final GeckoResult<WebResponse> geckoResult = new GeckoResult<>();
    mSession
        .getEventDispatcher()
        .queryBundle("GeckoView:PDFSave", null)
        .map(
            response -> {
              final GeckoResult<WebResponse> pdfResponse =
                  SessionPdfFileSaver.createResponse(
                      mSession,
                      response.getString("url"),
                      response.getString("filename"),
                      response.getString("originalUrl"),
                      true,
                      false);
              if (pdfResponse == null) {
                geckoResult.completeExceptionally(
                    new IllegalStateException("Could not get a PDF response."));
              } else {
                geckoResult.completeFrom(pdfResponse);
              }
              return null;
            },
            exception -> {
              geckoResult.completeExceptionally(exception);
              return null;
            });
    return geckoResult;
  }

  /**
   * Create a WebResponse from some binary data in order to use it to download a PDF file.
   *
   * @param session The session.
   * @param url The url for fetching the data.
   * @param filename The file name.
   * @param originalUrl The original url for the file.
   * @param skipConfirmation Whether to skip the confirmation dialog.
   * @param requestExternalApp Whether to request an external app to open the file.
   * @return a response used to "download" the pdf.
   */
  public static @Nullable GeckoResult<WebResponse> createResponse(
      @NonNull final GeckoSession session,
      @NonNull final String url,
      @NonNull final String filename,
      @NonNull final String originalUrl,
      final boolean skipConfirmation,
      final boolean requestExternalApp) {
    try {
      final GeckoWebExecutor executor = new GeckoWebExecutor(session.getRuntime());
      final WebRequest request = new WebRequest(url);
      return executor
          .fetch(request)
          .then(
              new GeckoResult.OnValueListener<WebResponse, WebResponse>() {
                @Override
                public GeckoResult<WebResponse> onValue(final WebResponse response) {
                  final int statusCode = response.statusCode != 0 ? response.statusCode : 200;
                  return GeckoResult.fromValue(
                      new WebResponse.Builder(
                              originalUrl.startsWith("content://") ? url : originalUrl)
                          .statusCode(statusCode)
                          .body(response.body)
                          .skipConfirmation(skipConfirmation)
                          .requestExternalApp(requestExternalApp)
                          .addHeader("Content-Type", "application/pdf")
                          .addHeader(
                              "Content-Disposition", "attachment; filename=\"" + filename + "\"")
                          .build());
                }
              });
    } catch (final Exception e) {
      Log.d(LOGTAG, e.getMessage());
      return null;
    }
  }
}
