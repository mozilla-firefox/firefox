/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * Class is used to access and manipulate Gecko preferences through GeckoView.
 *
 * <p>This API is experimental because manipulating Gecko preferences is risky and can interfere
 * with browser operation without special care.
 */
@ExperimentalGeckoViewApi
public class GeckoPreferenceController {
  /** Public constructor for GeckoPreferenceController. */
  public GeckoPreferenceController() {}

  private static final String LOGTAG = "GeckoPreference";
  private static final boolean DEBUG = false;

  private static final String GET_PREF = "GeckoView:Preferences:GetPref";
  private static final String SET_PREF = "GeckoView:Preferences:SetPref";
  private static final String CLEAR_PREF = "GeckoView:Preferences:ClearPref";

  /**
   * Retrieves the value of a given Gecko preference.
   *
   * @param prefName The preference to find the value of. e.g., some.pref.value.
   * @return The typed Gecko preference that corresponds to this value. Will return exceptionally if
   *     a deserialization issue occurs.
   */
  @HandlerThread
  public static @NonNull GeckoResult<GeckoPreference<?>> getGeckoPref(
      @NonNull final String prefName) {
    ThreadUtils.assertOnHandlerThread();
    final GeckoBundle bundle = new GeckoBundle(1);
    bundle.putStringArray("prefs", List.of(prefName));
    return EventDispatcher.getInstance()
        .queryBundle(GET_PREF, bundle)
        .map(
            result -> {
              if (result == null) {
                throw new Exception("Received a null preference message.");
              }
              final GeckoBundle[] prefsBundle = result.getBundleArray("prefs");
              if (prefsBundle != null && prefsBundle.length == 1) {
                final var deserialized = GeckoPreference.fromBundle(prefsBundle[0]);
                if (deserialized == null) {
                  throw new Exception("Could not deserialize the preference.");
                }
                return deserialized;
              }
              throw new Exception("Could not deserialize the preference.");
            },
            exception -> new Exception("Could not retrieve the preference."));
  }

  /**
   * Takes a list of given Gecko preferences and retrieves their corresponding values.
   *
   * @param prefNames The list of preferences to find the value of. e.g., [some.pref.value,
   *     other.pref].
   * @return A list of retrieved typed Gecko preferences. Will return exceptionally if a
   *     deserialization issue occurs.
   */
  @HandlerThread
  public static @NonNull GeckoResult<List<GeckoPreference<?>>> getGeckoPrefs(
      @NonNull final List<String> prefNames) {
    ThreadUtils.assertOnHandlerThread();
    final GeckoBundle bundle = new GeckoBundle(1);
    bundle.putStringArray("prefs", prefNames);
    return EventDispatcher.getInstance()
        .queryBundle(GET_PREF, bundle)
        .map(
            result -> {
              if (result == null) {
                throw new Exception("Received a null preference message.");
              }
              final var deserialized = GeckoPreference.fromBundleArray(result);
              if (deserialized == null) {
                throw new Exception("Could not deserialize the preference.");
              }
              return deserialized;
            },
            exception -> new Exception("Could not retrieve the preferences."));
  }

  /**
   * Sets a String preference with Gecko. Float preferences should use this API.
   *
   * @param prefName The name of the preference to change. e.g., "some.pref.item".
   * @param value The string value the preference should be set to.
   * @param branch The preference branch to operate on. For most usage this will usually be {@link
   *     #PREF_BRANCH_USER} to actively change the value that is active. {@link
   *     #PREF_BRANCH_DEFAULT} will change the current default. If there is ever a user preference
   *     value set, then the user value will be used over the default value. The user value will be
   *     saved as a part of the user's profile. The default value will not be saved on the user's
   *     profile.
   * @return Will return a GeckoResult when the pref is set or else complete exceptionally.
   */
  @HandlerThread
  public static @NonNull GeckoResult<Void> setGeckoPref(
      @NonNull final String prefName, @NonNull final String value, @PrefBranch final int branch) {
    ThreadUtils.warnOnHandlerThread();
    final var pref = SetGeckoPreference.setStringPref(prefName, value, branch);
    final GeckoBundle requestBundle = new GeckoBundle(1);
    requestBundle.putBundleArray("prefs", List.of(pref.toBundle()));
    return EventDispatcher.getInstance()
        .queryBundle(SET_PREF, requestBundle)
        .map(
            result -> {
              if (GeckoPreferenceController.parseResponseFromSetting(result)) {
                return null;
              }
              throw new Exception("Unable to set preference.");
            },
            exception -> new Exception("Could not retrieve the results."));
  }

  /**
   * Sets an Integer preference with Gecko.
   *
   * @param prefName The name of the preference to change. e.g., "some.pref.item".
   * @param value The integer value the preference should be set to.
   * @param branch The preference branch to operate on. For most usage this will usually be {@link
   *     #PREF_BRANCH_USER} to actively change the value that is active. {@link
   *     #PREF_BRANCH_DEFAULT} will change the current default. If there is ever a user preference
   *     value set, then the user value will be used over the default value. The user value will be
   *     saved as a part of the user's profile. The default value will not be saved on the user's
   *     profile.
   * @return Will return a GeckoResult when the pref is set or else complete exceptionally.
   */
  @HandlerThread
  public static @NonNull GeckoResult<Void> setGeckoPref(
      @NonNull final String prefName, @NonNull final Integer value, @PrefBranch final int branch) {
    ThreadUtils.warnOnHandlerThread();
    final var pref = SetGeckoPreference.setIntPref(prefName, value, branch);
    final GeckoBundle requestBundle = new GeckoBundle(1);
    requestBundle.putBundleArray("prefs", List.of(pref.toBundle()));
    return EventDispatcher.getInstance()
        .queryBundle(SET_PREF, requestBundle)
        .map(
            result -> {
              if (GeckoPreferenceController.parseResponseFromSetting(result)) {
                return null;
              }
              throw new Exception("Unable to set preference.");
            },
            exception -> new Exception("Could not retrieve the results."));
  }

  /**
   * Sets a boolean preference with Gecko.
   *
   * @param prefName The name of the preference to change. e.g., "some.pref.item".
   * @param value The boolean value the preference should be set to.
   * @param branch The preference branch to operate on. For most usage this will usually be {@link
   *     #PREF_BRANCH_USER} to actively change the value that is active. {@link
   *     #PREF_BRANCH_DEFAULT} will change the current default. If there is ever a user preference
   *     value set, then the user value will be used over the default value. The user value will be
   *     saved as a part of the user's profile. The default value will not be saved on the user's
   *     profile.
   * @return Will return a GeckoResult when the pref is set or else complete exceptionally.
   */
  @HandlerThread
  public static @NonNull GeckoResult<Void> setGeckoPref(
      @NonNull final String prefName, @NonNull final Boolean value, @PrefBranch final int branch) {
    ThreadUtils.warnOnHandlerThread();
    final var pref = SetGeckoPreference.setBoolPref(prefName, value, branch);
    final GeckoBundle requestBundle = new GeckoBundle(1);
    requestBundle.putBundleArray("prefs", List.of(pref.toBundle()));
    return EventDispatcher.getInstance()
        .queryBundle(SET_PREF, requestBundle)
        .map(
            result -> {
              if (GeckoPreferenceController.parseResponseFromSetting(result)) {
                return null;
              }
              throw new Exception("Unable to set preference.");
            },
            exception -> new Exception("Could not retrieve the results."));
  }

  /**
   * Convince method to parse messaging responses after setting an individual preference.
   *
   * @param result Is the response from Gecko after requesting to set a pref on an individual pref
   *     set.
   * @return True if the preference set or False if it did not.
   * @throws Exception Whenever parsing doesn't complete as expected.
   */
  private static boolean parseResponseFromSetting(final GeckoBundle result) throws Exception {
    if (result == null) {
      throw new Exception("Received a null result message.");
    }
    final GeckoBundle[] resultsBundle = result.getBundleArray("prefs");
    if (resultsBundle == null) {
      throw new Exception("Received a null result bundle.");
    }
    boolean isSet = false;
    if (resultsBundle.length == 1) {
      isSet = resultsBundle[0].getBoolean("isSet");
    }
    return isSet;
  }

  /**
   * Sets multiple Gecko preferences at once.
   *
   * @param prefs A list of {@link SetGeckoPreference} to set.
   * @return A Map of preference names (key) and if they successfully set (values).
   */
  @HandlerThread
  public static @NonNull GeckoResult<Map<String, Boolean>> setGeckoPrefs(
      @NonNull final List<SetGeckoPreference<?>> prefs) {
    ThreadUtils.assertOnHandlerThread();
    final List<GeckoBundle> itemBundles = new ArrayList<>(prefs.size());
    for (final SetGeckoPreference<?> pref : prefs) {
      itemBundles.add(pref.toBundle());
    }

    final GeckoBundle requestBundle = new GeckoBundle(1);
    requestBundle.putBundleArray("prefs", itemBundles);

    return EventDispatcher.getInstance()
        .queryBundle(SET_PREF, requestBundle)
        .map(
            result -> {
              if (result == null) {
                throw new Exception("Received a null result message.");
              }
              final GeckoBundle[] resultsBundle = result.getBundleArray("prefs");
              if (resultsBundle == null) {
                throw new Exception("Received a null result bundle.");
              }

              final Map<String, Boolean> resultMap = new HashMap<>(resultsBundle.length);
              for (final var resultBundle : resultsBundle) {
                final String pref = resultBundle.getString("pref");
                final boolean isSet = resultBundle.getBoolean("isSet");

                if (pref == null) {
                  throw new Exception("Received a null preference name.");
                } else {
                  resultMap.put(pref, isSet);
                }
              }
              return resultMap;
            },
            exception -> new Exception("Could not retrieve the results."));
  }

  /***
   * Restated from nsIPrefBranch.idl's clearUserPref:
   * <p>
   * Called to clear a user set value from a specific preference. This will, in
   * effect, reset the value to the default value. If no default value exists
   * the preference will cease to exist.
   *
   * @param prefName The name of the preference to clear. e.g., "some.pref.item".
   * @return Will return a GeckoResult once the pref is cleared.
   */
  @HandlerThread
  public static @NonNull GeckoResult<Void> clearGeckoUserPref(@NonNull final String prefName) {
    ThreadUtils.warnOnHandlerThread();
    final GeckoBundle bundle = new GeckoBundle(1);
    bundle.putString("pref", prefName);
    return EventDispatcher.getInstance().queryVoid(CLEAR_PREF, bundle);
  }

  /** The Observer class contains utilities for monitoring preference changes in Gecko. */
  public static final class Observer {
    /** Public constructor for Observer. */
    public Observer() {}

    private static final String REGISTER_PREF = "GeckoView:Preferences:RegisterObserver";
    private static final String UNREGISTER_PREF = "GeckoView:Preferences:UnregisterObserver";

    /**
     * This will register a preference for observation.
     *
     * @param preferenceName The Gecko preference that should be placed under observation. e.g.,
     *     "some.pref.item".
     * @return The GeckoResult will complete with the current preference value when observation is
     *     set.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> registerPreference(
        @NonNull final String preferenceName) {
      return registerPreferences(List.of(preferenceName));
    }

    /**
     * This will register preferences for observation.
     *
     * @param preferenceNames A list of Gecko preference that should be placed under observation.
     *     e.g., "some.pref.item", "some.pref.item.other".
     * @return The GeckoResult will complete with the current preference value when observation is
     *     set.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> registerPreferences(
        @NonNull final List<String> preferenceNames) {
      ThreadUtils.warnOnHandlerThread();
      final GeckoBundle bundle = new GeckoBundle();
      bundle.putStringArray("prefs", preferenceNames);
      return EventDispatcher.getInstance().queryVoid(REGISTER_PREF, bundle);
    }

    /**
     * This will deregister a preference for observation.
     *
     * @param preferenceName The Gecko preference that should be removed from observation. e.g.,
     *     "some.pref.item".
     * @return The GeckoResult will complete when the observer is removed. If the item requested is
     *     not under observation, the function will still return.
     */
    @UiThread
    public static @NonNull GeckoResult<Void> unregisterPreference(
        @NonNull final String preferenceName) {
      return unregisterPreferences(List.of(preferenceName));
    }

    /**
     * This will deregister preferences for observation.
     *
     * @param preferenceNames The Gecko preferences that should be removed from observation. e.g.,
     *     "some.pref.item", "some.pref.item.other".
     * @return The GeckoResult will complete when the observer is removed. If the item requested is
     *     not under observation, the function will still return.
     */
    @UiThread
    public static @NonNull GeckoResult<Void> unregisterPreferences(
        @NonNull final List<String> preferenceNames) {
      final GeckoBundle bundle = new GeckoBundle();
      bundle.putStringArray("prefs", preferenceNames);
      return EventDispatcher.getInstance().queryVoid(UNREGISTER_PREF, bundle);
    }

    /** Delegate definition for observing Gecko preferences. */
    public interface Delegate {
      /**
       * When a preference is registered using {@link #registerPreference(String)}, if the
       * preference's value changes, then this callback will occur.
       *
       * @param observedGeckoPreference The new Gecko preference value that was recently observed.
       */
      @HandlerThread
      default void onGeckoPreferenceChange(
          @NonNull final GeckoPreference<?> observedGeckoPreference) {}
    }
  }

  /**
   * Pref types as defined by Gecko in nsIPrefBranch.idl and should remain in sync.
   *
   * <p>Note: A Float preference will operate as a PREF_STRING due to Gecko's handling.
   */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({PREF_TYPE_INVALID, PREF_TYPE_STRING, PREF_TYPE_INT, PREF_TYPE_BOOL})
  public @interface PrefType {}

  /** Used when the preference does not have a type (i.e. is not defined). */
  public static final int PREF_TYPE_INVALID = 0;

  /** Used when the preference conforms to type string. */
  public static final int PREF_TYPE_STRING = 32;

  /** Used when the preference conforms to type integer. */
  public static final int PREF_TYPE_INT = 64;

  /** Used when the preference conforms to type boolean. */
  public static final int PREF_TYPE_BOOL = 128;

  /**
   * Convenience method for converting from {@link PrefType} to string. These values should remain
   * in sync with nsIPrefBranch.idl.
   *
   * @param prefType The defined {@link PrefType}.
   * @return The String representation of the construct.
   */
  @AnyThread
  /* package */ static @NonNull String toTypeString(@PrefType final int prefType) {
    switch (prefType) {
      case PREF_TYPE_INVALID:
        return "PREF_INVALID";
      case PREF_TYPE_STRING:
        return "PREF_STRING";
      case PREF_TYPE_INT:
        return "PREF_INT";
      case PREF_TYPE_BOOL:
        return "PREF_BOOL";
      default:
        return "UNKNOWN";
    }
  }

  /** Pref branch used to distinguish user and default Gecko preferences. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({PREF_BRANCH_USER, PREF_BRANCH_DEFAULT})
  public @interface PrefBranch {}

  /**
   * Used when the preference is a "user" defined preference. A "user" preference is specified to be
   * set as the current value of the preference. It will persist through restarts and is a part of
   * the user's profile.
   */
  public static final int PREF_BRANCH_USER = 0;

  /**
   * Used when the preference is a default preference. A "default" preference is what is used when
   * no user preference is set.
   */
  public static final int PREF_BRANCH_DEFAULT = 1;

  /**
   * Convenience method for converting from {@link PrefBranch} to string.
   *
   * @param prefBranch The defined {@link PrefBranch}.
   * @return The String representation of the construct.
   */
  @AnyThread
  /* package */ static @NonNull String toBranchString(@PrefBranch final int prefBranch) {
    switch (prefBranch) {
      case PREF_BRANCH_USER:
        return "user";
      case PREF_BRANCH_DEFAULT:
        return "default";
      default:
        Log.w(LOGTAG, "Tried to convert an unknown pref branch of " + prefBranch + " !");
        return "default";
    }
  }

  /**
   * This object is for constructing instructions on how to set a given preference.
   *
   * @param <T> May be constructed as String, Integer, or Boolean.
   */
  public static class SetGeckoPreference<T> {
    /** The preference name. */
    public final @NonNull String pref;

    /** The value the preference should be set to. */
    public final @NonNull T value;

    /** The preference branch to operate on. */
    public final @PrefBranch int branch;

    /** The Gecko specified type of preference. */
    public final @PrefType int type;

    /**
     * Internal constructor for creating a SetGeckoPreference.
     *
     * @param pref The preference name.
     * @param value The value the preference should be set to.
     * @param branch The preference branch to operate on
     * @param type The Gecko specified type of preference.
     */
    private SetGeckoPreference(
        @NonNull final String pref,
        @NonNull final T value,
        @PrefBranch final int branch,
        @PrefType final int type) {
      this.pref = pref;
      this.value = value;
      this.branch = branch;
      this.type = type;
    }

    /**
     * Constructor for setting a String preference.
     *
     * @param pref The preference name.
     * @param value The value the preference should be set to.
     * @param branch The preference branch to operate on.
     * @return A constructed SetGeckoPreference.
     */
    @AnyThread
    public static @NonNull SetGeckoPreference<String> setStringPref(
        @NonNull final String pref, @NonNull final String value, @PrefBranch final int branch) {
      return new SetGeckoPreference<>(pref, value, branch, PREF_TYPE_STRING);
    }

    /**
     * Constructor for setting an Integer preference.
     *
     * @param pref The preference name.
     * @param value The value the preference should be set to.
     * @param branch The preference branch to operate on.
     * @return A constructed SetGeckoPreference.
     */
    @AnyThread
    public static @NonNull SetGeckoPreference<Integer> setIntPref(
        @NonNull final String pref, @NonNull final Integer value, @PrefBranch final int branch) {
      return new SetGeckoPreference<>(pref, value, branch, PREF_TYPE_INT);
    }

    /**
     * Constructor for setting a Boolean preference.
     *
     * @param pref The preference name.
     * @param value The value the preference should be set to.
     * @param branch The preference branch to operate on.
     * @return A constructed SetGeckoPreference.
     */
    @AnyThread
    public static @NonNull SetGeckoPreference<Boolean> setBoolPref(
        @NonNull final String pref, @NonNull final Boolean value, @PrefBranch final int branch) {
      return new SetGeckoPreference<>(pref, value, branch, PREF_TYPE_BOOL);
    }

    /**
     * Convenience method to serialize the SetGeckoPreference object into a bundle.
     *
     * @return GeckoBundle for use in messaging.
     */
    @AnyThread
    @NonNull
    /* package */
    GeckoBundle toBundle() {
      final GeckoBundle bundle = new GeckoBundle(4);
      bundle.putString("pref", this.pref);
      bundle.putString("branch", toBranchString(this.branch));
      bundle.putInt("type", this.type);

      switch (this.type) {
        case PREF_TYPE_INVALID:
          {
            bundle.putString("value", null);
            return bundle;
          }
        case PREF_TYPE_STRING:
          {
            bundle.putString("value", (String) this.value);
            return bundle;
          }
        case PREF_TYPE_BOOL:
          {
            bundle.putBoolean("value", (boolean) this.value);
            return bundle;
          }
        case PREF_TYPE_INT:
          {
            bundle.putInt("value", (int) this.value);
            return bundle;
          }
        default:
          {
            bundle.putString("value", null);
            return bundle;
          }
      }
    }
  }

  /**
   * This object represents information on a GeckoPreference.
   *
   * @param <T> The type of the preference.
   */
  public static class GeckoPreference<T> {

    /** The Gecko preference name. (e.g., "some.pref.item") */
    public final @NonNull String pref;

    /** The Gecko type of preference. (e.g., "PREF_BOOL" or "PREF_STRING" or "PREF_INT") */
    public final @PrefType int type;

    /** The default value of the preference. Corresponds to the default branch's value. */
    public final @Nullable T defaultValue;

    /** The user value of the preference. Corresponds to the user branch's value. */
    public final @Nullable T userValue;

    /**
     * The current value of the preference that is in operation.
     *
     * @return Will return the user value if set, if not then the default value.
     */
    @AnyThread
    public @Nullable T getValue() {
      if (userValue != null) {
        return userValue;
      }
      return defaultValue;
    }

    /**
     * Checks to see if the user value has changed from the default value.
     *
     * @return Whether the user value has diverged from the default value.
     */
    @AnyThread
    public boolean getHasUserChangedValue() {
      return userValue != null;
    }

    /**
     * Constructor for a GeckoPreference.
     *
     * @param pref Name of preference. (e.g., "some.gecko.pref")
     * @param type The Gecko type for the preference. (e.g., PREF_STRING )
     * @param defaultValue The default value of the pref.
     * @param userValue The user value of the pref. unknown.)
     */
    /* package */ GeckoPreference(
        @NonNull final String pref,
        @PrefType final int type,
        @Nullable final T defaultValue,
        @Nullable final T userValue) {
      this.pref = pref;
      this.type = type;
      this.defaultValue = defaultValue;
      this.userValue = userValue;
    }

    /**
     * Convenience method to format the GeckoPreference object into a string.
     *
     * @return String representing GeckoPreference.
     */
    @NonNull
    @Override
    public String toString() {
      final StringBuilder builder = new StringBuilder("GeckoPreference {");
      builder
          .append("pref=")
          .append(pref)
          .append(", type=")
          .append(toTypeString(type))
          .append(", defaultValue=")
          .append(Objects.toString(defaultValue, "null"))
          .append(", userValue=")
          .append(Objects.toString(userValue, "null"))
          .append("}");
      return builder.toString();
    }

    /**
     * Convenience method to deserialize an array of preference information into a list of {@link
     * GeckoPreference}.
     *
     * @param bundle The bundle containing the preference array information. Should be of the form
     *     of bundle array "prefs" composed of pref, type, branch, status, and value for each
     *     element.
     * @return A list of typed preference objects. Will return null if a deserialization issue
     *     occurs on any item.
     */
    /* package */
    static @Nullable List<GeckoPreference<?>> fromBundleArray(@Nullable final GeckoBundle bundle) {
      if (bundle != null) {
        try {
          final GeckoBundle[] prefsBundle = bundle.getBundleArray("prefs");
          if (prefsBundle != null) {
            final List<GeckoPreference<?>> list = new ArrayList<>(prefsBundle.length);
            for (final var prefBundle : prefsBundle) {
              final GeckoPreference<?> pref = GeckoPreference.fromBundle(prefBundle);
              if (pref == null) {
                Log.w(LOGTAG, "An issue occurred when deserializing one of the GeckoPreferences.");
                return null;
              }
              list.add(pref);
            }
            return list;
          }
        } catch (final Exception e) {
          Log.w(LOGTAG, "An issue occurred when deserializing a map of GeckoPreferences: " + e);
          return null;
        }
      }
      Log.w(LOGTAG, "The bundle was null when deserializing a map of GeckoPreferences.");
      return null;
    }

    /**
     * Convenience method to deserialize preference information into a {@link GeckoPreference}.
     *
     * @param bundle The bundle containing the preference information. Should contain pref, type,
     *     branch, status, and value.
     * @return A typed preference object. Will return null if a deserialization issue occurs.
     */
    /* package */
    static @Nullable GeckoPreference<?> fromBundle(@Nullable final GeckoBundle bundle) {
      if (bundle == null) {
        Log.w(LOGTAG, "Bundle is null when attempting to deserialize a GeckoPreference.");
        return null;
      }
      try {
        final String pref = bundle.getString("pref", "");
        if (pref.isEmpty()) {
          Log.w(LOGTAG, "Deserialized an empty preference name.");
          return null;
        }
        final int type = bundle.getInt("type", 0);
        switch (type) {
          case PREF_TYPE_INVALID:
            {
              return new GeckoPreference<Object>(pref, type, null, null);
            }
          case PREF_TYPE_STRING:
            {
              final String defaultValue = bundle.getString("defaultValue");
              final String userValue = bundle.getString("userValue");
              return new GeckoPreference<String>(pref, type, defaultValue, userValue);
            }
          case PREF_TYPE_BOOL:
            {
              final Boolean defaultValue = bundle.getBooleanObject("defaultValue");
              final Boolean userValue = bundle.getBooleanObject("userValue");
              return new GeckoPreference<Boolean>(pref, type, defaultValue, userValue);
            }
          case PREF_TYPE_INT:
            {
              final Integer defaultValue = bundle.getInteger("defaultValue");
              final Integer userValue = bundle.getInteger("userValue");
              return new GeckoPreference<Integer>(pref, type, defaultValue, userValue);
            }
          default:
            {
              Log.w(LOGTAG, "Deserialized an unexpected preference type of " + type + ".");
              return null;
            }
        }
      } catch (final Exception e) {
        Log.w(LOGTAG, "Could not deserialize GeckoPreference object: " + e);
        return null;
      }
    }
  }
}
