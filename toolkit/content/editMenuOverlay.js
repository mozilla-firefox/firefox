/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// update menu items that rely on focus or on the current selection
function goUpdateGlobalEditMenuItems(force) {
  // Don't bother updating the edit commands if they aren't visible in any way
  // (i.e. the Edit menu isn't open, nor is the context menu open, nor have the
  // cut, copy, and paste buttons been added to the toolbars) for performance.
  // This only works in applications/on platforms that set the gEditUIVisible
  // flag, so we check to see if that flag is defined before using it.
  if (!force && typeof gEditUIVisible != "undefined" && !gEditUIVisible) {
    return;
  }

  goUpdateUndoEditMenuItems();
  goUpdateCommand("cmd_cut");
  goUpdateCommand("cmd_copy");
  goUpdatePasteMenuItems();
  goUpdateCommand("cmd_selectAll");
  goUpdateCommand("cmd_delete");
  goUpdateCommand("cmd_switchTextDirection");
}

// update menu items that relate to undo/redo
function goUpdateUndoEditMenuItems() {
  goUpdateCommand("cmd_undo");
  goUpdateCommand("cmd_redo");
}

// update menu items that depend on clipboard contents
function goUpdatePasteMenuItems() {
  goUpdateCommand("cmd_paste");
  goUpdateCommand("cmd_pasteNoFormatting");
}

// Inject the commandset here instead of relying on preprocessor to share this across documents.
window.addEventListener(
  "DOMContentLoaded",
  () => {
    let container =
      document.querySelector("commandset") || document.documentElement;
    let fragment = MozXULElement.parseXULToFragment(`
      <commandset id="editMenuCommands">
        <commandset id="editMenuCommandSetAll" commandupdater="true" events="focus,select" />
        <commandset id="editMenuCommandSetUndo" commandupdater="true" events="undo" />
        <commandset id="editMenuCommandSetPaste" commandupdater="true" events="clipboard" />
        <command id="cmd_undo" internal="true"/>
        <command id="cmd_redo" internal="true" />
        <command id="cmd_cut" internal="true" />
        <command id="cmd_copy" internal="true" />
        <command id="cmd_paste" internal="true" />
        <command id="cmd_pasteNoFormatting" internal="true" />
        <command id="cmd_delete" />
        <command id="cmd_selectAll" internal="true" />
        <command id="cmd_switchTextDirection" />
      </commandset>
    `);

    let editMenuCommandSetAll = fragment.querySelector(
      "#editMenuCommandSetAll"
    );
    editMenuCommandSetAll.addEventListener("commandupdate", function () {
      goUpdateGlobalEditMenuItems();
    });

    let editMenuCommandSetUndo = fragment.querySelector(
      "#editMenuCommandSetUndo"
    );
    editMenuCommandSetUndo.addEventListener("commandupdate", function () {
      goUpdateUndoEditMenuItems();
    });

    let editMenuCommandSetPaste = fragment.querySelector(
      "#editMenuCommandSetPaste"
    );
    editMenuCommandSetPaste.addEventListener("commandupdate", function () {
      goUpdatePasteMenuItems();
    });

    fragment.firstElementChild.addEventListener("command", event => {
      let commandID = event.target.id;
      goDoCommand(commandID);
    });

    container.appendChild(fragment);
  },
  { once: true }
);

// Support context menus on html textareas in the parent process:
window.addEventListener("contextmenu", e => {
  const HTML_NS = "http://www.w3.org/1999/xhtml";
  const XUL_NS =
    "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
  let target = e.composedTarget;
  let parent = target.parentNode;
  let needsContextMenu =
    target.ownerDocument == document &&
    !e.defaultPrevented &&
    !(parent.namespaceURI == XUL_NS && parent.localName == "moz-input-box") &&
    ["textarea", "input"].includes(target.localName) &&
    target.namespaceURI == HTML_NS;

  if (!needsContextMenu) {
    return;
  }

  let popup = document.getElementById("textbox-contextmenu");
  if (!popup) {
    MozXULElement.insertFTLIfNeeded("toolkit/global/textActions.ftl");
    document.documentElement.appendChild(
      MozXULElement.parseXULToFragment(`
      <menupopup id="textbox-contextmenu" class="textbox-contextmenu">
        <menuitem id="edit-contextmenu-undo" data-l10n-id="text-action-undo" command="cmd_undo"></menuitem>
        <menuitem id="edit-contextmenu-redo" data-l10n-id="text-action-redo" command="cmd_redo"></menuitem>
        <menuseparator></menuseparator>
        <menuitem id="edit-contextmenu-cut" data-l10n-id="text-action-cut" command="cmd_cut"></menuitem>
        <menuitem id="edit-contextmenu-copy" data-l10n-id="text-action-copy" command="cmd_copy"></menuitem>
        <menuitem id="edit-contextmenu-paste" data-l10n-id="text-action-paste" command="cmd_paste"></menuitem>
        <menuitem id="edit-contextmenu-delete" data-l10n-id="text-action-delete" command="cmd_delete"></menuitem>
        <menuitem id="edit-contextmenu-select-all" data-l10n-id="text-action-select-all" command="cmd_selectAll"></menuitem>
        <menuitem data-l10n-id="text-action-reveal-password" type="checkbox" id="edit-contextmenu-reveal-password" />
      </menupopup>
    `)
    );
    popup = document.documentElement.lastElementChild;
  }

  goUpdateGlobalEditMenuItems(true);
  const isPasswordInput =
    target.localName == "input" &&
    target.namespaceURI == HTML_NS &&
    target.type == "password";
  let revealPassword = popup.querySelector("#edit-contextmenu-reveal-password");
  // Reassigned on every contextmenu so it acts on the current target.
  revealPassword.oncommand = () => {
    target.revealPassword = !target.revealPassword;
  };
  revealPassword.hidden = !isPasswordInput;
  if (isPasswordInput) {
    if (target.revealPassword) {
      revealPassword.setAttribute("checked", "true");
    } else {
      revealPassword.removeAttribute("checked");
    }
  }
  popup.openPopupAtScreen(e.screenX, e.screenY, true, e);
  // Don't show any other context menu at the same time. There can be a
  // context menu from an ancestor too but we only want to show this one.
  e.preventDefault();
});
