# Smart Window (aiwindow)

This component grew quickly, and much of the existing code across this directory predates current standards, not only the largest files such as `ai-window.mjs`. Do not copy a pattern just because nearby code uses it: follow this file and the newer examples it names. When you touch legacy code, don't spread its patterns; clean them up only when that is in scope, otherwise in a separate patch.

## Where things live
- `ui/modules/`: chrome modules (`*.sys.mjs`) listed in `ui/moz.build` `MOZ_SRC_FILES` and imported through `moz-src:///`. Plain `.mjs` modules shared with content pages are packaged by `ui/jar.mn` under `chrome://browser/content/aiwindow/modules/`.
- `ui/components/`: Lit custom elements, each with a sibling `.css` and a `.stories.mjs`.
- `ui/actors/`: JSWindowActor pairs, registered in `browser/components/DesktopActorRegistry.sys.mjs`.
- `ui/content/`: HTML shells and page stylesheets, packaged by `ui/jar.mn`.
- `models/`: engine, prompts (`PromptLoader.sys.mjs`, Remote Settings collection `ai-window-prompts`), tools, `SecurityProperties.sys.mjs`, memories and agents. `services/`: storage.
- Startup: categories in `ui/aiwindow.manifest` call into `AIWindow.sys.mjs`, which owns per-window state and Smart/Classic switching (`toggleAIWindow`).
- Strings: add new ones to `browser/locales-preview/aiWindow.ftl`. The shipped `aiWindow.ftl` and `aiWindowContent.ftl` change only when strings are finalized.
- Telemetry: the `smart_window` category in `metrics.yaml`.
- Bugzilla: Core :: Machine Learning: Frontend for `ui/`, and Machine Learning: General for most of `models/`. Check a specific file with `./mach file-info bugzilla-component <file>`.

## Current standard, and legacy to avoid
- Imports: `moz-src:///` with `ChromeUtils.defineESModuleGetters`.
- Prefs: put defaults only in `browser/app/profile/firefox.js`, and read them with `XPCOMUtils.defineLazyPreferenceGetter(lazy, name, pref)` without a default. Make tunable numbers prefs. Prefs that experiments control go through the `smartWindow*` features in `toolkit/components/nimbus/FeatureManifest.yaml`. Legacy: getters that pass a default, and direct `Services.prefs.get*` calls.
- Logging: `console.createInstance({ prefix, maxLogLevelPref })` with a `browser.smartwindow.<area>.logLevel` pref that has a default in firefox.js. Legacy: bare `console.*` calls, `loglevel` spelling, prefs with no default.
- Private members: `#` fields and methods. Legacy: `_name` methods.
- JSDoc is enforced by eslint here (`require-jsdoc` and `valid-jsdoc` apply). Give every `@param` and `@property` a description unless the name makes it obvious.
- TODO and FIXME comments must cite a bug number.
- Prefer new, focused modules over growing the 1,500+ line files (`ai-window.mjs`, `ai-chat-content.mjs`, `AITab.sys.mjs`, `Tools.sys.mjs`, `AIWindow.sys.mjs`, `ChatStore.sys.mjs`, `SmartFormFillParent.sys.mjs`). Don't add a separate constants module either, since each extra file is another runtime load.
- Storage: extend `ui/modules/SQLiteStoreBase.sys.mjs`. Legacy: `ChatStore` manages its own connection.
- Reviewers flag generated-code habits: comments that narrate the code, try/catch or `typeof` checks that can't fail, optional chaining on values that can't be null or undefined, needless default arguments, nested ternaries, layers of indirection and vague `normalize` helpers. Prefer existing shared helpers and fix them rather than working around them.

## UI components
- Base new components on `MozLitElement`. Use a sibling CSS file with design tokens (the stylelint `use-design-tokens` rule applies everywhere except `ai-window.css`), `moz-*` reusable components, `data-l10n-id`, `static queries` and bubbling, composed `component:verb` events. Add a story. Good example: `ui/components/ai-clarifying-question/`. Don't use `ai-window.mjs` as a template.
- Use logical properties and CSS nesting. Pair text and background colours in the same rule so high-contrast mode works.
- Accessibility: support full keyboard navigation, show visible `:focus-visible` styles, use `title` alone rather than `title` plus `aria-label`, and give the safe button initial focus in destructive dialogs.
- Strings: never hardcode English. Add translator comments for ambiguous words.

## Process boundaries
- Cross into content through a small actor whose parent validates and caps what the child sends. Example: `ui/actors/AgentMonitorItem{Parent,Child}.sys.mjs`. Add `.catch` to `sendQuery` chains.
- Legacy: reaching into another component's `contentDocument` or `shadowRoot` from modules, and polling for elements (`AIWindowUI.getAiWindowElement`). Don't add more.
- Don't trust values the content process sends. Derive them in the parent (for example from `browsingContext.currentURI`). Keep sensitive data out of content processes, and remember that prefs are copied into them.

## Models, prompts and security
- Treat page-derived text as untrusted: pass it through `sanitizeUntrustedContent` (`models/ChatUtils.sys.mjs`) before it reaches a prompt. Tools that read private data or untrusted input must set the `SecurityProperties` flags, and feature-gated tools must be filtered from the tool list.
- Validate model output with `toolkit/modules/JsonSchema.sys.mjs`. Accept only http(s) URLs: `isAllowedURLProtocol`, whose changes need security review.
- Render templates in a single pass so substituted values are never expanded again.
- Don't run inference without user intent, per tab, or when a new tab opens. Bound queries, and make timeouts cancel the underlying work (`Promise.race` alone does not).
- Adding a model feature updates `MODEL_FEATURES` and `FEATURE_MAJOR_VERSIONS` (`models/Utils.sys.mjs`), `FEATURE_PURPOSES` (`models/PromptLoader.sys.mjs`), the Remote Settings records and dump (`services/settings/dumps/main/ai-window-prompts.json`, in its own patch), and `MOCK_RS_RECORDS` in `ui/test/browser/head.js`. Keep prompts in Remote Settings rather than inline.
- Chat uses v2 modular prompts with no fallback to v1. Remote Settings parsing must tolerate record changes, because old Firefox versions keep running.
- Re-request `#ai-security-reviewers` review when code that handles model input or output is actually hooked up.

## Telemetry
- Add metrics to the `smart_window` category and reuse the YAML anchors for shared extra keys. Set data classification and get data review. Keep extra keys single-purpose. Record metrics from the feature's module, and test them; if a metric lands without a test, file a bug and reference it in a comment.

## Patch scope and review
- Keep Remote Settings dump updates, toolkit reusable-component changes and drive-by fixes in separate patches. Rebase the stack before asking for review.
- Pref-gated early work may defer polish to filed bugs, but don't land code that isn't hooked up.
