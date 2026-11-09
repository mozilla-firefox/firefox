/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.os.Parcel;
import android.os.Parcelable;
import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.collection.ArrayMap;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Map;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.GeckoBundle;

/**
 * Base class for (nested) runtime settings.
 *
 * <p>Handles pref-based settings. Please extend this class when adding nested settings for
 * GeckoRuntimeSettings.
 */
public abstract class RuntimeSettings implements Parcelable {
  /**
   * Base class for (nested) runtime settings builders.
   *
   * <p>Please extend this class when adding nested settings builders for GeckoRuntimeSettings.
   *
   * @param <Settings> The type of settings this builder creates.
   */
  public abstract static class Builder<Settings extends RuntimeSettings> {
    private final Settings mSettings;

    /** Create a new Builder instance. */
    @SuppressWarnings("this-escape")
    public Builder() {
      mSettings = newSettings(null);
    }

    /**
     * Finalize and return the settings.
     *
     * @return The constructed settings.
     */
    @AnyThread
    public @NonNull Settings build() {
      return newSettings(mSettings);
    }

    /**
     * Get the current settings object being built.
     *
     * @return The settings object being constructed.
     */
    @AnyThread
    protected @NonNull Settings getSettings() {
      return mSettings;
    }

    /**
     * Create a default or copy settings object.
     *
     * @param settings Settings object to copy, null for default settings.
     * @return The constructed settings object.
     */
    @AnyThread
    protected abstract @NonNull Settings newSettings(final @Nullable Settings settings);
  }

  /** Used to handle pref-based settings. */
  /* package */ class Pref<T> {
    public final String name;
    public final T defaultValue;
    private T mValue;
    private boolean mIsSet;

    /**
     * Create a new preference.
     *
     * @param name The preference name.
     * @param defaultValue The default value for this preference.
     */
    public Pref(@NonNull final String name, final T defaultValue) {
      this.name = name;
      this.defaultValue = defaultValue;
      mValue = defaultValue;

      RuntimeSettings.this.addPref(this);
    }

    /**
     * Set the preference value.
     *
     * @param newValue The new value to set.
     */
    public void set(final T newValue) {
      mValue = newValue;
      mIsSet = true;
    }

    /**
     * Set the preference value and commit it immediately.
     *
     * @param newValue The new value to set and commit.
     */
    public void commit(final T newValue) {
      if (newValue.equals(mValue)) {
        return;
      }
      set(newValue);
      commit();
    }

    /** Commit the current preference value to the runtime. */
    public void commit() {
      final GeckoRuntime runtime = RuntimeSettings.this.getRuntime();
      if (runtime == null) {
        return;
      }
      final GeckoBundle prefs = new GeckoBundle(1);
      addToBundle(prefs);
      runtime.setDefaultPrefs(prefs);
    }

    /**
     * Get the current preference value.
     *
     * @return The current value of this preference.
     */
    public T get() {
      return mValue;
    }

    /**
     * Check if this preference has been explicitly set.
     *
     * @return True if the preference has been set, false otherwise.
     */
    public boolean isSet() {
      return mIsSet;
    }

    /**
     * Check if this preference has a default value.
     *
     * @return True if the preference has a default value.
     */
    public boolean hasDefault() {
      return true;
    }

    /** Reset the preference to its default value. */
    public void reset() {
      mValue = defaultValue;
      mIsSet = false;
    }

    private void addToBundle(final GeckoBundle bundle) {
      final T value = mIsSet ? mValue : defaultValue;
      if (value instanceof String) {
        bundle.putString(name, (String) value);
      } else if (value instanceof Integer) {
        bundle.putInt(name, (Integer) value);
      } else if (value instanceof Boolean) {
        bundle.putBoolean(name, (Boolean) value);
      } else {
        throw new UnsupportedOperationException("Unhandled pref type for " + name);
      }
    }
  }

  /**
   * Used to handle pref-based settings that should not have a default value, so that they will be
   * controlled by GeckoView only when they are set.
   *
   * <p>When no value is set for a PrefWithoutDefault, its value on the GeckoView side is expected
   * to be null, and the value set on the Gecko side to stay set to the either the prefs file
   * included in the GeckoView build, or the user prefs file created by the xpcshell and mochitest
   * test harness.
   */
  class PrefWithoutDefault<T> extends Pref<T> {
    /**
     * Create a preference without a default value.
     *
     * @param name The preference name.
     */
    public PrefWithoutDefault(@NonNull final String name) {
      super(name, null);
    }

    /**
     * Check if this preference has a default value.
     *
     * @return False, as this preference type has no default.
     */
    public boolean hasDefault() {
      return false;
    }

    /**
     * Get the current preference value, or null if not set.
     *
     * @return The current value or null if not explicitly set.
     */
    public @Nullable T get() {
      if (!isSet()) {
        return null;
      }
      return super.get();
    }

    /** Commit the preference value to the runtime if set. */
    public void commit() {
      if (!isSet()) {
        // Only add to the bundle prefs and
        // propagate to Gecko when explicitly set.
        return;
      }
      super.commit();
    }

    private void addToBundle(final GeckoBundle bundle) {
      if (!isSet()) {
        return;
      }
      super.addToBundle(bundle);
    }
  }

  private RuntimeSettings mParent;
  private final ArrayList<RuntimeSettings> mChildren;
  private final ArrayList<Pref<?>> mPrefs;

  /** Create a RuntimeSettings instance with no parent. */
  protected RuntimeSettings() {
    this(null /* parent */);
  }

  /**
   * Create settings object.
   *
   * @param parent The parent settings, specify in case of nested settings.
   */
  @SuppressWarnings("this-escape")
  protected RuntimeSettings(final @Nullable RuntimeSettings parent) {
    mPrefs = new ArrayList<Pref<?>>();
    mChildren = new ArrayList<RuntimeSettings>();

    setParent(parent);
  }

  /**
   * Update the prefs based on the provided settings.
   *
   * @param settings Copy from this settings.
   */
  @AnyThread
  protected void updatePrefs(final @NonNull RuntimeSettings settings) {
    if (mPrefs.size() != settings.mPrefs.size()) {
      throw new IllegalArgumentException("Settings must be compatible");
    }

    for (int i = 0; i < mPrefs.size(); ++i) {
      if (!mPrefs.get(i).name.equals(settings.mPrefs.get(i).name)) {
        throw new IllegalArgumentException("Settings must be compatible");
      }
      if (!settings.mPrefs.get(i).isSet()) {
        continue;
      }
      // We know it is safe.
      @SuppressWarnings("unchecked")
      final Pref<Object> uncheckedPref = (Pref<Object>) mPrefs.get(i);
      uncheckedPref.commit(settings.mPrefs.get(i).get());
    }
  }

  /**
   * Get the associated GeckoRuntime instance.
   *
   * @return The runtime instance, or null if not available.
   */
  @Nullable
  GeckoRuntime getRuntime() {
    if (mParent != null) {
      return mParent.getRuntime();
    }
    return null;
  }

  /**
   * Set the parent settings object.
   *
   * @param parent The parent settings, or null if none.
   */
  private void setParent(final @Nullable RuntimeSettings parent) {
    mParent = parent;
    if (mParent != null) {
      mParent.addChild(this);
    }
  }

  /**
   * Add a child settings object.
   *
   * @param child The child settings to add.
   */
  private void addChild(final @NonNull RuntimeSettings child) {
    mChildren.add(child);
  }

  /**
   * Add a preference to this settings object.
   *
   * @param pref The preference to add.
   */
  void addPref(final Pref<?> pref) {
    mPrefs.add(pref);
  }

  /**
   * Return a mapping of the prefs managed in this settings, including child settings.
   *
   * @return A key-value mapping of the prefs.
   */
  @NonNull
  Map<String, Object> getPrefsMap() {
    final ArrayMap<String, Object> prefs = new ArrayMap<>();
    forAllPrefs(pref -> prefs.put(pref.name, pref.get()));

    return Collections.unmodifiableMap(prefs);
  }

  /**
   * Iterates through all prefs in this RuntimeSettings instance and in all children, grandchildren,
   * etc.
   *
   * @param visitor The consumer to call for each preference.
   */
  private void forAllPrefs(final GeckoResult.Consumer<Pref<?>> visitor) {
    for (final RuntimeSettings child : mChildren) {
      child.forAllPrefs(visitor);
    }

    for (final Pref<?> pref : mPrefs) {
      visitor.accept(pref);
    }
  }

  /**
   * Reset the prefs managed by this settings and its children.
   *
   * <p>The actual prefs values are set via {@link #getPrefsMap} during initialization and via
   * {@link Pref#commit} during runtime for individual prefs.
   */
  /* package */ void commitResetPrefs() {
    final ArrayList<String> names = new ArrayList<String>();
    forAllPrefs(
        pref -> {
          // Do not reset prefs that don't have a default value
          // and are not set.
          if (!pref.hasDefault() && !pref.isSet()) {
            return;
          }
          names.add(pref.name);
        });

    final GeckoBundle data = new GeckoBundle(1);
    data.putStringArray("names", names);
    EventDispatcher.getInstance().dispatch("GeckoView:ResetUserPrefs", data);
  }

  @Override // Parcelable
  @AnyThread
  public int describeContents() {
    return 0;
  }

  @Override // Parcelable
  @AnyThread
  public void writeToParcel(final Parcel out, final int flags) {
    for (final Pref<?> pref : mPrefs) {
      out.writeValue(pref.get());
    }
  }

  // AIDL code may call readFromParcel even though it's not part of Parcelable.
  /**
   * Read settings from a Parcel.
   *
   * @param source The Parcel to read from
   */
  @AnyThread
  public void readFromParcel(final @NonNull Parcel source) {
    for (final Pref<?> pref : mPrefs) {
      if (pref.hasDefault()) {
        // We know this is safe.
        @SuppressWarnings("unchecked")
        final Pref<Object> uncheckedPref = (Pref<Object>) pref;
        uncheckedPref.commit(source.readValue(getClass().getClassLoader()));
      } else {
        // Don't commit PrefWithoutDefault instances where the value read
        // from the Parcel is null.
        @SuppressWarnings("unchecked")
        final PrefWithoutDefault<Object> uncheckedPref = (PrefWithoutDefault<Object>) pref;
        final Object sourceValue = source.readValue(getClass().getClassLoader());
        if (sourceValue != null) {
          uncheckedPref.commit(sourceValue);
        }
      }
    }
  }
}
