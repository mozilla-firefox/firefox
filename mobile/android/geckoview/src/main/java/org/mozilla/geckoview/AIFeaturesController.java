/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.HashMap;
import java.util.Map;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * Coordinates messaging between GeckoView and the AI feature toolkit.
 *
 * <p>Marked as experimental due to active development.
 */
@ExperimentalGeckoViewApi
public class AIFeaturesController {
  /** Public constructor for AIFeaturesController. */
  public AIFeaturesController() {}

  private static final boolean DEBUG = false;
  private static final String LOGTAG = "AIFeaturesController";

  /**
   * Coordinates runtime messaging between GeckoView and AI features that are not dependent on a
   * specific browsing session.
   */
  public static class RuntimeAIFeatures {
    /** Public constructor for RuntimeAIFeatures. */
    public RuntimeAIFeatures() {}

    private static final String LIST_FEATURES_EVENT = "GeckoView:AIFeature:ListFeatures";
    private static final String SET_FEATURE_ENABLED_EVENT = "GeckoView:AIFeature:SetEnabled";
    private static final String MAKE_FEATURE_AVAILABLE_EVENT = "GeckoView:AIFeature:MakeAvailable";

    /**
     * Returns a map of all known AI features and basic information keyed by feature ID.
     *
     * @return A GeckoResult resolving to a map of feature ID to {@link AIFeature} or a {@link
     *     AIFeaturesException}.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Map<String, AIFeature>> listFeatures() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting list of AI features.");
      }
      return EventDispatcher.getInstance()
          .queryBundle(LIST_FEATURES_EVENT)
          .map(
              bundle -> {
                final Map<String, AIFeature> map = new HashMap<>();
                try {
                  final GeckoBundle[] features = bundle.getBundleArray("features");
                  if (features != null) {
                    for (final GeckoBundle item : features) {
                      final String featureId = item.getString("featureId");
                      if (featureId != null) {
                        map.put(featureId, AIFeature.fromBundle(item));
                      }
                    }
                  }
                  return map;
                } catch (final Exception e) {
                  Log.w(LOGTAG, "An issue occurred while deserializing AI features: " + e);
                }
                throw new AIFeaturesException(AIFeaturesException.ERROR_COULD_NOT_PARSE);
              });
    }

    /**
     * Enables or blocks the given AI feature.
     *
     * @param featureId The identifier of the AI feature to configure.
     * @param enabled True to enable the feature, false to block it.
     * @return A GeckoResult that resolves on success or rejects with an error.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> setFeatureEnablement(
        @NonNull final String featureId, final boolean enabled) {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Setting AI feature enablement: " + featureId + " = " + enabled);
      }
      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putString("featureId", featureId);
      bundle.putBoolean("isEnabled", enabled);

      return EventDispatcher.getInstance()
          .queryVoid(SET_FEATURE_ENABLED_EVENT, bundle)
          .map(
              result -> result,
              exception -> {
                final String exceptionData =
                    ((EventDispatcher.QueryException) exception).data.toString();
                if (exceptionData.contains("Unknown AI feature")) {
                  throw new AIFeaturesException(AIFeaturesException.ERROR_UNKNOWN_FEATURE);
                }
                throw new AIFeaturesException(AIFeaturesException.ERROR_COULD_NOT_SET);
              });
    }

    /**
     * Makes the given AI feature available (resets to default state).
     *
     * @param featureId the identifier of the AI feature to make available
     * @return a GeckoResult that resolves on success or rejects with an error
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> makeFeatureAvailable(@NonNull final String featureId) {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Making AI feature available: " + featureId);
      }
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putString("featureId", featureId);

      return EventDispatcher.getInstance()
          .queryVoid(MAKE_FEATURE_AVAILABLE_EVENT, bundle)
          .map(
              result -> result,
              exception -> {
                final String exceptionData =
                    ((EventDispatcher.QueryException) exception).data.toString();
                if (exceptionData.contains("Unknown AI feature")) {
                  throw new AIFeaturesException(AIFeaturesException.ERROR_UNKNOWN_FEATURE);
                }
                throw new AIFeaturesException(AIFeaturesException.ERROR_COULD_NOT_MAKE_AVAILABLE);
              });
    }
  }

  /** Represents an AI feature. */
  public static class AIFeature {
    /** The feature id of the feature. */
    public final @NonNull String id;

    /** Whether the feature is enabled to use. */
    public final boolean isEnabled;

    /** Whether the feature is possible to use. */
    public final boolean isAllowed;

    /** Whether the feature is blocked from being used. */
    public final boolean isBlocked;

    /** For testing purposes only. */
    @VisibleForTesting
    protected AIFeature() {
      this.id = "";
      this.isEnabled = false;
      this.isAllowed = false;
      this.isBlocked = false;
    }

    /**
     * Represents a controllable AI feature on the browser level.
     *
     * @param builder The builder to set the constructor with.
     */
    private AIFeature(final @NonNull Builder builder) {
      this.id = builder.mId;
      this.isEnabled = builder.mIsEnabled;
      this.isAllowed = builder.mIsAllowed;
      this.isBlocked = builder.mIsBlocked;
    }

    /** Builder for {@link AIFeature}. */
    static class Builder {
      private final @NonNull String mId;
      private boolean mIsEnabled;
      private boolean mIsAllowed;
      private boolean mIsBlocked;

      /**
       * The unique id for the AI feature.
       *
       * @param id The unique identifier for this feature.
       */
      @AnyThread
      /* package */ Builder(@NonNull final String id) {
        this.mId = id;
      }

      /**
       * If the feature is enabled.
       *
       * @param isEnabled Whether the feature is actively in use or not.
       * @return This builder instance.
       */
      @AnyThread
      /* package */ @NonNull
      Builder isEnabled(final boolean isEnabled) {
        this.mIsEnabled = isEnabled;
        return this;
      }

      /**
       * If the feature is allowed.
       *
       * @param isAllowed Whether the feature is allowed to use.
       * @return This builder instance.
       */
      @AnyThread
      /* package */ @NonNull
      Builder isAllowed(final boolean isAllowed) {
        this.mIsAllowed = isAllowed;
        return this;
      }

      /**
       * If the feature is blocked.
       *
       * @param isBlocked Whether the feature is blocked from use.
       * @return This builder instance.
       */
      @AnyThread
      /* package */ @NonNull
      Builder isBlocked(final boolean isBlocked) {
        this.mIsBlocked = isBlocked;
        return this;
      }

      /**
       * @return A new {@link AIFeature} with the configured properties.
       */
      @AnyThread
      /* package */ @NonNull
      AIFeature build() {
        return new AIFeature(this);
      }
    }

    /**
     * Convenience method for deserializing an AIFeature.
     *
     * @param bundle Contains AI Feature information.
     * @return An AIFeature object, if possible, else null.
     */
    static @Nullable AIFeature fromBundle(final GeckoBundle bundle) {
      if (bundle == null) {
        return null;
      }
      try {
        final var featureId = bundle.getString("featureId");
        if (featureId == null) {
          return null;
        }

        return new Builder(featureId)
            .isEnabled(bundle.getBoolean("isEnabled"))
            .isAllowed(bundle.getBoolean("isAllowed"))
            .isBlocked(bundle.getBoolean("isBlocked"))
            .build();
      } catch (final Exception e) {
        Log.w(LOGTAG, "Could not deserialize AIFeature object: " + e);
        return null;
      }
    }

    @Override
    public String toString() {
      return "AIFeature { id="
          + id
          + ", isEnabled="
          + isEnabled
          + ", isAllowed="
          + isAllowed
          + ", isBlocked="
          + isBlocked
          + " }";
    }
  }

  /** An exception for when there is an issue communicating with the AI features toolkit. */
  public static class AIFeaturesException extends Exception {
    private static final long serialVersionUID = 1L;

    /**
     * Construct an [AIFeaturesException]
     *
     * @param code Error code the given exception corresponds to.
     */
    public AIFeaturesException(final @Code int code) {
      this.code = code;
    }

    /** Default error for unexpected issues. */
    public static final int ERROR_UNKNOWN = -1;

    /** An issue with deserialization or null result. */
    public static final int ERROR_COULD_NOT_PARSE = -2;

    /** The requested AI feature ID is not recognized. */
    public static final int ERROR_UNKNOWN_FEATURE = -3;

    /** Could not enable or block the AI feature. */
    public static final int ERROR_COULD_NOT_SET = -4;

    /** Could not make the AI feature available. */
    public static final int ERROR_COULD_NOT_MAKE_AVAILABLE = -5;

    /** AI features exception error codes. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        value = {
          ERROR_UNKNOWN,
          ERROR_COULD_NOT_PARSE,
          ERROR_UNKNOWN_FEATURE,
          ERROR_COULD_NOT_SET,
          ERROR_COULD_NOT_MAKE_AVAILABLE,
        })
    public @interface Code {}

    /** {@link Code} that provides more information about this exception. */
    public final @Code int code;

    @Override
    public String toString() {
      return "AIFeaturesException: " + code;
    }
  }
}
