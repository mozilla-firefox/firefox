/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { ContextualIdentityService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

// Stands in for a container icon we can't resolve.
const UNKNOWN_CONTAINER_ICON_URL = "chrome://global/skin/icons/help.svg";

/**
 * @typedef {object} ContainerDisplay
 *   How a container is presented in a permission row.
 * @property {?object} identity The identity as returned by
 *   ContextualIdentityService, null for a container we can't identify anymore.
 * @property {string} label The container name, or `#<userContextId>` for a
 *   container we can't identify anymore.
 * @property {?string} iconURL The URL of the container icon, null when the
 *   container can't be identified anymore or uses an unknown icon.
 */

/**
 * Helpers shared by the permission dialogs to list permissions that are
 * isolated per container (see permissions.isolateBy.userContext).
 */
class PermissionContainers {
  /**
   * Cache of container display data by userContextId to avoid repeated calls
   * into ContextualIdentityService.
   *
   * @type {Map<number, ContainerDisplay>}
   */
  #containers = new Map();

  /**
   * Returns whether a permission is isolated in a container
   *
   * @param {nsIPrincipal} principal
   * @returns {boolean}
   */
  isInContainer(principal) {
    return (
      principal.userContextId !==
      Services.scriptSecurityManager.DEFAULT_USER_CONTEXT_ID
    );
  }

  /**
   * How to present the container with the given id.
   *
   * @param {number} userContextId
   * @returns {ContainerDisplay}
   */
  #containerFor(userContextId) {
    let container = this.#containers.get(userContextId);
    if (!container) {
      let identity =
        ContextualIdentityService.getPublicIdentityFromId(userContextId) ??
        null;
      container = {
        identity,
        label: identity
          ? ContextualIdentityService.getUserContextLabel(userContextId)
          : `#${userContextId}`,
        iconURL:
          identity &&
          ContextualIdentityService.getContainerIconURL(identity.icon),
      };
      this.#containers.set(userContextId, container);
    }
    return container;
  }

  /**
   * The origin to show in the website column. It should only contain a
   * userContextId suffix if the permission is isolated in a container.
   * Show the complete origin if that is not the case.
   *
   * @param {nsIPrincipal} principal
   * @returns {string}
   */
  displayOriginFor(principal) {
    let containerSuffix = ChromeUtils.originAttributesToSuffix({
      userContextId: principal.userContextId,
    });
    return principal.originSuffix == containerSuffix
      ? principal.originNoSuffix
      : principal.origin;
  }

  /**
   * The key the container column is sorted and searched by, empty for the
   * default container. This is the container name, falling back to the bare
   * userContextId for a container we can't identify anymore
   *
   * @param {nsIPrincipal} principal
   * @returns {string}
   */
  labelFor(principal) {
    if (!this.isInContainer(principal)) {
      return "";
    }
    return this.#containerFor(principal.userContextId).label;
  }

  /**
   * Whether any of the given permissions is isolated in a container
   *
   * @param {Iterable<nsIPermission>} permissions
   * @returns {boolean}
   */
  anyInContainer(permissions) {
    for (let permission of permissions) {
      if (this.isInContainer(permission.principal)) {
        return true;
      }
    }
    return false;
  }

  /**
   * Comparator for sorting permission rows by the site or the container
   * column. Ties are broken on the other column, so that the rows of one
   * origin isolated in several containers stay grouped and ordered.
   *
   * @param {"siteCol"|"containerCol"} columnId The column to sort by.
   * @returns {function(Element, Element): number}
   */
  sortFunctionFor(columnId) {
    let [primary, secondary] =
      columnId == "containerCol"
        ? ["containerlabel", "displayorigin"]
        : ["displayorigin", "containerlabel"];
    let comp = new Services.intl.Collator(undefined, { usage: "sort" });
    return (a, b) =>
      comp.compare(a.getAttribute(primary), b.getAttribute(primary)) ||
      comp.compare(a.getAttribute(secondary), b.getAttribute(secondary));
  }

  /**
   * The container column cell of a permission row.
   *
   * @param {Document} doc Document to create the cell in.
   * @param {nsIPrincipal} principal The principal of the permission to create the cell for.
   * @returns {Element} An empty cell for the default container or a cell with
   *              the container icon and name for a permission isolated in a
   *              container.
   */
  createCell(doc, principal) {
    let cell = doc.createXULElement("hbox");
    cell.className = "website-container";
    if (!this.isInContainer(principal)) {
      return cell;
    }

    let { identity, label, iconURL } = this.#containerFor(
      principal.userContextId
    );

    let pill = doc.createXULElement("hbox");
    pill.className = identity
      ? `container-pill identity-color-${identity.color}`
      : "container-pill container-pill-unknown";

    let icon = doc.createXULElement("image");
    icon.className = "container-icon";
    icon.setAttribute("src", iconURL || UNKNOWN_CONTAINER_ICON_URL);
    // The name next to the icon already labels the container.
    icon.setAttribute("alt", "");

    let name = doc.createXULElement("label");
    name.className = "container-name";
    name.textContent = label;

    if (identity) {
      pill.setAttribute("tooltiptext", label);
    } else {
      doc.l10n.setAttributes(pill, "permissions-container-unknown");
    }

    pill.append(icon, name);
    cell.appendChild(pill);
    return cell;
  }
}

var gPermissionContainers = new PermissionContainers();
