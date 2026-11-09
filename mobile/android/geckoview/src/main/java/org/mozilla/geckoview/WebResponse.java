/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import org.mozilla.gecko.annotation.WrapForJNI;

/**
 * WebResponse represents an HTTP[S] response. It is normally created by {@link
 * GeckoWebExecutor#fetch(WebRequest)}.
 */
@WrapForJNI
@AnyThread
public class WebResponse extends WebMessage {
  /** The default read timeout for the {@link #body} stream. */
  public static final long DEFAULT_READ_TIMEOUT_MS = 30000;

  /** The HTTP status code for the response, e.g. 200. */
  public final int statusCode;

  /** A boolean indicating whether or not this response is the result of a redirection. */
  public final boolean redirected;

  /** Whether or not this response was delivered via a secure connection. */
  public final boolean isSecure;

  /** The server certificate used with this response, if any. */
  public final @Nullable X509Certificate certificate;

  /**
   * An {@link InputStream} containing the response body, if available. Attention: the stream must
   * be closed whenever the app is done with it, even when the body is ignored. Otherwise the
   * connection will not be closed until the stream is garbage collected
   */
  public final @Nullable InputStream body;

  /**
   * Specifies that the contents should request to be opened in another Android application. For
   * example, provide PDF content and set this to true to request that Android opens the PDF in a
   * system PDF viewer (if possible and allowed by the user).
   */
  public final @Nullable boolean requestExternalApp;

  /**
   * Specifies that the app may skip requesting the download in the UI. A confirmation of the
   * download will still be shown.
   */
  public final @Nullable boolean skipConfirmation;

  /**
   * Construct a new WebResponse from the given builder.
   *
   * @param builder Builder instance containing the response parameters
   */
  protected WebResponse(final @NonNull Builder builder) {
    super(builder);
    this.statusCode = builder.mStatusCode;
    this.redirected = builder.mRedirected;
    this.body = builder.mBody;
    this.requestExternalApp = builder.mRequestExternalApp;
    this.skipConfirmation = builder.mSkipConfirmation;
    this.isSecure = builder.mIsSecure;
    this.certificate = builder.mCertificate;

    setReadTimeoutMillis(this.body, DEFAULT_READ_TIMEOUT_MS);
  }

  /**
   * Sets the maximum amount of time to wait for data in the {@link #body} read() method. By
   * default, the read timeout is set to {@link #DEFAULT_READ_TIMEOUT_MS}.
   *
   * <p>If 0, there will be no timeout and read() will block indefinitely.
   *
   * @param millis The duration in milliseconds for the timeout.
   */
  public void setReadTimeoutMillis(final long millis) {
    setReadTimeoutMillis(this.body, millis);
  }

  @WrapForJNI(skip = true)
  private static void setReadTimeoutMillis(final InputStream body, final long millis) {
    if (body instanceof GeckoInputStream) {
      ((GeckoInputStream) body).setReadTimeoutMillis(millis);
    }
  }

  /** Builder offers a convenient way to create WebResponse instances. */
  @WrapForJNI
  @AnyThread
  public static class Builder extends WebMessage.Builder {
    /* package */ int mStatusCode;
    /* package */ boolean mRedirected;
    /* package */ InputStream mBody;
    /* package */ boolean mRequestExternalApp = false;
    /* package */ boolean mSkipConfirmation = false;
    /* package */ boolean mIsSecure;
    /* package */ X509Certificate mCertificate;

    /**
     * Constructs a new Builder instance with the specified URI.
     *
     * @param uri A URI String.
     */
    public Builder(final @NonNull String uri) {
      super(uri);
    }

    @Override
    public @NonNull Builder uri(final @NonNull String uri) {
      super.uri(uri);
      return this;
    }

    @Override
    public @NonNull Builder header(final @NonNull String key, final @NonNull String value) {
      super.header(key, value);
      return this;
    }

    @Override
    public @NonNull Builder addHeader(final @NonNull String key, final @NonNull String value) {
      super.addHeader(key, value);
      return this;
    }

    /**
     * Sets the {@link InputStream} containing the body of this response.
     *
     * @param stream An {@link InputStream} with the body of the response.
     * @return This Builder instance.
     */
    public @NonNull Builder body(final @NonNull InputStream stream) {
      mBody = stream;
      return this;
    }

    /**
     * Requests that the content be passed to an external Android application. The default is false.
     * For example, set to true to request that the user have the option to open the content in
     * another Android application.
     *
     * @param requestExternalApp request that the content be opened in another application.
     * @return This Builder instance.
     */
    public @NonNull Builder requestExternalApp(final boolean requestExternalApp) {
      mRequestExternalApp = requestExternalApp;
      return this;
    }

    /**
     * Specifies if a confirmation to begin downloading is necessary or not. (The confirmation that
     * a download occurred will still be shown.) The default is false, which is to request a
     * download confirmation. Skipping the confirmation is only advisable if the user has already
     * opted to download.
     *
     * @param skipConfirmation whether to skip or show the confirm download flow
     * @return This Builder instance.
     */
    public @NonNull Builder skipConfirmation(final boolean skipConfirmation) {
      mSkipConfirmation = skipConfirmation;
      return this;
    }

    /**
     * Set whether or not this response is secure.
     *
     * @param isSecure Whether or not this response is secure.
     * @return This Builder instance.
     */
    public @NonNull Builder isSecure(final boolean isSecure) {
      mIsSecure = isSecure;
      return this;
    }

    /**
     * Set the server certificate used with this response.
     *
     * @param certificate The certificate used.
     * @return This Builder instance.
     */
    public @NonNull Builder certificate(final @NonNull X509Certificate certificate) {
      mCertificate = certificate;
      return this;
    }

    /**
     * @param encodedCert The certificate used, encoded via DER. Only used via JNI.
     */
    @WrapForJNI(exceptionMode = "nsresult")
    private void certificateBytes(final @NonNull byte[] encodedCert) {
      try {
        final CertificateFactory factory = CertificateFactory.getInstance("X.509");
        final X509Certificate cert =
            (X509Certificate) factory.generateCertificate(new ByteArrayInputStream(encodedCert));
        certificate(cert);
      } catch (final CertificateException e) {
        throw new IllegalArgumentException("Unable to parse DER certificate");
      }
    }

    /**
     * Set the HTTP status code, e.g. 200.
     *
     * @param code A int representing the HTTP status code.
     * @return This Builder instance.
     */
    public @NonNull Builder statusCode(final int code) {
      mStatusCode = code;
      return this;
    }

    /**
     * Set whether or not this response was the result of a redirect.
     *
     * @param redirected A boolean representing whether or not the request was redirected.
     * @return This Builder instance.
     */
    public @NonNull Builder redirected(final boolean redirected) {
      mRedirected = redirected;
      return this;
    }

    /** {@return a {@link WebResponse} constructed with the values from this Builder instance} */
    public @NonNull WebResponse build() {
      return new WebResponse(this);
    }
  }
}
