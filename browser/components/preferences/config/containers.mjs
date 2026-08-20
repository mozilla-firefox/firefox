/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";
import { containerOptions } from "chrome://browser/content/usercontext/container-select.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "idnService", function () {
  return Cc["@mozilla.org/network/idn-service;1"].getService(Ci.nsIIDNService);
});

Preferences.addAll([
  {
    id: "browser.link.force_default_user_context_id_for_external_opens",
    type: "bool",
  },
]);

const IDENTITY_CHANGE_TOPICS = [
  "contextual-identity-created",
  "contextual-identity-updated",
  "contextual-identity-deleted",
  "contextual-identity-reordered",
];

const SITE_ASSOCIATION_CHANGE_TOPIC =
  "contextual-identity-site-association-changed";

const SWITCH_DURING_NAVIGATION_PREF =
  "privacy.containers.switchDuringNavigation.enabled";

function siteContainersEnabled() {
  return Services.prefs.getBoolPref(SWITCH_DURING_NAVIGATION_PREF, false);
}

function openContainerDialog(userContextId) {
  let identity = {
    name: "",
    icon: lazy.ContextualIdentityService.containerIcons[0],
    color: lazy.ContextualIdentityService.containerColors[0],
  };
  if (userContextId) {
    identity =
      lazy.ContextualIdentityService.getPublicIdentityFromId(userContextId);
    identity.name = lazy.ContextualIdentityService.getUserContextLabel(
      identity.userContextId
    );
  }
  window.gSubDialog.open(
    "chrome://browser/content/preferences/dialogs/containers.xhtml",
    undefined,
    { userContextId, identity }
  );
}

function openSiteContainerDialog() {
  window.gSubDialog.open(
    "chrome://browser/content/preferences/dialogs/siteContainer.xhtml"
  );
}

async function removeContainer(userContextId) {
  let count = lazy.ContextualIdentityService.countContainerTabs(userContextId);
  if (count > 0) {
    let [title, message, okButton, cancelButton] =
      await document.l10n.formatValues([
        { id: "containers-remove-alert-title" },
        { id: "containers-remove-alert-msg", args: { count } },
        { id: "containers-remove-ok-button" },
        { id: "containers-remove-cancel-button" },
      ]);

    let buttonFlags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1;

    let rv = Services.prompt.confirmEx(
      window,
      title,
      message,
      buttonFlags,
      okButton,
      cancelButton,
      null,
      null,
      {}
    );
    if (rv != 0) {
      return;
    }
    await lazy.ContextualIdentityService.closeContainerTabs(userContextId);
  }
  lazy.ContextualIdentityService.remove(userContextId);
}

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "containers-list";

    containers;

    beforeRefresh() {
      this.containers = lazy.ContextualIdentityService.getPublicIdentities();
    }

    async getControlConfig() {
      let items = this.containers.map(container => {
        const containerName =
          lazy.ContextualIdentityService.getUserContextLabel(
            container.userContextId
          );
        return {
          control: "moz-box-item",
          iconSrc: lazy.ContextualIdentityService.getContainerIconURL(
            container.icon
          ),
          controlAttrs: {
            label: containerName,
            class: `containers-identity-item identity-color-${container.color}`,
            value: container.userContextId,
          },
          options: [
            {
              control: "moz-button",
              iconSrc: "chrome://global/skin/icons/edit-outline.svg",
              l10nId: "containers-settings-button2",
              controlAttrs: {
                slot: "actions",
                action: "edit",
                value: container.userContextId,
              },
            },
            {
              control: "moz-button",
              iconSrc: "chrome://global/skin/icons/delete.svg",
              l10nId: "containers-remove-button3",
              controlAttrs: {
                slot: "actions",
                action: "remove",
                value: container.userContextId,
              },
            },
          ],
        };
      });
      return { options: items };
    }

    /** @param {CustomEvent} event */
    onUserReorder(event) {
      const { draggedElement, insertAt } = event.detail;
      const userContextId = parseInt(draggedElement.getAttribute("value"), 10);
      lazy.ContextualIdentityService.move([userContextId], insertAt);
    }

    async onUserClick(e) {
      const action = e.target.getAttribute("action");
      const value = parseInt(e.target.getAttribute("value"), 10);
      if (action === "edit") {
        openContainerDialog(value);
      } else if (action === "remove") {
        await removeContainer(value);
      }
    }

    setup() {
      for (const topic of IDENTITY_CHANGE_TOPICS) {
        Services.obs.addObserver(this.emitChange, topic);
      }
      return () => {
        for (const topic of IDENTITY_CHANGE_TOPICS) {
          Services.obs.removeObserver(this.emitChange, topic);
        }
      };
    }
  }
);

Preferences.addSetting({
  id: "containers-add-button",
  onUserClick() {
    openContainerDialog(null);
  },
});

Preferences.addSetting({
  id: "containers-new-tab-check",
  pref: "privacy.userContext.newTabContainerOnLeftClick.enabled",
});

Preferences.addSetting({
  id: "containers-external-links-check",
  pref: "browser.link.force_default_user_context_id_for_external_opens",
});

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "site-containers-add-button";

    containers;

    beforeRefresh() {
      this.containers = lazy.ContextualIdentityService.getPublicIdentities();
    }

    async visible() {
      return siteContainersEnabled();
    }

    async disabled() {
      return !this.containers.length;
    }

    onUserClick() {
      openSiteContainerDialog();
    }

    setup() {
      for (const topic of IDENTITY_CHANGE_TOPICS) {
        Services.obs.addObserver(this.emitChange, topic);
      }
      Services.prefs.addObserver(
        SWITCH_DURING_NAVIGATION_PREF,
        this.emitChange
      );
      return () => {
        for (const topic of IDENTITY_CHANGE_TOPICS) {
          Services.obs.removeObserver(this.emitChange, topic);
        }
        Services.prefs.removeObserver(
          SWITCH_DURING_NAVIGATION_PREF,
          this.emitChange
        );
      };
    }
  }
);

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "site-containers-list";

    associations;
    containers;

    beforeRefresh() {
      this.associations = lazy.ContextualIdentityService.getSiteAssociations()
        .map(({ site, userContextId }) => ({
          site,
          userContextId,
          displaySite: lazy.idnService.domainToDisplay(site),
        }))
        .sort((a, b) => a.displaySite.localeCompare(b.displaySite));
      this.containers = containerOptions();
    }

    async visible() {
      return siteContainersEnabled() && !!this.associations.length;
    }

    async getControlConfig() {
      let options = await Promise.all(
        this.associations.map(association => this.#itemConfig(association))
      );
      return { options };
    }

    async #itemConfig({ site, userContextId, displaySite }) {
      let [selectLabel] = await document.l10n.formatValues([
        { id: "containers-site-container-select", args: { site: displaySite } },
      ]);

      return {
        key: site,
        control: "moz-box-item",
        iconSrc: `page-icon:https://${site}/`,
        controlAttrs: {
          label: displaySite,
          value: site,
        },
        options: [
          {
            key: `${site}-container`,
            control: "container-select",
            value: String(userContextId),
            controlAttrs: {
              slot: "actions",
              size: "small",
              site,
              "aria-label": selectLabel,
            },
            options: this.containers.map(attrs => ({
              key: `${site}-container-${attrs.value}`,
              control: "moz-option",
              controlAttrs: attrs,
            })),
          },
          {
            key: `${site}-remove`,
            control: "moz-button",
            iconSrc: "chrome://global/skin/icons/delete.svg",
            l10nId: "containers-site-remove-button",
            controlAttrs: {
              slot: "actions",
              action: "remove",
              value: site,
            },
          },
        ],
      };
    }

    onUserClick(e) {
      if (e.target.getAttribute("action") !== "remove") {
        return;
      }
      lazy.ContextualIdentityService.removeSiteAssociation(
        e.target.getAttribute("value")
      );
    }

    setup() {
      const topics = [...IDENTITY_CHANGE_TOPICS, SITE_ASSOCIATION_CHANGE_TOPIC];
      for (const topic of topics) {
        Services.obs.addObserver(this.emitChange, topic);
      }
      Services.prefs.addObserver(
        SWITCH_DURING_NAVIGATION_PREF,
        this.emitChange
      );
      return () => {
        for (const topic of topics) {
          Services.obs.removeObserver(this.emitChange, topic);
        }
        Services.prefs.removeObserver(
          SWITCH_DURING_NAVIGATION_PREF,
          this.emitChange
        );
      };
    }
  }
);

SettingGroupManager.registerGroups({
  containers: {
    l10nId: "containers-card-header2",
    supportPage: "containers",
    headingLevel: 2,
    items: [
      {
        id: "containers-add-button",
        control: "moz-button",
        l10nId: "containers-add-button2",
      },
      {
        id: "containers-list",
        control: "moz-box-group",
        controlAttrs: {
          type: "reorderable-list",
        },
      },
      {
        id: "containers-new-tab-check",
        l10nId: "containers-new-tab-check3",
      },
      {
        id: "containers-external-links-check",
        l10nId: "containers-external-links-check",
      },
    ],
  },
  siteContainers: {
    l10nId: "containers-sites-card-header",
    headingLevel: 2,
    items: [
      {
        id: "site-containers-add-button",
        control: "moz-button",
        l10nId: "containers-sites-add-button",
      },
      {
        id: "site-containers-list",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
      },
    ],
  },
});
