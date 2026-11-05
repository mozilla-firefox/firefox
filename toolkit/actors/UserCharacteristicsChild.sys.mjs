/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "UserCharacteristicsPage",
    maxLogLevelPref: "toolkit.telemetry.user_characteristics_ping.logLevel",
  });
});

export class UserCharacteristicsChild extends JSWindowActorChild {
  /**
   * A placeholder for the collected data.
   *
   * @typedef {object} userDataDetails
   *   @property {string} debug - The debug messages.
   *   @property {Array<string>} gamepads - The user characteristics data for gamepads.
   *   @property {Map<string, number | string | boolean>} output - The user characteristics data.
   */
  userDataDetails;

  collectingDelay = 1000; // Collecting delay for 1000 ms.

  // Please add data collection here if the collection requires privilege
  // access, such as accessing ChromeOnly functions. The function is called
  // right after the content page finishes.
  async collectUserCharacteristicsData() {
    lazy.console.debug("Calling collectUserCharacteristicsData()");
  }

  // This function is similar to the above function, but we call this function
  // with a delay after the content page finished. This function is for data
  // that requires loading time.
  async collectUserCharacteristicsDataWithDelay() {
    lazy.console.debug("Calling collectUserCharacteristicsDataWithDelay()");

    await this.populateGamepadsInfo();
  }

  async populateGamepadsInfo() {
    lazy.console.debug("Calling populateGamepadsInfo()");
    const gamepads = await this.contentWindow.navigator.requestAllGamepads();

    lazy.console.debug(`Found ${gamepads.length} gamepads`);

    const gamepadsInfo = [];

    for (const gamepad of gamepads) {
      // We use an array to represent a gamepad device because it uses less size
      // then an object when convert to a JSON string. So, we can fit the string
      // into a Glean string which has a 100 size limitation.
      const data = [];
      data.push(gamepad.id);
      data.push(gamepad?.hand ?? "");
      data.push(gamepad.buttons.length);
      data.push(gamepad.axes?.length ?? 0);
      data.push(gamepad.hapticActuators?.length ?? 0);
      data.push(gamepad.lightIndicators?.length ?? 0);
      data.push(gamepad.touchEvents?.length ?? 0);

      gamepadsInfo.push(JSON.stringify(data));
    }

    lazy.console.debug(`Reporting gamepad: ${gamepadsInfo}`);
    this.userDataDetails.gamepads = gamepadsInfo;
  }

  async receiveMessage(msg) {
    lazy.console.debug("Got message ", msg.name);
    switch (msg.name) {
      case "WebGLInfo:Collect":
        return this.collectWebGLInfo(
          msg.data.version,
          msg.data.forceSoftwareRendering
        );
    }
    return null;
  }

  // Collect WebGL fingerprinting data in this (content) process and return a
  // plain serializable map to the parent. Running collection in the content
  // document -- where content WebGL normally runs -- is more reliable than using
  // the parent process HiddenFrame. This actor runs as a System caller, so it
  // retains access to the privileged MOZ_debug / forceSoftwareRendering surface
  // that the collection depends on.
  async collectWebGLInfo(version, forceSoftwareRendering) {
    const window = this.contentWindow;
    const document = this.document;

    const results = {
      parameters: {
        params: [],
        extensions: [],
      },
      shaderPrecision: {
        FRAGMENT_SHADER: {},
        VERTEX_SHADER: {},
      },
      debugShaders: {},
      debugParams: {},
    };

    const canvas = document.createElement("canvas");
    const gl = canvas.getContext(version === 2 ? "webgl2" : "webgl", {
      forceSoftwareRendering,
    });
    if (!gl) {
      lazy.console.error(
        "Unable to initialize WebGL. Your browser or machine may not support it."
      );
      return null;
    }

    // Some parameters are removed because they need to binded/set first.
    // We are only interested in fingerprintable parameters.
    // See https://phabricator.services.mozilla.com/D216337 for removed parameters.
    const PARAMS = {
      v1: [
        "ALIASED_LINE_WIDTH_RANGE",
        "ALIASED_POINT_SIZE_RANGE",
        "IMPLEMENTATION_COLOR_READ_FORMAT",
        "IMPLEMENTATION_COLOR_READ_TYPE",
        "MAX_COMBINED_TEXTURE_IMAGE_UNITS",
        "MAX_CUBE_MAP_TEXTURE_SIZE",
        "MAX_FRAGMENT_UNIFORM_VECTORS",
        "MAX_RENDERBUFFER_SIZE",
        "MAX_TEXTURE_IMAGE_UNITS",
        "MAX_TEXTURE_SIZE",
        "MAX_VARYING_VECTORS",
        "MAX_VERTEX_ATTRIBS",
        "MAX_VERTEX_TEXTURE_IMAGE_UNITS",
        "MAX_VERTEX_UNIFORM_VECTORS",
        "MAX_VIEWPORT_DIMS",
        "SHADING_LANGUAGE_VERSION",
        "STENCIL_BACK_VALUE_MASK",
        "STENCIL_BACK_WRITEMASK",
        "STENCIL_VALUE_MASK",
        "STENCIL_WRITEMASK",
        "SUBPIXEL_BITS",
      ],
      v2: [
        "MAX_3D_TEXTURE_SIZE",
        "MAX_ARRAY_TEXTURE_LAYERS",
        "MAX_CLIENT_WAIT_TIMEOUT_WEBGL",
        "MAX_COLOR_ATTACHMENTS",
        "MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS",
        "MAX_COMBINED_UNIFORM_BLOCKS",
        "MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS",
        "MAX_DRAW_BUFFERS",
        "MAX_ELEMENT_INDEX",
        "MAX_ELEMENTS_INDICES",
        "MAX_ELEMENTS_VERTICES",
        "MAX_FRAGMENT_INPUT_COMPONENTS",
        "MAX_FRAGMENT_UNIFORM_BLOCKS",
        "MAX_FRAGMENT_UNIFORM_COMPONENTS",
        "MAX_PROGRAM_TEXEL_OFFSET",
        "MAX_SAMPLES",
        "MAX_SERVER_WAIT_TIMEOUT",
        "MAX_TEXTURE_LOD_BIAS",
        "MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS",
        "MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS",
        "MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS",
        "MAX_UNIFORM_BLOCK_SIZE",
        "MAX_UNIFORM_BUFFER_BINDINGS",
        "MAX_VARYING_COMPONENTS",
        "MAX_VERTEX_OUTPUT_COMPONENTS",
        "MAX_VERTEX_UNIFORM_BLOCKS",
        "MAX_VERTEX_UNIFORM_COMPONENTS",
        "MIN_PROGRAM_TEXEL_OFFSET",
        "UNIFORM_BUFFER_OFFSET_ALIGNMENT",
      ],
      extensions: {
        EXT_texture_filter_anisotropic: ["MAX_TEXTURE_MAX_ANISOTROPY_EXT"],
        WEBGL_draw_buffers: [
          "MAX_COLOR_ATTACHMENTS_WEBGL",
          "MAX_DRAW_BUFFERS_WEBGL",
        ],
        EXT_disjoint_timer_query: ["QUERY_COUNTER_BITS_EXT", "TIMESTAMP_EXT"],
        OVR_multiview2: ["MAX_VIEWS_OVR"],
      },
    };

    const attemptToArray = value => {
      if (ArrayBuffer.isView(value)) {
        // gl.getParameter() returns a typed array belonging to the content
        // document, while this actor runs as system principal. Structured-clone
        // the raw bytes into this (chrome) realm and iterate the chrome-side
        // copy. This copies the numbers without invoking any content-realm
        // iterator/getter machinery (which waiving Xrays would expose).
        return Array.from(structuredClone(value));
      }
      return value;
    };
    function getParam(param, ext = gl) {
      const constant = ext[param];
      const value = attemptToArray(gl.getParameter(constant));
      return value;
    }

    // Get all parameters available in WebGL1
    if (version >= 1) {
      for (const parameter of PARAMS.v1) {
        results.parameters.params.push(getParam(parameter));
      }
    }

    // Get all parameters available in WebGL2
    if (version === 2) {
      for (const parameter of PARAMS.v2) {
        results.parameters.params.push(getParam(parameter));
      }
    }

    // Get all extension parameters
    for (const extension in PARAMS.extensions) {
      const ext = gl.getExtension(extension);
      if (!ext) {
        results.parameters.extensions.push(null);
        continue;
      }
      results.parameters.extensions.push(
        PARAMS.extensions[extension].map(param => getParam(param, ext))
      );
    }

    for (const shaderType of ["FRAGMENT_SHADER", "VERTEX_SHADER"]) {
      for (const precisionType of [
        "LOW_FLOAT",
        "MEDIUM_FLOAT",
        "HIGH_FLOAT",
        "LOW_INT",
        "MEDIUM_INT",
        "HIGH_INT",
      ]) {
        const { rangeMin, rangeMax, precision } = gl.getShaderPrecisionFormat(
          gl[shaderType],
          gl[precisionType]
        );
        results.shaderPrecision[shaderType][precisionType] = {
          rangeMin,
          rangeMax,
          precision,
        };
      }
    }

    const mozDebugExt = gl.getExtension("MOZ_debug");
    const debugExt = gl.getExtension("WEBGL_debug_renderer_info");

    results.debugParams = {
      versionRaw: mozDebugExt.getParameter(gl.VERSION),
      vendorRaw: mozDebugExt.getParameter(gl.VENDOR),
      rendererRaw: mozDebugExt.getParameter(gl.RENDERER),
      extensions: gl.getSupportedExtensions().join(" "),
      extensionsRaw: mozDebugExt.getParameter(mozDebugExt.EXTENSIONS),
      vendorDebugInfo: gl.getParameter(debugExt.UNMASKED_VENDOR_WEBGL),
      rendererDebugInfo: gl.getParameter(debugExt.UNMASKED_RENDERER_WEBGL),
      contextType: mozDebugExt.getParameter(mozDebugExt.CONTEXT_TYPE),
    };

    if (gl.getExtension("WEBGL_debug_shaders")) {
      // WEBGL_debug_shaders.getTranslatedShaderSource() produces GPU fingerprintable information

      // Taken from https://github.com/mdn/dom-examples/blob/b12b3a9e85747d3432135e6efa5bbc6581fc0774/webgl-examples/tutorial/sample3/webgl-demo.js#L29
      const vsSource = `
        attribute vec4 aVertexPosition;
        attribute vec4 aVertexColor;

        uniform mat4 uModelViewMatrix;
        uniform mat4 uProjectionMatrix;

        varying lowp vec4 vColor;

        void main(void) {
          gl_Position = uProjectionMatrix * uModelViewMatrix * aVertexPosition;
          vColor = aVertexColor;
        }
      `;

      // Taken from https://github.com/mdn/content/blob/acfe8c9f1f4145f77653a2bc64a9744b001358dc/files/en-us/web/api/webgl_api/tutorial/adding_2d_content_to_a_webgl_context/index.md?plain=1#L89
      const fsSource = `
        void main() {
          gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
      `;

      const minimalSource = `void main() {}`;

      // To keep the payload small, we'll hash vsSource and fsSource, but keep minimalSource as is.
      const translationExt = gl.getExtension("WEBGL_debug_shaders");

      const fragmentShader = gl.createShader(gl.FRAGMENT_SHADER);
      gl.shaderSource(fragmentShader, fsSource);
      gl.compileShader(fragmentShader);

      const vertexShader = gl.createShader(gl.VERTEX_SHADER);
      gl.shaderSource(vertexShader, vsSource);
      gl.compileShader(vertexShader);

      const minimalShader = gl.createShader(gl.FRAGMENT_SHADER);
      gl.shaderSource(minimalShader, minimalSource);
      gl.compileShader(minimalShader);

      async function sha1(message) {
        const msgUint8 = new TextEncoder().encode(message);
        const hashBuffer = await window.crypto.subtle.digest("SHA-1", msgUint8);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        const hashHex = hashArray
          .map(b => b.toString(16).padStart(2, "0"))
          .join("");
        return hashHex;
      }

      results.debugShaders = {
        fs: await sha1(
          translationExt.getTranslatedShaderSource(fragmentShader)
        ),
        vs: await sha1(translationExt.getTranslatedShaderSource(vertexShader)),
        ms: translationExt.getTranslatedShaderSource(minimalShader),
      };
    }

    const contextAttrs = gl.getContextAttributes();

    const map = {
      // Debug Params
      Extensions: results.debugParams.extensions,
      ExtensionsRaw: results.debugParams.extensionsRaw,
      Renderer: results.debugParams.rendererDebugInfo,
      RendererRaw: results.debugParams.rendererRaw,
      Vendor: results.debugParams.vendorDebugInfo,
      VendorRaw: results.debugParams.vendorRaw,
      VersionRaw: results.debugParams.versionRaw,
      ContextType: results.debugParams.contextType,
      // Debug Shaders
      FragmentShader: results.debugShaders.fs,
      VertexShader: results.debugShaders.vs,
      MinimalSource: results.debugShaders.ms,
      // Parameters
      ParamsExtensions: JSON.stringify(results.parameters.extensions),
      Params: JSON.stringify(results.parameters.params),
      // Shader Precision
      PrecisionFragment: JSON.stringify(
        results.shaderPrecision.FRAGMENT_SHADER
      ),
      PrecisionVertex: JSON.stringify(results.shaderPrecision.VERTEX_SHADER),
      // Context Attributes
      Antialias: String(contextAttrs.antialias),
      Alpha: String(contextAttrs.alpha),
    };

    return map;
  }

  async handleEvent(event) {
    lazy.console.debug("Got ", event.type);
    switch (event.type) {
      case "UserCharacteristicsDataDone":
        // Clone the data so we can modify it. Otherwise, we cannot change it
        // because it's behind Xray wrapper.
        this.userDataDetails = structuredClone(event.detail);

        await this.collectUserCharacteristicsData();

        await new Promise(resolve => {
          lazy.setTimeout(resolve, this.collectingDelay);
        });

        await this.collectUserCharacteristicsDataWithDelay();

        lazy.console.debug("creating IdleDispatch");
        ChromeUtils.idleDispatch(() => {
          lazy.console.debug("sending PageReady");
          this.sendAsyncMessage(
            "UserCharacteristics::PageReady",
            this.userDataDetails
          );
        });
        break;
    }
  }
}
