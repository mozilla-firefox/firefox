/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A generated surface before hydration.
 *
 * @typedef {import("../../models/aitab/AITab.sys.mjs").A2UISurface} A2UISurface
 */

/**
 * A component with its catalog properties.
 *
 * @typedef {import("../../models/aitab/AITab.sys.mjs").A2UIComponent &
 *   import("../../models/aitab/AITab.sys.mjs").A2UIComponentProps} AITabComponent
 */

/**
 * A Smart Page prepared for rendering.
 *
 * @typedef {object} HydratedAITabData
 * @property {string} component - Root component type.
 * @property {AITabComponent[]} children - Hydrated header and content components.
 */

import { httpUrl } from "chrome://browser/content/aiwindow/modules/AITabUtils.mjs";

/**
 * Parses A2UI JSON objects to prepare for rendering
 */
export class A2UI {
  #components;
  #dataModel;
  #hydrators;
  #surface;

  /**
   * @param {A2UISurface} a2ui - Generated surface to hydrate.
   */
  constructor(a2ui) {
    this.#surface = structuredClone(a2ui);

    this.#hydrators = new Map();
    this.#hydrators.set("Highlights", this.#hydrateHighlights.bind(this));
    this.#hydrators.set("SourceLinks", this.#hydrateSourceLinks.bind(this));
    this.#hydrators.set("TextBlock", this.#hydrateTextBlock.bind(this));
    this.#hydrators.set("RankedTable", this.#hydrateRankedTable.bind(this));
    this.#hydrators.set("Cards", this.#hydrateCards.bind(this));
    this.#hydrators.set("Timeline", this.#hydrateTimeline.bind(this));
    this.#hydrators.set("List", this.#hydrateList.bind(this));
  }

  /**
   * Iterates the A2UI JSON and prepares for rendering
   *
   * @returns {HydratedAITabData} Page with hydrated child components.
   */
  toUI() {
    const surface = structuredClone(this.#surface);
    this.#dataModel = surface.dataModel;
    this.#components = new Map();
    surface.components.forEach(component => {
      this.#components.set(component.id, component);
    });

    const root = this.#components.get("root");
    if (!root) {
      return {
        component: "Page",
        children: [],
      };
    }

    const page = {
      component: root.component,
      children: [],
    };

    this.#getHeader(root, page);
    this.#getChildren(root, page);

    return page;
  }

  #hydrateList(component, dataModel = undefined) {
    if (component.title) {
      component.title = this.#resolveValue(component.title, "", dataModel);
    }

    if (component.description) {
      component.description = this.#resolveValue(
        component.description,
        "",
        dataModel
      );
    }

    component.groups = this.#resolveArray(component.groups, [], dataModel);

    return component;
  }

  #hydrateTimeline(component, dataModel = undefined) {
    if (component.title) {
      component.title = this.#resolveValue(component.title, "", dataModel);
    }

    if (component.description) {
      component.description = this.#resolveValue(
        component.description,
        "",
        dataModel
      );
    }

    component.items = this.#resolveArray(component.items, [], dataModel);

    return component;
  }

  #hydrateCards(component, dataModel = undefined) {
    if (component.title) {
      component.title = this.#resolveValue(component.title, "", dataModel);
    }

    component.items = this.#resolveArray(component.items, [], dataModel);

    return component;
  }

  #hydrateRankedTable(component, dataModel = undefined) {
    if (component.title) {
      component.title = this.#resolveValue(component.title, "", dataModel);
    }

    if (component.description) {
      component.description = this.#resolveValue(
        component.description,
        "",
        dataModel
      );
    }

    component.rows = this.#resolveArray(component.rows, [], dataModel);

    return component;
  }

  #hydrateTextBlock(component, dataModel = undefined) {
    if (component.lead) {
      component.lead = this.#resolveValue(component.lead, "", dataModel);
    }

    if (component.paragraphs) {
      component.paragraphs = this.#resolveArray(
        component.paragraphs,
        [],
        dataModel
      );
    }

    return component;
  }

  #hydrateHighlights(component, dataModel = undefined) {
    component.title = component.title
      ? this.#resolveValue(component.title, "", dataModel)
      : undefined;

    component.items = this.#resolveArray(
      component.items,
      [],
      dataModel,
      highlightItem => this.#hydrateHighlightItem(highlightItem, dataModel)
    );

    return component;
  }

  #getHeader(root, page) {
    if (!root.header || !this.#components.has(root.header)) {
      return;
    }

    const header = this.#components.get(root.header);

    if (header.eyebrow) {
      header.eyebrow = this.#resolveValue(header.eyebrow, "");
    }

    if (header.title) {
      header.title = this.#resolveValue(header.title, "");
    }

    if (header.subhead) {
      header.subhead = this.#resolveValue(header.subhead, "");
    }

    if (header.references) {
      header.references = this.#hydrateSourceLinks(header.references);
    }

    page.children.push(header);
  }

  #hydrateSourceLinks(sourceLinks, dataModel = undefined) {
    if (sourceLinks.title) {
      sourceLinks.title = this.#resolveValue(sourceLinks.title, "", dataModel);
    }

    if (sourceLinks.items) {
      sourceLinks.items = this.#resolveArray(sourceLinks.items, [], dataModel);
    }

    return sourceLinks;
  }

  #getChildren(root, page) {
    if (!root.children) {
      return;
    }

    if (Array.isArray(root.children)) {
      for (const child of root.children) {
        if (!this.#components.has(child)) {
          continue;
        }

        const component = this.#components.get(child);
        this.#pushChild(page, this.#hydrateComponent(component));
      }

      return;
    }

    const { componentId, path } = root.children;
    if (
      typeof componentId !== "string" ||
      typeof path !== "string" ||
      !this.#components.has(componentId)
    ) {
      return;
    }

    const dataModels = this.#resolvePath(path, []);
    if (!Array.isArray(dataModels)) {
      return;
    }

    const template = this.#components.get(componentId);
    for (const dataModel of dataModels) {
      const component = structuredClone(template);
      this.#pushChild(page, this.#hydrateComponent(component, dataModel));
    }
  }

  #pushChild(page, component) {
    const child = this.#normalizeHighlights(component);
    if (child) {
      page.children.push(child);
    }
  }

  /**
   * Drops a Highlights block with no statements and turns one with a single
   * statement into a TextBlock. The stored surface is not changed, so the
   * block keeps its id.
   *
   * Link-click telemetry reports the converted block as a TextBlock.
   * Bug 2088273 decides how Glean reports these conversions.
   *
   * @param {AITabComponent} component - A hydrated body component.
   * @returns {AITabComponent|null} The component to render, or null to drop it.
   */
  #normalizeHighlights(component) {
    if (component?.component != "Highlights") {
      return component;
    }
    const items = (component.items ?? []).filter(Boolean);
    if (!items.length) {
      return null;
    }
    if (items.length > 1) {
      return component;
    }
    const [item] = items;
    return {
      id: component.id,
      component: "TextBlock",
      lead: item.title || component.title || item.eyebrow || "",
      paragraphs: item.body ? [item.body] : [],
      references: (item.sources?.items ?? []).filter(source =>
        httpUrl(source?.href)
      ),
    };
  }

  #hydrateComponent(component, dataModel = undefined) {
    const hydrator = this.#hydrators.get(component?.component);
    if (component && typeof hydrator === "function") {
      return hydrator(component, dataModel);
    }

    return component;
  }

  #resolveValue(target, defaultValue = undefined, dataModel = undefined) {
    if (!target) {
      return null;
    }

    if (
      typeof target === "object" &&
      !Array.isArray(target) &&
      typeof target.path === "string"
    ) {
      return this.#resolvePath(target.path, defaultValue, dataModel);
    }

    return target;
  }

  #resolveArray(
    target,
    defaultValue = [],
    dataModel = undefined,
    itemMapper = undefined
  ) {
    const value = this.#resolveValue(target, defaultValue, dataModel);
    if (!Array.isArray(value)) {
      return defaultValue;
    }

    return itemMapper
      ? value.map(item => (item ? itemMapper(item) : item))
      : value;
  }

  #hydrateHighlightItem(highlightItem, dataModel = undefined) {
    if (highlightItem.sources) {
      highlightItem.sources = this.#hydrateSourceLinks(
        highlightItem.sources,
        dataModel
      );
    }

    return highlightItem;
  }

  #resolvePath(path, defaultValue = undefined, dataModel = undefined) {
    const absolutePath = path.startsWith("/");
    if (!absolutePath && !dataModel) {
      return defaultValue;
    }

    let value = absolutePath ? this.#dataModel : dataModel;
    const fieldPath = path.split("/");
    if (absolutePath) {
      fieldPath.shift();
    }

    while (fieldPath.length) {
      if (value == null || typeof value !== "object") {
        return undefined;
      }

      let key = fieldPath.shift();
      key = key.replace(/~1/g, "/").replace(/~0/g, "~");

      value = value[key];
    }

    return value;
  }
}
