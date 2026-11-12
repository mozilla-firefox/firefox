/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from controller.js */

// On Wayland when D&D source popup is closed,
// D&D operation is canceled by window manager.
function closingPopupEndsDrag(popup) {
  if (!popup.isWaylandPopup) {
    return false;
  }
  if (popup.isWaylandDragSource) {
    return true;
  }
  for (let childPopup of popup.querySelectorAll("menu > menupopup")) {
    if (childPopup.isWaylandDragSource) {
      return true;
    }
  }
  return false;
}

// This is loaded into all XUL windows. Wrap in a block to prevent
// leaking to window scope.
{
  /**
   * This class handles the custom element for the places popup menu.
   */
  class MozPlacesPopup extends MozElements.MozMenuPopup {
    constructor() {
      super();

      const event_names = [
        "DOMMenuItemActive",
        "DOMMenuItemInactive",
        "dragstart",
        "drop",
        "dragover",
        "dragleave",
        "dragend",
        "popuphidden",
      ];
      for (let event_name of event_names) {
        this.addEventListener(event_name, this);
      }
    }

    static get markup() {
      return `
      <html:link rel="stylesheet" href="chrome://global/skin/global.css" />
      <hbox part="drop-indicator-container">
        <vbox part="drop-indicator-bar" hidden="true">
          <image part="drop-indicator"/>
        </vbox>
        <arrowscrollbox class="menupopup-arrowscrollbox" flex="1" orient="vertical"
                        exportparts="scrollbox: arrowscrollbox-scrollbox"
                        smoothscroll="false" part="arrowscrollbox content">
          <html:slot/>
        </arrowscrollbox>
      </hbox>
    `;
    }

    connectedCallback() {
      if (this.delayConnectedCallback()) {
        return;
      }

      /**
       * Sub-menus should be opened when the mouse drags over them, and closed
       * when the mouse drags off.  The overFolder object manages opening and
       * closing of folders when the mouse hovers.
       */
      this._overFolder = {
        _self: this,
        _folder: {
          elt: null,
          openTimer: null,
          hoverTime: 350,
          closeTimer: null,
        },
        _closeMenuTimer: null,

        get elt() {
          return this._folder.elt;
        },
        set elt(val) {
          this._folder.elt = val;
        },

        get openTimer() {
          return this._folder.openTimer;
        },
        set openTimer(val) {
          this._folder.openTimer = val;
        },

        get hoverTime() {
          return this._folder.hoverTime;
        },
        set hoverTime(val) {
          this._folder.hoverTime = val;
        },

        get closeTimer() {
          return this._folder.closeTimer;
        },
        set closeTimer(val) {
          this._folder.closeTimer = val;
        },

        get closeMenuTimer() {
          return this._closeMenuTimer;
        },
        set closeMenuTimer(val) {
          this._closeMenuTimer = val;
        },

        setTimer: function OF__setTimer(aTime) {
          var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
          timer.initWithCallback(this, aTime, timer.TYPE_ONE_SHOT);
          return timer;
        },

        notify: function OF__notify(aTimer) {
          // Function to process all timer notifications.

          if (aTimer == this._folder.openTimer) {
            // Timer to open a submenu that's being dragged over.
            this._folder.elt.lastElementChild.setAttribute(
              "autoopened",
              "true"
            );
            this._folder.elt.lastElementChild.openPopup();
            this._folder.openTimer = null;
          } else if (aTimer == this._folder.closeTimer) {
            // Timer to close a submenu that's been dragged off of.
            // Only close the submenu if the mouse isn't being dragged over any
            // of its child menus.
            var draggingOverChild =
              PlacesControllerDragHelper.draggingOverChildNode(
                this._folder.elt
              );
            if (draggingOverChild) {
              this._folder.elt = null;
            }
            this.clear();

            // Close any parent folders which aren't being dragged over.
            // (This is necessary because of the above code that keeps a folder
            // open while its children are being dragged over.)
            if (!draggingOverChild && !closingPopupEndsDrag(this._self)) {
              this.closeParentMenus();
            }
          } else if (aTimer == this.closeMenuTimer) {
            // Timer to close this menu after the drag exit.
            var popup = this._self;
            // if we are no more dragging we can leave the menu open to allow
            // for better D&D bookmark organization
            var hidePopup =
              PlacesControllerDragHelper.getSession() &&
              !PlacesControllerDragHelper.draggingOverChildNode(
                popup.parentNode
              );
            if (hidePopup) {
              if (!closingPopupEndsDrag(popup)) {
                popup.hidePopup();
                // Close any parent menus that aren't being dragged over;
                // otherwise they'll stay open because they couldn't close
                // while this menu was being dragged over.
                this.closeParentMenus();
              } else if (popup.isWaylandDragSource) {
                // Postpone popup hide until drag end on Wayland.
                this._closeMenuTimer = this.setTimer(this.hoverTime);
              }
            }
          }
        },

        //  Helper function to close all parent menus of this menu,
        //  as long as none of the parent's children are currently being
        //  dragged over.
        closeParentMenus: function OF__closeParentMenus() {
          var popup = this._self;
          var parent = popup.parentNode;
          while (parent) {
            if (parent.localName == "menupopup" && parent._placesNode) {
              if (
                PlacesControllerDragHelper.draggingOverChildNode(
                  parent.parentNode
                )
              ) {
                break;
              }
              parent.hidePopup();
            }
            parent = parent.parentNode;
          }
        },

        //  The mouse is no longer dragging over the stored menubutton.
        //  Close the menubutton, clear out drag styles, and clear all
        //  timers for opening/closing it.
        clear: function OF__clear() {
          if (this._folder.elt && this._folder.elt.lastElementChild) {
            var popup = this._folder.elt.lastElementChild;
            if (
              !popup.hasAttribute("dragover") &&
              !closingPopupEndsDrag(popup)
            ) {
              popup.hidePopup();
            }
            // remove menuactive style
            this._folder.elt.removeAttribute("_moz-menuactive");
            this._folder.elt = null;
          }
          if (this._folder.openTimer) {
            this._folder.openTimer.cancel();
            this._folder.openTimer = null;
          }
          if (this._folder.closeTimer) {
            this._folder.closeTimer.cancel();
            this._folder.closeTimer = null;
          }
        },
      };
    }

    get _indicatorBar() {
      if (!this.__indicatorBar) {
        this.__indicatorBar = this.shadowRoot.querySelector(
          "[part=drop-indicator-bar]"
        );
      }
      return this.__indicatorBar;
    }

    /**
     * This is the view that manages the popup.
     *
     * @see {@link PlacesUIUtils.getViewForNode}
     * @returns {DOMNode}
     */
    get _rootView() {
      if (!this.__rootView) {
        this.__rootView = PlacesUIUtils.getViewForNode(this);
      }
      return this.__rootView;
    }

    /**
     * Check if we should hide the drop indicator for the target
     *
     * @param {object} aEvent
     *   The event associated with the drop.
     * @returns {boolean}
     */
    _hideDropIndicator(aEvent) {
      let target = aEvent.target;

      // Don't draw the drop indicator outside of markers or if current
      // node is not a Places node.
      let betweenMarkers =
        this._startMarker.compareDocumentPosition(target) &
          Node.DOCUMENT_POSITION_FOLLOWING &&
        this._endMarker.compareDocumentPosition(target) &
          Node.DOCUMENT_POSITION_PRECEDING;

      // Hide the dropmarker if current node is not a Places node.
      return !(target && target._placesNode && betweenMarkers);
    }

    /**
     * This function returns information about where to drop when
     * dragging over this popup insertion point
     *
     * @param {object} aEvent
     *   The event associated with the drop.
     * @returns {object|null}
     *   The associated drop point information.
     */
    _getDropPoint(aEvent) {
      // Can't drop if the menu isn't a folder
      let resultNode = this._placesNode;

      if (
        !PlacesUtils.nodeIsFolderOrShortcut(resultNode) ||
        this._rootView.controller.disallowInsertion(resultNode)
      ) {
        return null;
      }

      var dropPoint = { ip: null, folderElt: null };

      // The element we are dragging over
      let elt = aEvent.target;
      if (elt.localName == "menupopup") {
        elt = elt.parentNode;
      }

      let eventY = aEvent.clientY;
      let { y: eltY, height: eltHeight } = elt.getBoundingClientRect();

      if (!elt._placesNode) {
        // If we are dragging over a non places node drop at the end.
        dropPoint.ip = new PlacesInsertionPoint({
          parentGuid: PlacesUtils.getConcreteItemGuid(resultNode),
        });
        // We can set folderElt if we are dropping over a static menu that
        // has an internal placespopup.
        let isMenu =
          elt.localName == "menu" ||
          (elt.localName == "toolbarbutton" &&
            elt.getAttribute("type") == "menu");
        if (
          isMenu &&
          elt.lastElementChild &&
          elt.lastElementChild.hasAttribute("placespopup")
        ) {
          dropPoint.folderElt = elt;
        }
        return dropPoint;
      }

      let tagName = PlacesUtils.nodeIsTagQuery(elt._placesNode)
        ? elt._placesNode.title
        : null;
      if (
        (PlacesUtils.nodeIsFolderOrShortcut(elt._placesNode) &&
          !PlacesUIUtils.isFolderReadOnly(elt._placesNode)) ||
        PlacesUtils.nodeIsTagQuery(elt._placesNode)
      ) {
        // This is a folder or a tag container.
        if (eventY - eltY < eltHeight * 0.2) {
          // If mouse is in the top part of the element, drop above folder.
          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(resultNode),
            orientation: Ci.nsITreeView.DROP_BEFORE,
            tagName,
            dropNearNode: elt._placesNode,
          });
          return dropPoint;
        } else if (eventY - eltY < eltHeight * 0.8) {
          // If mouse is in the middle of the element, drop inside folder.
          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(elt._placesNode),
            tagName,
          });
          dropPoint.folderElt = elt;
          return dropPoint;
        }
      } else if (eventY - eltY <= eltHeight / 2) {
        // This is a non-folder node or a readonly folder.
        // If the mouse is above the middle, drop above this item.
        dropPoint.ip = new PlacesInsertionPoint({
          parentGuid: PlacesUtils.getConcreteItemGuid(resultNode),
          orientation: Ci.nsITreeView.DROP_BEFORE,
          tagName,
          dropNearNode: elt._placesNode,
        });
        return dropPoint;
      }

      // Drop below the item.
      dropPoint.ip = new PlacesInsertionPoint({
        parentGuid: PlacesUtils.getConcreteItemGuid(resultNode),
        orientation: Ci.nsITreeView.DROP_AFTER,
        tagName,
        dropNearNode: elt._placesNode,
      });
      return dropPoint;
    }

    _cleanupDragDetails() {
      // Called on dragend and drop.
      PlacesControllerDragHelper.currentDropTarget = null;
      this._rootView._draggedElt = null;
      this.removeAttribute("dragover");
      this.removeAttribute("dragstart");
      this._indicatorBar.hidden = true;
    }

    on_DOMMenuItemActive(event) {
      if (super.on_DOMMenuItemActive) {
        super.on_DOMMenuItemActive(event);
      }

      let elt = event.target;
      if (elt.parentNode != this) {
        return;
      }

      if (window.XULBrowserWindow) {
        let placesNode = elt._placesNode;

        var linkURI;
        if (placesNode && PlacesUtils.nodeIsURI(placesNode)) {
          linkURI = placesNode.uri;
        } else if (elt.hasAttribute("targetURI")) {
          linkURI = elt.getAttribute("targetURI");
        }

        if (linkURI) {
          window.XULBrowserWindow.setOverLink(linkURI);
        }
      }
    }

    on_DOMMenuItemInactive(event) {
      let elt = event.target;
      if (elt.parentNode != this) {
        return;
      }

      if (window.XULBrowserWindow) {
        window.XULBrowserWindow.setOverLink("");
      }
    }

    on_dragstart(event) {
      let elt = event.target;
      if (!elt._placesNode) {
        return;
      }

      let draggedElt = elt._placesNode;

      // Force a copy action if parent node is a query or we are dragging a
      // not-removable node.
      let canMove = this._rootView.controller.canMoveNode(draggedElt);
      if (!canMove) {
        event.dataTransfer.effectAllowed = "copyLink";
      }

      // Activate the view and cache the dragged element.
      this._rootView._draggedElt = draggedElt;
      this._rootView.controller.setDataTransfer(event);
      this.setAttribute("dragstart", "true");
      if (canMove) {
        // Keep the popup from shrinking while the dragged item moves out of
        // it, since open submenus stay where they were anchored.
        this.style.minWidth = this.getBoundingClientRect().width + "px";
        elt._dragSourcePopup = this;
      }
      event.stopPropagation();
    }

    on_drop(event) {
      PlacesControllerDragHelper.currentDropTarget = event.target;

      let dropPoint = this._getDropPoint(event);
      if (dropPoint && dropPoint.ip) {
        PlacesControllerDragHelper.onDrop(
          dropPoint.ip,
          event.dataTransfer
        ).catch(console.error);
        event.preventDefault();
      }

      this._cleanupDragDetails();
      event.stopPropagation();
    }

    on_dragover(event) {
      PlacesControllerDragHelper.currentDropTarget = event.target;
      let dt = event.dataTransfer;

      let dropPoint = this._getDropPoint(event);
      if (
        !dropPoint ||
        !dropPoint.ip ||
        !PlacesControllerDragHelper.canDrop(dropPoint.ip, dt)
      ) {
        this._indicatorBar.hidden = true;
        event.stopPropagation();
        return;
      }

      // Mark this popup as being dragged over.
      this.setAttribute("dragover", "true");

      if (dropPoint.folderElt) {
        // We are dragging over a folder.
        // _overFolder should take the care of opening it on a timer.
        if (
          this._overFolder.elt &&
          this._overFolder.elt != dropPoint.folderElt
        ) {
          // We are dragging over a new folder, let's clear old values
          this._overFolder.clear();
        }
        if (!this._overFolder.elt) {
          this._overFolder.elt = dropPoint.folderElt;
          // Create the timer to open this folder.
          this._overFolder.openTimer = this._overFolder.setTimer(
            this._overFolder.hoverTime
          );
        }
        // Since we are dropping into a folder set the corresponding style.
        dropPoint.folderElt.setAttribute("_moz-menuactive", true);
      } else {
        // We are not dragging over a folder.
        // Clear out old _overFolder information.
        this._overFolder.clear();
      }

      // Autoscroll the popup strip if we drag over the scroll buttons.
      let scrollDir = 0;
      if (event.originalTarget == this.scrollBox._scrollButtonUp) {
        scrollDir = -1;
      } else if (event.originalTarget == this.scrollBox._scrollButtonDown) {
        scrollDir = 1;
      }
      if (scrollDir != 0) {
        this.scrollBox.scrollByIndex(scrollDir, true);
      }

      // Check if we should hide the drop indicator for this target.
      if (dropPoint.folderElt || this._hideDropIndicator(event)) {
        this._indicatorBar.hidden = true;
        event.preventDefault();
        event.stopPropagation();
        return;
      }

      // We should display the drop indicator relative to the arrowscrollbox.
      let scrollRect = this.scrollBox.getBoundingClientRect();
      let newMarginTop = 0;
      if (scrollDir == 0) {
        let elt = this.firstElementChild;
        for (; elt; elt = elt.nextElementSibling) {
          let height = elt.getBoundingClientRect().height;
          if (height == 0) {
            continue;
          }
          if (event.screenY <= elt.screenY + height / 2) {
            break;
          }
        }
        newMarginTop = elt
          ? elt.screenY - this.scrollBox.screenY
          : scrollRect.height;
      } else if (scrollDir == 1) {
        newMarginTop = scrollRect.height;
      }

      // Set the new marginTop based on arrowscrollbox.
      newMarginTop +=
        scrollRect.y - this._indicatorBar.parentNode.getBoundingClientRect().y;
      this._indicatorBar.firstElementChild.style.marginTop =
        newMarginTop + "px";
      this._indicatorBar.hidden = false;

      event.preventDefault();
      event.stopPropagation();
    }

    on_dragleave(event) {
      PlacesControllerDragHelper.currentDropTarget = null;
      this.removeAttribute("dragover");

      // If we have not moved to a valid new target clear the drop indicator
      // this happens when moving out of the popup.
      let target = event.relatedTarget;
      if (!target || !this.contains(target)) {
        this._indicatorBar.hidden = true;
      }

      // Close any folder being hovered over
      if (this._overFolder.elt) {
        this._overFolder.closeTimer = this._overFolder.setTimer(
          this._overFolder.hoverTime
        );
      }

      // The autoopened attribute is set when this folder was automatically
      // opened after the user dragged over it.  If this attribute is set,
      // auto-close the folder on drag exit.
      // We should also try to close this popup if the drag has started
      // from here, the timer will check if we are dragging over a child.
      if (this.hasAttribute("autoopened") || this.hasAttribute("dragstart")) {
        this._overFolder.closeMenuTimer = this._overFolder.setTimer(
          this._overFolder.hoverTime
        );
      }

      event.stopPropagation();
    }

    on_popuphidden(event) {
      if (event.target == this) {
        this.style.removeProperty("min-width");
      }
    }

    on_dragend(event) {
      let elt = event.target;
      if (elt._dragSourcePopup) {
        elt._dragSourcePopup.style.removeProperty("min-width");
        delete elt._dragSourcePopup;
      }
      this._cleanupDragDetails();
    }

    uninit() {
      this.__rootView = null;
    }
  }

  customElements.define("places-popup", MozPlacesPopup, {
    extends: "menupopup",
  });

  /**
   * Custom element for the places popup arrow.
   */
  class MozPlacesPopupArrow extends MozPlacesPopup {
    constructor() {
      super();

      const event_names = [
        "popupshowing",
        "popuppositioned",
        "popupshown",
        "popuphiding",
        "popuphidden",
      ];
      for (let event_name of event_names) {
        this.addEventListener(event_name, this);
      }
    }

    connectedCallback() {
      if (this.delayConnectedCallback()) {
        return;
      }

      super.connectedCallback();
      this.initializeAttributeInheritance();

      this.setAttribute("flip", "both");
      this.setAttribute("side", "top");
      this.setAttribute("position", "bottomright topright");
    }

    _setSideAttribute(event) {
      if (!this.anchorNode) {
        return;
      }

      var position = event.alignmentPosition;
      if (position.indexOf("start_") == 0 || position.indexOf("end_") == 0) {
        // The assigned side stays the same regardless of direction.
        let isRTL = this.matches(":-moz-locale-dir(rtl)");

        if (position.indexOf("start_") == 0) {
          this.setAttribute("side", isRTL ? "left" : "right");
        } else {
          this.setAttribute("side", isRTL ? "right" : "left");
        }
      } else if (
        position.indexOf("before_") == 0 ||
        position.indexOf("after_") == 0
      ) {
        if (position.indexOf("before_") == 0) {
          this.setAttribute("side", "bottom");
        } else {
          this.setAttribute("side", "top");
        }
      }
    }

    on_popupshowing(event) {
      if (event.target == this) {
        this.setAttribute("animate", "open");
        this.style.pointerEvents = "none";
      }
    }

    on_popuppositioned(event) {
      if (event.target == this) {
        this._setSideAttribute(event);
      }
    }

    on_popupshown(event) {
      if (event.target != this) {
        return;
      }

      this.setAttribute("panelopen", "true");
      this.style.removeProperty("pointer-events");
    }

    on_popuphiding(event) {
      if (event.target == this) {
        this.setAttribute("animate", "cancel");
      }
    }

    on_popuphidden(event) {
      super.on_popuphidden(event);
      if (event.target == this) {
        this.removeAttribute("panelopen");
        this.removeAttribute("animate");
      }
    }
  }

  customElements.define("places-popup-arrow", MozPlacesPopupArrow, {
    extends: "menupopup",
  });
}
