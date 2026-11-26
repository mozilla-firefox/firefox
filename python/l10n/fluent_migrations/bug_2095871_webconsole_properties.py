# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import CONCAT, COPY, PLURALS, REPLACE, REPLACE_IN_TEXT
from fluent.migrate.helpers import MESSAGE_REFERENCE, VARIABLE_REFERENCE


def migrate(ctx):
    """Bug 2095871 - Migrate webconsole.properties to Fluent, part {index}."""

    source = "devtools/shared/webconsole.properties"

    def message(id, key, variables=None):
        value = (
            REPLACE(
                source,
                key,
                {
                    placeholder: VARIABLE_REFERENCE(name)
                    for placeholder, name in variables.items()
                },
            )
            if variables
            else COPY(source, key)
        )
        return FTL.Message(id=FTL.Identifier(id), value=value)

    def attributes(id, keys):
        return FTL.Message(
            id=FTL.Identifier(id),
            attributes=[
                FTL.Attribute(id=FTL.Identifier(name), value=COPY(source, key))
                for name, key in keys.items()
            ],
        )

    def plural(id, key, selector, replacements):
        return FTL.Message(
            id=FTL.Identifier(id),
            value=PLURALS(
                source,
                key,
                VARIABLE_REFERENCE(selector),
                lambda text: REPLACE_IN_TEXT(
                    text,
                    {
                        placeholder: VARIABLE_REFERENCE(name)
                        for placeholder, name in replacements.items()
                    },
                ),
            ),
        )

    def markup(name, reference):
        return CONCAT(
            FTL.TextElement(f'<span data-l10n-name="{name}">'),
            reference,
            FTL.TextElement("</span>"),
        )

    def menu(id, key):
        return attributes(
            id,
            {
                "label": f"webconsole.menu.{key}.label",
                "accesskey": f"webconsole.menu.{key}.accesskey",
            },
        )

    def setting(id, key):
        return attributes(id, {"label": f"{key}.label", "title": f"{key}.tooltip"})

    ctx.add_transforms(
        "devtools/shared/webconsole.ftl",
        "devtools/shared/webconsole.ftl",
        [
            message(
                "browserconsole-multiprocess-title", "multiProcessBrowserConsole.title"
            ),
            message(
                "browserconsole-parent-process-title",
                "parentProcessBrowserConsole.title",
            ),
            message("webconsole-console-api-disabled", "ConsoleAPIDisabled"),
            message("webconsole-xhr-indicator", "webConsoleXhrIndicator"),
            message("webconsole-learn-more", "webConsoleMoreInfoLabel"),
            message(
                "webconsole-stacktrace-anonymous-function",
                "stacktrace.anonymousFunction",
            ),
            message(
                "webconsole-stacktrace-async-stack",
                "stacktrace.asyncStack",
                {"%1$S": "cause"},
            ),
            message(
                "webconsole-time-log",
                "timeLog",
                {"%1$S": "name", "%2$S": "duration"},
            ),
            message(
                "webconsole-time-end",
                "console.timeEnd",
                {"%1$S": "name", "%2$S": "duration"},
            ),
            message("webconsole-console-cleared", "consoleCleared"),
            message("webconsole-console-clear-prevented", "preventedConsoleClear"),
            message("webconsole-no-counter-label", "noCounterLabel"),
            message(
                "webconsole-counter-doesnt-exist",
                "counterDoesntExist",
                {"%1$S": "name"},
            ),
            message("webconsole-no-group-label", "noGroupLabel"),
            message("webconsole-max-timers-exceeded", "maxTimersExceeded"),
            message(
                "webconsole-timer-already-exists",
                "timerAlreadyExists",
                {"%1$S": "name"},
            ),
            message(
                "webconsole-timer-doesnt-exist", "timerDoesntExist", {"%1$S": "name"}
            ),
            message("webconsole-timer-js-error", "timerJSError"),
            plural(
                "webconsole-message-repeats",
                "messageRepeats.tooltip2",
                "repeats",
                {"#1": "repeats"},
            ),
            plural(
                "webconsole-warning-group-message-count",
                "webconsole.warningGroup.messageCount.tooltip",
                "count",
                {"#1": "count"},
            ),
            message("webconsole-selfxss-ok-string", "selfxss.okstring"),
            message("webconsole-selfxss-message", "selfxss.msg", {"%1$S": "okString"}),
            message(
                "webconsole-evaluation-notification-no-original-variable-mapping",
                "evaluationNotifcation.noOriginalVariableMapping.msg",
            ),
            message("webconsole-message-toggle-details", "messageToggleDetails"),
            message("webconsole-group-toggle", "groupToggle"),
            message("webconsole-table-index", "table.index"),
            message("webconsole-table-iteration-index", "table.iterationIndex"),
            message("webconsole-table-key", "table.key"),
            message("webconsole-table-value", "table.value"),
            message("webconsole-level-error", "level.error"),
            message("webconsole-level-warn", "level.warn"),
            message("webconsole-level-info", "level.info"),
            message("webconsole-level-log", "level.log"),
            message("webconsole-level-debug", "level.debug"),
            message("webconsole-level-jstracer", "level.jstracer"),
            message("webconsole-logpoint-title", "logpoint.title"),
            message("webconsole-blocked-request", "blockedrequest.label2"),
            message("webconsole-disabled-icon-title", "webconsole.disableIcon.title"),
            message("webconsole-command-title", "command.title"),
            message("webconsole-result-title", "result.title"),
            message("webconsole-find-key", "webconsole.find.key"),
            message("webconsole-close-key", "webconsole.close.key"),
            message("webconsole-clear-key", "webconsole.clear.key"),
            message("webconsole-clear-key-macos", "webconsole.clear.keyOSX"),
            message(
                "webconsole-clear-alternative-key-macos",
                "webconsole.clear.alternativeKeyOSX",
            ),
            menu("webconsole-menu-copy-url", "copyURL"),
            menu("webconsole-menu-open-url", "openURL"),
            menu("webconsole-menu-open-in-network-panel", "openInNetworkPanel"),
            menu("webconsole-menu-resend-network-request", "resendNetworkRequest"),
            menu("webconsole-menu-open-node-in-inspector", "openNodeInInspector"),
            menu("webconsole-menu-store-as-global-var", "storeAsGlobalVar"),
            menu("webconsole-menu-copy-message", "copyMessage"),
            menu("webconsole-menu-copy-object", "copyObject"),
            attributes(
                "webconsole-menu-open-in-sidebar",
                {
                    "label": "webconsole.menu.openInSidebar.label1",
                    "accesskey": "webconsole.menu.openInSidebar.accesskey",
                },
            ),
            menu("webconsole-menu-copy-all-messages", "copyAllMessages"),
            menu("webconsole-menu-save-all-messages-file", "saveAllMessagesFile"),
            message(
                "webconsole-clear-button-tooltip",
                "webconsole.clearButton.tooltipWithHotkeyHint",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-filter-input-placeholder",
                "webconsole.filterInput.placeholder",
            ),
            plural(
                "webconsole-filtered-messages-by-text",
                "webconsole.filteredMessagesByText.label",
                "count",
                {"#1": "count"},
            ),
            plural(
                "webconsole-filtered-messages-by-text-tooltip",
                "webconsole.filteredMessagesByText.tooltip",
                "count",
                {"#1": "count"},
            ),
            message(
                "webconsole-close-split-console-button-tooltip",
                "webconsole.closeSplitConsoleButton.tooltip",
            ),
            message(
                "webconsole-filter-button-errors", "webconsole.errorsFilterButton.label"
            ),
            message(
                "webconsole-filter-button-warnings",
                "webconsole.warningsFilterButton.label",
            ),
            message(
                "webconsole-filter-button-logs", "webconsole.logsFilterButton.label"
            ),
            message(
                "webconsole-filter-button-info", "webconsole.infoFilterButton.label"
            ),
            message(
                "webconsole-filter-button-debug", "webconsole.debugFilterButton.label"
            ),
            message("webconsole-filter-button-css", "webconsole.cssFilterButton.label"),
            message(
                "webconsole-filter-button-css-inactive-tooltip",
                "webconsole.cssFilterButton.inactive.tooltip",
            ),
            message("webconsole-filter-button-xhr", "webconsole.xhrFilterButton.label"),
            message(
                "webconsole-filter-button-requests",
                "webconsole.requestsFilterButton.label",
            ),
            message(
                "webconsole-filter-button-chrome", "webconsole.chromeFilterButton.label"
            ),
            message(
                "webconsole-filter-button-chrome-tooltip",
                "webconsole.chromeFilterButton.tooltip",
            ),
            message(
                "webconsole-filter-button-content",
                "webconsole.contentFilterButton.label",
            ),
            message(
                "webconsole-filter-button-content-tooltip",
                "webconsole.contentFilterButton.tooltip",
            ),
            message(
                "webconsole-settings-button-tooltip",
                "webconsole.console.settings.menu.button.tooltip",
            ),
            setting(
                "webconsole-settings-timestamps",
                "webconsole.console.settings.menu.item.timestamps",
            ),
            setting(
                "webconsole-settings-warning-groups",
                "webconsole.console.settings.menu.item.warningGroups",
            ),
            setting(
                "webconsole-settings-autocomplete",
                "webconsole.console.settings.menu.item.autocomplete",
            ),
            setting(
                "webconsole-settings-persistent-logs",
                "webconsole.console.settings.menu.item.enablePersistentLogs",
            ),
            setting(
                "webconsole-settings-instant-evaluation",
                "webconsole.console.settings.menu.item.instantEvaluation",
            ),
            setting(
                "browserconsole-settings-network-monitoring",
                "browserconsole.enableNetworkMonitoring",
            ),
            message("webconsole-navigated", "webconsole.navigated", {"%1$S": "url"}),
            message("webconsole-reloaded", "webconsole.reloaded"),
            message(
                "webconsole-close-sidebar-button-tooltip",
                "webconsole.closeSidebarButton.tooltip",
            ),
            message(
                "webconsole-reverse-search-input-placeholder",
                "webconsole.reverseSearch.input.placeHolder",
            ),
            message(
                "webconsole-reverse-search-close-button-tooltip",
                "webconsole.reverseSearch.closeButton.tooltip",
                {"%1$S": "shortcut"},
            ),
            plural(
                "webconsole-reverse-search-results",
                "webconsole.reverseSearch.results",
                "total",
                {"#1": "index", "#2": "total"},
            ),
            message(
                "webconsole-reverse-search-no-result",
                "webconsole.reverseSearch.noResult",
            ),
            message(
                "webconsole-reverse-search-previous-button-tooltip",
                "webconsole.reverseSearch.result.previousButton.tooltip",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-reverse-search-next-button-tooltip",
                "webconsole.reverseSearch.result.nextButton.tooltip",
                {"%1$S": "shortcut"},
            ),
            FTL.Message(
                id=FTL.Identifier("webconsole-confirm-dialog-getter-label"),
                value=REPLACE(
                    source,
                    "webconsole.confirmDialog.getter.label",
                    {"%1$S": markup("getter-name", VARIABLE_REFERENCE("getterName"))},
                ),
            ),
            message(
                "webconsole-confirm-dialog-getter-invoke-button",
                "webconsole.confirmDialog.getter.invokeButtonLabelWithShortcut",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-confirm-dialog-getter-close-button-tooltip",
                "webconsole.confirmDialog.getter.closeButton.tooltip",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-css-warning-elements",
                "webconsole.cssWarningElements.label",
                {"%1$S": "selector"},
            ),
            message(
                "webconsole-message-render-error",
                "webconsole.message.componentDidCatch.label",
                {"%1$S": "url"},
            ),
            message(
                "webconsole-message-render-error-copy-button",
                "webconsole.message.componentDidCatch.copyButton.label",
            ),
            message(
                "webconsole-editor-toolbar-execute-button-label",
                "webconsole.editor.toolbar.executeButton.label",
            ),
            message(
                "webconsole-editor-toolbar-execute-button-tooltip",
                "webconsole.editor.toolbar.executeButton.tooltip",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-editor-toolbar-pretty-print-button-tooltip",
                "webconsole.editor.toolbar.prettyPrintButton.tooltip",
            ),
            message(
                "webconsole-editor-toolbar-prev-expression-button-tooltip",
                "webconsole.editor.toolbar.history.prevExpressionButton.tooltip",
            ),
            message(
                "webconsole-editor-toolbar-next-expression-button-tooltip",
                "webconsole.editor.toolbar.history.nextExpressionButton.tooltip",
            ),
            message(
                "webconsole-editor-toolbar-open-reverse-search-tooltip",
                "webconsole.editor.toolbar.reverseSearchButton.openReverseSearch.tooltip",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-editor-toolbar-close-reverse-search-tooltip",
                "webconsole.editor.toolbar.reverseSearchButton.closeReverseSearch.tooltip",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-editor-toolbar-close-button-tooltip",
                "webconsole.editor.toolbar.closeButton.tooltip2",
                {"%1$S": "shortcut"},
            ),
            message(
                "webconsole-input-open-editor-button-tooltip",
                "webconsole.input.openEditorButton.tooltip2",
                {"%1$S": "shortcut"},
            ),
            FTL.Message(
                id=FTL.Identifier("webconsole-input-editor-onboarding"),
                value=REPLACE(
                    source,
                    "webconsole.input.editor.onboarding.label",
                    {
                        "%1$S": markup(
                            "enter-key", MESSAGE_REFERENCE("webconsole-enter-key")
                        ),
                        "%2$S": markup(
                            "run-shortcut", VARIABLE_REFERENCE("runShortcut")
                        ),
                    },
                ),
            ),
            message(
                "webconsole-input-editor-onboarding-dismiss",
                "webconsole.input.editor.onboarding.dismiss.label",
            ),
            message("webconsole-enter-key", "webconsole.enterKey"),
            message(
                "webconsole-input-open-javascript-file",
                "webconsole.input.openJavaScriptFile",
            ),
            message(
                "webconsole-input-open-javascript-file-filter",
                "webconsole.input.openJavaScriptFileFilter",
            ),
            message("webconsole-input-selector-top", "webconsole.input.selector.top"),
            message(
                "webconsole-input-selector-tooltip", "webconsole.input.selector.tooltip"
            ),
            message("webconsole-group-cookie", "webconsole.group.cookie"),
            message("webconsole-group-csp", "webconsole.group.csp"),
        ],
    )

    ctx.add_transforms(
        "devtools/shared/webconsole-commands.ftl",
        "devtools/shared/webconsole-commands.ftl",
        [
            message(
                "webconsole-commands-copy-value-to-clipboard",
                "webconsole.message.commands.copyValueToClipboard",
            ),
            message(
                "webconsole-commands-copy-error",
                "webconsole.error.commands.copyError",
                {"%1$S": "error"},
            ),
            message(
                "webconsole-commands-blocked-url",
                "webconsole.message.commands.blockedURL",
                {"%1$S": "url"},
            ),
            message(
                "webconsole-commands-unblocked-url",
                "webconsole.message.commands.unblockedURL",
                {"%1$S": "url"},
            ),
            message(
                "webconsole-commands-block-arg-missing",
                "webconsole.messages.commands.blockArgMissing",
            ),
            message(
                "webconsole-commands-start-tracing-to-web-console",
                "webconsole.message.commands.startTracingToWebConsole",
            ),
            message(
                "webconsole-commands-start-tracing-to-debugger-sidebar",
                "webconsole.message.commands.startTracingToDebuggerSidebar",
            ),
            message(
                "webconsole-commands-start-tracing-to-stdout",
                "webconsole.message.commands.startTracingToStdout",
            ),
            message(
                "webconsole-commands-start-tracing-to-profiler",
                "webconsole.message.commands.startTracingToProfiler",
            ),
            message(
                "webconsole-commands-stop-tracing",
                "webconsole.message.commands.stopTracing",
            ),
            message(
                "webconsole-commands-stop-tracing-with-reason",
                "webconsole.message.commands.stopTracingWithReason",
                {"%1$S": "reason"},
            ),
        ],
    )
