/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A base class for AI features in Firefox.
 *
 * Each AI feature must implement this contract in order to be included in AI Controls.
 *
 * Each feature implementation is required to manage its own preferences and artifacts.
 *
 * @interface
 */
export class AIFeature {
  /**
   * The feature identifier (e.g. "linkpreview", "translations").
   *
   * @returns {string} The feature identifier.
   */
  static get id() {
    throw new Error("AIFeature subclass must implement static get id()");
  }

  /**
   * Returns true if this feature has a distinct "Enabled" AI Controls state,
   * separate from its "Available" state.
   *
   * If true, then this feature has distinct "Available" and "Enabled" control
   * states. This usually means that while the feature is "Available", it may
   * still require some additional activation step before it is fully enabled.
   *
   * LinkPreview is an example implementor with this configuration set to true.
   *
   * If false, then this feature has no distinct "Enabled" control state.
   *
   * TranslationsFeature is an example implementor with this configuration set to false.
   *
   * @returns {boolean} True if this feature has a distinct "Enabled" AI
   *   Controls state.
   */
  static get hasDistinctEnabledState() {
    throw new Error(
      "AIFeature subclass must implement static get hasDistinctEnabledState()"
    );
  }

  /**
   * Checks if the feature is in its enabled and usable state.
   *
   * This describes the feature's internal state rather than the label that the
   * AI Controls surface chooses to display for it.
   *
   * This state is only meaningful when the feature is allowed to be offered in
   * the current environment.
   *
   * @returns {boolean} True if the feature is in its enabled and usable state,
   *   false otherwise.
   */
  static get isEnabled() {
    throw new Error("AIFeature subclass must implement static get isEnabled()");
  }

  /**
   * Checks if the feature is in the blocked state.
   *
   * This state is only meaningful when the feature is allowed to be offered in
   * the current environment.
   *
   * @returns {boolean} True if blocked, false otherwise.
   */
  static get isBlocked() {
    throw new Error("AIFeature subclass must implement static get isBlocked()");
  }

  /**
   * Checks if the feature is allowed to be used for this user.
   *
   * This covers non-hardware restrictions such as regional availability or
   * rollout status.
   *
   * This determines whether the feature can be offered at all in the current environment.
   * If this is false, the feature should be hidden from the AI Controls UI.
   *
   * @returns {boolean} True if the feature is allowed, false otherwise.
   */
  static get isAllowed() {
    throw new Error("AIFeature subclass must implement static get isAllowed()");
  }

  /**
   * Checks if the current device can run the feature.
   *
   * This covers hardware-related restrictions such as required CPU
   * instructions or GPU support.
   *
   * This is separate from {@link isAllowed}. A feature may still be included
   * in AI Controls even when it cannot run on the current device, so that the
   * feature's own UI surfaces can present device-specific messaging or guidance.
   *
   * @returns {boolean} True if the current device can run the feature.
   */
  static get canRunOnDevice() {
    throw new Error(
      "AIFeature subclass must implement static get canRunOnDevice()"
    );
  }

  /**
   * Checks if the feature's AI Controls state is entirely managed by enterprise policy.
   *
   * This should return true if the prefs that control the feature's AI
   * Controls state are locked by the policy code, and cannot be changed by
   * the user.
   *
   * When this returns true, the user cannot change this feature's AI Controls
   * state, and its AI Controls should be disabled.
   *
   * @returns {boolean} True if the feature's AI Controls state cannot be
   *   modified, otherwise false.
   */
  static get isManagedByPolicy() {
    throw new Error(
      "AIFeature subclass must implement static get isManagedByPolicy()"
    );
  }

  /**
   * Returns the shared AI Controls state for this feature.
   *
   * This must return one of "available", "enabled", or "blocked". Features
   * that expose additional custom AI Controls options should still return the
   * corresponding base state here.
   *
   * @returns {string} The shared AI Controls state for this feature.
   */
  static get aiControlState() {
    if (!this.isAllowed) {
      // This feature is not allowed at all. It should not have a visible feature-specific
      // control in the AI-Controls settings. For all intents and purposes this may function
      // the same as "Blocked," but there should be no user-facing way to make this feature available.
      return "disallowed";
    }

    if (this.isBlocked) {
      // This feature is blocked, meaning that all functionality is disabled, but it will have a
      // feature-specific control in the AI-Controls settings in case the user wants to make the
      // user available. The feature is allowed, but it is currently blocked via AI Controls.
      return "blocked";
    }

    if (this.hasDistinctEnabledState && this.isEnabled) {
      // This feature is available to use, and the required in-feature selections have been made
      // to enable it, such as choosing a preferred chat provider, or logging in with your account.
      return "enabled";
    }

    // This feature is available to use. The feature will be discoverable within Firefox, however if the
    // feature has extra in-feature steps to further enable functionality, they have not yet been satisfied.
    // Alternatively, this feature may not have any extra steps in order to use it and is already fully functional.
    return "available";
  }

  /**
   * Sets the feature into its default "Available" AI Controls state and
   * ensures that all downloaded artifacts are deleted. This can be thought of
   * as a "reset" of the feature's state to a blank slate.
   *
   * Features that do not have a distinct enabled state {@link hasDistinctEnabledState}
   * may implement this equivalently to the {@link enable} function, with the exception
   * that this function must delete all downloaded artifacts, whereas {@link enable} will not.
   *
   * This must not rely on `Services.prefs.clearUserPref`, because enterprise policy may
   * override runtime defaults. Implementors should instead explicitly set prefs to values
   * that will make the feature become "Available" in AI Controls.
   *
   * @returns {Promise<void>} Resolves when makeAvailable is complete
   */
  static async makeAvailable() {
    throw new Error("AIFeature subclass must implement static makeAvailable()");
  }

  /**
   * Sets the feature into its "Enabled" AI Controls state.
   *
   * Features that do not have a distinct enabled state {@link hasDistinctEnabledState}
   * may still result in the AI Controls state remaining "Available" after this is called.
   *
   * @returns {Promise<void>} Resolves when enablement process is complete
   */
  static async enable() {
    throw new Error("AIFeature subclass must implement static enable()");
  }

  /**
   * Sets the feature into its "Blocked" AI Controls state and ensures that all
   * downloaded artifacts are deleted.
   *
   * @returns {Promise<void>} Resolves when blocking process is complete
   */
  static async block() {
    throw new Error("AIFeature subclass must implement static block()");
  }
}
