/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var gProfiles = {
  async init() {
    this.copyProfile = this.copyProfile.bind(this);
    this.createNewProfile = this.createNewProfile.bind(this);
    this.handleCommand = this.handleCommand.bind(this);
    this.launchProfile = this.launchProfile.bind(this);
    this.manageProfiles = this.manageProfiles.bind(this);
    this.onPopupShowing = this.onPopupShowing.bind(this);
    this.toggleProfileMenus = this.toggleProfileMenus.bind(this);
    this.updateView = this.updateView.bind(this);

    this.bundle = Services.strings.createBundle(
      "chrome://browser/locale/browser.properties"
    );

    this.profilesButton = PanelMultiView.getViewNode(
      document,
      "appMenu-profiles-button"
    );
    this.appMenuCreateProfileButton = PanelMultiView.getViewNode(
      document,
      "appMenu-create-profile-button"
    );
    this.fxaMenuProfileButtonsContainer = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profile-buttons"
    );
    this.fxaMenuProfilesHeaderLabel = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profiles-header-label"
    );
    this.fxaMenuProfilesSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profiles-separator"
    );
    this.fxaMenuAllProfilesPanel = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-all-profiles"
    );
    this.fxaMenuAllProfilesPanel.addEventListener(
      "command",
      this.handleCommand
    );
    this.subview = PanelMultiView.getViewNode(document, "PanelUI-profiles");
    this.subview.addEventListener("command", this.handleCommand);

    PanelUI.mainView.addEventListener("ViewShowing", () =>
      this._onPanelShowing(this.profilesButton)
    );

    let fxaPanelView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
    fxaPanelView.addEventListener("ViewShowing", () =>
      this._onFxaMenuPanelShowing()
    );

    this.profilesButton.addEventListener("command", this.handleCommand);
    this.appMenuCreateProfileButton.addEventListener(
      "command",
      this.handleCommand
    );

    this.fxaMenuProfileButtonsContainer.addEventListener(
      "command",
      this.handleCommand
    );

    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-create-profile"
    ).addEventListener("command", this.handleCommand);

    // moz-button emits "click" rather than "command".
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-create-profile-confirm-button"
    ).addEventListener("click", this.handleCommand);

    this.toggleProfileMenus(SelectableProfileService?.isEnabled);

    if (SelectableProfileService) {
      let listener = (event, isEnabled) => this.toggleProfileMenus(isEnabled);

      SelectableProfileService.on("enableChanged", listener);
      window.addEventListener("unload", () =>
        SelectableProfileService.off("enableChanged", listener)
      );
    }
  },

  toggleProfileMenus(isEnabled) {
    let profilesMenu = document.getElementById("profiles-menu");
    profilesMenu.hidden = !isEnabled;
  },

  async _onPanelShowing(profilesButton) {
    if (!SelectableProfileService?.isEnabled) {
      profilesButton.hidden = true;
      this.appMenuCreateProfileButton.hidden = true;
      return;
    }

    // If the feature is preffed on, but we haven't created profiles yet, the
    // service will not be initialized.
    let profiles = SelectableProfileService.initialized
      ? await SelectableProfileService.getAllProfiles()
      : [];
    if (!profiles.length) {
      profilesButton.hidden = true;
      this.appMenuCreateProfileButton.hidden = false;
      return;
    }
    this.appMenuCreateProfileButton.hidden = true;

    profilesButton.hidden = false;

    let { themeBg, themeFg } = SelectableProfileService.currentProfile.theme;
    profilesButton.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
    profilesButton.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
    profilesButton.setAttribute(
      "label",
      SelectableProfileService.currentProfile.name
    );
    profilesButton.setAttribute(
      "image",
      await SelectableProfileService.currentProfile.getAvatarURL(24)
    );
  },

  async _onFxaMenuPanelShowing() {
    const container = this.fxaMenuProfileButtonsContainer;
    const headerLabel = this.fxaMenuProfilesHeaderLabel;
    const separator = this.fxaMenuProfilesSeparator;

    const hideProfilesSection = () => {
      container.hidden = true;
      headerLabel.hidden = true;
      separator.hidden = true;
    };

    const showProfilesSection = () => {
      container.hidden = false;
      headerLabel.hidden = false;
      separator.hidden = false;
    };

    if (!SelectableProfileService?.isEnabled) {
      hideProfilesSection();
      return;
    }

    let profiles = SelectableProfileService.initialized
      ? await SelectableProfileService.getAllProfiles()
      : [];

    while (container.lastChild) {
      container.lastChild.remove();
    }

    // When there are no user created profiles, surface a
    // call to action to create a new profile instead of the profile list.
    if (!profiles.length) {
      let createBtn = document.createXULElement("toolbarbutton");
      createBtn.id = "PanelUI-fxa-menu-create-profile-button";
      createBtn.classList.add(
        "subviewbutton",
        "subviewbutton-iconic",
        "subviewbutton-nav"
      );
      createBtn.setAttribute("closemenu", "none");
      createBtn.setAttribute("data-l10n-id", "appmenu-create-profile2");
      container.appendChild(createBtn);

      showProfilesSection();
      return;
    }

    let currentProfileId = SelectableProfileService.currentProfile?.id;
    profiles.sort((a, b) => {
      if (a.id === currentProfileId) {
        return -1;
      }
      if (b.id === currentProfileId) {
        return 1;
      }
      return 0;
    });

    for (let profile of profiles.slice(0, 3)) {
      let btn = document.createXULElement("toolbarbutton");
      btn.classList.add(
        "subviewbutton",
        "subviewbutton-iconic",
        "profile-item"
      );
      if (profile.id === SelectableProfileService.currentProfile?.id) {
        btn.classList.add("subviewbutton-nav");
      }
      btn.setAttribute("closemenu", "none");
      btn.setAttribute("profileid", profile.id);
      btn.setAttribute("label", profile.name);
      let { themeBg, themeFg } = profile.theme;
      btn.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
      btn.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
      let avatarURL = await profile.getAvatarURL(24);
      btn.setAttribute("image", avatarURL);
      container.appendChild(btn);
    }

    if (profiles.length > 3) {
      let allBtn = document.createXULElement("toolbarbutton");
      allBtn.id = "PanelUI-fxa-menu-all-profiles-button";
      allBtn.classList.add("subviewbutton", "subviewbutton-nav");
      allBtn.setAttribute("closemenu", "none");
      allBtn.setAttribute("data-l10n-id", "appmenu-all-profiles");
      container.appendChild(allBtn);
    }

    showProfilesSection();
  },

  async _populateAllProfilesPanel() {
    const list = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-all-profiles-list"
    );

    while (list.lastChild) {
      list.lastChild.remove();
    }

    let profiles = SelectableProfileService.initialized
      ? await SelectableProfileService.getAllProfiles()
      : [];

    let currentProfileId = SelectableProfileService.currentProfile?.id;
    profiles.sort((a, b) => {
      if (a.id === currentProfileId) {
        return -1;
      }
      if (b.id === currentProfileId) {
        return 1;
      }
      return 0;
    });

    for (let profile of profiles) {
      let btn = document.createXULElement("toolbarbutton");
      btn.classList.add(
        "subviewbutton",
        "subviewbutton-iconic",
        "profile-item"
      );
      btn.setAttribute("profileid", profile.id);
      let { themeBg, themeFg } = profile.theme;
      btn.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
      btn.style.setProperty("--appmenu-profiles-theme-fg", themeFg);

      if (profile.id === currentProfileId) {
        btn.setAttribute("closemenu", "none");
        btn.setAttribute("align", "center");

        let icon = document.createXULElement("image");
        icon.classList.add("toolbarbutton-icon");
        icon.setAttribute("src", await profile.getAvatarURL(24));
        btn.appendChild(icon);

        let labelVbox = document.createXULElement("vbox");
        labelVbox.classList.add("toolbarbutton-text", "profile-item-labels");

        let mainLabel = document.createXULElement("label");
        mainLabel.setAttribute("value", profile.name);
        mainLabel.setAttribute("crop", "end");
        labelVbox.appendChild(mainLabel);

        let currentLabel = document.createXULElement("label");
        document.l10n.setAttributes(
          currentLabel,
          "appmenu-profile-current-in-use"
        );
        currentLabel.classList.add("profile-item-current-label");
        labelVbox.appendChild(currentLabel);

        btn.appendChild(labelVbox);

        let checkIcon = document.createXULElement("image");
        checkIcon.classList.add("profile-item-check-icon");
        checkIcon.setAttribute("src", "chrome://global/skin/icons/check.svg");
        btn.appendChild(checkIcon);
      } else {
        btn.setAttribute("label", profile.name);
        btn.setAttribute("image", await profile.getAvatarURL(24));
      }

      list.appendChild(btn);
    }
  },

  /**
   * Draws the menubar panel contents.
   */
  async onPopupShowing() {
    let menuPopup = document.getElementById("menu_ProfilesPopup");
    let profiles = await SelectableProfileService.getAllProfiles();
    let currentProfile = SelectableProfileService.currentProfile;
    let insertionPoint = document.getElementById("menu_newProfile");
    let existingItems = [
      ...menuPopup.querySelectorAll(":scope > menuitem[profileid]"),
    ];
    for (let profile of profiles) {
      let menuitem = existingItems.shift();
      let isNewItem = !menuitem;
      if (isNewItem) {
        menuitem = document.createXULElement("menuitem");
        menuitem.classList.add("menuitem-iconic", "menuitem-iconic-profile");
        menuitem.setAttribute("command", "Profiles:LaunchProfile");
      }
      let { themeBg, themeFg } = profile.theme;
      menuitem.setAttribute("profileid", profile.id);
      menuitem.setAttribute("image", await profile.getAvatarURL(48));
      menuitem.style.setProperty("--menu-profiles-theme-bg", themeBg);
      menuitem.style.setProperty("--menu-profiles-theme-fg", themeFg);

      if (profile.id === currentProfile.id) {
        menuitem.classList.add("current");
        menuitem.setAttribute("data-l10n-id", "menu-profiles-current");
        menuitem.setAttribute(
          "data-l10n-args",
          JSON.stringify({ profileName: profile.name })
        );
      } else {
        menuitem.classList.remove("current");
        menuitem.removeAttribute("data-l10n-id");
        menuitem.removeAttribute("data-l10n-args");
        menuitem.setAttribute("label", profile.name);
      }

      if (isNewItem) {
        menuPopup.insertBefore(menuitem, insertionPoint);
      }
    }
    // If there's any old item to remove, do so now.
    for (let remaining of existingItems) {
      remaining.remove();
    }
  },

  manageProfiles() {
    return SelectableProfileService.maybeSetupDataStore().then(() => {
      toOpenWindowByType(
        "about:profilemanager",
        "about:profilemanager",
        "chrome,extrachrome,menubar,resizable,scrollbars,status,toolbar,centerscreen"
      );
    });
  },

  copyProfile() {
    const profile =
      this._subViewProfile ?? SelectableProfileService.currentProfile;
    SelectableProfileService.maybeSetupDataStore().then(() => {
      profile.copyProfile();
    });
  },

  createNewProfile(source) {
    SelectableProfileService.createNewProfile(true, null, source);
  },

  updateView(target) {
    const closestPanelView = target.closest("panelview");
    const panelView = closestPanelView
      ? PanelView.forNode(closestPanelView)
      : null;
    const wasKeyboardActivation = panelView?._doingKeyboardActivation ?? false;
    this.populateSubView();
    if (panelView && wasKeyboardActivation) {
      panelView._doingKeyboardActivation = true;
    }
    PanelUI.showSubView("PanelUI-profiles", target);
  },

  async updateFxAView(target) {
    const profileId = parseInt(target.getAttribute("profileid"), 10);
    this._subViewProfile = profileId
      ? await SelectableProfileService.getProfile(profileId)
      : null;
    this.populateSubView(this._subViewProfile);
    PanelUI.showSubView("PanelUI-profiles", target);
  },

  launchProfile(aEvent) {
    SelectableProfileService.getProfile(
      aEvent.target.getAttribute("profileid")
    ).then(profile => {
      SelectableProfileService.launchInstance(profile);
    });
  },

  async openTabsInProfile(aEvent, tabsToOpen) {
    let profile = await SelectableProfileService.getProfile(
      aEvent.target.getAttribute("profileid")
    );
    SelectableProfileService.launchInstance(
      profile,
      tabsToOpen.map(tab => tab.linkedBrowser.currentURI.spec)
    );
  },

  async handleCommand(aEvent) {
    switch (aEvent.target.id) {
      /* App menu button events */
      case "appMenu-profiles-button": {
        this.updateView(aEvent.target);
        break;
      }
      /* FxA menu button events */
      case "PanelUI-fxa-menu-all-profiles-button": {
        aEvent.stopPropagation();
        this._populateAllProfilesPanel();
        PanelUI.showSubView("PanelUI-fxa-menu-all-profiles", aEvent.target);
        break;
      }
      case "PanelUI-fxa-menu-all-profiles-manage-button": {
        this.manageProfiles();
        break;
      }
      case "PanelUI-fxa-menu-all-profiles-create-button": {
        this.createNewProfile("profiles-panel");
        break;
      }
      case "appMenu-create-profile-button":
      // fall through
      case "PanelUI-fxa-menu-create-profile-button": {
        aEvent.stopPropagation();
        PanelUI.showSubView("PanelUI-fxa-menu-create-profile", aEvent.target);
        break;
      }
      case "PanelUI-fxa-menu-create-profile-confirm-button": {
        this.createNewProfile("profiles-panel");
        break;
      }
      case "PanelUI-fxa-menu-create-profile-learn-more-button": {
        openTrustedLinkIn(
          "https://support.mozilla.org/kb/profile-management",
          "tab"
        );
        break;
      }
      case "PanelUI-fxa-menu-create-profile-copy-button": {
        this.copyProfile();
        break;
      }
      case "PanelUI-fxa-menu-create-profile-manage-button": {
        this.manageProfiles();
        break;
      }
      default: {
        if (
          aEvent.target.classList.contains("profile-item") &&
          aEvent.target.classList.contains("subviewbutton-nav")
        ) {
          aEvent.stopPropagation();
          this.updateFxAView(aEvent.target);
        }
        break;
      }
      /* Subpanel events that may be triggered in FxA menu or app menu */
      case "profiles-appmenu-back-button": {
        aEvent.target.closest("panelview").panelMultiView.goBack();
        aEvent.target.blur();
        break;
      }
      case "profiles-edit-this-profile-button": {
        openTrustedLinkIn("about:editprofile", "tab");
        break;
      }
      case "profiles-manage-profiles-button": {
        this.manageProfiles();
        break;
      }
      case "profiles-copy-profile-button": {
        this.copyProfile();
        break;
      }
      case "profiles-create-profile-button": {
        this.createNewProfile("profiles-panel");
        break;
      }

      /* Menubar events */
      case "Profiles:CreateProfile": {
        this.createNewProfile("main-menu");
        break;
      }
      case "Profiles:ManageProfiles": {
        this.manageProfiles();
        break;
      }
      case "Profiles:LaunchProfile": {
        this.launchProfile(aEvent.sourceEvent);
        break;
      }
      case "Profiles:MoveTabsToProfile": {
        let tabs;
        if (TabContextMenu.contextTab.multiselected) {
          tabs = gBrowser.selectedTabs;
        } else {
          tabs = [TabContextMenu.contextTab];
        }
        this.openTabsInProfile(aEvent.sourceEvent, tabs);
        break;
      }
    }
    /* Subpanel profile events that may be triggered in FxA menu or app menu */
    if (
      aEvent.target.classList.contains("profile-item") &&
      aEvent.target.hasAttribute("profileid") &&
      aEvent.target.getAttribute("profileid") !==
        String(SelectableProfileService.currentProfile?.id)
    ) {
      this.launchProfile(aEvent);
    }
  },

  /**
   * Inserts the subpanel contents for the PanelUI subpanel, which may be shown
   * either in the app menu or the FxA toolbar button menu.
   *
   *   @param {SelectableProfile|null} [displayProfile]
   *   The profile to display in the subview. If null, falls back to the
   *   current profile.
   */
  async populateSubView(displayProfile = null) {
    let profiles = [];
    let currentProfile = null;

    if (SelectableProfileService.initialized) {
      profiles = await SelectableProfileService.getAllProfiles();
      currentProfile = SelectableProfileService.currentProfile;
    }

    const targetProfile = displayProfile ?? currentProfile;
    const showProfileInfo = displayProfile !== null || profiles.length >= 1;

    let subview = PanelMultiView.getViewNode(document, "PanelUI-profiles");

    let backButton = PanelMultiView.getViewNode(
      document,
      "profiles-appmenu-back-button"
    );
    backButton.setAttribute(
      "aria-label",
      this.bundle.GetStringFromName("panel.back")
    );
    backButton.style.fill = "var(--appmenu-profiles-theme-fg, currentColor)";

    const subviewBody = subview.querySelector(".panel-subview-body");
    subviewBody.hidden = !targetProfile;

    let currentProfileCard = PanelMultiView.getViewNode(
      document,
      "current-profile"
    );
    currentProfileCard.hidden = !targetProfile;

    let profilesHeader = PanelMultiView.getViewNode(
      document,
      "PanelUI-profiles-header"
    );

    let editThisProfileButton = PanelMultiView.getViewNode(
      document,
      "profiles-edit-this-profile-button"
    );
    if (!editThisProfileButton) {
      editThisProfileButton = document.createXULElement("toolbarbutton");
      editThisProfileButton.id = "profiles-edit-this-profile-button";
      editThisProfileButton.classList.add("subviewbutton");
      editThisProfileButton.setAttribute(
        "data-l10n-id",
        "appmenu-edit-this-profile"
      );
    }

    // Automatically created by PanelMultiView.
    const headerSeparator = profilesHeader.nextElementSibling;
    let footerSeparator = PanelMultiView.getViewNode(
      document,
      "footer-separator"
    );
    if (!footerSeparator) {
      footerSeparator = document.createXULElement("toolbarseparator");
      footerSeparator.id = "footer-separator";
    }

    let createProfileButton = PanelMultiView.getViewNode(
      document,
      "profiles-create-profile-button"
    );
    if (!createProfileButton) {
      createProfileButton = document.createXULElement("toolbarbutton");
      createProfileButton.id = "profiles-create-profile-button";
      createProfileButton.classList.add(
        "subviewbutton",
        "panel-subview-footer-button"
      );
      createProfileButton.setAttribute(
        "data-l10n-id",
        "appmenu-create-profile2"
      );
    }

    let copyProfileButton = PanelMultiView.getViewNode(
      document,
      "profiles-copy-profile-button"
    );

    if (!copyProfileButton) {
      copyProfileButton = document.createXULElement("toolbarbutton");
      copyProfileButton.id = "profiles-copy-profile-button";
      copyProfileButton.classList.add("subviewbutton");
      copyProfileButton.setAttribute("data-l10n-id", "appmenu-copy-profile");
    }

    let manageProfilesButton = PanelMultiView.getViewNode(
      document,
      "profiles-manage-profiles-button"
    );

    if (!manageProfilesButton) {
      manageProfilesButton = document.createXULElement("toolbarbutton");
      manageProfilesButton.id = "profiles-manage-profiles-button";
      manageProfilesButton.classList.add("subviewbutton");
      manageProfilesButton.setAttribute(
        "data-l10n-id",
        "appmenu-manage-profiles"
      );
    }

    if (targetProfile) {
      let { themeBg, themeFg } = targetProfile.theme;
      subview.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
      subview.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
      profilesHeader.style.backgroundColor = "var(--appmenu-profiles-theme-bg)";
      profilesHeader.style.color = "var(--appmenu-profiles-theme-fg)";

      let headerText = PanelMultiView.getViewNode(
        document,
        "profiles-header-content"
      );
      headerText.textContent = targetProfile.name;

      let profileIconEl = PanelMultiView.getViewNode(
        document,
        "profile-icon-image"
      );
      currentProfileCard.style.setProperty(
        "--appmenu-profiles-theme-bg",
        themeBg
      );
      currentProfileCard.style.setProperty(
        "--appmenu-profiles-theme-fg",
        themeFg
      );

      profileIconEl.style.listStyleImage = `url(${await targetProfile.getAvatarURL(80)})`;
    } else {
      profilesHeader.removeAttribute("style");
    }

    if (showProfileInfo) {
      headerSeparator.hidden = true;
      editThisProfileButton.hidden = false;
      subview.appendChild(editThisProfileButton);
      subview.appendChild(copyProfileButton);

      for (let item of subview.querySelectorAll(".profiles-subview-item")) {
        item.remove();
      }
      PanelMultiView.getViewNode(
        document,
        "profiles-subview-list-start-separator"
      )?.remove();
      PanelMultiView.getViewNode(document, "profiles-subview-list")?.remove();

      let hasOtherProfiles = false;
      if (displayProfile === null) {
        let profilesListStartSeparator =
          document.createXULElement("toolbarseparator");
        profilesListStartSeparator.id = "profiles-subview-list-start-separator";

        subview.appendChild(profilesListStartSeparator);

        let profilesList = document.createXULElement("vbox");
        profilesList.id = "profiles-subview-list";
        subview.appendChild(profilesList);

        for (let profile of profiles) {
          if (profile.id === targetProfile?.id) {
            continue;
          }
          let btn = document.createXULElement("toolbarbutton");
          btn.classList.add(
            "subviewbutton",
            "subviewbutton-iconic",
            "profile-item",
            "profiles-subview-item"
          );
          btn.setAttribute("profileid", profile.id);
          btn.setAttribute("label", profile.name);
          let { themeBg, themeFg } = profile.theme;
          btn.style.setProperty("--appmenu-profiles-theme-bg", themeBg);
          btn.style.setProperty("--appmenu-profiles-theme-fg", themeFg);
          btn.setAttribute("image", await profile.getAvatarURL(24));
          profilesList.appendChild(btn);
          hasOtherProfiles = true;
        }

        // With no other profiles the list is empty, so hide it and the footer
        // separator to avoid rendering two adjacent separators.
        profilesList.hidden = !hasOtherProfiles;
      }

      footerSeparator.hidden = displayProfile === null && !hasOtherProfiles;
      subview.appendChild(footerSeparator);

      subview.appendChild(manageProfilesButton);
      subview.appendChild(createProfileButton);
    } else {
      headerSeparator.hidden = false;
      footerSeparator.hidden = true;
      editThisProfileButton.hidden = true;
      subview.insertBefore(editThisProfileButton, subviewBody);
      subview.insertBefore(createProfileButton, subviewBody);
      subview.insertBefore(copyProfileButton, subviewBody);
      subview.insertBefore(manageProfilesButton, subviewBody);
    }
  },

  async populateMoveTabMenu(menuPopup) {
    if (!SelectableProfileService.initialized) {
      return;
    }

    const profiles = await SelectableProfileService.getAllProfiles();
    const currentProfile = SelectableProfileService.currentProfile;

    const separator = document.getElementById("moveTabSeparator");
    separator.hidden = profiles.length < 2;

    let existingItems = [
      ...menuPopup.querySelectorAll(":scope > menuitem[profileid]"),
    ];

    // Place the profile items immediately after the separator, repositioning
    // reused ones too. Anchoring keeps them in their declared slot regardless of
    // any later items in the popup (e.g. Select All Tabs in the alt layout).
    let anchor = separator;
    for (let profile of profiles) {
      if (profile.id === currentProfile.id) {
        continue;
      }

      let menuitem = existingItems.shift();
      if (!menuitem) {
        menuitem = document.createXULElement("menuitem");
        menuitem.setAttribute("tbattr", "tabbrowser-multiple-visible");
        menuitem.setAttribute("data-l10n-id", "move-to-new-profile");
        menuitem.setAttribute("command", "Profiles:MoveTabsToProfile");
      }

      menuitem.disabled = false;
      menuitem.setAttribute("profileid", profile.id);
      menuitem.setAttribute(
        "data-l10n-args",
        JSON.stringify({ profileName: profile.name })
      );

      anchor.after(menuitem);
      anchor = menuitem;
    }
    // If there's any old item to remove, do so now.
    for (let remaining of existingItems) {
      remaining.remove();
    }
  },
};
