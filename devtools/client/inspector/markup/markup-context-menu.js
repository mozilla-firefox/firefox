/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PSEUDO_CLASSES,
  ELEMENT_SPECIFIC_PSEUDO_CLASSES,
} = require("resource://devtools/shared/css/constants.js");
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");

loader.lazyRequireGetter(
  this,
  "Menu",
  "resource://devtools/client/framework/menu.js"
);
loader.lazyRequireGetter(
  this,
  "MenuItem",
  "resource://devtools/client/framework/menu-item.js"
);
loader.lazyRequireGetter(
  this,
  "clipboardHelper",
  "resource://devtools/shared/platform/clipboard.js"
);

loader.lazyGetter(this, "TOOLBOX_L10N", function () {
  return new LocalizationHelper("devtools/client/locales/toolbox.properties");
});

const INSPECTOR_L10N = new LocalizationHelper(
  "devtools/client/locales/inspector.properties"
);

/**
 * Context menu for the Markup view.
 */
class MarkupContextMenu {
  constructor(markup) {
    this.markup = markup;
    this.inspector = markup.inspector;
    this.selection = this.inspector.selection;
    this.target = this.inspector.currentTarget;
    this.telemetry = this.inspector.telemetry;
    this.toolbox = this.inspector.toolbox;
    this.walker = this.inspector.walker;
  }

  destroy() {
    this.markup = null;
    this.inspector = null;
    this.selection = null;
    this.target = null;
    this.telemetry = null;
    this.toolbox = null;
    this.walker = null;
  }

  show(event) {
    const inInput = event.composedTarget.matches(
      "input:is([type=text], [type=search], :not([type])), textarea"
    );
    if (inInput) {
      return;
    }

    event.stopPropagation();
    event.preventDefault();

    this.openMenu({
      screenX: event.screenX,
      screenY: event.screenY,
      target: event.target,
    });
  }

  /**
   * This method is here for the benefit of copying links.
   */
  #copyAttributeLink(link) {
    this.inspector.inspectorFront
      .resolveRelativeURL(link, this.selection.nodeFront)
      .then(url => {
        clipboardHelper.copyString(url);
      }, console.error);
  }

  /**
   * Copy the full CSS Path of the selected Node to the clipboard.
   */
  #copyCssPath() {
    if (!this.selection.isNode()) {
      return;
    }

    this.selection.nodeFront
      .getCssPath()
      .then(path => {
        clipboardHelper.copyString(path);
      })
      .catch(console.error);
  }

  /**
   * Copy the data-uri for the currently selected image in the clipboard.
   */
  #copyImageDataUri() {
    const container = this.markup.getContainer(this.selection.nodeFront);
    if (container && container.isPreviewable()) {
      container.copyImageDataUri();
    }
  }

  /**
   * Copy the innerHTML of the selected Node to the clipboard.
   */
  #copyInnerHTML() {
    this.markup.copyInnerHTML();
  }

  /**
   * Copy the outerHTML of the selected Node to the clipboard.
   */
  #copyOuterHTML() {
    this.markup.copyOuterHTML();
  }

  /**
   * Copy a unique selector of the selected Node to the clipboard.
   */
  #copyUniqueSelector() {
    if (!this.selection.isNode()) {
      return;
    }

    this.selection.nodeFront
      .getUniqueSelector()
      .then(selector => {
        clipboardHelper.copyString(selector);
      })
      .catch(console.error);
  }

  /**
   * Copy the XPath of the selected Node to the clipboard.
   */
  #copyXPath() {
    if (!this.selection.isNode()) {
      return;
    }

    this.selection.nodeFront
      .getXPath()
      .then(path => {
        clipboardHelper.copyString(path);
      })
      .catch(console.error);
  }

  /**
   * Delete the selected node.
   */
  #deleteNode() {
    if (!this.selection.isNode() || this.selection.isRoot()) {
      return;
    }

    const nodeFront = this.selection.nodeFront;

    // If the markup panel is active, use the markup panel to delete
    // the node, making this an undoable action.
    if (this.markup) {
      this.markup.deleteNode(nodeFront);
    } else {
      // remove the node from content
      nodeFront.walkerFront.removeNode(nodeFront);
    }
  }

  /**
   * Duplicate the selected node
   */
  #duplicateNode() {
    if (
      !this.selection.isElementNode() ||
      this.selection.isRoot() ||
      this.selection.isNativeAnonymousNode() ||
      this.selection.isPseudoElementNode()
    ) {
      return;
    }

    const nodeFront = this.selection.nodeFront;
    nodeFront.walkerFront.duplicateNode(nodeFront).catch(console.error);
  }

  /**
   * Edit the outerHTML of the selected Node.
   */
  #editHTML() {
    if (!this.selection.isNode()) {
      return;
    }
    this.markup.beginEditingHTML(this.selection.nodeFront);
  }

  /**
   * Jumps to the custom element definition in the debugger.
   */
  #jumpToCustomElementDefinition() {
    const { url, line, column } =
      this.selection.nodeFront.customElementLocation;
    this.toolbox.viewSourceInDebugger(
      url,
      line,
      column,
      null,
      "show_custom_element"
    );
  }

  /**
   * Add attribute to node.
   * Used for node context menu and shouldn't be called directly.
   */
  #onAddAttribute() {
    const container = this.markup.getContainer(this.selection.nodeFront);
    container.addAttribute();
  }

  /**
   * Copy attribute value for node.
   * Used for node context menu and shouldn't be called directly.
   */
  #onCopyAttributeValue() {
    clipboardHelper.copyString(this.nodeMenuTriggerInfo.value);
  }

  /**
   * This method is here for the benefit of the node-menu-link-copy menu item
   * in the inspector contextual-menu.
   */
  #onCopyLink() {
    this.#copyAttributeLink(this.contextMenuTarget.dataset.link);
  }

  /**
   * Edit attribute for node.
   * Used for node context menu and shouldn't be called directly.
   */
  #onEditAttribute() {
    const container = this.markup.getContainer(this.selection.nodeFront);
    container.editAttribute(this.nodeMenuTriggerInfo.name);
  }

  /**
   * This method is here for the benefit of the node-menu-link-follow menu item
   * in the inspector contextual-menu.
   */
  #onFollowLink() {
    const type = this.contextMenuTarget.dataset.type;
    const link = this.contextMenuTarget.dataset.link;
    this.markup.followAttributeLink(type, link);
  }

  /**
   * Remove attribute from node.
   * Used for node context menu and shouldn't be called directly.
   */
  #onRemoveAttribute() {
    const container = this.markup.getContainer(this.selection.nodeFront);
    container.removeAttribute(this.nodeMenuTriggerInfo.name);
  }

  /**
   * Paste the contents of the clipboard as adjacent HTML to the selected Node.
   *
   * @param  {string} position
   *         The position as specified for Element.insertAdjacentHTML
   *         (i.e. "beforeBegin", "afterBegin", "beforeEnd", "afterEnd").
   */
  #pasteAdjacentHTML(position) {
    const content = this.#getClipboardContentForPaste();
    if (!content) {
      return Promise.reject("No clipboard content for paste");
    }

    const node = this.selection.nodeFront;
    return this.markup.insertAdjacentHTMLToNode(node, position, content);
  }

  /**
   * Paste the contents of the clipboard into the selected Node's inner HTML.
   */
  #pasteInnerHTML() {
    const content = this.#getClipboardContentForPaste();
    if (!content) {
      return Promise.reject("No clipboard content for paste");
    }

    const node = this.selection.nodeFront;
    return this.markup.getNodeInnerHTML(node).then(oldContent => {
      this.markup.updateNodeInnerHTML(node, content, oldContent);
    });
  }

  /**
   * Paste the contents of the clipboard into the selected Node's outer HTML.
   */
  #pasteOuterHTML() {
    const content = this.#getClipboardContentForPaste();
    if (!content) {
      return Promise.reject("No clipboard content for paste");
    }

    const node = this.selection.nodeFront;
    return this.markup.getNodeOuterHTML(node).then(oldContent => {
      this.markup.updateNodeOuterHTML(node, content, oldContent);
    });
  }

  /**
   * Show Accessibility properties for currently selected node
   */
  async #showAccessibilityProperties() {
    const a11yPanel = await this.toolbox.selectTool("accessibility");
    // Select the accessible object in the panel and wait for the event that
    // tells us it has been done.
    const onSelected = a11yPanel.once("new-accessible-front-selected");
    a11yPanel.selectAccessibleForNode(
      this.selection.nodeFront,
      "inspector-context-menu"
    );
    await onSelected;
  }

  /**
   * Show DOM properties
   */
  #showDOMProperties() {
    this.toolbox.openSplitConsole().then(() => {
      const { hud } = this.toolbox.getPanel("webconsole");
      hud.ui.wrapper.dispatchEvaluateExpression("inspect($0, true)");
    });
  }

  /**
   * Use in Console.
   *
   * Takes the currently selected node in the inspector and assigns it to a
   * temp variable on the content window.  Also opens the split console and
   * autofills it with the temp variable.
   */
  async #useInConsole() {
    await this.toolbox.openSplitConsole();
    const { hud } = this.toolbox.getPanel("webconsole");

    const evalString = `{ let i = 0;
      while (window.hasOwnProperty("temp" + i) && i < 1000) {
        i++;
      }
      window["temp" + i] = $0;
      "temp" + i;
    }`;

    const res = await this.toolbox.commands.scriptCommand.execute(evalString, {
      selectedNodeActor: this.selection.nodeFront.actorID,
      // Prevent any type of breakpoint when evaluating this code
      disableBreaks: true,
      // Ensure always overriding "$0" console command, even if the page implements its own "$0" variable.
      preferConsoleCommandsOverLocalSymbols: true,
    });
    hud.setInputValue(res.result);
    this.inspector.emit("console-var-ready");
  }

  #getAttributesSubmenu(isEditableElement) {
    const attributesSubmenu = new Menu();
    const nodeInfo = this.nodeMenuTriggerInfo;
    const isAttributeClicked =
      isEditableElement && nodeInfo && nodeInfo.type === "attribute";

    attributesSubmenu.append(
      new MenuItem({
        id: "node-menu-add-attribute",
        label: INSPECTOR_L10N.getStr("inspectorAddAttribute.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorAddAttribute.accesskey"),
        disabled: !isEditableElement,
        click: () => this.#onAddAttribute(),
      })
    );
    attributesSubmenu.append(
      new MenuItem({
        id: "node-menu-copy-attribute",
        label: INSPECTOR_L10N.getFormatStr(
          "inspectorCopyAttributeValue.label",
          isAttributeClicked ? `${nodeInfo.value}` : ""
        ),
        accesskey: INSPECTOR_L10N.getStr(
          "inspectorCopyAttributeValue.accesskey"
        ),
        disabled: !isAttributeClicked,
        click: () => this.#onCopyAttributeValue(),
      })
    );
    attributesSubmenu.append(
      new MenuItem({
        id: "node-menu-edit-attribute",
        label: INSPECTOR_L10N.getFormatStr(
          "inspectorEditAttribute.label",
          isAttributeClicked ? `${nodeInfo.name}` : ""
        ),
        accesskey: INSPECTOR_L10N.getStr("inspectorEditAttribute.accesskey"),
        disabled: !isAttributeClicked,
        click: () => this.#onEditAttribute(),
      })
    );
    attributesSubmenu.append(
      new MenuItem({
        id: "node-menu-remove-attribute",
        label: INSPECTOR_L10N.getFormatStr(
          "inspectorRemoveAttribute.label",
          isAttributeClicked ? `${nodeInfo.name}` : ""
        ),
        accesskey: INSPECTOR_L10N.getStr("inspectorRemoveAttribute.accesskey"),
        disabled: !isAttributeClicked,
        click: () => this.#onRemoveAttribute(),
      })
    );

    return attributesSubmenu;
  }

  /**
   * Returns the clipboard content if it is appropriate for pasting
   * into the current node's outer HTML, otherwise returns null.
   */
  #getClipboardContentForPaste() {
    const content = clipboardHelper.getText();
    if (content && content.trim().length) {
      return content;
    }
    return null;
  }

  #getCopySubmenu(markupContainer, isElement, isFragment) {
    const copySubmenu = new Menu();
    copySubmenu.append(
      new MenuItem({
        id: "node-menu-copyinner",
        label: INSPECTOR_L10N.getStr("inspectorCopyInnerHTML.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorCopyInnerHTML.accesskey"),
        disabled: !isElement && !isFragment,
        click: () => this.#copyInnerHTML(),
      })
    );
    copySubmenu.append(
      new MenuItem({
        id: "node-menu-copyouter",
        label: INSPECTOR_L10N.getStr("inspectorCopyOuterHTML.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorCopyOuterHTML.accesskey"),
        disabled: !isElement,
        click: () => this.#copyOuterHTML(),
      })
    );
    copySubmenu.append(
      new MenuItem({
        id: "node-menu-copyuniqueselector",
        label: INSPECTOR_L10N.getStr("inspectorCopyCSSSelector.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorCopyCSSSelector.accesskey"),
        disabled: !isElement,
        click: () => this.#copyUniqueSelector(),
      })
    );
    copySubmenu.append(
      new MenuItem({
        id: "node-menu-copycsspath",
        label: INSPECTOR_L10N.getStr("inspectorCopyCSSPath.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorCopyCSSPath.accesskey"),
        disabled: !isElement,
        click: () => this.#copyCssPath(),
      })
    );
    copySubmenu.append(
      new MenuItem({
        id: "node-menu-copyxpath",
        label: INSPECTOR_L10N.getStr("inspectorCopyXPath.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorCopyXPath.accesskey"),
        disabled: !isElement,
        click: () => this.#copyXPath(),
      })
    );
    copySubmenu.append(
      new MenuItem({
        id: "node-menu-copyimagedatauri",
        label: INSPECTOR_L10N.getStr("inspectorImageDataUri.label"),
        disabled:
          !isElement || !markupContainer || !markupContainer.isPreviewable(),
        click: () => this.#copyImageDataUri(),
      })
    );

    return copySubmenu;
  }

  #getDOMBreakpointSubmenu(isElement) {
    const menu = new Menu();
    const mutationBreakpoints = this.selection.nodeFront.mutationBreakpoints;

    menu.append(
      new MenuItem({
        id: "node-menu-mutation-breakpoint-subtree",
        checked: mutationBreakpoints.subtree,
        click: () => this.markup.toggleMutationBreakpoint("subtree"),
        disabled: !isElement,
        label: INSPECTOR_L10N.getStr("inspectorSubtreeModification.label"),
        type: "checkbox",
      })
    );

    menu.append(
      new MenuItem({
        id: "node-menu-mutation-breakpoint-attribute",
        checked: mutationBreakpoints.attribute,
        click: () => this.markup.toggleMutationBreakpoint("attribute"),
        disabled: !isElement,
        label: INSPECTOR_L10N.getStr("inspectorAttributeModification.label"),
        type: "checkbox",
      })
    );

    menu.append(
      new MenuItem({
        checked: mutationBreakpoints.removal,
        click: () => this.markup.toggleMutationBreakpoint("removal"),
        disabled: !isElement,
        label: INSPECTOR_L10N.getStr("inspectorNodeRemoval.label"),
        type: "checkbox",
      })
    );

    return menu;
  }

  /**
   * Link menu items can be shown or hidden depending on the context and
   * selected node, and their labels can vary.
   *
   * @return {Array} list of visible menu items related to links.
   */
  #getNodeLinkMenuItems() {
    const linkFollow = new MenuItem({
      id: "node-menu-link-follow",
      visible: false,
      click: () => this.#onFollowLink(),
    });
    const linkCopy = new MenuItem({
      id: "node-menu-link-copy",
      visible: false,
      click: () => this.#onCopyLink(),
    });

    // Get information about the right-clicked node.
    const popupNode = this.contextMenuTarget;
    if (!popupNode || !popupNode.classList.contains("link")) {
      return [linkFollow, linkCopy];
    }

    const type = popupNode.dataset.type;
    if (type === "uri" || type === "cssresource" || type === "jsresource") {
      // Links can't be opened in new tabs in the browser toolbox.
      if (type === "uri" && !this.toolbox.isBrowserToolbox) {
        linkFollow.visible = true;
        linkFollow.label = INSPECTOR_L10N.getStr(
          "inspector.menu.openUrlInNewTab.label"
        );
      } else if (type === "cssresource") {
        linkFollow.visible = true;
        linkFollow.label = TOOLBOX_L10N.getStr(
          "toolbox.viewCssSourceInStyleEditor.label"
        );
      } else if (type === "jsresource") {
        linkFollow.visible = true;
        linkFollow.label = TOOLBOX_L10N.getStr(
          "toolbox.viewJsSourceInDebugger.label"
        );
      }

      linkCopy.visible = true;
      linkCopy.label = INSPECTOR_L10N.getStr(
        "inspector.menu.copyUrlToClipboard.label"
      );
    } else if (type === "idref") {
      linkFollow.visible = true;
      linkFollow.label = INSPECTOR_L10N.getFormatStr(
        "inspector.menu.selectElement.label",
        popupNode.dataset.link
      );
    }

    return [linkFollow, linkCopy];
  }

  #getPasteSubmenu(isElement, isFragment, isAnonymous) {
    const isPasteable =
      !isAnonymous &&
      (isFragment || isElement) &&
      this.#getClipboardContentForPaste();
    const disableAdjacentPaste =
      !isPasteable ||
      !isElement ||
      this.selection.isRoot() ||
      this.selection.isBodyNode() ||
      this.selection.isHeadNode();
    const disableFirstLastPaste =
      !isPasteable ||
      !isElement ||
      (this.selection.isHTMLNode() && this.selection.isRoot());

    const pasteSubmenu = new Menu();
    pasteSubmenu.append(
      new MenuItem({
        id: "node-menu-pasteinnerhtml",
        label: INSPECTOR_L10N.getStr("inspectorPasteInnerHTML.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorPasteInnerHTML.accesskey"),
        disabled: !isPasteable,
        click: () => this.#pasteInnerHTML(),
      })
    );
    pasteSubmenu.append(
      new MenuItem({
        id: "node-menu-pasteouterhtml",
        label: INSPECTOR_L10N.getStr("inspectorPasteOuterHTML.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorPasteOuterHTML.accesskey"),
        disabled: !isPasteable || !isElement,
        click: () => this.#pasteOuterHTML(),
      })
    );
    pasteSubmenu.append(
      new MenuItem({
        id: "node-menu-pastebefore",
        label: INSPECTOR_L10N.getStr("inspectorHTMLPasteBefore.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorHTMLPasteBefore.accesskey"),
        disabled: disableAdjacentPaste,
        click: () => this.#pasteAdjacentHTML("beforeBegin"),
      })
    );
    pasteSubmenu.append(
      new MenuItem({
        id: "node-menu-pasteafter",
        label: INSPECTOR_L10N.getStr("inspectorHTMLPasteAfter.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorHTMLPasteAfter.accesskey"),
        disabled: disableAdjacentPaste,
        click: () => this.#pasteAdjacentHTML("afterEnd"),
      })
    );
    pasteSubmenu.append(
      new MenuItem({
        id: "node-menu-pastefirstchild",
        label: INSPECTOR_L10N.getStr("inspectorHTMLPasteFirstChild.label"),
        accesskey: INSPECTOR_L10N.getStr(
          "inspectorHTMLPasteFirstChild.accesskey"
        ),
        disabled: disableFirstLastPaste,
        click: () => this.#pasteAdjacentHTML("afterBegin"),
      })
    );
    pasteSubmenu.append(
      new MenuItem({
        id: "node-menu-pastelastchild",
        label: INSPECTOR_L10N.getStr("inspectorHTMLPasteLastChild.label"),
        accesskey: INSPECTOR_L10N.getStr(
          "inspectorHTMLPasteLastChild.accesskey"
        ),
        disabled: disableFirstLastPaste,
        click: () => this.#pasteAdjacentHTML("beforeEnd"),
      })
    );

    return pasteSubmenu;
  }

  #createPseudoClassMenuItem(pseudoClass, enabled) {
    const suffix = pseudoClass.substring(1);
    const menuitem = new MenuItem({
      id: "node-menu-pseudo-" + suffix,
      label: suffix,
      type: "checkbox",
      click: () => this.inspector.togglePseudoClass(pseudoClass),
    });

    if (enabled) {
      const checked = this.selection.nodeFront.hasPseudoClassLock(pseudoClass);
      menuitem.checked = checked;
    } else {
      menuitem.disabled = true;
    }

    return menuitem;
  }

  #getPseudoClassSubmenu() {
    const menu = new Menu();
    const enabled = this.inspector.canTogglePseudoClassForSelectedNode();

    // Set the pseudo classes
    for (const name of PSEUDO_CLASSES) {
      menu.append(this.#createPseudoClassMenuItem(name, enabled));
    }

    const tagName = this.selection.nodeFront.tagName?.toLowerCase();
    for (const [pseudo, elementTypes] of Object.entries(
      ELEMENT_SPECIFIC_PSEUDO_CLASSES
    )) {
      if (elementTypes.has(tagName)) {
        menu.append(this.#createPseudoClassMenuItem(pseudo, enabled));
      }
    }

    return menu;
  }

  #getEditMarkupString() {
    if (this.selection.isHTMLNode()) {
      return "inspectorHTMLEdit";
    } else if (this.selection.isSVGNode()) {
      return "inspectorSVGEdit";
    } else if (this.selection.isMathMLNode()) {
      return "inspectorMathMLEdit";
    }
    return "inspectorXMLEdit";
  }

  openMenu({ target, screenX = 0, screenY = 0 } = {}) {
    if (this.selection.isSlotted()) {
      // Slotted elements should not show any context menu.
      return null;
    }

    const markupContainer = this.markup.getContainer(this.selection.nodeFront);

    this.contextMenuTarget = target;
    this.nodeMenuTriggerInfo =
      markupContainer && markupContainer.editor.getInfoAtNode(target);

    const isFragment = this.selection.isDocumentFragmentNode();
    const isAnonymous = this.selection.isNativeAnonymousNode();
    const isElement =
      this.selection.isElementNode() && !this.selection.isPseudoElementNode();
    const isDuplicatableElement =
      isElement && !isAnonymous && !this.selection.isRoot();
    const isScreenshotable =
      isElement && this.selection.nodeFront.isTreeDisplayed;

    const menu = new Menu({ id: "markup-context-menu" });
    menu.append(
      new MenuItem({
        id: "node-menu-edithtml",
        label: INSPECTOR_L10N.getStr(`${this.#getEditMarkupString()}.label`),
        accesskey: INSPECTOR_L10N.getStr("inspectorHTMLEdit.accesskey"),
        disabled: !this.markup.canEditSelectedNodeHTML(),
        click: () => this.#editHTML(),
      })
    );
    menu.append(
      new MenuItem({
        id: "node-menu-add",
        label: INSPECTOR_L10N.getStr("inspectorAddNode.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorAddNode.accesskey"),
        disabled: !this.inspector.canAddHTMLChild(),
        click: () => this.inspector.addNode(),
      })
    );
    menu.append(
      new MenuItem({
        id: "node-menu-duplicatenode",
        label: INSPECTOR_L10N.getStr("inspectorDuplicateNode.label"),
        disabled: !isDuplicatableElement,
        click: () => this.#duplicateNode(),
      })
    );
    menu.append(
      new MenuItem({
        id: "node-menu-delete",
        label: INSPECTOR_L10N.getStr("inspectorHTMLDelete.label"),
        accesskey: INSPECTOR_L10N.getStr("inspectorHTMLDelete.accesskey"),
        disabled: !this.markup.isDeletable(this.selection.nodeFront),
        click: () => this.#deleteNode(),
      })
    );

    menu.append(
      new MenuItem({
        label: INSPECTOR_L10N.getStr("inspectorAttributesSubmenu.label"),
        accesskey: INSPECTOR_L10N.getStr(
          "inspectorAttributesSubmenu.accesskey"
        ),
        submenu: this.#getAttributesSubmenu(isElement && !isAnonymous),
      })
    );

    menu.append(
      new MenuItem({
        type: "separator",
      })
    );

    if (this.selection.nodeFront.mutationBreakpoints) {
      menu.append(
        new MenuItem({
          label: INSPECTOR_L10N.getStr("inspectorBreakpointSubmenu.label"),
          // FIXME(bug 1598952): This doesn't work in shadow trees at all, but
          // we still display the active menu. Also, this should probably be
          // enabled for ShadowRoot, at least the non-attribute breakpoints.
          submenu: this.#getDOMBreakpointSubmenu(isElement),
          id: "node-menu-mutation-breakpoint",
        })
      );
    }

    menu.append(
      new MenuItem({
        id: "node-menu-useinconsole",
        label: INSPECTOR_L10N.getStr("inspectorUseInConsole.label"),
        click: () => this.#useInConsole(),
      })
    );

    menu.append(
      new MenuItem({
        id: "node-menu-showdomproperties",
        label: INSPECTOR_L10N.getStr("inspectorShowDOMProperties.label"),
        click: () => this.#showDOMProperties(),
      })
    );

    if (this.selection.isElementNode() || this.selection.isTextNode()) {
      menu.append(
        new MenuItem({
          id: "node-menu-showaccessibilityproperties",
          label: INSPECTOR_L10N.getStr(
            "inspectorShowAccessibilityProperties.label"
          ),
          click: () => this.#showAccessibilityProperties(),
        })
      );
    }

    if (this.selection.nodeFront.customElementLocation) {
      menu.append(
        new MenuItem({
          id: "node-menu-jumptodefinition",
          label: INSPECTOR_L10N.getStr(
            "inspectorCustomElementDefinition.label"
          ),
          click: () => this.#jumpToCustomElementDefinition(),
        })
      );
    }

    menu.append(
      new MenuItem({
        type: "separator",
      })
    );

    menu.append(
      new MenuItem({
        label: INSPECTOR_L10N.getStr("inspectorPseudoClassSubmenu.label"),
        submenu: this.#getPseudoClassSubmenu(),
      })
    );

    menu.append(
      new MenuItem({
        id: "node-menu-screenshotnode",
        label: INSPECTOR_L10N.getStr("inspectorScreenshotNode.label"),
        disabled: !isScreenshotable,
        click: () => this.inspector.screenshotNode().catch(console.error),
      })
    );

    menu.append(
      new MenuItem({
        id: "node-menu-scrollnodeintoview",
        label: INSPECTOR_L10N.getStr("inspectorScrollNodeIntoView.label"),
        accesskey: INSPECTOR_L10N.getStr(
          "inspectorScrollNodeIntoView.accesskey"
        ),
        disabled: !this.inspector.selection.supportsScrollIntoView(),
        click: () => this.markup.scrollNodeIntoView(),
      })
    );

    menu.append(
      new MenuItem({
        type: "separator",
      })
    );

    menu.append(
      new MenuItem({
        label: INSPECTOR_L10N.getStr("inspectorCopyHTMLSubmenu.label"),
        submenu: this.#getCopySubmenu(markupContainer, isElement, isFragment),
      })
    );

    menu.append(
      new MenuItem({
        label: INSPECTOR_L10N.getStr("inspectorPasteHTMLSubmenu.label"),
        submenu: this.#getPasteSubmenu(isElement, isFragment, isAnonymous),
      })
    );

    menu.append(
      new MenuItem({
        type: "separator",
      })
    );

    const isNodeWithChildren =
      this.selection.isNode() && markupContainer.hasChildren;
    menu.append(
      new MenuItem({
        id: "node-menu-expand",
        label: INSPECTOR_L10N.getStr("inspectorExpandNode.label"),
        disabled: !isNodeWithChildren,
        click: () => this.markup.expandAll(this.selection.nodeFront),
      })
    );
    menu.append(
      new MenuItem({
        id: "node-menu-collapse",
        label: INSPECTOR_L10N.getStr("inspectorCollapseAll.label"),
        disabled: !isNodeWithChildren || !markupContainer.expanded,
        click: () => this.markup.collapseAll(this.selection.nodeFront),
      })
    );

    const nodeLinkMenuItems = this.#getNodeLinkMenuItems();
    if (nodeLinkMenuItems.filter(item => item.visible).length) {
      menu.append(
        new MenuItem({
          id: "node-menu-link-separator",
          type: "separator",
        })
      );
    }

    for (const menuitem of nodeLinkMenuItems) {
      menu.append(menuitem);
    }

    menu.popup(screenX, screenY, this.toolbox.doc);
    return menu;
  }
}

module.exports = MarkupContextMenu;
