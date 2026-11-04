/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { ChatConversation } from "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs" */
/** @import { Conversation } from "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs" */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  expandUrlTokens,
  stripUnresolvedUrlTokens,
  URL_REGEX,
  UrlTokenizer,
} from "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs";
import { componentTypeName } from "chrome://browser/content/aiwindow/modules/AITabUtils.mjs";
import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

import {
  getSmartPageName,
  isSmartPageURL,
} from "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AITabStore:
    "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs",
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  ConversationStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  GetPageContent: "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs",
  isAllowedURLProtocol:
    "moz-src:///browser/components/aiwindow/models/SecurityProperties.sys.mjs",
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  buildEngineForFeature:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  FEATURE_MAJOR_VERSIONS:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  parseVersion: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  renderPrompt: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "AITab",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["preview/aiWindow.ftl"], true)
);

// Dev override: when the pref holds a non-empty string, it is parsed as an
// A2UICatalog instead of the packaged component_schema.json, so the catalog
// can be iterated on without rebuilding.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "overrideCatalog",
  "browser.smartwindow.aitab.components",
  "",
  null,
  prefValue => {
    const pref = prefValue.trim();
    if (!pref) {
      return null;
    }
    try {
      return JSON.parse(pref);
    } catch (e) {
      throw new Error(
        `failed to parse browser.smartwindow.aitab.components: ${e.message}`
      );
    }
  }
);

// Packaged A2UI component catalog (see models/aitab/jar.mn). The service
// produces a validated surface only.
//
// component_schema.json is an A2UI-compatible *catalog*: a single JSON-Schema
// object with a `components` map (name -> property schema), shared `$defs`,
// and a `version` ("{major}.{minor}") paired with the aitab Remote Settings
// major (FEATURE_MAJOR_VERSIONS.aitab): the prompts published at that major
// describe this catalog. A catalog change that older prompts cannot describe
// (a new component, a new property on a component that forbids unknown ones)
// bumps the catalog major, FEATURE_MAJOR_VERSIONS.aitab, and the packaged
// dump's aitab records together, so two releases can be served side by side.
const COMPONENT_SCHEMA_URL =
  "chrome://browser/content/aiwindow/aitab/component_schema.json";
// The `component` type every surface's root must use.
const ROOT_COMPONENT = "Page";
// The id the single root component must carry.
const ROOT_ID = "root";
// JSON Schema dialect the per-component validators declare.
const JSON_SCHEMA_DIALECT = "https://json-schema.org/draft/2020-12/schema";

// Total budget (characters) for page text sent to the model, split evenly
// across the requested tabs so multi-tab requests don't overflow the prompt.
// Measured after URL tokenization, since URLs only reach the model as tokens.
const SOURCE_TEXT_BUDGET = 10000;
// Separator inserted between each page's text in the model prompt.
const PAGE_BREAK = "\n\n<----- PAGE BREAK ---->\n\n";

export const MAX_AITAB_CONTEXT_TABS = 10;

// Surface properties the browser navigates to or loads. The model may only
// fill them with URL tokens it was given (see expandSurfaceUrlTokens).
const LINK_FIELDS = new Set(["href", "image"]);

/**
 * @param {string} url
 * @returns {string|null} The form URLs are compared in when checking a link
 *   against the ones a conversation has seen, or null when `url` is invalid.
 */
function linkKey(url) {
  return URL.parse(String(url ?? ""))?.href ?? null;
}

// Nesting limit for walking model output in expandSurfaceUrlTokens. Surfaces
// are flat adjacency lists, so real ones stay far below this.
const MAX_EXPAND_RECURSION_DEPTH = 32;

// Heading for the `rawContent` section, so the model can tell content handed to
// it directly apart from text extracted from a fetched page.
const RAW_CONTENT_HEADING = "## Provided content (not fetched from a URL)";
// Heading for the `modifyInstructions` section. A modification continues the
// conversation that produced the page, so this marks which turn's surface the
// instructions apply to: the one the model wrote last.
const MODIFY_CONTENT_HEADING =
  "## Instructions for modifications from previous response";

// Cap on each free-text parameter the model fills in (`focus` and
// `modifyInstructions`). Both re-enter the generation prompt, and nothing the
// model is told bounds how much it writes into them.
const MAX_INSTRUCTION_CHARS = 1500;

// Ceiling on the generation history handed to the model. A modification only
// ever adds to that history — the surface it wrote, plus the turn asking for
// the next change — and past roughly this much the output degrades, so the
// request is refused rather than answered badly.
const MAX_HISTORY_CHARS = 100000;

const CANCELED_RESULT = Object.freeze({
  error: "page generation was canceled",
  code: "canceled",
});

// The viewer page for AITab pages. The `page` query string parameter names the
// slug the page data is loaded from.
const AITAB_VIEWER_BASE = "about:smartpage?page=";

const HISTORY_LIMIT_ERROR = "No more modifications are supported";

/**
 * A JSON Schema object from the catalog: a component's property schema, one of
 * the shared `$defs`, or any subschema of those. Only the keywords the
 * validation walk itself inspects are listed here; every other JSON Schema
 * keyword may be present and is handled by the compiled validators.
 *
 * @typedef {object} A2UISchema
 * @property {string} [$ref] - Reference into the catalog's `$defs`, e.g. "#/$defs/DynamicString".
 * @property {string} [type] - JSON Schema type of the described value.
 * @property {A2UISchema[]} [oneOf] - Alternative shapes the value may take,
 *   e.g. a literal array or an `A2UIDataBinding` to one.
 * @property {Record<string, A2UISchema>} [properties] - Property name to its schema.
 * @property {A2UISchema} [items] - Schema each element of an array must satisfy.
 * @property {string[]} [required] - Names of the properties that must be present.
 */

/**
 * An A2UI component catalog: the packaged component_schema.json, or the object
 * held by the browser.smartwindow.aitab.components pref. A single JSON-Schema
 * document whose `components` map holds one property schema per component
 * type, all sharing the document's `$defs`.
 *
 * @typedef {object} A2UICatalog
 * @property {string} [version] - "{major}.{minor}"; the major is paired with
 *   FEATURE_MAJOR_VERSIONS.aitab. Required on the packaged catalog.
 * @property {Record<string, A2UISchema>} components - Component type name to its property schema.
 * @property {Record<string, A2UISchema>} [$defs] - Definitions the component schemas `$ref`.
 * @property {string} [catalogId] - Identifier of the catalog.
 * @property {string} [title] - Human-readable catalog name.
 * @property {string} [description] - What the catalog covers.
 */

/**
 * A surface: the generated page, as a flat list of component instances plus
 * the data model their bindings resolve against.
 *
 * @typedef {object} A2UISurface
 * @property {A2UIComponent[]} components - Every component instance, as a flat
 *   adjacency list: parent/child structure comes from the id references, not
 *   from this array's order.
 * @property {A2UIDataModel} [dataModel] - The data the components bind to.
 */

/**
 * One component instance in a surface. Alongside the two discriminators, a
 * component carries the catalog properties declared for its type (see
 * `#propsOf`), each either a literal or an `A2UIDataBinding`; `header` and
 * `children` are the two such properties this module reads directly.
 *
 * @typedef {object} A2UIComponent
 * @property {string} id - Unique within the surface; the root component's id is ROOT_ID.
 * @property {string} component - A component type name from the catalog.
 * @property {A2UIIdReference} [header] - Id of this component's header component.
 * @property {A2UIIdReference} [children] - Ids of this component's child components.
 */

/**
 * A component's catalog properties: a component instance minus its `id` and
 * `component` discriminators, which the per-component schemas don't declare.
 *
 * @typedef {Record<string, *>} A2UIComponentProps
 */

/**
 * A surface's data model: an arbitrary JSON tree that binding paths resolve
 * into. Keeps real content out of the component instances.
 *
 * @typedef {Record<string, *>} A2UIDataModel
 */

/**
 * A binding to a value in the data model. `path` is a JSON-Pointer (RFC 6901),
 * absolute from the data-model root when it starts with "/", otherwise
 * relative to the enclosing template scope.
 *
 * @typedef {object} A2UIDataBinding
 * @property {string} path - Pointer to the bound value.
 */

/**
 * A template repeater: instantiates the `componentId` component once per
 * element of the array bound at `path`.
 *
 * @typedef {object} A2UITemplateRef
 * @property {string} componentId - Id of the template component.
 * @property {string} path - Absolute JSON-Pointer to the array to repeat over.
 */

/**
 * Related components, always referenced by id and never inlined: a single id,
 * a fixed ordered list of ids, or a template repeater.
 *
 * @typedef {string|string[]|A2UITemplateRef} A2UIIdReference
 */

/**
 * A compiled JSON Schema validator: the part of a `JsonSchema.Validator`
 * instance this module uses.
 *
 * @typedef {object} SchemaValidator
 * @property {(instance: *) => {valid: boolean, errors: ValidationError[]}} validate -
 *   Validates one instance against the compiled schema, collecting every
 *   failure (the validators are built with shortCircuit off).
 */

/**
 * Everything needed to validate a surface against one catalog: the catalog
 * itself, its component type names in catalog order, and one compiled
 * validator per component type.
 *
 * @typedef {object} ValidationEnv
 * @property {A2UICatalog} catalog - The catalog the validators were built from.
 * @property {string[]} names - Component type names, in catalog order.
 * @property {Record<string, SchemaValidator>} validators - Component type name to its props validator.
 */

/**
 * A single validation failure. Errors raised by this module carry `message`
 * (plus `component` or `dataPath` when the failure is attributable to one);
 * errors coming out of a JsonSchema validator carry the JSON Schema
 * output-unit fields instead.
 *
 * @typedef {object} ValidationError
 * @property {string} [message] - Description, for errors raised by this module.
 * @property {string} [component] - Id of the offending component instance.
 * @property {string} [dataPath] - JSON-Pointer of the offending data-model value.
 * @property {string} [error] - Description, for errors from a JsonSchema validator.
 * @property {string} [keyword] - The JSON Schema keyword that failed.
 * @property {string} [keywordLocation] - Pointer to the failed keyword within the schema.
 * @property {string} [instanceLocation] - Pointer to the failing value within the validated instance.
 */

/**
 * The outcome of validating a surface: the surface itself when it is valid,
 * every collected failure otherwise.
 *
 * @typedef {{ok: true, surface: A2UISurface} | {ok: false, errors: ValidationError[]}} SurfaceResult
 */

/**
 * One source page a surface was generated from, in the order requested.
 *
 * @typedef {object} AITabSource
 * @property {string} url - The page the content was pulled from.
 * @property {string} title - The open tab's label, falling back to the URL.
 * @property {string} favIconUrl - A `page-icon:` URL for `url`.
 * @property {string|null} imageUrl - Cached og:image, or null when there is none.
 * @property {string} extractedText - Untruncated readable page text.
 */

/**
 * How a surface came to be: what was asked for, and what it was built from.
 *
 * @typedef {object} AITabContext
 * @property {string} creationPrompt - The user's focus, or "" when none was given.
 * @property {AITabSource[]} urlsUsed - The source pages, in the order requested.
 * @property {object[]} relevantMemories - Memories that informed the page.
 */

/**
 * Metadata derived from a validated surface, for the caller to persist or
 * display. Not sent to the viewer, which only receives the surface.
 *
 * @typedef {object} AITabMetadata
 * @property {string} id - Slug of `title` (see `#slugify`), or the slug being
 *   revised when this generation modified an existing page.
 * @property {string} [convId] - Id of the chat that owns the page being
 *   revised, which keeps it whichever chat asked for the revision. Absent for
 *   a new page, which belongs to the chat that asked for it.
 * @property {string} toolConvId - Id of the conversation the surface was
 *   composed in, in the ConversationStore. Stored alongside the page so a
 *   later modification can continue that exchange, and so deleting the page
 *   can clear it.
 * @property {string} title - The surface's Header title, falling back to the
 *   user's focus, the single source page's title, then a localized default.
 * @property {string} howCreated - How the page was generated.
 * @property {AITabContext} context - What the page was generated from.
 */

/**
 * The result of a generation attempt: metadata plus the validated surface, or
 * a description of what went wrong and its canonical error code (the one
 * reported to telemetry).
 *
 * @typedef {{metadata: AITabMetadata, surface: A2UISurface} | {error: string, code: string}} AITabResult
 */

/**
 * The aitab prompt set: a conversation wired to the aitab model, and the two
 * prompt templates to render into it.
 *
 * @typedef {object} PromptSet
 * @property {Conversation} conversation - Conversation wired to the aitab
 *   model: the one being continued, or a fresh one.
 * @property {string} system - System-instructions template, taking `{schemas}`.
 * @property {string} user - User-data template, taking `{focus}` and `{pageContent}`.
 */

/**
 * The AITab generation service. Everything is static: the class holds no
 * per-instance state and is never constructed.
 *
 * Private helpers are referenced as `AITab.#name` rather than `this.#name` so
 * the public methods keep working when callers destructure them off the class.
 */
export class AITab {
  /** @type {Promise<A2UICatalog>|undefined} */
  static #packagedCatalogPromise;

  /**
   * Load the component catalog: from the browser.smartwindow.aitab.components
   * pref when it is set (see lazy.overrideCatalog), otherwise from the packaged
   * catalog. Returns a validation `env` built from it.
   *
   * @returns {Promise<{env: ValidationEnv}>}
   */
  static async loadAssets() {
    const override = lazy.overrideCatalog;
    const catalog = override ?? (await AITab.#loadPackagedCatalog());
    // The packaged catalog must declare its version; a dev override may omit
    // it, but when it declares one it is held to the same pairing rule.
    AITab.#checkCatalogVersion(catalog, { versionRequired: !override });
    return { env: AITab.#makeEnv(catalog) };
  }

  /**
   * Enforce the catalog/prompt pairing: the catalog's major must equal the
   * aitab major this build reads prompts for, or the prompt and the validator
   * would disagree on which components exist.
   *
   * @param {A2UICatalog} catalog
   * @param {object} options
   * @param {boolean} options.versionRequired - Reject a catalog with no `version`.
   * @throws {Error} with `clientReason` "catalogVersionMismatch".
   */
  static #checkCatalogVersion(catalog, { versionRequired }) {
    const expected = lazy.FEATURE_MAJOR_VERSIONS[lazy.MODEL_FEATURES.AITAB];
    if (catalog?.version == null && !versionRequired) {
      lazy.console.warn(
        `aitab catalog override has no version; assuming major ${expected}`
      );
      return;
    }
    const parsed = lazy.parseVersion(catalog?.version);
    if (!parsed || parsed.major !== expected) {
      const err = new Error(
        `aitab catalog version ${JSON.stringify(catalog?.version)} does not match this build's aitab major ${expected}`
      );
      err.clientReason = "catalogVersionMismatch";
      throw err;
    }
  }

  /**
   * Validate a surface against the catalog. A surface is a flat list of
   * component instances plus a data model; validation covers, in one pass:
   * the shape of the surface, that exactly one component is the `root` Page,
   * that every component's props match its catalog schema, that all id
   * references resolve, and that every absolute data binding resolves in the
   * data model.
   *
   * @param {A2UISurface} surface
   * @param {ValidationEnv} env
   * @returns {SurfaceResult}
   */
  static buildSurface(surface, env) {
    if (!env.validators) {
      return { ok: false, errors: [{ message: "no component catalog" }] };
    }
    const components = surface?.components;
    if (!Array.isArray(components)) {
      return {
        ok: false,
        errors: [{ message: "surface.components must be an array" }],
      };
    }
    const dataModel = surface.dataModel ?? {};
    const errors = [];

    // Structural: unique ids, exactly one root, root is a Page.
    const ids = components.map(c => c?.id);
    const idSet = new Set();
    for (const id of ids) {
      if (typeof id != "string" || !id) {
        errors.push({ message: "every component needs a string id" });
      } else if (idSet.has(id)) {
        errors.push({ message: `duplicate component id "${id}"` });
      } else {
        idSet.add(id);
      }
    }
    const roots = components.filter(c => c?.id === ROOT_ID);
    if (roots.length !== 1) {
      errors.push({
        message: `expected exactly one component with id "${ROOT_ID}", found ${roots.length}`,
      });
    } else if (roots[0].component !== ROOT_COMPONENT) {
      errors.push({
        message: `the "${ROOT_ID}" component must be a ${ROOT_COMPONENT}, got "${roots[0].component}"`,
      });
    }

    // Per-component: known type + props validate against catalog schema.
    for (const comp of components) {
      const validator = env.validators[comp?.component];
      if (!validator) {
        errors.push({
          message: `unknown component "${comp?.component}" (id "${comp?.id}")`,
        });
        continue;
      }
      const { valid, errors: propErrors } = validator.validate(
        AITab.#propsOf(comp)
      );
      if (!valid) {
        for (const e of propErrors) {
          errors.push({ ...e, component: comp.id });
        }
      }
    }

    // Id-reference integrity: header/children (and template targets).
    for (const comp of components) {
      AITab.#checkRefs(comp?.header, idSet, `${comp?.id}.header`, errors);
      AITab.#checkRefs(comp?.children, idSet, `${comp?.id}.children`, errors);
    }

    // Binding integrity: absolute { path } bindings must resolve in dataModel.
    for (const comp of components) {
      if (!comp) {
        continue;
      }
      AITab.#checkBindings(AITab.#propsOf(comp), dataModel, comp.id, errors);
    }

    // Bound-array item validation: when an array prop is a { path } binding, its
    // items live in the data model — which the per-component (literal) schema
    // can't reach — so resolve the array and validate each item against the
    // array's item schema from the catalog.
    const itemCache = new Map();
    for (const comp of components) {
      const compSchema = comp && env.catalog.components[comp.component];
      if (!compSchema) {
        continue;
      }
      AITab.#checkBoundArrays(
        compSchema,
        AITab.#propsOf(comp),
        env.catalog,
        dataModel,
        comp.id,
        errors,
        itemCache
      );
    }

    if (errors.length) {
      return { ok: false, errors };
    }
    return { ok: true, surface };
  }

  /**
   * Fetch and cache the packaged catalog object from chrome://.
   *
   * @returns {Promise<A2UICatalog>}
   */
  static #loadPackagedCatalog() {
    if (!AITab.#packagedCatalogPromise) {
      AITab.#packagedCatalogPromise = fetch(COMPONENT_SCHEMA_URL)
        .then(r => r.json())
        .catch(error => {
          AITab.#packagedCatalogPromise = undefined;
          throw error;
        });
    }
    return AITab.#packagedCatalogPromise;
  }

  /**
   * Build a validation `env` from an A2UI catalog. Compiles one JsonSchema
   * validator per component type (each rooted at that component's property
   * schema, with the catalog's shared `$defs` inlined so `#/$defs/...` refs
   * resolve), and records the ordered component-name list.
   *
   * @param {A2UICatalog} catalog
   * @returns {ValidationEnv}
   */
  static #makeEnv(catalog) {
    if (!catalog || typeof catalog != "object" || !catalog.components) {
      throw new Error("aitab catalog must be an object with a components map");
    }
    const $defs = catalog.$defs ?? {};
    const names = Object.keys(catalog.components);
    const validators = {};
    for (const name of names) {
      // Each per-type doc carries the shared $defs so the component's internal
      // `#/$defs/...` references resolve. No $id: each validator is standalone
      // and there are no cross-document refs to resolve.
      const doc = {
        $schema: JSON_SCHEMA_DIALECT,
        $defs,
        ...catalog.components[name],
      };
      validators[name] = new lazy.JsonSchema.Validator(doc, {
        shortCircuit: false,
      });
    }
    return { catalog, names, validators };
  }

  /**
   * Push an error for every id reference in `value` that is not a known
   * component id. `value` may be a single id string, an array of id strings, or
   * a template `{ componentId, path }` object (children).
   *
   * @param {A2UIIdReference} value
   * @param {Set<string>} idSet
   * @param {string} where
   * @param {ValidationError[]} errors
   */
  static #checkRefs(value, idSet, where, errors) {
    if (value == null) {
      return;
    }
    if (typeof value == "string") {
      if (!idSet.has(value)) {
        errors.push({ message: `${where} references missing id "${value}"` });
      }
    } else if (Array.isArray(value)) {
      for (const v of value) {
        AITab.#checkRefs(v, idSet, where, errors);
      }
    } else if (
      typeof value == "object" &&
      typeof value.componentId == "string"
    ) {
      if (!idSet.has(value.componentId)) {
        errors.push({
          message: `${where} template references missing id "${value.componentId}"`,
        });
      }
    }
  }

  /**
   * Walk a props object for binding objects `{ path }` and push an error for
   * every absolute path (starting with "/") that does not resolve in the data
   * model. Relative paths (template scope) are not checked here.
   *
   * @param {A2UIComponentProps|*} value - Props object, or any nested value
   *   reached while walking one.
   * @param {A2UIDataModel} dataModel
   * @param {string} where
   * @param {ValidationError[]} errors
   */
  static #checkBindings(value, dataModel, where, errors) {
    if (Array.isArray(value)) {
      for (const v of value) {
        AITab.#checkBindings(v, dataModel, where, errors);
      }
      return;
    }
    if (!value || typeof value != "object") {
      return;
    }
    const keys = Object.keys(value);
    if (keys.length === 1 && typeof value.path == "string") {
      if (
        value.path.startsWith("/") &&
        AITab.#resolvePath(dataModel, value.path) === undefined
      ) {
        errors.push({
          message: `${where}: binding path "${value.path}" not found in dataModel`,
        });
      }
      return;
    }
    for (const v of Object.values(value)) {
      AITab.#checkBindings(v, dataModel, where, errors);
    }
  }

  /**
   * A component instance's catalog properties: everything but the `id` and
   * `component` discriminators, which the per-component schemas don't declare.
   *
   * @param {A2UIComponent} comp
   * @returns {A2UIComponentProps}
   */
  static #propsOf(comp) {
    const props = { ...comp };
    delete props.id;
    delete props.component;
    return props;
  }

  /**
   * Resolve a JSON-Pointer (RFC 6901) into `root`. Returns the pointed-at value
   * (truthy check at the callsite treats `undefined` as "not found"; a present
   * value — including empty string/array — is considered resolved).
   *
   * @param {A2UIDataModel} root
   * @param {string} pointer - Absolute pointer starting with "/".
   * @returns {*}
   */
  static #resolvePath(root, pointer) {
    let node = root;
    for (let seg of pointer.slice(1).split("/")) {
      if (node == null || typeof node != "object") {
        return undefined;
      }
      seg = seg.replace(/~1/g, "/").replace(/~0/g, "~");
      node = node[seg];
    }
    return node;
  }

  /**
   * True when `v` is an `A2UIDataBinding`: an object with just a string
   * `path`.
   *
   * @param {*} v
   * @returns {boolean}
   */
  static #isBinding(v) {
    return (
      !!v &&
      typeof v == "object" &&
      !Array.isArray(v) &&
      typeof v.path == "string" &&
      Object.keys(v).length === 1
    );
  }

  /**
   * Resolve a schema that may be `{ $ref: "#/$defs/X", ...siblings }` to its target `$def`.
   *
   * @param {A2UISchema} schema - Schema that may be a `$ref` into the catalog.
   * @param {A2UICatalog} catalog - The catalog holding `$defs`.
   * @returns {A2UISchema} The resolved `$def`, or `schema` when it is not a `$ref`.
   */
  static #resolveDef(schema, catalog) {
    if (schema && typeof schema.$ref == "string") {
      const name = schema.$ref.replace(/^#\/\$defs\//, "");
      return catalog.$defs?.[name] ?? schema;
    }
    return schema;
  }

  /**
   * Compile + cache a validator for an array item schema (with catalog `$defs` in scope).
   *
   * @param {A2UISchema} itemSchema - Schema each array element must satisfy.
   * @param {A2UICatalog} catalog - The catalog whose `$defs` the item schema may $ref.
   * @param {Map<string, SchemaValidator>} cache - Per-validation compiled-validator cache.
   * @returns {SchemaValidator} A JsonSchema validator for `itemSchema`.
   */
  static #itemValidator(itemSchema, catalog, cache) {
    return cache.getOrInsertComputed(
      JSON.stringify(itemSchema),
      () =>
        new lazy.JsonSchema.Validator(
          {
            $schema: JSON_SCHEMA_DIALECT,
            $defs: catalog.$defs ?? {},
            ...itemSchema,
          },
          { shortCircuit: false }
        )
    );
  }

  /**
   * Walk `value` against its catalog `schema`; wherever the schema is a
   * `oneOf` of [ array-of-items, DataBinding ] and the value is a `{ path }`
   * binding, resolve the array from the data model and validate each element
   * against the array's item schema. Recurses into object properties so nested
   * arrays (e.g. Header.references.items) are covered. Literal arrays are
   * already validated by the per-component validator, so they are skipped here.
   *
   * @param {A2UISchema} schema
   * @param {*} value
   * @param {A2UICatalog} catalog
   * @param {A2UIDataModel} dataModel
   * @param {string} where
   * @param {ValidationError[]} errors
   * @param {Map<string, SchemaValidator>} cache
   */
  static #checkBoundArrays(
    schema,
    value,
    catalog,
    dataModel,
    where,
    errors,
    cache
  ) {
    schema = AITab.#resolveDef(schema, catalog);
    if (!schema || value == null) {
      return;
    }
    if (Array.isArray(schema.oneOf)) {
      const arrayBranch = schema.oneOf.find(
        s => s && s.type === "array" && s.items
      );
      if (arrayBranch && AITab.#isBinding(value)) {
        if (!value.path.startsWith("/")) {
          return; // relative paths aren't resolvable from the root here
        }
        const arr = AITab.#resolvePath(dataModel, value.path);
        if (arr === undefined) {
          return; // missing path already reported by #checkBindings
        }
        if (!Array.isArray(arr)) {
          errors.push({
            message: `${where}: data at "${value.path}" must be an array`,
          });
          return;
        }
        const iv = AITab.#itemValidator(arrayBranch.items, catalog, cache);
        arr.forEach((item, i) => {
          const { valid, errors: itemErrors } = iv.validate(item);
          if (!valid) {
            for (const e of itemErrors) {
              errors.push({ ...e, dataPath: `${value.path}/${i}` });
            }
          }
        });
      }
      return;
    }
    const props = schema.properties;
    if (props && typeof value == "object" && !Array.isArray(value)) {
      for (const [k, sub] of Object.entries(props)) {
        if (k in value) {
          AITab.#checkBoundArrays(
            sub,
            value[k],
            catalog,
            dataModel,
            `${where}/${k}`,
            errors,
            cache
          );
        }
      }
    }
  }

  /**
   * Build the viewer URL for a stored page. The page data stays in the
   * database and is loaded from the slug, so nothing about the page travels
   * in the URL.
   *
   * @param {string} slug - The persisted page's slug.
   * @returns {string}
   */
  static buildViewerURL(slug) {
    return `${AITAB_VIEWER_BASE}${slug}`;
  }

  /**
   * Selects up to MAX_AITAB_CONTEXT_TABS unique context urls (http, https
   * or a smartpage) to build an AITab. Prioritizes candidate urls from
   * open tabs (sorted by lastAccessed), followed by the remaining
   * candidates as urls/mentions from the chat conversation.
   *
   * @param {string[]} candidateUrls - Candidate context urls.
   * @returns {string[]}
   */
  static selectContextUrls(candidateUrls) {
    if (!Array.isArray(candidateUrls)) {
      return [];
    }
    const candidates = new Set(
      candidateUrls.filter(
        url => lazy.isAllowedURLProtocol(url) || isSmartPageURL(URL.parse(url))
      )
    );
    const openTabUrls = new Set(
      lazy.BrowserWindowTracker.orderedWindows
        .filter(win => !win.closed && lazy.AIWindow.isAIWindowActive(win))
        .flatMap(win => win.gBrowser.tabs)
        .filter(tab => candidates.has(tab.linkedBrowser.currentURI.spec))
        .sort((a, b) => b.lastAccessed - a.lastAccessed)
        .map(tab => tab.linkedBrowser.currentURI.spec)
    );
    return Array.from(openTabUrls.union(candidates)).slice(
      0,
      MAX_AITAB_CONTEXT_TABS
    );
  }

  /**
   * Generate an AITab from a list of URLs, content handed over inline, or both.
   * Each URL's readable content is pulled via get_page_content, then an LLM
   * composes a structured A2UI surface that is validated against the packaged
   * catalog. The validated surface and its derived metadata are returned to the
   * caller — nothing is persisted and no HTML is assembled here (rendering
   * happens in about:smartpage). If generation fails, an `error` string
   * describing the problem is returned instead.
   *
   * `modifySlug` revises a stored page instead of writing a new one: the
   * conversation that composed that page is loaded and continued, so the model
   * edits the surface it already wrote rather than being handed it back as
   * source content. Only a page belonging to `conversation` can be revised.
   *
   * @param {object} options
   * @param {string[]} [options.urlList] - The URLs to include, already expanded
   *   from URL tokens by the tool dispatcher. Trims at MAX_AITAB_URLS urls.
   * @param {string} [options.focus] - What the user wants the page to focus
   *   on. Cut to MAX_INSTRUCTION_CHARS.
   * @param {string} [options.rawContent] - Content supplied inline rather than
   *   fetched, appended to the source text as its own PAGE_BREAK section.
   *   Shares the same source-text budget as the pages.
   * @param {string} [options.modifySlug] - Slug of the stored page to revise.
   * @param {string} [options.modifyInstructions] - What to change about it.
   *   Cut to MAX_INSTRUCTION_CHARS.
   * @param {string} [options.howInitiated] - How the generation was requested,
   *   for telemetry: tab_list, tab_group, fxms, chat, chat_followup or
   *   refresh.
   * @param {AbortSignal} [options.signal] - Cancels the generation. Checked at
   *   every await boundary here and in #generateStructuredSurface, and passed to
   *   the page extractions so they can be torn down early.
   * @param {ChatConversation} conversation
   * @returns {Promise<AITabResult>} The derived metadata and validated
   *   surface, or an error description.
   */
  static async generateAITab(
    {
      urlList,
      focus = "",
      rawContent,
      modifySlug,
      modifyInstructions,
      howInitiated = "chat",
      signal,
    } = {},
    conversation
  ) {
    const urls = Array.isArray(urlList)
      ? urlList
          .filter(url => typeof url == "string")
          .slice(0, MAX_AITAB_CONTEXT_TABS)
      : [];
    const rawText = typeof rawContent == "string" ? rawContent.trim() : "";
    const slug = typeof modifySlug == "string" ? modifySlug.trim() : "";

    const initiateExtra = {
      num_tabs: urls.length,
      how_initiated: howInitiated,
      raw_content_len: typeof rawContent == "string" ? rawContent.length : 0,
    };
    if (slug) {
      Glean.smartWindow.aitabEditInitiate.record(initiateExtra);
    } else {
      Glean.smartWindow.aitabCreateInitiate.record({
        ...initiateExtra,
        num_tabs_total_attempted: Array.isArray(urlList) ? urlList.length : 0,
      });
    }

    const startTime = ChromeUtils.now();
    const stats = { charsRead: 0, charsOut: 0 };
    const result = await AITab.#generate(
      { urls, rawText, slug, focus, modifyInstructions, signal, stats },
      conversation
    );

    const completeExtra = {
      is_success: !result.error,
      components_used: result.surface
        ? AITab.#componentsUsed(result.surface)
        : "[]",
      num_chars_read: stats.charsRead,
      num_chars_out: stats.charsOut,
      seconds_elapsed: Math.round((ChromeUtils.now() - startTime) / 1000),
    };
    if (result.error) {
      completeExtra.error = result.code;
    }
    if (slug) {
      Glean.smartWindow.aitabEditComplete.record(completeExtra);
    } else {
      Glean.smartWindow.aitabCreateComplete.record(completeExtra);
    }

    return result.error ? { error: result.error, code: result.code } : result;
  }

  /**
   * The body of generateAITab, with its options already normalized.
   *
   * @param {object} options
   * @param {string[]} options.urls
   * @param {string} options.rawText
   * @param {string} options.slug - Slug of the page to revise, or "".
   * @param {string} options.focus
   * @param {string} [options.modifyInstructions]
   * @param {AbortSignal} [options.signal]
   * @param {{charsRead: number, charsOut: number}} options.stats - Filled in
   *   with the characters sent to and received from the model.
   * @param {ChatConversation} conversation
   * @returns {Promise<AITabResult|{error: string, code: string}>} On failure,
   *   `code` is the canonical error code reported to telemetry.
   */
  static async #generate(
    { urls, rawText, slug, focus, modifyInstructions, signal, stats },
    conversation
  ) {
    // A modification already has a page to work from, so it needs no material
    // of its own; anything else has to bring something to build one out of.
    if (!urls.length && !rawText && !slug) {
      return {
        error: "no URLs or content were provided to build a page from",
        code: "no_content",
      };
    }

    if (signal?.aborted) {
      return CANCELED_RESULT;
    }

    // Resolved before any content is fetched: a slug that names no page of
    // this conversation's is the model's mistake to correct, not work to pay
    // for.
    let prior = null;
    if (slug) {
      prior = await AITab.#loadPageToModify(slug, conversation);
      if (prior.error) {
        return prior;
      }

      if (signal?.aborted) {
        return CANCELED_RESULT;
      }
    }

    const sources = await AITab.#collectSources({
      urls,
      rawText,
      conversation,
      signal,
    });

    if (sources.error) {
      return sources;
    }
    const { urlsUsed, readUrls, sourceText } = sources;

    const focusText = AITab.#capped(focus);

    // Compose the surface with the LLM. Modification instructions only mean
    // something alongside the conversation they revise, so they are dropped
    // when there is no page to modify. With no new sources to carry it, the
    // focus is the only place a modification's request can be, so it stands in
    // for missing instructions.
    let instructions = "";
    if (prior) {
      instructions =
        AITab.#capped(modifyInstructions) || (sourceText ? "" : focusText);
    }
    const structured = await AITab.#generateStructuredSurface({
      sourceText,
      focus: focusText,
      modifyInstructions: instructions,
      priorConversation: prior?.conversation,
      allowedLinks: conversation?.securityProperties.untrustedInput
        ? AITab.#allowedLinks({ conversation, readUrls, prior })
        : null,
      signal,
      stats,
    });

    if (signal?.aborted) {
      return CANCELED_RESULT;
    }

    if (structured.error) {
      return structured;
    }

    // Every page in urlList was extracted into this conversation, so it holds
    // private data (a credentialed page load) and untrusted input (arbitrary
    // web content, possibly carrying a prompt injection). Security flags on
    // this tool conversation will be used when getPageContent is called on the
    // associated generated page: bug 2069128 propagates untrustedInput from
    // here to the chat conversation that reads it.
    const toolConversation = structured.conversation;
    toolConversation.securityProperties.setPrivateData();
    toolConversation.securityProperties.setUntrustedInput();
    toolConversation.securityProperties.commit();

    // Inherit the chat's URL ledgers, and nothing beyond them. Search result
    // URLs carry their own anonymous-fetch exemption, so they come across
    // separately rather than being folded in as ordinary seen URLs.
    toolConversation.addSeenUrls(conversation?.seenUrls ?? []);
    toolConversation.addSerpUrlsForAnonymousFetch(
      conversation?.serpUrlsForAnonymousFetch ?? []
    );

    // Awaited, not fired and forgotten: the stored page points back at this
    // row through its tool_conv_id, and a modification can only continue the
    // exchange once it has landed. A failure still yields a usable page, so it
    // is logged rather than failing the generation.
    toolConversation.updatedDate = Date.now();
    await toolConversation
      .save()
      .catch(error =>
        lazy.console.error(`Could not save tool conversation: ${error}`)
      );

    // Fill in link-item favicons from Places, before the surface is linked or
    // stored.
    await AITab.#hydrateFavicons(structured.surface, signal);

    if (signal?.aborted) {
      return CANCELED_RESULT;
    }

    const title =
      AITab.#titleFromSurface(structured.surface) ||
      // A revision that comes back untitled keeps the title it had, which is
      // more specific than anything this turn could fall back to.
      prior?.page.title ||
      focusText ||
      (urls.length === 1 && urlsUsed[0].title) ||
      lazy.l10n.formatValueSync("ai-tab-default-page-title");

    /** @type {AITabMetadata} */
    const metadata = {
      id: prior ? prior.page.slug : AITab.#slugify(title),
      convId: prior?.page.convId,
      toolConvId: toolConversation.id,
      title,
      howCreated: "chat",
      context: {
        creationPrompt: prior?.page.context?.creationPrompt || focusText,
        // A revision keeps the sources the page was already built from: the
        // turn that asked for it may have brought no URLs of its own.
        urlsUsed: AITab.#mergeSources(prior?.page.context?.urlsUsed, urlsUsed),
        relevantMemories: [],
      },
    };

    return { metadata, surface: structured.surface };
  }

  /**
   * The URLs a generated page may link to: those the chat has seen, its
   * search results and @mentions, the pages read for this turn, and, for a
   * revision, the links already on the page.
   *
   * @param {object} options
   * @param {ChatConversation} options.conversation - The chat asking for the
   *   page.
   * @param {string[]} options.readUrls - Pages read for this turn.
   * @param {{page: object, conversation: Conversation}|null} options.prior -
   *   The page being revised, from #loadPageToModify.
   * @returns {Set<string>} Normalized URLs (see linkKey).
   */
  static #allowedLinks({ conversation, readUrls, prior }) {
    const urls = [
      ...(conversation?.seenUrls ?? []),
      ...(conversation?.serpUrlsForAnonymousFetch ?? []),
      ...(conversation?.getAllMentionURLs() ?? []),
      ...readUrls,
      ...AITab.#linkedUrls(prior?.page.components?.surface),
    ];
    return new Set(urls.map(linkKey).filter(Boolean));
  }

  /**
   * The `href` values in a surface, including those in its data model.
   *
   * @param {any} value - A surface, or a value nested in one.
   * @param {number} [recursionDepth] - Nesting level of `value`; callers
   *   leave it unset.
   * @returns {string[]}
   */
  static #linkedUrls(value, recursionDepth = 0) {
    if (
      !value ||
      typeof value != "object" ||
      recursionDepth > MAX_EXPAND_RECURSION_DEPTH
    ) {
      return [];
    }
    return Object.entries(value).flatMap(([key, sub]) =>
      key == "href" && typeof sub == "string"
        ? [sub]
        : AITab.#linkedUrls(sub, recursionDepth + 1)
    );
  }

  /**
   * A model-supplied free-text parameter, trimmed and cut to
   * MAX_INSTRUCTION_CHARS.
   *
   * @param {*} text - The parameter as the model sent it, which is not
   *   guaranteed to be a string.
   * @returns {string}
   */
  static #capped(text) {
    return typeof text == "string"
      ? text.trim().slice(0, MAX_INSTRUCTION_CHARS)
      : "";
  }

  /**
   * The longest prefix of `text` that fits in `budget` once its URLs are
   * tokenized. Never ends inside a URL, which would hand the model a token
   * for a URL that doesn't exist.
   *
   * @param {string} text
   * @param {number} budget
   * @returns {string}
   */
  static #truncateToBudget(text, budget) {
    // Measures token lengths only. The prompt is tokenized separately, so
    // nothing registered here can resolve a link in the model's output.
    const urlTokenizer = new UrlTokenizer();
    let length = 0;
    let end = 0;
    for (const match of text.matchAll(URL_REGEX)) {
      const gap = match.index - end;
      if (length + gap >= budget) {
        break;
      }
      length += gap + urlTokenizer.tokenizeText(match[0]).length;
      if (length > budget) {
        return text.slice(0, match.index);
      }
      end = match.index + match[0].length;
    }
    return text.slice(0, end + Math.max(0, budget - length));
  }

  /**
   * Read the material a surface is composed from: the readable text of each
   * requested URL, plus any content handed over inline, laid out as the
   * prompt's source sections and separated by PAGE_BREAK.
   *
   * @param {object} options
   * @param {string[]} options.urls - URLs to read, in the requested order.
   * @param {string} options.rawText - Content supplied inline, or "".
   * @param {ChatConversation} options.conversation - Gates each URL's access
   *   check, so a page the chat may not read contributes nothing.
   * @param {AbortSignal} [options.signal] - Cancels the extractions.
   * @returns {Promise<{urlsUsed: AITabSource[], readUrls: string[],
   *   sourceText: string}|{error: string}>} The sources in the order
   *   requested, the URLs among them that could be read, and the text for the
   *   prompt, or a description of why there is nothing to build from.
   */
  static async #collectSources({ urls, rawText, conversation, signal }) {
    // Pull the readable content for each requested URL (order-aligned with
    // urls). Structured results, so a refusal is distinguishable from page
    // text rather than being composed into the page as if it were content.
    let contents = [];
    if (urls.length) {
      contents = await lazy.GetPageContent.getPageContent(
        { url_list: urls, signal },
        conversation
      );

      if (signal?.aborted) {
        return CANCELED_RESULT;
      }

      // Nothing readable: report it instead of generating a page whose only
      // source material is the refusal. A partial read still generates, from
      // whichever URLs were allowed.
      if (!contents.some(result => result.ok)) {
        return {
          error: "none of the requested pages could be read",
          code: "pages_unreadable",
        };
      }
    }

    // Split the source-text budget evenly across the sections the prompt will
    // carry — one per requested tab, plus one for inline content — so the model
    // prompt stays bounded no matter how many tabs are included. A modification
    // can bring neither, in which case there is nothing to budget.
    const sections = urls.length + (rawText ? 1 : 0);
    const perTabBudget = Math.floor(SOURCE_TEXT_BUDGET / (sections || 1));

    /** @type {AITabSource[]} */
    const urlsUsed = [];
    const sourceParts = [];
    for (const [index, url] of urls.entries()) {
      // Prefer the open tab's title for the heading; fall back to the URL.
      const tab = lazy.GetPageContent.getTabWithURL(url);
      const heading = tab?.label || url;
      const text = contents[index]?.content ?? "";
      // Best-effort og:image lookup ("" when none cached), gated on the same
      // access-control decision as the page text so a refused URL leaks no
      // image either.
      const imageUrl = lazy.GetPageContent.isContentAllowed(url, conversation)
        ? await AITab.#getPageImage(url)
        : "";
      urlsUsed.push({
        url,
        title: heading,
        favIconUrl: `page-icon:${url}`,
        imageUrl: imageUrl || null,
        extractedText: text,
      });

      // Trim each page's text to its share of the budget before sending to the
      // model.
      const budgetedText = AITab.#truncateToBudget(text, perTabBudget);
      const headLines = [`## ${heading}`, `URL: ${url}`];
      // Omit the Image: line when absent so the model never echoes an empty
      // value.
      if (imageUrl) {
        headLines.push(`Image: ${imageUrl}`);
      }
      sourceParts.push(`${headLines.join("\n")}\n\n${budgetedText}`);
    }

    if (signal?.aborted) {
      return CANCELED_RESULT;
    }

    if (rawText) {
      sourceParts.push(
        `${RAW_CONTENT_HEADING}\n\n${AITab.#truncateToBudget(rawText, perTabBudget)}`
      );
    }

    return {
      urlsUsed,
      readUrls: urls.filter((url, index) => contents[index]?.ok),
      sourceText: sourceParts.join(PAGE_BREAK),
    };
  }

  /**
   * Load the stored page a modification names, along with the conversation that
   * composed it. The stored conversation carries its messages, URL ledgers and
   * security flags but no engine; #resolvePromptSet attaches one.
   *
   * Only a page belonging to `conversation`, or one the user referenced in it
   * (the current tab or an @mention), can be loaded: slugs are visible in page
   * URLs, so without that check a model could be talked into rewriting a page
   * from an unrelated chat. URLs the conversation merely came across, such as
   * open tabs or links in fetched pages, do not count. Keying the lookup on
   * the chat conversation would also file the new version under the wrong
   * slug.
   *
   * @param {string} slug
   * @param {ChatConversation} conversation - The chat asking for the revision.
   * @returns {Promise<{page: object, conversation: Conversation}
   *   |{error: string}>}
   */
  static async #loadPageToModify(slug, conversation) {
    let page;
    let priorConversation;
    try {
      page = await lazy.AITabStore.getBySlug(slug);
      if (page?.toolConvId) {
        priorConversation = await lazy.ConversationStore.findConversationById(
          page.toolConvId
        );
      }
    } catch (error) {
      lazy.console.error("could not load the page to modify", slug, error);
      return {
        error: `the page "${slug}" could not be loaded`,
        code: "page_load_failed",
      };
    }

    const referencedByUser = [
      ...(conversation?.getAllMentionURLs() ?? []),
    ].some(url => getSmartPageName(URL.parse(url)) === slug);
    if (!page || (page.convId !== conversation?.id && !referencedByUser)) {
      return {
        error: `this conversation has no page with the slug "${slug}"`,
        code: "page_not_found",
      };
    }

    // The surface to revise lives in that conversation's assistant message, so
    // without the exchange there is nothing to modify — regenerating is the
    // only way forward.
    if (!priorConversation?.messageCount) {
      return {
        error:
          `the conversation that generated "${slug}" is no longer available, ` +
          `so it can only be generated again from scratch`,
        code: "conversation_unavailable",
      };
    }

    return { page, conversation: priorConversation };
  }

  /**
   * The source list for a revised page: everything it was already built from,
   * then whatever this turn added, deduplicated by URL so a page re-read on a
   * later turn is listed once, with its newest extraction.
   *
   * @param {AITabSource[]} [previous] - Sources from the stored page.
   * @param {AITabSource[]} added - Sources read on this turn.
   * @returns {AITabSource[]}
   */
  static #mergeSources(previous, added) {
    if (!Array.isArray(previous) || !previous.length) {
      return added;
    }
    const byUrl = new Map(previous.map(source => [source?.url, source]));
    for (const source of added) {
      byUrl.set(source.url, source);
    }
    return Array.from(byUrl.values());
  }

  /**
   * The page title for metadata: the Header component's `title`, resolving a
   * `{ path }` binding against the surface's data model when needed.
   *
   * @param {A2UISurface} surface
   * @returns {string}
   */
  static #titleFromSurface(surface) {
    const header = (surface?.components || []).find(
      c => c?.component === "Header"
    );
    const title = header?.title;
    if (typeof title == "string") {
      return title;
    }
    if (title && typeof title == "object" && typeof title.path == "string") {
      const value = title.path.startsWith("/")
        ? AITab.#resolvePath(surface.dataModel ?? {}, title.path)
        : undefined;
      return typeof value == "string" ? value : "";
    }
    return "";
  }

  /**
   * The component types a validated surface uses, for telemetry: a
   * JSON-encoded array in render order, walking from the root through each
   * component's header then children. A component carrying a `layout` is
   * listed as `component_layout`.
   *
   * @param {A2UISurface} surface
   * @returns {string}
   */
  static #componentsUsed(surface) {
    const byId = new Map(surface.components.map(c => [c.id, c]));
    const visited = new Set();
    const used = [];
    const visit = id => {
      const comp = byId.get(id);
      if (!comp || visited.has(id)) {
        return;
      }
      visited.add(id);
      used.push(componentTypeName(comp));
      for (const ref of [comp.header, comp.children].flat()) {
        visit(typeof ref == "string" ? ref : ref?.componentId);
      }
    };
    visit(ROOT_ID);
    return JSON.stringify(used);
  }

  /**
   * Look up a page's preview-image (og:image) URL from Places. Never rejects;
   * returns "" on failure or when none is cached.
   *
   * @param {string} url
   * @returns {Promise<string>}
   */
  static async #getPageImage(url) {
    try {
      const pageInfo = await lazy.PlacesUtils.history
        .fetch(url, { includeMeta: true })
        .catch(() => null);
      // previewImageURL is a URL object when the page cached an og:image.
      return pageInfo?.previewImageURL ? pageInfo.previewImageURL.href : "";
    } catch (e) {
      lazy.console.debug("getPageImage failed", url, e);
      return "";
    }
  }

  /**
   * Make the browser the only source of link-item favicons: on the surface's
   * SourceLink items (the SourceLinks component, Header.references, and each
   * Highlights item's `sources`) and Card items, drop whatever the model put
   * in `favicon` — the catalog accepts the property, so instructing the model
   * to leave it out is no guarantee — and fill it from the favicons Places
   * has stored. Mutates `surface` in place; best-effort — an item keeps no
   * `favicon` when Places has none or the lookup fails.
   *
   * Places only has favicons for sites the user has visited, so these images
   * automatically count as seen URLs and are not subject to security
   * restrictions.
   *
   * @param {A2UISurface} surface
   * @param {AbortSignal} [signal] - Stops the remaining Places lookups (the
   *   caller re-checks it and discards the surface).
   */
  static async #hydrateFavicons(surface, signal) {
    const dataModel = surface?.dataModel ?? {};
    const itemsOf = value => {
      let items = value;
      if (AITab.#isBinding(value)) {
        // #resolvePath needs an absolute pointer, and validation skips
        // relative (template-scope) paths, so those must not resolve here
        // either. The resolved array is the same object the serialized
        // surface carries, so mutating its items reaches the output.
        items = value.path.startsWith("/")
          ? AITab.#resolvePath(dataModel, value.path)
          : undefined;
      }
      return Array.isArray(items) ? items : [];
    };

    const linkItems = [];
    for (const component of surface?.components ?? []) {
      switch (component?.component) {
        case "SourceLinks":
        case "Cards":
          linkItems.push(...itemsOf(component.items));
          break;
        case "Header":
          linkItems.push(...itemsOf(component.references?.items));
          break;
        case "Highlights":
          for (const item of itemsOf(component.items)) {
            linkItems.push(...itemsOf(item?.sources?.items));
          }
          break;
      }
    }

    // One Places lookup per unique href; sources typically repeat across the
    // header references and per-highlight sources.
    const faviconByHref = new Map();
    for (const item of linkItems) {
      if (!item || typeof item != "object") {
        continue;
      }
      delete item.favicon;
      if (typeof item.href != "string" || signal?.aborted) {
        continue;
      }
      if (!faviconByHref.has(item.href)) {
        faviconByHref.set(item.href, await AITab.#getFaviconURL(item.href));
      }
      const favicon = faviconByHref.get(item.href);
      if (favicon) {
        item.favicon = favicon;
      }
    }
  }

  /**
   * The page-icon: URL for a page whose favicon Places has stored. Never
   * rejects; returns "" when no favicon is stored for the page or the lookup
   * fails.
   *
   * @param {string} url
   * @returns {Promise<string>}
   */
  static async #getFaviconURL(url) {
    try {
      const favicon = await lazy.PlacesUtils.favicons.getFaviconForPage(
        Services.io.newURI(url)
      );
      return favicon ? `page-icon:${url}` : "";
    } catch (e) {
      lazy.console.debug("getFaviconURL failed", url, e);
      return "";
    }
  }

  /**
   * Extract a JSON object from the model's text output, tolerating markdown
   * code fences or surrounding prose.
   *
   * @param {string} text
   * @returns {A2UISurface|null} The candidate surface, not yet validated
   *   against the catalog.
   */
  static parsePageConfig(text) {
    // The outermost {...} span covers the common cases in two indexOf scans: a
    // bare object, an object inside a ```json fence, and an object wrapped in
    // unfenced prose (e.g. "Here is the page: {...} — let me know if...").
    const surface = AITab.#parseJsonSpan(text);
    if (surface) {
      return surface;
    }
    // The span only misses when prose on either side of a fenced block has
    // braces of its own, so fall back to the region between the first pair of
    // fences. Any language tag is left in it, which is harmless:
    // #parseJsonSpan starts at "{".
    const open = text.indexOf("```");
    const close = open < 0 ? -1 : text.indexOf("```", open + 3);
    const fenced =
      close > open ? AITab.#parseJsonSpan(text.slice(open + 3, close)) : null;
    if (fenced) {
      return fenced;
    }
    lazy.console.error("failed to parse surface JSON", text);
    return null;
  }

  /**
   * Expand the URL tokens in a parsed model surface back into full URLs.
   * Tokens inside free text are expanded, or removed when hallucinated. Link
   * fields (see LINK_FIELDS) must hold exactly one known token; an item whose
   * `href` does not is dropped, and any other invalid link field is removed,
   * so a URL the model was never given can't become a link or image.
   *
   * When `allowedLinks` is given, an `href` must also be one of them.
   *
   * @param {any} value - A parsed surface, or a value nested in one.
   * @param {UrlTokenizer} urlTokenizer - The tokenizer used for the prompt.
   * @param {Set<string>|null} [allowedLinks] - Normalized URLs (see
   *   linkKey) an `href` may point to; an item linking anywhere else is
   *   dropped. Null allows any URL the prompt contained.
   * @param {number} [recursionDepth] - Nesting level of `value`; callers
   *   leave it unset.
   * @returns {any} A detokenized copy, or undefined when `value` is dropped or
   *   nested deeper than MAX_EXPAND_RECURSION_DEPTH.
   */
  static expandSurfaceUrlTokens(
    value,
    urlTokenizer,
    allowedLinks = null,
    recursionDepth = 0
  ) {
    if (typeof value == "string") {
      return stripUnresolvedUrlTokens(
        expandUrlTokens(value, urlTokenizer.tokenToUrl)
      );
    }
    if (recursionDepth > MAX_EXPAND_RECURSION_DEPTH) {
      return undefined;
    }
    if (Array.isArray(value)) {
      return value
        .map(item =>
          AITab.expandSurfaceUrlTokens(
            item,
            urlTokenizer,
            allowedLinks,
            recursionDepth + 1
          )
        )
        .filter(item => item !== undefined);
    }
    if (!value || typeof value != "object") {
      return value;
    }
    const result = {};
    for (const [key, sub] of Object.entries(value)) {
      if (LINK_FIELDS.has(key)) {
        const url = urlTokenizer.resolveExactToken(sub);
        if (
          key == "href" &&
          url &&
          allowedLinks &&
          !allowedLinks.has(linkKey(url))
        ) {
          lazy.console.warn("dropping item with unseen link", url);
          return undefined;
        }
        if (url) {
          result[key] = url;
        } else if (key == "href") {
          lazy.console.warn("dropping item with unknown link", sub);
          return undefined;
        }
        continue;
      }
      const expanded = AITab.expandSurfaceUrlTokens(
        sub,
        urlTokenizer,
        allowedLinks,
        recursionDepth + 1
      );
      if (expanded !== undefined) {
        result[key] = expanded;
      }
    }
    return result;
  }

  /**
   * The inverse of expandSurfaceUrlTokens: replace the URLs in a stored surface
   * with tokens, so a surface the model wrote on an earlier turn can be handed
   * back to it under this turn's tokenizer.
   *
   * @param {any} value - A surface, or a value nested in one.
   * @param {UrlTokenizer} urlTokenizer - The tokenizer for this request.
   * @param {number} [recursionDepth] - Nesting level of `value`; callers
   *   leave it unset.
   * @returns {any} A tokenized copy, or undefined when `value` is nested deeper
   *   than MAX_EXPAND_RECURSION_DEPTH.
   */
  static tokenizeSurfaceUrls(value, urlTokenizer, recursionDepth = 0) {
    if (typeof value == "string") {
      return urlTokenizer.tokenizeText(value);
    }
    if (recursionDepth > MAX_EXPAND_RECURSION_DEPTH) {
      return undefined;
    }
    if (Array.isArray(value)) {
      return value
        .map(item =>
          AITab.tokenizeSurfaceUrls(item, urlTokenizer, recursionDepth + 1)
        )
        .filter(item => item !== undefined);
    }
    if (!value || typeof value != "object") {
      return value;
    }
    const result = {};
    for (const [key, sub] of Object.entries(value)) {
      const tokenized =
        LINK_FIELDS.has(key) && typeof sub == "string"
          ? urlTokenizer.formatToken(sub)
          : AITab.tokenizeSurfaceUrls(sub, urlTokenizer, recursionDepth + 1);
      if (tokenized !== undefined) {
        result[key] = tokenized;
      }
    }
    return result;
  }

  /**
   * The conversation's messages as they are sent to the model. They are stored
   * with full URLs, as chat messages are, and tokenized here under one
   * tokenizer, so a continued conversation's earlier turns share the token map
   * the reply is expanded with.
   *
   * @param {Conversation} conversation
   * @param {UrlTokenizer} urlTokenizer - The tokenizer for this request.
   * @returns {object[]} Messages in the chat-completions format.
   */
  static #tokenizedMessages(conversation, urlTokenizer) {
    return conversation.getMessagesInChatCompletionsFormat().map(message => {
      if (message.role == "system" || typeof message.content != "string") {
        return message;
      }
      const surface =
        message.role == "assistant"
          ? AITab.parsePageConfig(message.content)
          : null;
      const content = surface
        ? JSON.stringify(AITab.tokenizeSurfaceUrls(surface, urlTokenizer))
        : urlTokenizer.tokenizeText(message.content);
      return { ...message, content };
    });
  }

  /**
   * Ask the model for a validated surface for the given source content, either
   * in a conversation of its own or as the next turn of `priorConversation`.
   * Returns the validated surface and the conversation that produced it on
   * success, or an object with an `error` string describing why generation
   * failed.
   *
   * @param {object} options Options, as detailed in the Tool specification for AITab
   * @param {string} [options.focus] Focus of page information.
   * @param {string} options.sourceText Page content separated by PAGE_BREAK_TOKEN
   * @param {string} [options.modifyInstructions] What to change about the
   *   surface `priorConversation` already produced.
   * @param {Conversation} [options.priorConversation] The conversation to
   *   continue; a fresh one is built when there is none.
   * @param {Set<string>|null} options.allowedLinks URLs the surface may link
   *   to, as expandSurfaceUrlTokens takes them.
   * @param {AbortSignal} [options.signal] - Cancels the generation.
   * @param {{charsRead: number, charsOut: number}} options.stats - Filled in
   *   with the characters sent to and received from the model.
   * @returns {Promise<{surface: A2UISurface, conversation: Conversation}
   *   | {error: string}>}
   */
  static async #generateStructuredSurface({
    sourceText,
    focus,
    modifyInstructions,
    priorConversation,
    allowedLinks,
    signal,
    stats,
  }) {
    try {
      const { env } = await AITab.loadAssets();

      const { conversation, system, user } =
        await AITab.#resolvePromptSet(priorConversation);
      // An upsert at index 0, so a continued conversation is re-pointed at
      // today's catalog rather than the one it was first generated against.
      conversation.setSystemMessage(
        lazy.renderPrompt(system, { schemas: AITab.#schemaText(env) })
      );

      // A modification can arrive with nothing new to read. Rendering the
      // source-content template around an empty body would read as "the
      // sources are gone", so it is left out and only the instructions go in.
      const userParts = [];
      let charsRead = modifyInstructions?.length ?? 0;
      if (sourceText) {
        charsRead += sourceText.length + (focus?.length ?? 0);
        userParts.push(
          lazy.renderPrompt(user, {
            focus: focus ?? "",
            pageContent: sourceText,
          })
        );
      }
      if (modifyInstructions) {
        userParts.push(`${MODIFY_CONTENT_HEADING}\n\n${modifyInstructions}`);
      }
      if (!userParts.length) {
        return {
          error: "nothing was provided to change the page with",
          code: "nothing_to_change",
        };
      }
      conversation.addUserMessage(userParts.join(PAGE_BREAK));

      // URLs only reach the model as tokens, so any link it emits can be traced
      // back to a URL it was actually given.
      const urlTokenizer = new UrlTokenizer();
      const args = AITab.#tokenizedMessages(conversation, urlTokenizer);

      // Measured on the wire format, so this is the payload run() sends rather
      // than an estimate of it. Only a continued conversation can reach the
      // ceiling: a first generation is one capped turn against a fixed system
      // message.
      if (priorConversation) {
        const history = JSON.stringify(args);
        if (history.length > MAX_HISTORY_CHARS) {
          lazy.console.warn(
            `refusing to modify: history is ${history.length} chars`
          );
          return { error: HISTORY_LIMIT_ERROR, code: "history_limit" };
        }
      }

      if (signal?.aborted) {
        return CANCELED_RESULT;
      }

      stats.charsRead = charsRead;
      // An AbortSignal cannot be structured-cloned to the engine actor.
      // We pass it between functions in the calling process to identify
      // when it's aborted. When that happens, the engine sends a
      // cancellation message with a request ID so the backend
      // aborts the HTTP request to the model.
      const response = await conversation.run({
        args,
        token: await lazy.AuthProvider.getToken(),
        signal,
      });

      if (signal?.aborted) {
        return CANCELED_RESULT;
      }

      const text = response?.finalOutput?.trim();
      stats.charsOut = text?.length ?? 0;
      lazy.console.debug(
        `model returned ${text?.length || 0} chars`,
        text ? text.slice(0, 500) : response
      );
      if (!text) {
        return {
          error: "the model returned an empty response",
          code: "empty_response",
        };
      }

      const parsed = AITab.parsePageConfig(text);
      if (!parsed) {
        lazy.console.error("model did not return valid JSON:", text);
        return {
          error: "the model did not return valid JSON",
          code: "invalid_json",
        };
      }

      if (parsed.error && typeof parsed.error == "string") {
        lazy.console.warn("Model returned response error", parsed.error);
        return {
          error: sanitizeUntrustedContent(parsed.error),
          code: "content_related",
        };
      }

      const surface = AITab.expandSurfaceUrlTokens(
        parsed,
        urlTokenizer,
        allowedLinks
      );

      const result = AITab.buildSurface(surface, env);
      if (!result.ok) {
        lazy.console.error("surface failed validation", result.errors, surface);
        return {
          error: "the generated page did not match the required format",
          code: "invalid_format",
        };
      }

      // Record what the model wrote, with its URLs expanded as the user turn's
      // are. `run()` does not add its own reply, and a later modification
      // continues this conversation to revise this surface, so without this
      // there would be nothing there to revise.
      conversation.addAssistantMessage(JSON.stringify(result.surface));

      lazy.console.debug("structured surface validated successfully");
      return { surface: result.surface, conversation };
    } catch (error) {
      if (signal?.aborted) {
        return CANCELED_RESULT;
      }
      lazy.console.error("structured generation failed", error);
      return {
        error: `page generation failed: ${error?.message ?? error}`,
        code: "generation_failed",
      };
    }
  }

  /**
   * Load the aitab prompt templates and engine from Remote Settings. The
   * "ai-window-prompts" collection ships a packaged dump
   * (services/settings/dumps/main/ai-window-prompts.json), so the records are
   * always available — offline and on first run — without an in-tree fallback.
   * A genuine failure propagates and is surfaced as a generation error by the
   * caller.
   *
   * @param {Conversation} [existing] - Conversation to render the templates
   *   into, for a modification, which gets an aitab engine attached; a fresh
   *   one is built when there is none.
   * @returns {Promise<PromptSet>}
   */
  static async #resolvePromptSet(existing) {
    let conversation = existing;
    if (conversation) {
      const { engine, parameters } = await lazy.buildEngineForFeature(
        lazy.MODEL_FEATURES.AITAB
      );
      conversation.engine = engine;
      conversation.parameters = parameters;
    } else {
      conversation = await lazy.buildConversation(lazy.MODEL_FEATURES.AITAB);
    }
    const [{ prompt: system }, { prompt: user }] = await Promise.all([
      lazy.loadPrompt(lazy.MODEL_FEATURES.AITAB, {
        module: "system-instructions",
      }),
      lazy.loadPrompt(lazy.MODEL_FEATURES.AITAB, { module: "user-data" }),
    ]);
    return { conversation, system, user };
  }

  /**
   * Serialize the catalog for a prompt's `{schemas}` placeholder: the shared
   * `$defs` once up front (the component schemas reference them), then each
   * component's property schema. Compact (no indentation). The natural-language
   * assembly rules live in the Remote Settings system-instructions prompt, not
   * here — this method injects catalog data only.
   *
   * @param {ValidationEnv} env
   * @returns {string}
   */
  static #schemaText(env) {
    const { catalog, names } = env;
    const parts = [];
    if (catalog.$defs) {
      parts.push(`=== $defs (shared) ===\n${JSON.stringify(catalog.$defs)}`);
    }
    for (const name of names) {
      parts.push(
        `=== ${name} ===\n${JSON.stringify(catalog.components[name])}`
      );
    }
    return parts.join("\n\n");
  }

  /**
   * Parse the span from the first "{" to the last "}" of `text` as JSON, or
   * null when there is no such span or it does not parse.
   *
   * @param {string} text
   * @returns {A2UISurface|null}
   */
  static #parseJsonSpan(text) {
    const start = text.indexOf("{");
    const end = text.lastIndexOf("}");
    if (start < 0 || end <= start) {
      return null;
    }
    try {
      return JSON.parse(text.slice(start, end + 1));
    } catch {
      return null;
    }
  }

  /**
   * Turn a title into a lowercase kebab-case slug.
   *
   * @param {string} title
   * @returns {string}
   */
  static #slugify(title) {
    const slug = title
      .slice(0, 60)
      .toLowerCase()
      .normalize("NFD")
      .replace(/\p{M}/gu, "")
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "");
    // A title with no ASCII alphanumerics at all (e.g. one written in Japanese)
    // slugs to the empty string, which is not a usable identifier.
    return slug || "aitab";
  }
}
