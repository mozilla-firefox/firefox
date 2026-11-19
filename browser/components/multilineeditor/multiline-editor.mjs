/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import {
  Decoration,
  DecorationSet,
  EditorState,
  EditorView,
  MarkdownParser,
  MarkdownSerializer,
  Plugin as PmPlugin,
  Schema,
  TextSelection,
  baseKeymap,
  basicSchema,
  defaultMarkdownParser,
  defaultMarkdownSerializer,
  history as historyPlugin,
  keymap,
  redo as historyRedo,
  undo as historyUndo,
} from "chrome://browser/content/multilineeditor/prosemirror.bundle.mjs";

const PLACEHOLDER_TRANSITION_FRACTION = 0.2;

/**
 * A ProseMirror transaction.
 *
 * TODO (Bug 2052510): Export and use `Transaction` from the ProseMirror bundle.
 *
 * @typedef {EditorState["tr"]} Transaction
 */

/**
 * @typedef {object} MultilineEditorPlugin
 * @property {object} [schemaExtension] - Schema extensions
 * @property {object} [schemaExtension.nodes] - Node specs
 * @property {object} [schemaExtension.marks] - Mark specs
 * @property {function(MultilineEditor): PmPlugin} [createPlugin] - Create plugin
 * @property {object} [parseMarkdown] - Markdown token specs
 * @property {object} [toMarkdown] - Markdown serializer functions
 */

/**
 * @class MultilineEditor
 *
 * A ProseMirror-based multiline editor.
 *
 * @property {string} placeholder - Placeholder text for the editor.
 * @property {Array<MultilineEditorPlugin>} plugins - Editor plugins.
 * @property {boolean} readOnly - Whether the editor is read-only.
 */
export class MultilineEditor extends MozLitElement {
  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    placeholder: { type: String, reflect: true, fluent: true },
    placeholderHints: { type: Array, attribute: false },
    readOnly: { type: Boolean, reflect: true, attribute: "readonly" },
    plugins: { type: Array, attribute: false },
    maxLength: { type: Number, attribute: "maxlength" },
    // ARIA combobox state forwarded from the host to the inner contenteditable.
    // reflect:false so the host attribute stays the source of truth.
    ariaControls: { type: String, attribute: "aria-controls", reflect: false },
    ariaAutoComplete: {
      type: String,
      attribute: "aria-autocomplete",
      reflect: false,
    },
    ariaExpanded: { type: String, attribute: "aria-expanded", reflect: false },
    ariaActiveDescendant: {
      type: String,
      attribute: "aria-activedescendant",
      reflect: false,
    },
    ariaHasPopup: { type: String, attribute: "aria-haspopup", reflect: false },
    showPlaceholderAnimation: {
      type: Boolean,
      attribute: "show-placeholder-animation",
    },
  };

  static PLACEHOLDER_HINT_DURATION_MS = 3000;

  static schema = new Schema({
    nodes: {
      doc: basicSchema.spec.nodes.get("doc"),
      paragraph: basicSchema.spec.nodes.get("paragraph"),
      text: basicSchema.spec.nodes.get("text"),
    },
  });

  /**
   * ARIA state forwarded from the host's reactive Lit properties to the inner
   * contenteditable. Each entry pairs the property name (used in `updated()`
   * change detection) with the attribute name applied to the inner element.
   */
  static FORWARDED_ARIA = [
    { prop: "ariaControls", attr: "aria-controls" },
    { prop: "ariaAutoComplete", attr: "aria-autocomplete" },
    { prop: "ariaExpanded", attr: "aria-expanded" },
    { prop: "ariaActiveDescendant", attr: "aria-activedescendant" },
    { prop: "ariaHasPopup", attr: "aria-haspopup" },
  ];

  #instanceId = crypto.randomUUID();
  #placeholderVersion = 0;
  #placeholderTimer = null;
  #reducedMotionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
  #onReducedMotionChange = () => this.#refreshView();
  #pendingValue = "";
  #placeholderPlugin;
  #plugins;
  #suppressInputEvent = false;
  #view;
  #markdownSerializer;
  #innerRole = null;
  #valueCacheDoc = null;
  #valueCacheStr = "";

  constructor() {
    super();

    this.placeholder = "";
    this.placeholderHints = [];
    this.plugins = [];
    this.readOnly = false;
    this.maxLength = 0;
    this.showPlaceholderAnimation = false;
    this.#placeholderPlugin = this.#createPlaceholderPlugin();
    const plugins = [
      historyPlugin(),
      keymap({
        Enter: () => true,
        "Shift-Enter": (state, dispatch) =>
          this.#insertParagraph(state, dispatch),
        "Mod-z": historyUndo,
        "Mod-y": historyRedo,
        "Shift-Mod-z": historyRedo,
      }),
      keymap(baseKeymap),
      this.#placeholderPlugin,
    ];

    if (document.contentType === "application/xhtml+xml") {
      plugins.push(this.#createCleanupOrphanedBreaksPlugin());
    }

    this.#plugins = plugins;
  }

  /**
   * The ProseMirror view instance.
   *
   * @type {EditorView}
   */
  get view() {
    return this.#view;
  }

  /**
   * Whether the editor is composing.
   *
   * @type {boolean}
   */
  get composing() {
    return this.#view?.composing ?? false;
  }

  /**
   * The current text content of the editor.
   *
   * @type {string}
   */
  get value() {
    if (!this.#view) {
      return this.#pendingValue;
    }
    const doc = this.#view.state.doc;
    if (this.#valueCacheDoc == doc) {
      return this.#valueCacheStr;
    }
    this.#valueCacheDoc = doc;
    this.#valueCacheStr = this.#markdownSerializer
      ? this.#markdownSerializer.serialize(doc)
      : doc.textBetween(0, doc.content.size, "\n", "\n");
    return this.#valueCacheStr;
  }

  /**
   * Set the text content of the editor.
   *
   * @param {string} val
   */
  set value(val) {
    if (!this.#view) {
      this.#pendingValue = val;
      return;
    }

    if (val === this.value) {
      return;
    }

    const state = this.#view.state;
    const doc = this.#textToDoc(val, state.schema);

    const tr = state.tr.replaceWith(0, state.doc.content.size, doc.content);
    const cursorPos = this.#posFromTextOffset(val.length, tr.doc);
    tr.setSelection(
      TextSelection.between(
        tr.doc.resolve(cursorPos),
        tr.doc.resolve(cursorPos)
      )
    );
    this.#dispatchSilently(tr);
  }

  /**
   * Set a range of text and selection.
   *
   * @param {string} replacement - The string to insert.
   * @param {number} [start] - Index of the first character to replace.
   * @param {number} [end] - Index of the after the last character to replace.
   * @param {SelectionMode} [selectionMode] - Selection after the replacement.
   */
  setRangeText(replacement, start, end, selectionMode) {
    if (!this.#view) {
      return;
    }

    const state = this.#view.state;
    const from = this.#posFromTextOffset(
      start ?? this.selectionStart,
      state.doc
    );
    const to = this.#posFromTextOffset(end ?? this.selectionEnd, state.doc);
    const tr = state.tr.insertText(replacement, from, to);

    let selectionStart;
    let selectionEnd;
    switch (selectionMode) {
      case "select":
        selectionStart = from;
        selectionEnd = tr.mapping.map(to);
        break;
      case "start":
        selectionStart = from;
        selectionEnd = from;
        break;
      case "end":
        selectionStart = selectionEnd = tr.mapping.map(to);
        break;
      case "preserve":
      default:
        selectionStart = tr.mapping.map(state.selection.from);
        selectionEnd = tr.mapping.map(state.selection.to);
    }

    tr.setSelection(
      TextSelection.between(
        tr.doc.resolve(selectionStart),
        tr.doc.resolve(selectionEnd)
      )
    );
    this.#dispatchSilently(tr);
  }

  /**
   * Dispatch a transaction for programmatic updates without adding it to the
   * history and firing an input event.
   *
   * @param {Transaction} tr - The transaction to dispatch.
   */
  #dispatchSilently(tr) {
    tr.setMeta("addToHistory", false);
    this.#suppressInputEvent = true;
    try {
      this.#view.dispatch(tr);
    } finally {
      this.#suppressInputEvent = false;
    }
  }

  /**
   * The plain-text content of the editor with atom nodes (e.g. mentions)
   * contributing zero characters. Useful for persisting input alongside a
   * structured list of inline atoms.
   *
   * @type {string}
   */
  get plainText() {
    if (!this.#view) {
      return this.#pendingValue;
    }
    return this.#view.state.doc.textBetween(
      0,
      this.#view.state.doc.content.size,
      "\n",
      ""
    );
  }

  /**
   * Convert a ProseMirror document position to a text-character offset that
   * matches `plainText`.
   *
   * @param {number} pos
   * @returns {number}
   */
  posToTextOffset(pos) {
    return this.#textOffsetFromPos(pos, this.#view?.state.doc, "\n", "");
  }

  /**
   * Convert a text-character offset (matching `plainText`) to a ProseMirror
   * document position.
   *
   * @param {number} offset
   * @returns {number}
   */
  textOffsetToPos(offset) {
    return this.#posFromTextOffset(offset);
  }

  /**
   * The start offset of the selection.
   *
   * @type {number}
   */
  get selectionStart() {
    if (!this.#view) {
      return 0;
    }
    return this.#textOffsetFromPos(this.#view.state.selection.from);
  }

  /**
   * Set the start offset of the selection.
   *
   * @param {number} val
   */
  set selectionStart(val) {
    this.setSelectionRange(val, this.selectionEnd ?? val);
  }

  /**
   * The end offset of the selection.
   *
   * @type {number}
   */
  get selectionEnd() {
    if (!this.#view) {
      return 0;
    }
    return this.#textOffsetFromPos(this.#view.state.selection.to);
  }

  /**
   * Set the end offset of the selection.
   *
   * @param {number} val
   */
  set selectionEnd(val) {
    this.setSelectionRange(this.selectionStart ?? 0, val);
  }

  /**
   * Set the selection range in the editor.
   *
   * @param {number} start
   * @param {number} end
   */
  setSelectionRange(start, end) {
    if (!this.#view) {
      return;
    }

    const doc = this.#view.state.doc;
    const docSize = doc.content.size;
    const fromOffset = start ?? 0;
    const [resolvedFrom, resolvedTo] = this.#posFromTextOffsets(
      [fromOffset, end ?? fromOffset],
      doc
    );
    const from = Math.max(0, Math.min(resolvedFrom, docSize));
    const to = Math.max(0, Math.min(resolvedTo, docSize));

    if (
      this.#view.state.selection.from === from &&
      this.#view.state.selection.to === to
    ) {
      return;
    }

    let selection;
    try {
      selection = TextSelection.between(doc.resolve(from), doc.resolve(to));
    } catch (_e) {
      const anchor = Math.max(0, Math.min(to, docSize));
      selection = TextSelection.near(doc.resolve(anchor));
    }
    this.#view.dispatch(
      this.#view.state.tr.setSelection(selection).scrollIntoView()
    );
  }

  /**
   * Select all text in the editor.
   */
  select() {
    this.setSelectionRange(0, this.value.length);
    this.focus();
  }

  /**
   * Focus the editor.
   */
  focus() {
    this.#view?.focus();
    super.focus();
  }

  /**
   * Run ProseMirror's normal paste pipeline with the given DataTransfer's
   * contents. Used by the context-menu cmd_paste route, which on some
   * platforms (e.g. Windows) does not reliably reach a contenteditable
   * inside a shadow DOM — TODO(Bug 2047067), remove once that is fixed.
   *
   * `new ClipboardEvent("paste", { clipboardData })` can't be used because
   * Firefox's ClipboardEventInit dictionary has no clipboardData member, so
   * the field is dropped and the event arrives at ProseMirror with null
   * clipboardData. EditorView.pasteText / pasteHTML invoke the same paste
   * machinery (plugins, history entry) directly.
   *
   * @param {DataTransfer} dataTransfer - Clipboard data to paste.
   */
  paste(dataTransfer) {
    if (!this.#view || !dataTransfer) {
      return;
    }
    const htmlData = dataTransfer.getData("text/html");
    const textData = dataTransfer.getData("text/plain");
    if (!htmlData && !textData) {
      return;
    }
    this.#view.focus();
    if (htmlData) {
      this.#view.pasteHTML(htmlData);
    } else {
      this.#view.pasteText(textData);
    }
  }

  /**
   * Called when the element is added to the DOM.
   */
  connectedCallback() {
    super.connectedCallback();
    // Capture a consumer-supplied role for the inner contenteditable before
    // hiding this host element from the accessibility tree. With
    // delegatesFocus, screen readers focus the inner contenteditable, so its
    // role and ARIA state are what get announced.
    const hostRole = this.getAttribute("role");
    if (hostRole && hostRole !== "presentation") {
      this.#innerRole = hostRole;
    }
    this.setAttribute("role", "presentation");
    this.#reducedMotionQuery.addEventListener(
      "change",
      this.#onReducedMotionChange
    );
  }

  /**
   * Called when the element is removed from the DOM.
   */
  disconnectedCallback() {
    this.#reducedMotionQuery.removeEventListener(
      "change",
      this.#onReducedMotionChange
    );
    clearTimeout(this.#placeholderTimer);
    this.#destroyView();
    this.#pendingValue = "";
    super.disconnectedCallback();
  }

  /**
   * Called after the element’s DOM has been rendered for the first time.
   */
  firstUpdated() {
    this.#createView();
  }

  /**
   * Called when the element’s properties are updated.
   *
   * @param {Map} changedProps
   */
  updated(changedProps) {
    if (
      changedProps.has("placeholder") ||
      changedProps.has("placeholderHints") ||
      changedProps.has("plugins") ||
      changedProps.has("readOnly") ||
      changedProps.has("showPlaceholderAnimation")
    ) {
      this.#refreshView();
    }
    this.#applyForwardedAria(changedProps);
  }

  #buildSchema() {
    const nodes = {};
    const marks = {};

    for (const plugin of this.plugins) {
      Object.assign(nodes, plugin.schemaExtension?.nodes ?? {});
      Object.assign(marks, plugin.schemaExtension?.marks ?? {});
    }

    if (Object.keys(nodes).length === 0 && Object.keys(marks).length === 0) {
      return MultilineEditor.schema;
    }

    const baseSchemaSpec = MultilineEditor.schema.spec;
    return new Schema({
      nodes: baseSchemaSpec.nodes.append(nodes),
      marks: baseSchemaSpec.marks.append(marks),
    });
  }

  #createMarkdownClipboardPlugin(schema) {
    const parsers = {};
    const serializers = {};

    for (const plugin of this.plugins) {
      Object.assign(parsers, plugin.parseMarkdown ?? {});
      Object.assign(serializers, plugin.toMarkdown ?? {});
    }

    if (
      Object.keys(parsers).length === 0 &&
      Object.keys(serializers).length === 0
    ) {
      return null;
    }

    // Filter parser tokens that reference nodes or marks not in the schema.
    const filterParserTokens = tokens => {
      const filtered = {};
      for (const [key, spec] of Object.entries(tokens)) {
        if (
          Object.values(spec).every(
            val =>
              typeof val !== "string" || schema.nodes[val] || schema.marks[val]
          )
        ) {
          filtered[key] = spec;
        }
      }
      return filtered;
    };

    const filterBySchema = (items, schemaItems) => {
      const filtered = {};
      for (const [key, value] of Object.entries(items)) {
        if (schemaItems[key]) {
          filtered[key] = value;
        }
      }
      return filtered;
    };

    const parser = new MarkdownParser(schema, defaultMarkdownParser.tokenizer, {
      ...filterParserTokens(defaultMarkdownParser.tokens),
      ...parsers,
    });
    const serializer = new MarkdownSerializer(
      {
        ...defaultMarkdownSerializer.nodes,
        ...filterBySchema(serializers, schema.nodes),
      },
      {
        ...defaultMarkdownSerializer.marks,
        ...filterBySchema(serializers, schema.marks),
      }
    );
    this.#markdownSerializer = serializer;

    return new PmPlugin({
      props: {
        clipboardTextParser: text => parser.parse(text)?.content,
        clipboardTextSerializer: slice => serializer.serialize(slice.content),
      },
    });
  }

  #createView() {
    const mount = this.renderRoot.querySelector(".multiline-editor");
    if (!mount) {
      return;
    }

    const schema = this.#buildSchema();
    const markdownPlugin = this.#createMarkdownClipboardPlugin(schema);
    const customPlugins = this.plugins
      .map(plugin => plugin.createPlugin?.(this))
      .filter(Boolean);

    const state = EditorState.create({
      schema,
      plugins: [
        ...this.#plugins,
        ...(markdownPlugin ? [markdownPlugin] : []),
        ...customPlugins,
      ],
    });

    this.#view = new EditorView(mount, {
      state,
      attributes: this.#viewAttributes(),
      editable: () => !this.readOnly,
      dispatchTransaction: this.#dispatchTransaction,
      handleDOMEvents: {
        contextmenu: (view, event) => {
          if (this.readOnly) {
            return false;
          }
          // TODO(Bug 2047067): right-clicks on the inner contenteditable show
          // a native context menu on Windows whose Paste command does not
          // reach ProseMirror. Forward the event to the host so it can show
          // its own menu. Remove this workaround once the platform bug is
          // fixed.
          event.preventDefault();
          const host = view.dom.getRootNode().host;
          if (host && host != view.dom) {
            host.dispatchEvent(new PointerEvent("contextmenu", event));
          }
          return true;
        },
      },
      nodeViews: Object.assign(
        {},
        ...this.plugins.map(plugin => plugin.nodeViews).filter(Boolean)
      ),
    });

    // An `input` event is already dispatched from #dispatchTransaction:
    // Stop the contenteditable’s duplicate event from propagating.
    this.#view.dom.addEventListener("input", event => event.stopPropagation(), {
      capture: true,
    });

    if (this.#pendingValue) {
      this.value = this.#pendingValue;
      this.#pendingValue = "";
    }
  }

  #destroyView() {
    this.#view?.destroy();
    this.#view = null;
  }

  #dispatchTransaction = tr => {
    if (!this.#view) {
      return;
    }

    if (
      tr.docChanged &&
      this.maxLength > 0 &&
      tr.doc.content.size > this.maxLength
    ) {
      const newText = tr.doc.textBetween(0, tr.doc.content.size, "\n", "\n");
      const truncated = newText.slice(0, this.maxLength);

      /* tr.doc.content.size includes ProseMirror structural tokens, so check actual text length here */
      if (newText.length > this.maxLength && truncated !== this.value) {
        const doc = this.#textToDoc(truncated, this.#view.state.schema);
        tr = this.#view.state.tr
          .replaceWith(0, this.#view.state.doc.content.size, doc.content)
          .scrollIntoView();
      }
    }

    // Reading `this.value` serializes the whole document, so only do it when an
    // input event will actually be dispatched (i.e. the doc changed). Capture
    // the previous text before updating the state.
    const willDispatchInput = tr.docChanged && !this.#suppressInputEvent;
    const prevText = willDispatchInput ? this.value : null;
    const prevSelection = this.#view.state.selection;
    const nextState = this.#view.state.apply(tr);
    this.#view.updateState(nextState);

    const selectionChanged =
      tr.selectionSet &&
      (prevSelection.from !== nextState.selection.from ||
        prevSelection.to !== nextState.selection.to);

    if (selectionChanged) {
      this.#dispatchSelectionChange();
    }

    if (willDispatchInput) {
      const nextText = this.value;
      let insertedText = "";
      for (const step of tr.steps) {
        insertedText += step.slice?.content?.textBetween(
          0,
          step.slice.content.size,
          "",
          ""
        );
      }
      this.dispatchEvent(
        new InputEvent("input", {
          bubbles: true,
          composed: true,
          data: insertedText || null,
          inputType:
            insertedText || nextText.length >= prevText.length
              ? "insertText"
              : "deleteContentBackward",
        })
      );
    }
  };

  #dispatchSelectionChange() {
    this.dispatchEvent(
      new Event("selectionchange", { bubbles: true, composed: true })
    );
  }

  #insertParagraph(state, dispatch) {
    const paragraph = state.schema.nodes.paragraph;
    if (!paragraph) {
      return false;
    }
    const { $from } = state.selection;
    let tr = state.tr;
    if (!state.selection.empty) {
      tr = tr.deleteSelection();
    }
    tr = tr.split(tr.mapping.map($from.pos)).scrollIntoView();
    dispatch(tr);
    return true;
  }

  #createPlaceholderHints() {
    const hints = document.createElement("ul");
    hints.id = `placeholder-hints-${this.#instanceId}-${this.#placeholderVersion}`;
    hints.className = "placeholder-hints";
    hints.setAttribute("role", "presentation");

    for (const text of [this.placeholder, ...this.placeholderHints]) {
      const item = document.createElement("li");
      item.textContent = text;
      hints.appendChild(item);
    }

    this.#startPlaceholderRotation(hints);
    return hints;
  }

  /**
   * Creates a plugin that shows a placeholder when the editor is empty.
   *
   * @returns {PmPlugin}
   */
  #createPlaceholderPlugin() {
    return new PmPlugin({
      props: {
        decorations: ({ doc }) => {
          if (
            doc.childCount !== 1 ||
            !doc.firstChild.isTextblock ||
            doc.firstChild.content.size !== 0
          ) {
            return null;
          }

          if (this.placeholderHints.length) {
            return DecorationSet.create(doc, [
              Decoration.widget(1, () => this.#createPlaceholderHints(), {
                key: `placeholder-hints-${this.#instanceId}-${this.#placeholderVersion}`,
                side: -1,
                ignoreSelection: true,
              }),
            ]);
          }

          if (!this.placeholder) {
            return null;
          }

          return DecorationSet.create(doc, [
            Decoration.node(0, doc.firstChild.nodeSize, {
              class: "placeholder",
              "data-placeholder": this.placeholder,
            }),
          ]);
        },
      },
    });
  }

  /**
   * Creates a plugin that removes orphaned hard breaks from empty paragraphs.
   *
   * In XHTML contexts the trailing break element in paragraphs are rendered as
   * uppercase (<BR> instead of <br>). ProseMirror seems to have issues parsing
   * these breaks, which leads to orphaned breaks after deleting text content.
   *
   * @returns {PmPlugin}
   */
  #createCleanupOrphanedBreaksPlugin() {
    return new PmPlugin({
      appendTransaction(transactions, prevState, nextState) {
        if (!transactions.some(tr => tr.docChanged)) {
          return null;
        }

        const tr = nextState.tr;
        let modified = false;

        nextState.doc.descendants((nextNode, nextPos) => {
          if (
            nextNode.type.name !== "paragraph" ||
            nextNode.textContent ||
            nextNode.childCount === 0
          ) {
            return true;
          }

          for (let i = 0; i < nextNode.childCount; i++) {
            if (nextNode.child(i).type.name === "hard_break") {
              const prevNode = prevState.doc.nodeAt(nextPos);
              if (prevNode?.type.name === "paragraph" && prevNode.textContent) {
                tr.replaceWith(
                  nextPos + 1,
                  nextPos + nextNode.content.size + 1,
                  []
                );
                modified = true;
              }
              break;
            }
          }

          return true;
        });

        return modified ? tr : null;
      },
    });
  }

  #applyForwardedAria(changedProps) {
    if (!this.#view) {
      return;
    }
    for (const { prop, attr } of MultilineEditor.FORWARDED_ARIA) {
      if (!changedProps.has(prop)) {
        continue;
      }
      if (this[prop] == null) {
        this.#view.dom.removeAttribute(attr);
      } else {
        this.#view.dom.setAttribute(attr, this[prop]);
      }
    }
  }

  #refreshView() {
    if (!this.#view) {
      return;
    }

    this.#placeholderVersion++;
    this.#view.setProps({
      attributes: this.#viewAttributes(),
      editable: () => !this.readOnly,
    });
    this.#view.dispatch(this.#view.state.tr);
  }

  /**
   * Shows the first hint and schedules the next ones.
   *
   * @param {HTMLElement} hints
   */
  #startPlaceholderRotation(hints) {
    clearTimeout(this.#placeholderTimer);
    this.#showPlaceholderHint(hints, 0);
    if (!this.showPlaceholderAnimation || this.#reducedMotionQuery.matches) {
      return;
    }
    const duration = MultilineEditor.PLACEHOLDER_HINT_DURATION_MS;
    const transition = duration * PLACEHOLDER_TRANSITION_FRACTION;
    hints.style.setProperty(
      "--multiline-editor-placeholder-transition-duration",
      `${transition}ms`
    );
    let index = 0;
    const showNext = () => {
      if (!hints.isConnected) {
        return;
      }
      if (!document.hidden) {
        index = (index + 1) % hints.children.length;
        this.#showPlaceholderHint(hints, index);
      }
      this.#placeholderTimer = setTimeout(showNext, duration);
    };
    this.#placeholderTimer = setTimeout(showNext, duration - transition);
  }

  #showPlaceholderHint(hints, index) {
    const count = hints.children.length;
    const incoming = hints.children[index];
    if (incoming.classList.contains("previous")) {
      // With two hints, reset the incoming one so it enters from below.
      incoming.classList.remove("previous");
      incoming.getBoundingClientRect();
    }
    for (const [i, item] of [...hints.children].entries()) {
      item.classList.toggle("current", i == index);
      item.classList.toggle("previous", i == (index + count - 1) % count);
    }
  }

  #textToDoc(text, schema) {
    const paragraphs = text.split("\n").map(line => {
      const content = line ? [schema.text(line)] : [];
      return schema.node("paragraph", null, content);
    });
    return schema.node("doc", null, paragraphs);
  }

  #textOffsetFromPos(
    pos,
    doc = this.#view?.state.doc,
    blockSeparator = "\n",
    leafText = "\n"
  ) {
    if (!doc) {
      return 0;
    }
    return doc.textBetween(0, pos, blockSeparator, leafText).length;
  }

  /**
   * Resolve several text-character offsets to ProseMirror document positions in
   * a single traversal. Counts every leaf node (mentions, hard breaks) as one
   * character to match `selectionStart`/`selectionEnd` (which use `textBetween`
   * with a one-char leaf separator); `setSelectionRange` pairs with this.
   *
   * This intentionally differs from the singular `#posFromTextOffset`, which
   * counts mentions as zero to match `posToTextOffset` and the save/restore
   * flow -- don't collapse the two without preserving that difference.
   *
   * Offsets are clamped to `[0, textLength]`; an offset past the end resolves to
   * the position of the final character. Used on hot paths (e.g. selection
   * updates) to avoid walking the document once per offset plus a separate
   * `#textLength` walk.
   *
   * @param {number[]} offsets
   * @param {object} [doc]
   * @returns {number[]} Positions parallel to `offsets`.
   */
  #posFromTextOffsets(offsets, doc = this.#view?.state.doc) {
    if (!doc) {
      return offsets.map(() => 0);
    }
    const targets = offsets.map(offset => Math.max(0, offset ?? 0));
    const results = new Array(targets.length).fill(-1);
    let remaining = targets.length;
    let seen = 0;
    let paragraphCount = 0;
    let lastPos = doc.content.size;

    const resolveReached = (charCount, posAt) => {
      const limit = seen + charCount;
      for (let i = 0; i < targets.length; i++) {
        if (results[i] == -1 && targets[i] <= limit) {
          results[i] = posAt(targets[i]);
          remaining--;
        }
      }
      seen = limit;
    };

    doc.descendants((node, nodePos) => {
      if (!remaining) {
        return false;
      }
      if (node.type.name === "paragraph") {
        if (paragraphCount > 0) {
          resolveReached(1, () => nodePos);
          lastPos = nodePos;
        }
        paragraphCount++;
      } else if (node.isText) {
        const start = nodePos;
        resolveReached(node.text.length, target => start + (target - seen));
        lastPos = nodePos + node.text.length;
      } else if (node.isLeaf) {
        // Atoms/leaves (mentions, hard breaks) count as one character, matching
        // how `textBetween` serializes them with a one-char leaf separator.
        const start = nodePos;
        resolveReached(1, target => start + (target - seen));
        lastPos = nodePos + node.nodeSize;
      }
      return true;
    });

    return results.map(pos => (pos == -1 ? lastPos : pos));
  }

  /**
   * Resolve a single text-character offset to a ProseMirror document position.
   * Counts mentions as zero characters (only text, paragraph breaks, and hard
   * breaks advance the offset) to match `posToTextOffset`; `textOffsetToPos`
   * and the save/restore-input flow pair with this. See `#posFromTextOffsets`
   * for the selection variant, which counts every leaf as one character.
   *
   * @param {number} offset
   * @param {object} [doc]
   * @returns {number}
   */
  #posFromTextOffset(offset, doc = this.#view?.state.doc) {
    if (!doc) {
      return 0;
    }
    const target = Math.max(0, Math.min(offset ?? 0, this.#textLength(doc)));
    let seen = 0;
    let pos = doc.content.size;
    let found = false;
    let paragraphCount = 0;
    doc.descendants((node, nodePos) => {
      if (found) {
        return false;
      }
      if (node.type.name === "paragraph") {
        if (paragraphCount > 0) {
          if (target <= seen + 1) {
            pos = nodePos;
            found = true;
            return false;
          }
          seen += 1;
        }
        paragraphCount++;
      }
      if (node.isText) {
        const textNodeLength = node.text.length;
        const start = nodePos;
        if (target <= seen + textNodeLength) {
          pos = start + (target - seen);
          found = true;
          return false;
        }
        seen += textNodeLength;
      } else if (node.type.name === "hard_break") {
        if (target <= seen + 1) {
          pos = nodePos;
          found = true;
          return false;
        }
        seen += 1;
      }
      return true;
    });
    return pos;
  }

  #textLength(doc) {
    if (!doc) {
      return 0;
    }
    return doc.textBetween(0, doc.content.size, "\n", "\n").length;
  }

  #viewAttributes() {
    const attrs = {
      "aria-label": this.placeholder,
      "aria-readonly": this.readOnly ? "true" : "false",
    };

    if (this.#innerRole) {
      // Combobox is a single-line widget per ARIA; omit aria-multiline so the
      // role's semantics aren't muddled.
      attrs.role = this.#innerRole;
    } else {
      attrs.role = "textbox";
      attrs["aria-multiline"] = "true";
    }

    if (this.placeholderHints.length) {
      attrs["aria-describedby"] =
        `placeholder-hints-${this.#instanceId}-${this.#placeholderVersion}`;
    }
    return attrs;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/multilineeditor/prosemirror.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/multilineeditor/multiline-editor.css"
      />
      <div class="multiline-editor"></div>
    `;
  }
}

customElements.define("moz-multiline-editor", MultilineEditor);
