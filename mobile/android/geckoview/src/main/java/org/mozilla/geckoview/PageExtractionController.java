/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import androidx.annotation.NonNull;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/** Manges the page content extraction */
@ExperimentalGeckoViewApi
public class PageExtractionController {

  /** Metadata about a page extracted by {@link SessionPageExtractor#getPageMetadata()} */
  public static class PageMetadata {
    /** JSON-LD types as defined by Schema.org */
    @NonNull public final String[] structuredDataTypes;

    /** Word count of all the content on the page */
    public final int wordCount;

    /** BCP 47 language tag of the page */
    @NonNull public final String language;

    /**
     * Construct a new page metadata object.
     *
     * @param structuredDataTypes JSON-LD types as defined by Schema.org
     * @param wordCount word count of all the content on the page
     * @param language BCP 47 language tag of the page
     */
    public PageMetadata(
        @NonNull final String[] structuredDataTypes,
        final int wordCount,
        @NonNull final String language) {
      this.structuredDataTypes = structuredDataTypes;
      this.wordCount = wordCount;
      this.language = language;
    }

    /* package */ static PageMetadata fromBundle(@NonNull final GeckoBundle bundle) {
      final String[] structuredDataTypes = bundle.getStringArray("structuredDataTypes");
      final int wordCount = bundle.getInt("wordCount", -1);
      final String language = bundle.getString("language");

      return new PageMetadata(structuredDataTypes, wordCount, language);
    }
  }

  /** Options for controlling how text is extracted from a page. */
  public static class ContentParams {

    /**
     * When true, attempts to remove boilerplate content from the page using reader mode, falling
     * back to full extraction if unavailable.
     */
    public final boolean removeBoilerplate;

    /**
     * Construct a new ContentParams.
     *
     * @param removeBoilerplate whether to remove boilerplate using reader mode
     */
    public ContentParams(final boolean removeBoilerplate) {
      this.removeBoilerplate = removeBoilerplate;
    }

    /* package */ GeckoBundle toBundle() {
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putBoolean("removeBoilerplate", removeBoilerplate);
      return bundle;
    }
  }

  /**
   * Session page extractor coordinates session messaging between the page extractor actor and
   * GeckoView.
   *
   * <p>Performs page extraction actions that are dependent on the page.
   */
  public static class SessionPageExtractor {

    // Events dispatched to GeckoViewPageExtractor
    private static final String GET_TEXT_CONTENT_EVENT = "GeckoView:PageExtractor:GetTextContent";
    private static final String GET_PAGE_METADATA_EVENT = "GeckoView:PageExtractor:GetPageMetadata";

    private static final String GET_TEXT_CONTENT_RESULT_KEY = "text";

    private final GeckoSession mSession;

    /**
     * Construct a new page extractor session.
     *
     * @param session that will be dispatching and receiving events.
     */
    public SessionPageExtractor(final GeckoSession session) {
      mSession = session;
    }

    /**
     * Gets the page content for the current page with default options.
     *
     * @return the content of the current page as a {@link String} or a {@link
     *     PageExtractionException} if an error occurs while extracting the page content
     */
    @HandlerThread
    public @NonNull GeckoResult<String> getPageContent() {
      return getPageContent(new ContentParams(false));
    }

    /**
     * Gets the page content for the current page using the provided options.
     *
     * @param options the options to control how the text is extracted
     * @return the content of the current page as a {@link String} or a {@link
     *     PageExtractionException} if an error occurs while extracting the page content
     */
    @HandlerThread
    public @NonNull GeckoResult<String> getPageContent(@NonNull final ContentParams options) {
      ThreadUtils.assertOnHandlerThread();
      return mSession
          .getEventDispatcher()
          .queryBundle(GET_TEXT_CONTENT_EVENT, options.toBundle())
          .then(
              result -> {
                if (result == null)
                  return GeckoResult.fromException(
                      new PageExtractionException(PageExtractionException.ERROR_NULL_RESULT));

                final String textContent = result.getString(GET_TEXT_CONTENT_RESULT_KEY);
                if (textContent == null)
                  return GeckoResult.fromException(
                      new PageExtractionException(PageExtractionException.ERROR_MALFORMED_RESULT));

                return GeckoResult.fromValue(textContent);
              },
              exception ->
                  GeckoResult.fromException(
                      new PageExtractionException(
                          PageExtractionException.ERROR_UNKNOWN, exception)));
    }

    /**
     * Gets metadata about the current page.
     *
     * @return a {@link PageMetadata} for the current page or a {@link PageExtractionException} if
     *     an error occurs
     */
    @HandlerThread
    public @NonNull GeckoResult<PageMetadata> getPageMetadata() {
      ThreadUtils.assertOnHandlerThread();
      return mSession
          .getEventDispatcher()
          .queryBundle(GET_PAGE_METADATA_EVENT)
          .then(
              result -> {
                if (result == null)
                  return GeckoResult.fromException(
                      new PageExtractionException(PageExtractionException.ERROR_NULL_RESULT));

                return GeckoResult.fromValue(PageMetadata.fromBundle(result));
              },
              exception ->
                  GeckoResult.fromException(
                      new PageExtractionException(
                          PageExtractionException.ERROR_UNKNOWN, exception)));
    }
  }

  /** The exception thrown when a page extraction fails */
  public static class PageExtractionException extends Exception {

    /** An error identifier for when the result is unexpectedly null */
    public static final String ERROR_NULL_RESULT = "NULL_RESULT";

    /** An error identifier for when the result is malformed e.g. the `text` key is missing */
    public static final String ERROR_MALFORMED_RESULT = "MALFORMED_RESULT";

    /**
     * An error identifier for when an unknown error occurs. This is likely to happen if an error
     * occurs in the toolkit layer that we don't know already know of.
     */
    public static final String ERROR_UNKNOWN = "UNKNOWN_ERROR";

    /**
     * The type of the error. One of {@link #ERROR_NULL_RESULT}, {@link #ERROR_MALFORMED_RESULT} or
     * {@link #ERROR_UNKNOWN}
     */
    @NonNull public final String errorType;

    /**
     * Construct a new page extraction exception.
     *
     * @param errorType the type of the error. One of {@link #ERROR_NULL_RESULT}, {@link
     *     #ERROR_MALFORMED_RESULT} or {@link #ERROR_UNKNOWN}
     */
    public PageExtractionException(@NonNull final String errorType) {
      super("Unable to extract page content: " + errorType);
      this.errorType = errorType;
    }

    /**
     * Construct a new page extraction exception.
     *
     * @param errorType the type of error. One of {@link #ERROR_NULL_RESULT}, {@link
     *     #ERROR_MALFORMED_RESULT} or {@link #ERROR_UNKNOWN}
     * @param cause the cause of the error
     */
    public PageExtractionException(@NonNull final String errorType, final Throwable cause) {
      super("Unable to extract page content: " + errorType, cause);
      this.errorType = errorType;
    }
  }
}
