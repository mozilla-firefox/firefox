export class ReftestFissionChild extends JSWindowActorChild {
  forwardAfterPaintEventToParent(
    rects,
    originalTargetUri,
    dispatchToSelfAsWell
  ) {
    if (dispatchToSelfAsWell && this.contentWindow) {
      let event = new this.contentWindow.CustomEvent(
        "Reftest:MozAfterPaintFromChild",
        { bubbles: true, detail: { rects, originalTargetUri } }
      );
      this.contentWindow.dispatchEvent(event);
    }

    let parentContext = this.browsingContext.parent;
    if (parentContext) {
      try {
        this.sendAsyncMessage("ForwardAfterPaintEvent", {
          toBrowsingContext: parentContext,
          fromBrowsingContext: this.browsingContext,
          rects,
          originalTargetUri,
        });
      } catch (e) {
        // |this| can be destroyed here and unable to send messages, which is
        // not a problem, the reftest harness probably torn down the page and
        // moved on to the next test.
        console.error(e);
      }
    }
  }

  handleEvent(evt) {
    switch (evt.type) {
      case "MozAfterPaint":
        // We want to forward any after paint events to our parent document so that
        // that it reaches the root content document where the main reftest harness
        // code (reftest-content.js) will process it and update the canvas.
        // Invalidations in a document whose parent is in the same process
        // already cause invalidations in the process root document, so only
        // process roots need to forward.
        if (!this.manager.isProcessRoot) {
          break;
        }
        var rects = [];
        for (let r of evt.clientRects) {
          rects.push({
            left: r.left,
            top: r.top,
            right: r.right,
            bottom: r.bottom,
          });
        }
        this.forwardAfterPaintEventToParent(
          rects,
          this.document.documentURI,
          /* dispatchToSelfAsWell */ false
        );
        break;
    }
  }

  transformRect(transform, rect) {
    let p1 = transform.transformPoint({ x: rect.left, y: rect.top });
    let p2 = transform.transformPoint({ x: rect.right, y: rect.top });
    let p3 = transform.transformPoint({ x: rect.left, y: rect.bottom });
    let p4 = transform.transformPoint({ x: rect.right, y: rect.bottom });
    let quad = new DOMQuad(p1, p2, p3, p4);
    return quad.getBounds();
  }

  SetupDisplayportRoot() {
    let returnStrings = { infoStrings: [], errorStrings: [] };

    let contentRootElement = this.contentWindow.document.documentElement;
    if (!contentRootElement) {
      return Promise.resolve(returnStrings);
    }

    // If we don't have the reftest-async-scroll attribute we only look at
    // the root element for potential display ports to set.
    if (!contentRootElement.hasAttribute("reftest-async-scroll")) {
      let winUtils = this.contentWindow.windowUtils;
      this.setupDisplayportForElement(
        contentRootElement,
        winUtils,
        returnStrings
      );
      return Promise.resolve(returnStrings);
    }

    // Send a msg to the parent side to get the parent side to tell all
    // process roots to do the displayport setting.
    let browsingContext = this.browsingContext;
    let promise = this.sendQuery("TellChildrenToSetupDisplayport", {
      browsingContext,
    });
    return promise.then(
      function (result) {
        for (let errorString of result.errorStrings) {
          returnStrings.errorStrings.push(errorString);
        }
        for (let infoString of result.infoStrings) {
          returnStrings.infoStrings.push(infoString);
        }
        return returnStrings;
      },
      function (reason) {
        returnStrings.errorStrings.push(
          "SetupDisplayport SendQuery to parent promise rejected: " + reason
        );
        return returnStrings;
      }
    );
  }

  attrOrDefault(element, attr, def) {
    return element.hasAttribute(attr)
      ? Number(element.getAttribute(attr))
      : def;
  }

  setupDisplayportForElement(element, winUtils, returnStrings) {
    var dpw = this.attrOrDefault(element, "reftest-displayport-w", 0);
    var dph = this.attrOrDefault(element, "reftest-displayport-h", 0);
    var dpx = this.attrOrDefault(element, "reftest-displayport-x", 0);
    var dpy = this.attrOrDefault(element, "reftest-displayport-y", 0);
    if (dpw !== 0 || dph !== 0 || dpx != 0 || dpy != 0) {
      returnStrings.infoStrings.push(
        "Setting displayport to <x=" +
          dpx +
          ", y=" +
          dpy +
          ", w=" +
          dpw +
          ", h=" +
          dph +
          ">"
      );
      winUtils.setDisplayPortForElement(dpx, dpy, dpw, dph, element, 1);
    }
  }

  setupDisplayportForElementSubtree(element, winUtils, returnStrings) {
    this.setupDisplayportForElement(element, winUtils, returnStrings);
    for (let c = element.firstElementChild; c; c = c.nextElementSibling) {
      this.setupDisplayportForElementSubtree(c, winUtils, returnStrings);
    }
    if (
      typeof element.contentDocument !== "undefined" &&
      element.contentDocument
    ) {
      returnStrings.infoStrings.push(
        "setupDisplayportForElementSubtree descending into subdocument"
      );
      this.setupDisplayportForElementSubtree(
        element.contentDocument.documentElement,
        element.contentWindow.windowUtils,
        returnStrings
      );
    }
  }

  setupAsyncScrollOffsetsForElement(
    element,
    winUtils,
    allowFailure,
    returnStrings
  ) {
    let sx = this.attrOrDefault(element, "reftest-async-scroll-x", 0);
    let sy = this.attrOrDefault(element, "reftest-async-scroll-y", 0);
    if (sx != 0 || sy != 0) {
      try {
        // This might fail when called from RecordResult since layers
        // may not have been constructed yet
        winUtils.setAsyncScrollOffset(element, sx, sy);
        return true;
      } catch (e) {
        if (allowFailure) {
          returnStrings.infoStrings.push(
            "setupAsyncScrollOffsetsForElement error calling setAsyncScrollOffset: " +
              e
          );
        } else {
          returnStrings.errorStrings.push(
            "setupAsyncScrollOffsetsForElement error calling setAsyncScrollOffset: " +
              e
          );
        }
      }
    }
    return false;
  }

  setupAsyncScrollOffsetsForElementSubtree(
    element,
    winUtils,
    allowFailure,
    returnStrings
  ) {
    let updatedAny = this.setupAsyncScrollOffsetsForElement(
      element,
      winUtils,
      returnStrings
    );
    for (let c = element.firstElementChild; c; c = c.nextElementSibling) {
      if (
        this.setupAsyncScrollOffsetsForElementSubtree(
          c,
          winUtils,
          allowFailure,
          returnStrings
        )
      ) {
        updatedAny = true;
      }
    }
    if (
      typeof element.contentDocument !== "undefined" &&
      element.contentDocument
    ) {
      returnStrings.infoStrings.push(
        "setupAsyncScrollOffsetsForElementSubtree Descending into subdocument"
      );
      if (
        this.setupAsyncScrollOffsetsForElementSubtree(
          element.contentDocument.documentElement,
          element.contentWindow.windowUtils,
          allowFailure,
          returnStrings
        )
      ) {
        updatedAny = true;
      }
    }
    return updatedAny;
  }

  async receiveMessage(msg) {
    switch (msg.name) {
      case "ForwardAfterPaintEventToSelfAndParent": {
        // The embedderElement can be null if the child we got this from was removed.
        // Not much we can do to transform the rects, but it doesn't matter, the rects
        // won't reach reftest-content.js.
        if (msg.data.fromBrowsingContext.embedderElement == null) {
          this.forwardAfterPaintEventToParent(
            msg.data.rects,
            msg.data.originalTargetUri,
            /* dispatchToSelfAsWell */ true
          );
          return undefined;
        }

        let translate = new DOMMatrixReadOnly().translate(0, 0);
        if (this.contentWindow) {
          // Transform the rects from fromBrowsingContext to us.
          // We first translate from the content rect to the border rect of the iframe.
          let style = this.contentWindow.getComputedStyle(
            msg.data.fromBrowsingContext.embedderElement
          );
          translate = new DOMMatrixReadOnly().translate(
            parseFloat(style.paddingLeft) + parseFloat(style.borderLeftWidth),
            parseFloat(style.paddingTop) + parseFloat(style.borderTopWidth)
          );
        }

        // Then we transform from the iframe to our root frame.
        // We are guaranteed to be the process with the embedderElement for fromBrowsingContext.
        let transform =
          msg.data.fromBrowsingContext.embedderElement.getTransformToViewport();
        let combined = translate.multiply(transform);

        let newrects = msg.data.rects.map(r => this.transformRect(combined, r));

        this.forwardAfterPaintEventToParent(
          newrects,
          msg.data.originalTargetUri,
          /* dispatchToSelfAsWell */ true
        );
        break;
      }

      case "EmptyMessage":
        return undefined;
      case "UpdateLayerTree": {
        let errorStrings = [];
        try {
          if (this.manager.isProcessRoot) {
            this.contentWindow.windowUtils.updateLayerTree();
          }
        } catch (e) {
          errorStrings.push("updateLayerTree failed: " + e);
        }
        return { errorStrings };
      }
      case "FlushRendering": {
        let errorStrings = [];
        let warningStrings = [];
        let infoStrings = [];

        try {
          let { ignoreThrottledAnimations, needsAnimationFrame } = msg.data;

          if (this.manager.isProcessRoot) {
            var anyPendingPaintsGeneratedInDescendants = false;

            if (this.contentWindow && needsAnimationFrame) {
              await new Promise(resolve =>
                this.contentWindow.requestAnimationFrame(resolve)
              );
            }

            function flushWindow(win) {
              var utils = win.windowUtils;
              var afterPaintWasPending = utils.isMozAfterPaintPending;

              var root = win.document.documentElement;
              if (root && !root.classList.contains("reftest-no-flush")) {
                try {
                  if (ignoreThrottledAnimations) {
                    utils.flushLayoutWithoutThrottledAnimations();
                  } else {
                    root.getBoundingClientRect();
                  }
                } catch (e) {
                  warningStrings.push("flushWindow failed: " + e + "\n");
                }
              }

              if (!afterPaintWasPending && utils.isMozAfterPaintPending) {
                infoStrings.push(
                  "FlushRendering generated paint for window " +
                    win.location.href
                );
                anyPendingPaintsGeneratedInDescendants = true;
              }

              for (let i = 0; i < win.frames.length; ++i) {
                try {
                  if (!Cu.isRemoteProxy(win.frames[i])) {
                    flushWindow(win.frames[i]);
                  }
                } catch (e) {
                  console.error(e);
                }
              }
            }

            // `contentWindow` will be null if the inner window for this actor
            // has been navigated away from.
            if (this.contentWindow) {
              flushWindow(this.contentWindow);
            }

            if (
              anyPendingPaintsGeneratedInDescendants &&
              !this.contentWindow.windowUtils.isMozAfterPaintPending
            ) {
              warningStrings.push(
                "Internal error: descendant frame generated a MozAfterPaint event, but the root document doesn't have one!"
              );
            }
          }
        } catch (e) {
          errorStrings.push("flushWindow failed: " + e);
        }
        return { errorStrings, warningStrings, infoStrings };
      }

      case "SetupDisplayport": {
        let contentRootElement = this.document.documentElement;
        let winUtils = this.contentWindow.windowUtils;
        let returnStrings = { infoStrings: [], errorStrings: [] };
        if (contentRootElement) {
          this.setupDisplayportForElementSubtree(
            contentRootElement,
            winUtils,
            returnStrings
          );
        }
        return returnStrings;
      }

      case "SetupAsyncScrollOffsets": {
        let returns = { infoStrings: [], errorStrings: [], updatedAny: false };
        let contentRootElement = this.document.documentElement;

        if (!contentRootElement) {
          return returns;
        }

        let winUtils = this.contentWindow.windowUtils;

        returns.updatedAny = this.setupAsyncScrollOffsetsForElementSubtree(
          contentRootElement,
          winUtils,
          msg.data.allowFailure,
          returns
        );
        return returns;
      }
    }
    return undefined;
  }
}
