/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import static org.mozilla.geckoview.ExperimentDelegate.ExperimentException.ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import org.json.JSONObject;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * This delegate is used to pass experiment information between the embedding application and
 * GeckoView.
 *
 * <p>An experiment is used to give users different application behavior in order to learn and
 * improve upon what features users prefer the most. This is accomplished by providing users
 * different application experiences and collecting data about how the differing experiences
 * impacted user behavior.
 */
public interface ExperimentDelegate {
  /**
   * Used to retrieve experiment information for the given feature identification.
   *
   * <p>A @param feature is the item or experience the experimented is about. For example, "prompt"
   * or "print" could be a feature.
   *
   * <p>The @return experiment information will be information on what the application should do for
   * the experiment. This is highly context dependent on how the experiment was setup and is decided
   * and controlled by the experiment framework. For example, a feature of "prompt" may return
   * {dismiss-button: {color: "red", full-screen: true}} or "print" may return {dotprint-enabled:
   * true}. That information can then be used to present differing behavior for the user.
   *
   * @param feature The name or identification of the experiment feature.
   * @return A {@link GeckoResult<JSONObject>} with experiment criteria. Typically will have a value
   *     related to showing or adjusting a feature. Will complete exceptionally with {@link
   *     ExperimentException} if the feature wasn't found.
   */
  @HandlerThread
  default @NonNull GeckoResult<JSONObject> onGetExperimentFeature(@NonNull final String feature) {
    ThreadUtils.assertOnHandlerThread();
    final GeckoResult<JSONObject> result = new GeckoResult<>();
    result.completeExceptionally(
        new ExperimentException(ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED));
    return result;
  }

  /**
   * Used to let the experiment framework know that the user was shown the feature. Should be
   * recorded as close as possible to the differing behavior.
   *
   * <p>One important part of experimentation is knowing when users encountered an experiment
   * surface or difference in behavior. Sending an exposure event is recording with the experiment
   * framework that the user encountered a differing behavior.
   *
   * <p>For example, if a user never encountered a @param feature "prompt", then the exposure event
   * would never be recorded. However, if the user does encounter a "prompt", then the experiment
   * framework needs a record that the user encountered the experiment surface.
   *
   * @param feature The name or identification the experiment feature.
   * @return A {@link GeckoResult<Void>} will complete if the feature was found and exposure
   *     recorded. Will complete exceptionally with {@link ExperimentException} if the feature
   *     wasn't found.
   */
  @HandlerThread
  default @NonNull GeckoResult<Void> onRecordExposureEvent(@NonNull final String feature) {
    ThreadUtils.warnOnHandlerThread();
    final GeckoResult<Void> result = new GeckoResult<>();
    result.completeExceptionally(
        new ExperimentException(ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED));
    return result;
  }

  /**
   * Used to let the experiment framework know that the user was shown the feature in a given
   * experiment. Should be recorded as close as possible to the differing behavior.
   *
   * <p>Use [onRecordExposureEvent], if there is no experiment slug.
   *
   * <p>This API is used similarly to [onRecordExposureEvent], but when a specific feature was
   * encountered. For example a @param feature may be "prompt" and a given @param slug may be
   * "dismiss" or "confirm". This is used to indicate a specific experiment surface was encountered.
   *
   * @param feature The name or identification the experiment feature.
   * @param slug The name or identification of the specific experiment feature.
   * @return A {@link GeckoResult<Void>} will complete if the feature was found and exposure
   *     recorded. Will complete exceptionally with {@link ExperimentException} if the feature
   *     wasn't found or not recorded.
   */
  @HandlerThread
  default @NonNull GeckoResult<Void> onRecordExperimentExposureEvent(
      @NonNull final String feature, @NonNull final String slug) {
    ThreadUtils.warnOnHandlerThread();
    final GeckoResult<Void> result = new GeckoResult<>();
    result.completeExceptionally(
        new ExperimentException(ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED));
    return result;
  }

  /**
   * Used to let the experiment framework send a malformed configuration event when the feature
   * configuration is not semantically valid.
   *
   * @param feature The name or identification the experiment feature.
   * @param part An optional detail or part identifier to be attached to the event.
   * @return A {@link GeckoResult<Void>} will complete if the feature was found and the event
   *     recorded. Will complete exceptionally with {@link ExperimentException} if the feature
   *     wasn't found or not recorded.
   */
  @HandlerThread
  default @NonNull GeckoResult<Void> onRecordMalformedConfigurationEvent(
      @NonNull final String feature, @NonNull final String part) {
    ThreadUtils.warnOnHandlerThread();
    final GeckoResult<Void> result = new GeckoResult<>();
    result.completeExceptionally(
        new ExperimentException(ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED));
    return result;
  }

  /**
   * An exception to be used when there is an issue retrieving or sending information to the
   * experiment framework.
   */
  class ExperimentException extends Exception {
    private static final long serialVersionUID = 1L;

    /**
     * Construct an [ExperimentException]
     *
     * @param code error code the given exception corresponds to
     */
    public ExperimentException(final @Codes int code) {
      this.code = code;
    }

    /** Default error for unexpected issues. */
    public static final int ERROR_UNKNOWN = -1;

    /** The experiment feature was not available. */
    public static final int ERROR_FEATURE_NOT_FOUND = -2;

    /** The experiment slug was not available. */
    public static final int ERROR_EXPERIMENT_SLUG_NOT_FOUND = -3;

    /** The experiment delegate is not implemented. */
    public static final int ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED = -4;

    /** Experiment exception error codes. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        value = {
          ERROR_UNKNOWN,
          ERROR_FEATURE_NOT_FOUND,
          ERROR_EXPERIMENT_SLUG_NOT_FOUND,
          ERROR_EXPERIMENT_DELEGATE_NOT_IMPLEMENTED
        })
    public @interface Codes {}

    /** One of {@link Codes} that provides more information about this exception. */
    public final @Codes int code;

    @Override
    public String toString() {
      return "ExperimentException: " + code;
    }
  }
}
