/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { normalizePath } from "vite";
import mozillaUriResolver from "./moz-uri-utils.js";

const MOZ_URI_REGEX = /^(?:chrome:\/\/|moz-src:\/\/\/)/;

// import styles from "some.css" with { type: "css" };
const CSS_MODULE_SCRIPT_IMPORT_REGEX =
  /(\bimport\s+[$\w]+\s+from\s+)(["'])([^"']+\.css)\2\s*with\s*{\s*type:\s*["']css["']\s*,?\s*}/g;
// import styles from "some.css";
const CSS_DEFAULT_IMPORT_REGEX =
  /(\bimport\s+[$\w]+\s+from\s+)(["'])([^"']+\.css)\2/g;
// <link rel="stylesheet" href="chrome://global/content/elements/moz-toggle.css" />
const CSS_URI_IN_JS_REGEX = /(?:chrome:\/\/|moz-src:\/\/\/)[^"'`\s]*?\.css/g;
// URL schemes with "-", "+", "." or digits, e.g. moz-icon://.
const NON_ALPHA_SCHEME_REGEX =
  /(?<=["'(\s,])(?<!data:,)[a-z][a-z\d+.-]*[\d+.-][a-z\d+.-]*:\/\//gi;

const CSS_MODULE_PREFIX = "\0firefox-storybook-css-module:";

function splitId(id) {
  const suffixIndex = id.search(/[?#]/);
  return suffixIndex < 0
    ? { file: id, suffix: "" }
    : { file: id.slice(0, suffixIndex), suffix: id.slice(suffixIndex) };
}

function parseId(id) {
  const { file, suffix } = splitId(id);
  const hashIndex = suffix.indexOf("#");
  const search = hashIndex < 0 ? suffix : suffix.slice(0, hashIndex);
  return {
    file,
    params: search.slice(1).split("&").filter(Boolean),
    hash: hashIndex < 0 ? "" : suffix.slice(hashIndex),
  };
}

function formatId({ file, params, hash }) {
  return file + (params.length ? `?${params.join("&")}` : "") + hash;
}

const paramKey = param => param.split("=")[0];

function hasQuery(id, key) {
  return parseId(id).params.some(param => paramKey(param) === key);
}

function withQuery(id, key) {
  const parsed = parseId(id);
  return formatId({ ...parsed, params: [...parsed.params, key] });
}

function withoutQuery(id, key) {
  const parsed = parseId(id);
  return formatId({
    ...parsed,
    params: parsed.params.filter(param => paramKey(param) !== key),
  });
}

function relativeSpecifier(fromFile, toFile) {
  const relative = normalizePath(path.relative(path.dirname(fromFile), toFile));
  return relative.startsWith(".") ? relative : `./${relative}`;
}

function resolveMozillaUri(uri) {
  const { file, suffix } = splitId(uri);
  const resolved = file.startsWith("chrome://")
    ? mozillaUriResolver.rewriteChromeUri(file)
    : file.startsWith("moz-src:///") &&
      mozillaUriResolver.rewriteMozSrcUri(file);
  return resolved ? normalizePath(resolved) + suffix : "";
}

/**
 * Rewrite stylesheet references in a JS module so that Vite can handle them:
 *
 * - CSS module scripts get a `?css-module` query, which this plugin turns into
 *   a module exporting a CSSStyleSheet.
 * - Default imports of CSS files and chrome:// or moz-src:/// stylesheet URIs
 *   in strings (e.g. `<link rel="stylesheet">` in templates) use Vite's `?url`
 *   import, so they get a URL to the processed stylesheet.
 *
 * chrome:// and moz-src:/// URIs are rewritten to paths relative to `file`.
 * URIs that can't be resolved are left untouched.
 *
 * @param {string} source - The module source.
 * @param {string} file - The module's path on disk.
 * @returns {string} The rewritten source.
 */
function rewriteJsStylesheets(source, file) {
  const toSpecifier = uri => {
    if (!MOZ_URI_REGEX.test(uri)) {
      return uri;
    }
    const resolved = resolveMozillaUri(uri);
    return resolved && relativeSpecifier(file, resolved);
  };

  let rewritten = source
    .replace(
      CSS_MODULE_SCRIPT_IMPORT_REGEX,
      (statement, importClause, quote, uri) => {
        const specifier = toSpecifier(uri);
        return specifier
          ? `${importClause}${JSON.stringify(`${specifier}?css-module`)}`
          : statement;
      }
    )
    .replace(
      CSS_DEFAULT_IMPORT_REGEX,
      (statement, importClause, quote, uri) => {
        const specifier = toSpecifier(uri);
        return specifier
          ? `${importClause}${JSON.stringify(`${specifier}?url`)}`
          : statement;
      }
    );

  const imports = [];
  for (const uri of new Set(rewritten.match(CSS_URI_IN_JS_REGEX))) {
    const specifier = toSpecifier(uri);
    if (!specifier) {
      continue;
    }
    const identifier = `__chrome_styles_loader__${path
      .basename(uri, ".css")
      .replaceAll(/[-.]/g, "")}Styles`;
    // These files set the URI from a plain string rather than in a template.
    if (
      ["moz-label.mjs", "panel-list.mjs"].includes(path.basename(file)) ||
      file.endsWith(".js")
    ) {
      rewritten = rewritten.replaceAll(`"${uri}"`, identifier);
    } else {
      rewritten = rewritten.replaceAll(uri, `\${${identifier}}`);
    }
    imports.push(
      `import ${identifier} from ${JSON.stringify(`${specifier}?url`)};\n`
    );
  }
  return imports.join("") + rewritten;
}

/**
 * Vite's external URL check only allows letters in schemes, so it tries to
 * read URLs like moz-icon:// from disk. They can't load outside of Firefox, so
 * this turns them into inert data: URLs. This runs as a PostCSS plugin because
 * Vite reads `@import`ed stylesheets without running plugin transforms.
 */
const nonAlphaSchemePostcssPlugin = {
  postcssPlugin: "firefox-storybook-non-alpha-schemes",
  Declaration(declaration) {
    const value = declaration.value.replace(
      NON_ALPHA_SCHEME_REGEX,
      scheme => `data:,${scheme}`
    );
    if (value !== declaration.value) {
      declaration.value = value;
    }
  },
};

// @import url("chrome://global/content/elements/moz-box.tokens.css") layer(x) screen;
const CSS_IMPORT_PARAMS_REGEX =
  /^\s*(?:url\(\s*(["']?)([^"')]+)\1\s*\)|(["'])([^"']+)\3)\s*(.*)$/s;
// url("chrome://global/skin/icons/info.svg")
const MOZ_URI_IN_CSS_URL_REGEX =
  /(?<=\burl\(\s*["']?)(?:chrome:\/\/|moz-src:\/\/\/)[^"')\s]+/g;

function parseImportParams(params) {
  const match = CSS_IMPORT_PARAMS_REGEX.exec(params);
  if (!match) {
    return null;
  }
  let conditions = match[5].trim();
  const layer = /^layer(?:\(([^)]*)\))?\s*/.exec(conditions);
  conditions = layer ? conditions.slice(layer[0].length) : conditions;
  const supports = /^supports\(((?:[^()]|\([^()]*\))*)\)\s*/.exec(conditions);
  conditions = supports ? conditions.slice(supports[0].length) : conditions;
  return {
    uri: match[2] ?? match[4],
    layer: layer && (layer[1] ?? ""),
    supports: supports?.[1],
    media: conditions.trim(),
  };
}

function wrapInConditions(nodes, { layer, supports, media }, postcss) {
  const wrappers = [
    media && postcss.atRule({ name: "media", params: media }),
    supports && postcss.atRule({ name: "supports", params: supports }),
    layer != null && postcss.atRule({ name: "layer", params: layer }),
  ].filter(Boolean);
  for (const wrapper of wrappers.reverse()) {
    wrapper.append(nodes);
    nodes = [wrapper];
  }
  return nodes;
}

/**
 * Inline `@import`s of chrome:// and moz-src:/// stylesheets, along with any
 * `@import`s in the stylesheets they pull in.
 *
 * @param {import("postcss").Root} root - The stylesheet to inline imports in.
 * @param {object} helpers - PostCSS plugin helpers.
 * @param {Set<string>} inlined - Files already inlined into the stylesheet.
 * @param {boolean} mozillaOnly - Whether to skip relative `@import`s, which
 *   Vite's own `@import` handling has already inlined.
 */
async function inlineImports(root, helpers, inlined, mozillaOnly) {
  const { postcss, result } = helpers;
  const importer = root.source?.input.file;
  for (const node of [...root.nodes]) {
    if (node.type !== "atrule" || node.name !== "import") {
      continue;
    }
    const parsed = parseImportParams(node.params);
    if (!parsed) {
      continue;
    }
    const isMozillaUri = MOZ_URI_REGEX.test(parsed.uri);
    if (mozillaOnly && !isMozillaUri) {
      continue;
    }
    let file;
    if (isMozillaUri) {
      file = splitId(resolveMozillaUri(parsed.uri)).file;
    } else if (
      !/^(?:[a-z][a-z\d+.-]*:|\/\/|\/)/i.test(parsed.uri) &&
      importer
    ) {
      file = normalizePath(
        path.resolve(path.dirname(importer), splitId(parsed.uri).file)
      );
    }
    if (!file) {
      if (isMozillaUri) {
        node.warn(result, `Unable to resolve Mozilla resource ${parsed.uri}`);
      }
      continue;
    }
    if (inlined.has(file)) {
      node.remove();
      continue;
    }
    inlined.add(file);
    result.messages.push({
      type: "dependency",
      plugin: "firefox-storybook-mozilla-css",
      file,
      parent: importer,
    });
    const imported = postcss.parse(await readFile(file, "utf8"), {
      from: file,
    });
    await inlineImports(imported, helpers, inlined, false);
    imported.walkAtRules("charset", charset => charset.remove());
    node.replaceWith(wrapInConditions(imported.nodes, parsed, postcss));
  }
}

/**
 * Resolve chrome:// and moz-src:/// URIs in stylesheets to files on disk.
 * Vite treats these as external URLs, so it neither inlines `@import`s of
 * them nor processes them in `url()`. This inlines those `@import`s after
 * Vite's own `@import` handling, and rewrites `url()`s to relative paths so
 * that Vite's URL handling, which runs last, picks them up as assets.
 */
const mozillaUriPostcssPlugin = {
  postcssPlugin: "firefox-storybook-mozilla-css",
  async Once(root, helpers) {
    await inlineImports(root, helpers, new Set(), true);
  },
  Declaration(declaration) {
    const importer = declaration.source?.input.file;
    const value = declaration.value.replace(MOZ_URI_IN_CSS_URL_REGEX, uri => {
      const resolved = resolveMozillaUri(uri);
      if (!resolved) {
        return uri;
      }
      return importer ? relativeSpecifier(importer, resolved) : resolved;
    });
    if (value !== declaration.value) {
      declaration.value = value;
    }
  },
};

export function createMozillaResourcePlugin({ projectRoot }) {
  const storybookRoot = normalizePath(
    path.join(projectRoot, "browser/components/storybook/.storybook")
  );
  const cssModules = new Map();

  function isProjectSource(file) {
    const normalized = normalizePath(file);
    return (
      normalized.startsWith(`${projectRoot}/`) &&
      !normalized.includes("/node_modules/") &&
      !normalized.startsWith(`${storybookRoot}/`)
    );
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
            // Vite skips import specifiers that look like external URLs
            // unless an alias matches them, so these no-op aliases let
            // chrome:// and moz-src:/// imports reach resolveId.
            { find: /^chrome:\/\//, replacement: "chrome://" },
            { find: /^moz-src:\/\/\//, replacement: "moz-src:///" },
          ],
        },
        css: {
          postcss: {
            plugins: [mozillaUriPostcssPlugin, nonAlphaSchemePostcssPlugin],
          },
        },
        // Lightning CSS rejects Firefox-only syntax like @media -moz-pref().
        build: { cssMinify: false },
      };
    },

    async resolveId(source, importer) {
      if (cssModules.has(source)) {
        return source;
      }
      if (hasQuery(source, "css-module")) {
        const resolved = await this.resolve(
          withoutQuery(source, "css-module"),
          importer
        );
        if (!resolved || resolved.external) {
          this.error(`Unable to resolve stylesheet ${source}`);
        }
        // Hashed so that production output filenames stay short.
        const id = `${CSS_MODULE_PREFIX}${createHash("sha256")
          .update(resolved.id)
          .digest("hex")
          .slice(0, 16)}.mjs`;
        cssModules.set(id, resolved.id);
        return id;
      }
      if (!MOZ_URI_REGEX.test(source)) {
        return null;
      }
      const resolved = resolveMozillaUri(source);
      if (!resolved) {
        this.error(`Unable to resolve Mozilla resource ${source}`);
      }
      return resolved;
    },

    async load(id) {
      const stylesheet = cssModules.get(id);
      if (stylesheet) {
        return (
          `import cssText from ${JSON.stringify(withQuery(stylesheet, "inline"))};\n` +
          "const sheet = new CSSStyleSheet();\n" +
          "sheet.replaceSync(cssText);\n" +
          "export default sheet;\n"
        );
      }
      if (/\.(?:ftl|md)$/.test(id) && !/(stories|story)\.md$/.test(id)) {
        this.addWatchFile(id);
        return `export default ${JSON.stringify(await readFile(id, "utf8"))};`;
      }
      return null;
    },

    transform(source, id) {
      if (id.startsWith("\0")) {
        return null;
      }
      const { file } = splitId(id);
      if (
        !/\.(?:mjs|js)$/.test(file) ||
        !isProjectSource(file) ||
        hasQuery(id, "raw") ||
        hasQuery(id, "url")
      ) {
        return null;
      }
      const code = rewriteJsStylesheets(source, file);
      return code === source ? null : { code, map: null };
    },

    handleHotUpdate({ file, modules, server }) {
      if (!file.endsWith(".css") || !isProjectSource(file)) {
        return undefined;
      }
      // Stylesheets end up in shadow roots and custom elements can't be
      // redefined, so reload the page instead of hot-swapping. Invalidate any
      // stylesheets that inlined this one through @import.
      const pending = [...modules];
      const seen = new Set();
      while (pending.length) {
        const mod = pending.pop();
        if (seen.has(mod)) {
          continue;
        }
        seen.add(mod);
        server.moduleGraph.invalidateModule(mod);
        for (const importer of mod.importers) {
          if (
            importer.id?.startsWith(CSS_MODULE_PREFIX) ||
            /\.css(?:[?#]|$)/.test(importer.id ?? "")
          ) {
            pending.push(importer);
          }
        }
      }
      server.ws.send({ type: "full-reload" });
      return [];
    },
  };
}
