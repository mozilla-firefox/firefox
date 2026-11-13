/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { normalizePath } from "vite";
import mozillaUriResolver from "./moz-uri-utils.js";

const CSS_MODULE_SCRIPT_IMPORT_REGEX =
  /import\s+\S+\s+from\s+["'](chrome:\/\/[^"']+?\.css|moz-src:\/\/\/[^"']+?\.css)["']\s+with\s*{\s*type:\s*["']css["']\s*,?\s*}\s*;?/g;

function resolveCssUri(uri, resolveUri) {
  return resolveUri(uri).replaceAll("\\", "/");
}

function transformMozStyles(source, resourcePath, resolveUri) {
  const handledUris = new Set();
  let rewrittenSource = source.replace(
    CSS_MODULE_SCRIPT_IMPORT_REGEX,
    (statement, cssUri) => {
      const localPath = resolveCssUri(cssUri, resolveUri);
      if (!localPath) {
        return statement;
      }
      handledUris.add(cssUri);
      return (
        statement
          .replace(cssUri, `${localPath}?css-module`)
          // Remove the CSS import attribute, including an optional trailing comma.
          .replace(/\s+with\s*{\s*type:\s*["']css["']\s*,?\s*}/, "")
      );
    }
  );

  // Preserve URL exports for ordinary CSS imports without processing chrome @imports.
  rewrittenSource = rewrittenSource.replace(
    /\bimport\s+[$\w]+\s+from\s+(["'])([^"']+\.css)\1/g,
    (statement, quote, cssUri) => {
      const localPath = /^(?:chrome:\/\/|moz-src:\/\/\/)/.test(cssUri)
        ? resolveCssUri(cssUri, resolveUri)
        : cssUri;
      if (!localPath) {
        return statement;
      }
      handledUris.add(cssUri);
      return statement.replace(
        `${quote}${cssUri}${quote}`,
        JSON.stringify(`${localPath}?moz-css-url`)
      );
    }
  );

  const cssUris = new Set();
  for (const regex of [/chrome:\/\/.*?\.css/g, /moz-src:\/\/\/.*?\.css/g]) {
    for (const match of rewrittenSource.matchAll(regex)) {
      if (!handledUris.has(match[0])) {
        cssUris.add(match[0]);
      }
    }
  }
  for (const cssUri of cssUris) {
    const localPath = resolveCssUri(cssUri, resolveUri);
    if (!localPath) {
      continue;
    }
    const cssImport = `__chrome_styles_loader__${path
      .basename(localPath, ".css")
      .replaceAll(/[-.]/g, "")}Styles`;
    if (
      ["moz-label.mjs", "panel-list.mjs"].includes(
        path.basename(resourcePath)
      ) ||
      resourcePath.endsWith(".js")
    ) {
      rewrittenSource = rewrittenSource.replaceAll(`"${cssUri}"`, cssImport);
    } else {
      rewrittenSource = rewrittenSource.replaceAll(
        cssUri,
        `\$\{${cssImport}\}`
      );
    }
    rewrittenSource =
      `import ${cssImport} from ${JSON.stringify(`${localPath}?moz-css-url`)};\n` +
      rewrittenSource;
  }
  return rewrittenSource;
}

const VIRTUAL_PREFIX = "\0firefox-storybook:";
const STYLES_PREFIX = "/__firefox_storybook_styles__/";

function splitId(id) {
  const suffixIndex = id.search(/[?#]/);
  return suffixIndex < 0
    ? { file: id, suffix: "" }
    : { file: id.slice(0, suffixIndex), suffix: id.slice(suffixIndex) };
}

function normalizeId(id) {
  const { file, suffix } = splitId(id);
  return normalizePath(file) + suffix;
}

function stylesheetRequest(id) {
  const fragmentIndex = id.indexOf("#");
  const request = fragmentIndex < 0 ? id : id.slice(0, fragmentIndex);
  const fragment = fragmentIndex < 0 ? "" : id.slice(fragmentIndex);
  const queryIndex = request.indexOf("?");
  if (queryIndex < 0) {
    return null;
  }
  const parameters = request.slice(queryIndex + 1).split("&");
  const kind = parameters.includes("css-module")
    ? "css-module"
    : parameters.includes("moz-css-url") && "moz-css-url";
  if (!kind) {
    return null;
  }
  const remaining = parameters
    .filter(parameter => parameter !== kind)
    .join("&");
  return {
    kind,
    request:
      request.slice(0, queryIndex) +
      (remaining ? `?${remaining}` : "") +
      fragment,
  };
}

function rawRequest(id) {
  const { file, suffix } = splitId(id);
  const fragmentIndex = suffix.indexOf("#");
  const query = fragmentIndex < 0 ? suffix : suffix.slice(0, fragmentIndex);
  const fragment = fragmentIndex < 0 ? "" : suffix.slice(fragmentIndex);
  return `${file}${query}${query ? "&" : "?"}raw${fragment}`;
}

export function createMozillaResourcePlugin({ projectRoot }) {
  const storybookRoot = normalizePath(
    path.join(projectRoot, "browser/components/storybook/.storybook")
  );
  const virtualModules = new Map();
  const stylesheetRoutes = new Map();
  const stylesheetModules = new Map();
  let production = false;

  function resolveMozillaUri(id) {
    const { file, suffix } = splitId(id);
    const resolved = file.startsWith("chrome://")
      ? mozillaUriResolver.rewriteChromeUri(file)
      : file.startsWith("moz-src:///") &&
        mozillaUriResolver.rewriteMozSrcUri(file);
    return resolved ? resolved + suffix : "";
  }

  return {
    name: "firefox-storybook-resources",
    enforce: "pre",

    config() {
      return {
        resolve: {
          // Keep Vite from treating these URIs as external URLs so resolveId
          // can handle them.
          alias: [
            { find: /^chrome:\/\//, replacement: "chrome://" },
            { find: /^moz-src:\/\/\//, replacement: "moz-src:///" },
          ],
        },
      };
    },

    configResolved(config) {
      production = config.command === "build";
    },

    async resolveId(source, importer) {
      if (virtualModules.has(source)) {
        return source;
      }
      const stylesheet = stylesheetRequest(source);
      if (stylesheet) {
        const mozillaUri = /^(?:chrome:\/\/|moz-src:\/\/\/)/.test(
          stylesheet.request
        );
        const resolved = mozillaUri
          ? resolveMozillaUri(stylesheet.request)
          : await this.resolve(stylesheet.request, importer, {
              skipSelf: true,
            });
        if (!resolved || resolved.external) {
          if (mozillaUri) {
            this.error(
              `Unable to resolve Mozilla resource ${stylesheet.request}`
            );
          }
          return null;
        }
        const resolvedId = normalizeId(
          typeof resolved === "string" ? resolved : resolved.id
        );
        const { file, suffix } = splitId(resolvedId);
        const token = Buffer.from(resolvedId).toString("base64url");
        const moduleToken = production
          ? createHash("sha256").update(resolvedId).digest("hex")
          : token;
        const id = `${VIRTUAL_PREFIX}${stylesheet.kind}:${moduleToken}.mjs`;
        virtualModules.set(id, { kind: stylesheet.kind, file, suffix });
        if (!stylesheetModules.has(file)) {
          stylesheetModules.set(file, new Set());
        }
        stylesheetModules.get(file).add(id);
        if (stylesheet.kind === "moz-css-url") {
          stylesheetRoutes.set(`${token}.css`, file);
        }
        return id;
      }
      const resolved = resolveMozillaUri(source);
      if (!resolved && /^(?:chrome:\/\/|moz-src:\/\/\/)/.test(source)) {
        this.error(`Unable to resolve Mozilla resource ${source}`);
      }
      return resolved ? normalizeId(resolved) : null;
    },

    async load(id) {
      const stylesheet = virtualModules.get(id);
      if (!stylesheet) {
        if (/\.(?:ftl|md)$/.test(id) && !/(stories|story)\.md$/.test(id)) {
          this.addWatchFile(id);
          return `export default ${JSON.stringify(await readFile(id, "utf8"))};`;
        }
        return null;
      }
      const { kind, file, suffix } = stylesheet;
      this.addWatchFile(file);
      if (kind === "css-module") {
        return (
          `import cssText from ${JSON.stringify(rawRequest(file + suffix))};\n` +
          "const sheet = new CSSStyleSheet();\n" +
          "sheet.replaceSync(cssText);\n" +
          "export default sheet;\n"
        );
      }
      if (production) {
        const referenceId = this.emitFile({
          type: "asset",
          name: path.basename(file),
          originalFileName: normalizePath(path.relative(projectRoot, file)),
          source: await readFile(file),
        });
        return `export default import.meta.ROLLDOWN_FILE_URL_${referenceId} + ${JSON.stringify(suffix)};\n`;
      }
      const token = Buffer.from(file + suffix).toString("base64url");
      return `export default ${JSON.stringify(STYLES_PREFIX + token + ".css" + suffix)};\n`;
    },

    transform(source, id) {
      const { file, suffix } = splitId(id);
      const normalizedFile = normalizePath(file);
      const relativePath = path.relative(projectRoot, file);
      // Only rewrite project JavaScript, excluding virtual modules, raw or URL requests, dependencies, and Storybook configuration.
      if (
        id.startsWith("\0") ||
        /[?&](?:raw|url)(?:[=&]|$)/.test(suffix) ||
        !/\.(?:mjs|js)$/.test(file) ||
        relativePath === ".." ||
        relativePath.startsWith(`..${path.sep}`) ||
        path.isAbsolute(relativePath) ||
        normalizedFile.includes("/node_modules/") ||
        normalizedFile.startsWith(`${storybookRoot}/`)
      ) {
        return null;
      }
      const code = transformMozStyles(source, file, resolveMozillaUri);
      return code === source ? null : { code, map: null };
    },

    configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        const pathname = request.url?.split(/[?#]/, 1)[0];
        if (!pathname?.startsWith(STYLES_PREFIX)) {
          next();
          return;
        }
        const file = stylesheetRoutes.get(pathname.slice(STYLES_PREFIX.length));
        if (!file) {
          response.statusCode = 404;
          response.end("Unknown Storybook stylesheet");
          return;
        }
        try {
          const source = await readFile(file);
          response.setHeader("Content-Type", "text/css");
          response.setHeader("Cache-Control", "no-store");
          response.end(source);
        } catch (error) {
          response.statusCode = 500;
          next(error);
        }
      });
    },

    handleHotUpdate(context) {
      const ids = stylesheetModules.get(normalizePath(context.file));
      if (!ids) {
        return undefined;
      }
      const graph = context.server.moduleGraph;
      const modules = new Set([
        ...context.modules,
        ...(graph.getModulesByFile(context.file) ?? []),
      ]);
      for (const id of ids) {
        const affectedModule = graph.getModuleById(id);
        if (affectedModule) {
          modules.add(affectedModule);
        }
      }
      for (const affectedModule of modules) {
        graph.invalidateModule(affectedModule);
      }
      context.server.ws.send({ type: "full-reload" });
      return [];
    },
  };
}
