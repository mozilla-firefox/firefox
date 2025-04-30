/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    prefix: "UserCharacteristicsPage",
    maxLogLevelPref: "toolkit.telemetry.user_characteristics_ping.logLevel",
  });
});

/* This actor is responsible for rendering the canvas elements defined in
 * recipes. It renders with both hardware and software rendering.
 * It also provides debug information about the canvas rendering
 * capabilities of its window (not all windows get HW rendering).
 *
 * See the recipes object for the list of canvases to render.
 * WebGL is still being rendered in toolkit/components/resistfingerprinting/content/usercharacteristics.js
 *
 */
export class UserCharacteristicsCanvasRenderingChild extends JSWindowActorChild {
  constructor() {
    super();

    this.destroyed = false;
  }

  async render(hwRenderingExpected) {
    // I couldn't think of a good name. Recipes as in instructions to render.
    const runRecipe = async (isAccelerated, recipe) => {
      const canvas = this.document.createElement("canvas");
      canvas.width = recipe.size[0];
      canvas.height = recipe.size[1];

      const ctx = canvas.getContext("2d", {
        forceSoftwareRendering: !isAccelerated,
      });

      if (!ctx) {
        lazy.console.error("Could not get 2d context");
        return { error: "COULD_NOT_GET_CONTEXT" };
      }

      let debugInfo = null;
      try {
        debugInfo = ctx.getDebugInfo(true /* ensureTarget */);
      } catch (e) {
        lazy.console.error(
          "Error getting canvas debug info during render: ",
          await stringifyError(e)
        );
        return {
          error: "COULD_NOT_GET_DEBUG_INFO",
          originalError: await stringifyError(e),
        };
      }

      if (debugInfo.isAccelerated !== isAccelerated) {
        lazy.console.error(
          `Canvas is not rendered with expected mode. Expected: ${isAccelerated}, got: ${debugInfo.isAccelerated}`
        );
        return { error: "WRONG_RENDERING_MODE" };
      }

      try {
        await recipe.func(this.contentWindow, canvas, ctx);
      } catch (e) {
        lazy.console.error("Error rendering canvas: ", await stringifyError(e));
        return {
          error: "RENDERING_ERROR",
          originalError: await stringifyError(e),
        };
      }

      return sha1Uint8Array(
        ctx.getImageData(0, 0, canvas.width, canvas.height).data
      ).catch(stringifyError);
    };

    const errors = [];
    const renderings = new Map();

    // Run HW renderings
    // Attempt HW rendering regardless of the expected rendering mode.
    for (const [name, recipe] of Object.entries(lazy.recipes)) {
      lazy.console.debug("[HW] Rendering ", name);
      const result = await runRecipe(true, recipe);
      if (result.error) {
        if (!hwRenderingExpected && result.error === "WRONG_RENDERING_MODE") {
          // If the rendering mode is wrong, we can ignore the error.
          lazy.console.debug(
            "Ignoring error because HW rendering is not expected: ",
            result.error
          );
          continue;
        }

        errors.push({
          name,
          error: result.error,
          originalError: result.originalError,
        });
        continue;
      }
      renderings.set(name, result);
    }

    // Run SW renderings
    for (const [name, recipe] of Object.entries(lazy.recipes)) {
      lazy.console.debug("[SW] Rendering ", name);
      const result = await runRecipe(false, recipe);
      if (result.error) {
        errors.push({
          name: name + "software",
          error: result.error,
          originalError: result.originalError,
        });
        continue;
      }
      renderings.set(name + "software", result);
    }

    const data = new Map();
    data.set("renderings", renderings);
    data.set("errors", errors);
    data.set("dpr", this.contentWindow.devicePixelRatio.toString());

    return data;
  }

  async getDebugInfo() {
    const canvas = this.document.createElement("canvas");

    if (canvas == null) {
      throw new Error("Canvas is ${canvas}");
    }

    if (typeof canvas.getContext !== "function") {
      // Huh? How? Why?
      throw new Error(
        `Canvas is ${canvas} and doesn't have getContext. TagName: ${canvas.tagName}`
      );
    }

    const ctx = canvas.getContext("2d");

    if (!ctx) {
      return null;
    }

    try {
      return ctx.getDebugInfo(true /* ensureTarget */);
    } catch (e) {
      lazy.console.error(
        "Error getting canvas debug info: ",
        await stringifyError(e)
      );
      return null;
    }
  }

  sendMessage(name, obj, transferables) {
    if (this.destroyed) {
      return;
    }

    this.sendAsyncMessage(name, obj, transferables);
  }

  didDestroy() {
    this.destroyed = true;
  }

  async receiveMessage(msg) {
    lazy.console.debug("Actor Child: Got ", msg.name);
    switch (msg.name) {
      case "CanvasRendering:GetDebugInfo":
        return this.getDebugInfo();
      case "CanvasRendering:Render":
        return this.render(msg.data.hwRenderingExpected);
    }

    return null;
  }
}

ChromeUtils.defineLazyGetter(lazy, "recipes", () => {
  return {
    // Metric name => (optionally async) function to render
    canvasdata1: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "orange";
        ctx.fillRect(100, 100, 50, 50);
      },
      size: [250, 250],
    },
    canvasdata2: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "blue";
        ctx.beginPath();
        ctx.moveTo(50, 50);
        ctx.lineTo(200, 200);
        ctx.lineTo(175, 100);
        ctx.closePath();
        ctx.fill();
        ctx.strokeStyle = "red";
        ctx.lineWidth = 5;
        ctx.stroke();
      },
      size: [250, 250],
    },
    canvasdata3: {
      func: async (window, canvas, ctx) => {
        // CC Public Domain - https://www.flickr.com/photos/birds_and_critters/53695948491/
        const imageB64 =
          "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";

        // See bug 1936592 for the rationale of the
        // createImageBitmap() call.
        const byteCharacters = atob(imageB64);
        const byteArray = new Uint8Array(
          [...byteCharacters].map(c => c.charCodeAt(0))
        );
        const blob = new Blob([byteArray], { type: "image/jpeg" });
        const bitmap = await window.createImageBitmap(blob);
        ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
      },
      size: [250, 250],
    },
    canvasdata4: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "orange";
        ctx.globalAlpha = 0.5;
        ctx.translate(100, 100);
        ctx.rotate((45.0 * Math.PI) / 180.0);
        ctx.fillRect(0, 0, 50, 50);
        ctx.rotate((-15.0 * Math.PI) / 180.0);
        ctx.fillRect(0, 0, 50, 50);
      },
      size: [250, 250],
    },
    canvasdata5: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "green";
        ctx.font = "italic 30px Georgia";
        ctx.fillText("The quick brown", 15, 100);
        ctx.fillText("fox jumps over", 15, 150);
        ctx.fillText("the lazy dog", 15, 200);
      },
      size: [250, 250],
    },
    canvasdata6: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "green";
        ctx.translate(10, 100);
        ctx.rotate((45.0 * Math.PI) / 180.0);
        ctx.shadowColor = "blue";
        ctx.shadowBlur = 50;
        ctx.font = "italic 40px Georgia";
        ctx.fillText("The quick", 0, 0);
      },
      size: [250, 250],
    },
    canvasdata7: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "green";
        ctx.font = "italic 30px system-ui";
        ctx.fillText("The quick brown", 15, 100);
        ctx.fillText("fox jumps over", 15, 150);
        ctx.fillText("the lazy dog", 15, 200);
      },
      size: [250, 250],
    },
    canvasdata8: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "green";
        ctx.translate(10, 100);
        ctx.rotate((45.0 * Math.PI) / 180.0);
        ctx.shadowColor = "blue";
        ctx.shadowBlur = 50;
        ctx.font = "italic 40px system-ui";
        ctx.fillText("The quick", 0, 0);
      },
      size: [250, 250],
    },
    canvasdata9: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "green";
        ctx.font = "italic 30px LocalFiraSans";
        ctx.fillText("The quick brown", 15, 100);
        ctx.fillText("fox jumps over", 15, 150);
        ctx.fillText("the lazy dog", 15, 200);
      },
      size: [250, 250],
    },
    canvasdata10: {
      func: (window, canvas, ctx) => {
        ctx.fillStyle = "green";
        ctx.translate(10, 100);
        ctx.rotate((45.0 * Math.PI) / 180.0);
        ctx.shadowColor = "blue";
        ctx.shadowBlur = 50;
        ctx.font = "italic 40px LocalFiraSans";
        ctx.fillText("The quick", 0, 0);
      },
      size: [250, 250],
    },
    // fingerprintjs
    // Their fingerprinting code went to the BSL license from MIT in
    // https://github.com/fingerprintjs/fingerprintjs/commit/572fd98f9e4f27b4e854137ea0d53231b3b4eb6e
    // So use the version of the code in the parent commit which is still MIT
    // https://github.com/fingerprintjs/fingerprintjs/blob/aca79b37f7956eee58018e4a317a2bdf8be62d0f/src/sources/canvas.ts
    canvasdata12Fingerprintjs1: {
      func: (window, canvas, ctx) => {
        ctx.textBaseline = "alphabetic";
        ctx.fillStyle = "#f60";
        ctx.fillRect(100, 1, 62, 20);

        ctx.fillStyle = "#069";
        // It's important to use explicit built-in fonts in order to exclude the affect of font preferences
        // (there is a separate entropy source for them).
        ctx.font = '11pt "Times New Roman"';
        // The choice of emojis has a gigantic impact on rendering performance (especially in FF).
        // Some newer emojis cause it to slow down 50-200 times.
        // There must be no text to the right of the emoji, see https://github.com/fingerprintjs/fingerprintjs/issues/574
        // A bare emoji shouldn't be used because the canvas will change depending on the script encoding:
        // https://github.com/fingerprintjs/fingerprintjs/issues/66
        // Escape sequence shouldn't be used too because Terser will turn it into a bare unicode.
        const printedText = `Cwm fjordbank gly ${
          String.fromCharCode(55357, 56835) /* 😃 */
        }`;
        ctx.fillText(printedText, 2, 15);
        ctx.fillStyle = "rgba(102, 204, 0, 0.2)";
        ctx.font = "18pt Arial";
        ctx.fillText(printedText, 4, 45);
      },
      // usercharacteristics.html uses 240x60, but we can't get HW acceleration
      // if an axis is less than 128px
      size: [240, 128],
    },
    canvasdata13Fingerprintjs2: {
      func: (window, canvas, ctx) => {
        // Canvas blending
        // https://web.archive.org/web/20170826194121/http://blogs.adobe.com/webplatform/2013/01/28/blending-features-in-canvas/
        // http://jsfiddle.net/NDYV8/16/
        ctx.globalCompositeOperation = "multiply";
        for (const [color, x, y] of [
          ["#f2f", 40, 40],
          ["#2ff", 80, 40],
          ["#ff2", 60, 80],
        ]) {
          ctx.fillStyle = color;
          ctx.beginPath();
          ctx.arc(x, y, 40, 0, Math.PI * 2, true);
          ctx.closePath();
          ctx.fill();
        }

        // Canvas winding
        // https://web.archive.org/web/20130913061632/http://blogs.adobe.com/webplatform/2013/01/30/winding-rules-in-canvas/
        // http://jsfiddle.net/NDYV8/19/
        ctx.fillStyle = "#f9c";
        ctx.arc(60, 60, 60, 0, Math.PI * 2, true);
        ctx.arc(60, 60, 20, 0, Math.PI * 2, true);
        ctx.fill("evenodd");
      },
      // usercharacteristics.html uses 122x110, but we can't get HW acceleration
      // if an axis is less than 128px
      size: [128, 128],
    },
  };
});

async function sha1Uint8Array(bytes) {
  const hashBuffer = await crypto.subtle.digest("SHA-1", bytes);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  const hashHex = hashArray.map(b => b.toString(16).padStart(2, "0")).join("");
  return hashHex;
}

async function stringifyError(error) {
  if (error instanceof Error) {
    const stack = error.stack ?? "";
    return `${error.toString()} ${stack}`;
  }
  // A hacky attempt to extract as much as info from error
  const errStr = await (async () => {
    const asStr = await (async () => error.toString())().catch(() => "");
    const asJson = await (async () => JSON.stringify(error))().catch(() => "");
    return asStr.length > asJson.len ? asStr : asJson;
  })();
  return errStr;
}
