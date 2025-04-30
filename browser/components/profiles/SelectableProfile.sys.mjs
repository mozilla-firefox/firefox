/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ProfilesDatastoreService } from "resource:///modules/profiles/ProfilesDatastoreService.sys.mjs";
import { SelectableProfileService } from "resource:///modules/profiles/SelectableProfileService.sys.mjs";

/**
 * The selectable profile
 */
export class SelectableProfile {
  // DB internal autoincremented integer ID.
  // eslint-disable-next-line no-unused-private-class-members
  #id;

  // Path to profile on disk.
  #path;

  // The user-editable name
  #name;

  // Name of the user's chosen avatar, which corresponds to a list of built-in
  // SVG avatars.
  #avatar;

  // Cached theme properties, used to allow displaying a SelectableProfile
  // without loading the AddonManager to get theme info.
  #themeId;
  #themeFg;
  #themeBg;

  constructor(row) {
    this.#id = row.getResultByName("id");
    this.#path = row.getResultByName("path");
    this.#name = row.getResultByName("name");
    this.#avatar = row.getResultByName("avatar");
    this.#themeId = row.getResultByName("themeId");
    this.#themeFg = row.getResultByName("themeFg");
    this.#themeBg = row.getResultByName("themeBg");
  }

  /**
   * Get the id of the profile.
   *
   * @returns {number} Id of profile
   */
  get id() {
    return this.#id;
  }

  // Note: setters update the object, then ask the SelectableProfileService to save it.

  /**
   * Get the user-editable name for the profile.
   *
   * @returns {string} Name of profile
   */
  get name() {
    return this.#name;
  }

  /**
   * Update the user-editable name for the profile, then trigger saving the profile,
   * which will notify() other running instances.
   *
   * @param {string} aName The new name of the profile
   */
  set name(aName) {
    this.#name = aName;

    this.saveUpdatesToDB();

    Services.prefs.setBoolPref("browser.profiles.profile-name.updated", true);
  }

  /**
   * Get the full path to the profile as a string.
   *
   * @returns {string} Path of profile
   */
  get path() {
    return PathUtils.joinRelative(
      ProfilesDatastoreService.constructor.getDirectory("UAppData").path,
      this.#path
    );
  }

  /**
   * Get the profile directory as an nsIFile.
   *
   * @returns {Promise<nsIFile>} A promise that resolves to an nsIFile for
   * the profile directory
   */
  get rootDir() {
    return IOUtils.getDirectory(this.path);
  }

  /**
   * Get the profile local directory as an nsIFile.
   *
   * @returns {Promise<nsIFile>} A promise that resolves to an nsIFile for
   * the profile local directory
   */
  get localDir() {
    return this.rootDir.then(root => {
      let relative = root.getRelativePath(
        ProfilesDatastoreService.constructor.getDirectory("DefProfRt")
      );
      let local =
        ProfilesDatastoreService.constructor.getDirectory("DefProfLRt");
      local.appendRelativePath(relative);
      return local;
    });
  }

  /**
   * Get the name of the avatar for the profile.
   *
   * @returns {string} Name of the avatar
   */
  get avatar() {
    return this.#avatar;
  }

  /**
   * Update the avatar, then trigger saving the profile, which will notify()
   * other running instances.
   *
   * @param {string} aAvatar Name of the avatar
   */
  set avatar(aAvatar) {
    this.#avatar = aAvatar;

    this.saveUpdatesToDB();
  }

  /**
   * Get the l10n id for the current avatar.
   *
   * @returns {string} L10n id for the current avatar
   */
  get avatarL10nId() {
    switch (this.avatar) {
      case "book":
        return "book-avatar-alt";
      case "briefcase":
        return "briefcase-avatar-alt";
      case "flower":
        return "flower-avatar-alt";
      case "heart":
        return "heart-avatar-alt";
      case "shopping":
        return "shopping-avatar-alt";
      case "star":
        return "star-avatar-alt";
    }

    return "";
  }

  // Note, theme properties are set and returned as a group.

  /**
   * Get the theme l10n-id as a string, like "theme-foo-name".
   *     the theme foreground color as CSS style string, like "rgb(1,1,1)",
   *     the theme background color as CSS style string, like "rgb(0,0,0)".
   *
   * @returns {object} an object of the form { themeId, themeFg, themeBg }.
   */
  get theme() {
    return {
      themeId: this.#themeId,
      themeFg: this.#themeFg,
      themeBg: this.#themeBg,
    };
  }

  get iconPaintContext() {
    return {
      fillColor: this.#themeBg,
      strokeColor: this.#themeFg,
      fillOpacity: 1.0,
      strokeOpacity: 1.0,
    };
  }

  /**
   * Update the theme (all three properties are required), then trigger saving
   * the profile, which will notify() other running instances.
   *
   * @param {object} param0 The theme object
   * @param {string} param0.themeId L10n id of the theme
   * @param {string} param0.themeFg Foreground color of theme as CSS style string, like "rgb(1,1,1)",
   * @param {string} param0.themeBg Background color of theme as CSS style string, like "rgb(0,0,0)".
   */
  set theme({ themeId, themeFg, themeBg }) {
    this.#themeId = themeId;
    this.#themeFg = themeFg;
    this.#themeBg = themeBg;

    this.saveUpdatesToDB();
  }

  saveUpdatesToDB() {
    SelectableProfileService.updateProfile(this);
  }

  toObject() {
    let profileObj = {
      id: this.id,
      path: this.#path,
      name: this.name,
      avatar: this.avatar,
      avatarL10nId: this.avatarL10nId,
      ...this.theme,
    };

    return profileObj;
  }
}
