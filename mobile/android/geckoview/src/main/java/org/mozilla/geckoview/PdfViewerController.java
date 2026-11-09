/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * The PDF viewer controller coordinates messaging between GeckoView and the PDF viewer when the
 * session contains a PDF.
 */
@ExperimentalGeckoViewApi
public class PdfViewerController {
  /** Public constructor for PdfViewerController. */
  public PdfViewerController() {}

  /** The session editor contains functions related to editing a PDF, such as adding a signature. */
  @ExperimentalGeckoViewApi
  public static class SessionEditor {

    private static final String ADD_SIGNATURE = "GeckoView:PdfViewer:AddSignature";

    private final GeckoSession mSession;

    /**
     * Construct a new PDF editor session.
     *
     * @param session that will be dispatching and receiving events.
     */
    /* package */ SessionEditor(@NonNull final GeckoSession session) {
      mSession = session;
    }

    /**
     * Used for requesting that the PDF viewer place a typed signature on the document.
     *
     * @param text The text used for the signature.
     * @return A {@link GeckoResult} that completes once the signature has been handed off to the
     *     PDF viewer or a {@link PdfViewerException} if the viewer cannot handle the request.
     */
    @ExperimentalGeckoViewApi
    @HandlerThread
    @NonNull
    public GeckoResult<Void> addSignature(@NonNull final String text) {
      ThreadUtils.assertOnHandlerThread();
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putString("text", text);

      return mSession
          .getEventDispatcher()
          .queryVoid(ADD_SIGNATURE, bundle)
          .map(
              result -> result,
              exception ->
                  PdfViewerException.from(
                      (EventDispatcher.QueryException) exception, ADD_SIGNATURE));
    }
  }

  /**
   * An exception to be used when there is an issue sending information to the PDF viewer displaying
   * a document.
   */
  public static class PdfViewerException extends Exception {
    private static final long serialVersionUID = 1L;

    /**
     * Construct a [PdfViewerException]
     *
     * @param code Error code the given exception corresponds to.
     */
    public PdfViewerException(final @Code int code) {
      this.code = code;
    }

    /** Default error for unexpected issues. */
    public static final int ERROR_UNKNOWN = -1;

    /** The session is not displaying a PDF. */
    public static final int ERROR_NOT_A_PDF = -2;

    /** PDF viewer exception error codes. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(value = {ERROR_UNKNOWN, ERROR_NOT_A_PDF})
    public @interface Code {}

    /** {@link Code} that provides more information about this exception. */
    public final @Code int code;

    @Override
    public String toString() {
      return "PdfViewerException: " + code;
    }

    /**
     * Convenience method for finding the right PDF exception.
     *
     * @param exception The query exception the original request failed with.
     * @param event The event the original request was dispatched on.
     * @return A {@link PdfViewerException} with the {@link Code} that matches the failure.
     */
    static PdfViewerException from(
        @NonNull final EventDispatcher.QueryException exception, @NonNull final String event) {
      final String exceptionData = exception.data.toString();
      if (exceptionData.contains("not a PDF")
          // Listeners register only when the document is a PDF
          || exceptionData.contains("No listener for " + event)) {
        return new PdfViewerException(ERROR_NOT_A_PDF);
      }
      return new PdfViewerException(ERROR_UNKNOWN);
    }
  }
}
