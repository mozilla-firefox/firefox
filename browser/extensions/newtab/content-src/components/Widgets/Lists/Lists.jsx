/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, {
  useRef,
  useState,
  useEffect,
  useCallback,
  useMemo,
} from "react";
import { useSelector } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  resolveWidgetSize,
  WIDGET_USER_ACTIONS,
  WIDGET_SOURCES,
} from "common/WidgetsRegistry.mjs";
import { WidgetCelebration } from "../WidgetCelebration";
import { useWidgetCelebration } from "../useWidgetCelebration";
import { SizeSubmenu } from "../SizeSubmenu";
import { WidgetMenuButton } from "../WidgetMenuButton";
import { WidgetMenuFooter } from "../WidgetMenuFooter";
import { useWidgetTelemetry } from "../useWidgetTelemetry";
import { focusOnceRendered } from "../../../lib/utils";

const TASK_TYPE = {
  IN_PROGRESS: "tasks",
  COMPLETED: "completed",
};

const USER_ACTION_TYPES = {
  ...WIDGET_USER_ACTIONS,
  LIST_COPY: "list_copy",
  LIST_CREATE: "list_create",
  LIST_EDIT: "list_edit",
  LIST_DELETE: "list_delete",
  TASK_CREATE: "task_create",
  TASK_EDIT: "task_edit",
  TASK_DELETE: "task_delete",
  TASK_COMPLETE: "task_complete",
};

const PREF_WIDGETS_LISTS_MAX_LISTS = "widgets.lists.maxLists";
const PREF_WIDGETS_LISTS_MAX_LISTITEMS = "widgets.lists.maxListItems";
const PREF_WIDGETS_LISTS_BADGE_ENABLED = "widgets.lists.badge.enabled";
const PREF_WIDGETS_LISTS_BADGE_LABEL = "widgets.lists.badge.label";
const PREF_WIDGETS_LISTS_SIZE = "widgets.lists.size";
// @nova-cleanup(remove-pref): Delete this const; see getListsWidgetSize below.
const PREF_NOVA_ENABLED = "nova.enabled";
const LISTS_EMPTY_STATE_ILLUSTRATION =
  "chrome://newtab/content/data/content/assets/lists-empty-state-comet.svg";
const LISTS_CELEBRATION = {
  headlineL10nId: "newtab-widget-lists-celebration-headline",
  illustrationSrc:
    "chrome://newtab/content/data/content/assets/firefox-motion-head-pop-up-no-bg.svg",
  subheadL10nId: "newtab-widget-lists-celebration-subhead",
};
const ENABLE_COMPACT_COMPLETED_PREVIEW = false;

const getCompactPreviewState = ({
  enableCompactCompletedPreview,
  isCompactMediumSize,
  selectedList,
  showCompactCompleted,
}) => {
  const hasIncompleteTasks = selectedList?.tasks.length >= 1;
  const hasCompletedTasks = selectedList?.completed.length >= 1;
  const hasAnyTasks = hasIncompleteTasks || hasCompletedTasks;
  const isShowingCompactCompleted =
    enableCompactCompletedPreview &&
    isCompactMediumSize &&
    hasCompletedTasks &&
    (showCompactCompleted || !hasIncompleteTasks);
  let hasVisibleTasks = hasAnyTasks;

  if (isCompactMediumSize) {
    hasVisibleTasks = isShowingCompactCompleted
      ? hasCompletedTasks
      : hasIncompleteTasks;
  }

  return {
    hasIncompleteTasks,
    hasCompletedTasks,
    hasAnyTasks,
    hasVisibleTasks,
    isShowingCompactCompleted,
    compactPreviewTasks: isShowingCompactCompleted
      ? selectedList?.completed
      : selectedList?.tasks,
    compactPreviewTaskType: isShowingCompactCompleted
      ? TASK_TYPE.COMPLETED
      : TASK_TYPE.IN_PROGRESS,
  };
};

const renderListSwitcherOrTitle = ({
  currentListsCount,
  lists,
  onSelect,
  selected,
  defaultListLabelL10nId,
  onCreateList,
  isAtMaxListsLimit,
  switcherButtonRef,
}) => {
  const selectedLabel = lists[selected]?.label;

  if (currentListsCount > 1) {
    return (
      <div className="lists-switcher">
        <moz-button
          className="lists-switcher-button"
          ref={switcherButtonRef}
          type="default"
          size="small"
          iconSrc="chrome://global/skin/icons/arrow-down-12.svg"
          iconPosition="end"
          menuId="lists-switcher-panel"
          {...(selectedLabel
            ? {}
            : {
                "data-l10n-id": defaultListLabelL10nId,
              })}
        >
          {selectedLabel || null}
        </moz-button>
        <panel-list id="lists-switcher-panel">
          {Object.entries(lists).map(([key, list]) => (
            <panel-item
              key={key}
              type="checkbox"
              checked={key === selected || undefined}
              onClick={() => onSelect(key)}
              {...(list.label
                ? {}
                : {
                    "data-l10n-id": defaultListLabelL10nId,
                  })}
            >
              {list.label || null}
            </panel-item>
          ))}
          <hr />
          <panel-item
            className="lists-switcher-create"
            {...(isAtMaxListsLimit ? { disabled: true } : {})}
            data-l10n-id="newtab-widget-lists-menu-create"
            onClick={onCreateList}
          ></panel-item>
        </panel-list>
      </div>
    );
  }

  return (
    <span
      className="newtab-widget-title lists-title"
      {...(selectedLabel
        ? {}
        : {
            "data-l10n-id": defaultListLabelL10nId,
          })}
    >
      {selectedLabel || null}
    </span>
  );
};

// eslint-disable-next-line complexity, max-statements
function Lists({
  dispatch,
  handleUserInteraction,
  isMaximized,
  widgetsMayBeMaximized,
  widgetEnabledMap,
}) {
  const prefs = useSelector(state => state.Prefs.values);
  const { selected, lists } = useSelector(state => state.ListsWidget);
  const [newTask, setNewTask] = useState("");
  const [isAddingTask, setIsAddingTask] = useState(false);
  const [isEditing, setIsEditing] = useState(false);
  const [isCreatingNewList, setIsCreatingNewList] = useState(false);
  const [showCompactCompleted, setShowCompactCompleted] = useState(false);
  const selectedList = useMemo(() => lists[selected], [lists, selected]);

  // @nova-cleanup(remove-pref): Delete novaEnabled and collapse
  // getListsWidgetSize to just the Nova branch, deleting everything after it
  // (the PREF_WIDGETS_LISTS_SIZE fallback chain) and the now-unused
  // PREF_WIDGETS_LISTS_SIZE const.
  const novaEnabled = prefs[PREF_NOVA_ENABLED];
  const listsWidget = WIDGET_REGISTRY.find(w => w.id === "lists");
  const getListsWidgetSize = () => {
    if (novaEnabled) {
      const resolvedSize = resolveWidgetSize(listsWidget, prefs);
      return resolvedSize === "small" ? "medium" : resolvedSize;
    }

    const requestedSize = prefs[PREF_WIDGETS_LISTS_SIZE];
    if (requestedSize === "large" || requestedSize === "medium") {
      return requestedSize;
    }
    if (requestedSize === "small") {
      return "medium";
    }

    if (!widgetsMayBeMaximized) {
      return "large";
    }

    return isMaximized ? "large" : "medium";
  };
  const widgetSize = getListsWidgetSize();
  const isMediumSize = widgetSize === "medium";

  const inputRef = useRef(null);
  const reorderListRef = useRef(null);
  const widgetRef = useRef(null);
  const switcherButtonRef = useRef(null);
  const focusSwitcherAfterEditRef = useRef(false);
  const {
    celebrationFrame,
    celebrationId,
    completeCelebration,
    isCelebrating,
    triggerCelebration,
  } = useWidgetCelebration(widgetRef);

  // Pre-hook code reported widget_size as "medium" when the widgets row is
  // not maximizable, regardless of the resolved widgetSize. Preserve that.
  const telemetrySize = widgetsMayBeMaximized ? widgetSize : "medium";
  const { impressionRef, recordUserAction } = useWidgetTelemetry({
    dispatch,
    widget: listsWidget,
    widgetSize: telemetrySize,
    legacyImpressionTypes: [at.WIDGETS_LISTS_USER_IMPRESSION],
    legacyUserEventType: at.WIDGETS_LISTS_USER_EVENT,
  });

  const handleListInteraction = useCallback(
    () => handleUserInteraction("lists"),
    [handleUserInteraction]
  );

  const handleSelectList = useCallback(
    listId => {
      setIsEditing(false);
      setIsCreatingNewList(false);
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_CHANGE_SELECTED,
          data: listId,
        })
      );
      handleListInteraction();
    },
    [dispatch, handleListInteraction]
  );

  // store selectedList with useMemo so it isn't re-calculated on every re-render
  const isValidUrl = useCallback(str => URL.canParse(str), []);

  const reorderLists = useCallback(
    (draggedElement, targetElement, before = false) => {
      const draggedIndex = selectedList.tasks.findIndex(
        ({ id }) => id === draggedElement.id
      );
      const targetIndex = selectedList.tasks.findIndex(
        ({ id }) => id === targetElement.id
      );

      // return early is index is not found
      if (
        draggedIndex === -1 ||
        targetIndex === -1 ||
        draggedIndex === targetIndex
      ) {
        return;
      }

      const reordered = [...selectedList.tasks];
      const [removed] = reordered.splice(draggedIndex, 1);
      const insertIndex = before ? targetIndex : targetIndex + 1;

      reordered.splice(
        insertIndex > draggedIndex ? insertIndex - 1 : insertIndex,
        0,
        removed
      );

      const updatedLists = {
        ...lists,
        [selected]: {
          ...selectedList,
          tasks: reordered,
        },
      };

      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_UPDATE,
          data: { lists: updatedLists },
        })
      );
      handleListInteraction();
    },
    [lists, selected, selectedList, dispatch, handleListInteraction]
  );

  const moveTask = useCallback(
    (task, direction) => {
      const index = selectedList.tasks.findIndex(({ id }) => id === task.id);

      // guardrail a falsey index
      if (index === -1) {
        return;
      }

      const targetIndex = direction === "up" ? index - 1 : index + 1;
      const before = direction === "up";
      const targetTask = selectedList.tasks[targetIndex];

      if (targetTask) {
        reorderLists(task, targetTask, before);
      }
    },
    [selectedList, reorderLists]
  );

  useEffect(() => {
    const reorderNode = reorderListRef.current;

    function handleReorder(e) {
      const { draggedElement, targetElement, position } = e.detail;
      reorderLists(draggedElement, targetElement, position === -1);
    }

    reorderNode?.addEventListener("reorder", handleReorder);

    return () => {
      reorderNode?.removeEventListener("reorder", handleReorder);
    };
  }, [reorderLists]);

  useEffect(() => {
    if (isAddingTask) {
      inputRef.current?.focus();
    }
  }, [isAddingTask]);

  // The name input replaces the switcher, so the switcher loses focus. Put
  // focus back on it when editing ends, unless the user has moved focus
  // somewhere else. After a save, wait until the new list is selected,
  // because selecting it re-creates the switcher.
  useEffect(() => {
    const pending = focusSwitcherAfterEditRef.current;
    if (isEditing || !pending || (pending !== true && pending !== selected)) {
      return;
    }
    focusSwitcherAfterEditRef.current = false;
    if (document.activeElement && document.activeElement !== document.body) {
      return;
    }
    focusOnceRendered(switcherButtonRef.current);
  }, [isEditing, selected]);

  useEffect(() => {
    setShowCompactCompleted(false);
  }, [selected]);

  useEffect(() => {
    if (!selectedList?.completed?.length) {
      setShowCompactCompleted(false);
    }
  }, [selectedList]);

  function saveTask() {
    const trimmedTask = newTask.trimEnd();
    // only add new task if it has a length, to avoid creating empty tasks
    if (trimmedTask) {
      const formattedTask = {
        value: trimmedTask,
        completed: false,
        created: Date.now(),
        id: crypto.randomUUID(),
        isUrl: isValidUrl(trimmedTask),
      };
      const updatedLists = {
        ...lists,
        [selected]: {
          ...selectedList,
          tasks: [formattedTask, ...lists[selected].tasks],
        },
      };
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_UPDATE,
          data: { lists: updatedLists },
        })
      );
      recordUserAction(USER_ACTION_TYPES.TASK_CREATE, {
        source: WIDGET_SOURCES.WIDGET,
        legacy: true,
      });
      setNewTask("");
    }
    setIsAddingTask(false);
    handleListInteraction();
  }

  function updateTask(updatedTask, type) {
    const isCompletedType = type === TASK_TYPE.COMPLETED;
    const isNowCompleted = updatedTask.completed;

    let newTasks = selectedList.tasks;
    let newCompleted = selectedList.completed;
    let userAction;

    // If the task is in the completed array and is now unchecked
    const shouldMoveToTasks = isCompletedType && !isNowCompleted;

    // If we're moving the task from tasks → completed (user checked it)
    const shouldMoveToCompleted = !isCompletedType && isNowCompleted;

    //  Move task from completed -> task
    if (shouldMoveToTasks) {
      newCompleted = selectedList.completed.filter(
        task => task.id !== updatedTask.id
      );
      newTasks = [...selectedList.tasks, updatedTask];
      // Move task to completed, but also create local version
    } else if (shouldMoveToCompleted) {
      newTasks = selectedList.tasks.filter(task => task.id !== updatedTask.id);
      newCompleted = [...selectedList.completed, updatedTask];

      userAction = USER_ACTION_TYPES.TASK_COMPLETE;
      if (!newTasks.length && newCompleted.length) {
        triggerCelebration();
      }
    } else {
      const targetKey = isCompletedType ? "completed" : "tasks";
      const updatedArray = selectedList[targetKey].map(task =>
        task.id === updatedTask.id ? updatedTask : task
      );
      // In-place update: toggle checkbox (but stay in same array or edit name)
      if (targetKey === "tasks") {
        newTasks = updatedArray;
      } else {
        newCompleted = updatedArray;
      }
      userAction = USER_ACTION_TYPES.TASK_EDIT;
    }

    const updatedLists = {
      ...lists,
      [selected]: {
        ...selectedList,
        tasks: newTasks,
        completed: newCompleted,
      },
    };

    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_LISTS_UPDATE,
        data: { lists: updatedLists },
      })
    );
    if (userAction) {
      recordUserAction(userAction, {
        source: WIDGET_SOURCES.WIDGET,
        legacy: true,
        alsoToMain: true,
      });
    }
    handleListInteraction();
  }

  function deleteTask(task, type) {
    const selectedTasks = lists[selected][type];
    const updatedTasks = selectedTasks.filter(({ id }) => id !== task.id);

    const updatedLists = {
      ...lists,
      [selected]: {
        ...selectedList,
        [type]: updatedTasks,
      },
    };
    dispatch(
      ac.AlsoToMain({
        type: at.WIDGETS_LISTS_UPDATE,
        data: { lists: updatedLists },
      })
    );
    recordUserAction(USER_ACTION_TYPES.TASK_DELETE, {
      source: WIDGET_SOURCES.WIDGET,
      legacy: true,
    });
    handleListInteraction();
  }

  function handleKeyDown(e) {
    if (e.key === "Enter" && document.activeElement === inputRef.current) {
      saveTask();
    } else if (
      e.key === "Escape" &&
      document.activeElement === inputRef.current
    ) {
      // Clear out the input when esc is pressed
      setNewTask("");
      setIsAddingTask(false);
    }
  }

  function handleShowTaskInput() {
    setIsAddingTask(true);
    handleListInteraction();
  }

  function handleListNameSave(newLabel) {
    const trimmedLabel = newLabel.trimEnd();

    if (isCreatingNewList) {
      setIsCreatingNewList(false);

      if (!trimmedLabel) {
        handleListInteraction();
        return;
      }

      const id = crypto.randomUUID();
      if (focusSwitcherAfterEditRef.current) {
        focusSwitcherAfterEditRef.current = id;
      }
      const newLists = {
        ...lists,
        [id]: {
          label: trimmedLabel,
          tasks: [],
          completed: [],
        },
      };

      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_UPDATE,
          data: { lists: newLists },
        })
      );
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_CHANGE_SELECTED,
          data: id,
        })
      );
      recordUserAction(USER_ACTION_TYPES.LIST_CREATE, {
        source: WIDGET_SOURCES.WIDGET,
        legacy: true,
      });
      handleListInteraction();
      return;
    }

    if (trimmedLabel && trimmedLabel !== selectedList?.label) {
      const updatedLists = {
        ...lists,
        [selected]: {
          ...selectedList,
          label: trimmedLabel,
        },
      };
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_UPDATE,
          data: { lists: updatedLists },
        })
      );
      recordUserAction(USER_ACTION_TYPES.LIST_EDIT, {
        source: WIDGET_SOURCES.WIDGET,
        legacy: true,
      });
      setIsEditing(false);
      handleListInteraction();
    }
  }

  function handleCreateNewList() {
    setIsCreatingNewList(true);
    setIsEditing(true);
    handleListInteraction();
  }

  function handleCreateNewListFromSwitcher() {
    focusSwitcherAfterEditRef.current = true;
    handleCreateNewList();
  }

  function handleCancelNewList() {
    if (isCreatingNewList) {
      setIsCreatingNewList(false);
    }

    handleListInteraction();
  }

  function handleDeleteList() {
    let updatedLists = { ...lists };
    if (updatedLists[selected]) {
      delete updatedLists[selected];

      // if this list was the last one created, add a new list as default
      if (Object.keys(updatedLists)?.length === 0) {
        updatedLists = {
          [crypto.randomUUID()]: {
            label: "",
            tasks: [],
            completed: [],
          },
        };
      }
      const listKeys = Object.keys(updatedLists);
      const key = listKeys[listKeys.length - 1];
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_UPDATE,
          data: { lists: updatedLists },
        })
      );
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_LISTS_CHANGE_SELECTED,
          data: key,
        })
      );
      recordUserAction(USER_ACTION_TYPES.LIST_DELETE, {
        source: WIDGET_SOURCES.WIDGET,
        legacy: true,
      });
    }
    handleListInteraction();
  }

  function handleCopyListToClipboard() {
    const currentList = lists[selected];

    if (!currentList) {
      return;
    }

    const { label, tasks = [], completed = [] } = currentList;

    const uncompleted = tasks.filter(task => !task.completed);
    const currentCompleted = tasks.filter(task => task.completed);

    // In order in include all items, we need to iterate through both current and completed tasks list and mark format all completed tasks accordingly.
    const formatted = [
      `List: ${label}`,
      `---`,
      ...uncompleted.map(task => `- [ ] ${task.value}`),
      ...currentCompleted.map(task => `- [x] ${task.value}`),
      ...completed.map(task => `- [x] ${task.value}`),
    ].join("\n");

    try {
      navigator.clipboard.writeText(formatted);
    } catch (err) {
      console.error("Copy failed", err);
    }

    recordUserAction(USER_ACTION_TYPES.LIST_COPY, {
      source: WIDGET_SOURCES.WIDGET,
      legacy: true,
    });
    handleListInteraction();
  }

  const handleChangeSize = useCallback(
    size => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PREF_WIDGETS_LISTS_SIZE, value: size },
        })
      );
      recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
        source: WIDGET_SOURCES.CONTEXT_MENU,
        value: size,
        size,
      });
    },
    [dispatch, recordUserAction]
  );

  useEffect(() => {
    setIsAddingTask(false);
  }, [selected]);

  if (!lists) {
    return null;
  }

  // Enforce maximum count limits to lists
  const currentListsCount = Object.keys(lists).length;
  // Ensure a minimum of 1, but allow higher values from prefs
  const maxListsCount = Math.max(1, prefs[PREF_WIDGETS_LISTS_MAX_LISTS]);
  const isAtMaxListsLimit = currentListsCount >= maxListsCount;

  // Enforce maximum count limits to list items
  // The maximum applies to the total number of items (both incomplete and completed items)
  const currentSelectedListItemsCount =
    selectedList?.tasks.length + selectedList?.completed.length;

  // Ensure a minimum of 1, but allow higher values from prefs
  const maxListItemsCount = Math.max(
    1,
    prefs[PREF_WIDGETS_LISTS_MAX_LISTITEMS]
  );

  const isAtMaxListItemsLimit =
    currentSelectedListItemsCount >= maxListItemsCount;

  // Figure out if the selected list is the first (default) or a new one.
  // Index 0 → use "Task list"; any later index → use "New list".
  // Fallback to 0 if the selected id isn’t found.
  const listKeys = Object.keys(lists);
  const selectedIndex = Math.max(0, listKeys.indexOf(selected));

  const listNamePlaceholder =
    currentListsCount > 1 && selectedIndex !== 0
      ? "newtab-widget-lists-name-placeholder-new2"
      : "newtab-widget-lists-name-placeholder-checklist2";

  const nimbusBadgeEnabled = prefs.widgetsConfig?.listsBadgeEnabled;
  const nimbusBadgeLabel = prefs.widgetsConfig?.listsBadgeLabel;
  const nimbusBadgeTrainhopEnabled =
    prefs.trainhopConfig?.widgets?.listsBadgeEnabled;
  const nimbusBadgeTrainhopLabel =
    prefs.trainhopConfig?.widgets?.listsBadgeLabel;

  const badgeEnabled =
    (nimbusBadgeEnabled || nimbusBadgeTrainhopEnabled) ??
    prefs[PREF_WIDGETS_LISTS_BADGE_ENABLED] ??
    false;

  const badgeLabel =
    (nimbusBadgeLabel || nimbusBadgeTrainhopLabel) ??
    prefs[PREF_WIDGETS_LISTS_BADGE_LABEL] ??
    "";

  const {
    hasIncompleteTasks,
    hasCompletedTasks,
    hasAnyTasks,
    hasVisibleTasks,
    isShowingCompactCompleted,
  } = getCompactPreviewState({
    enableCompactCompletedPreview: ENABLE_COMPACT_COMPLETED_PREVIEW,
    isCompactMediumSize: isMediumSize,
    selectedList,
    showCompactCompleted,
  });
  const showCompactPopulatedState = isMediumSize && hasAnyTasks;
  const showCompletedTasks = !isMediumSize && hasCompletedTasks;
  const showInlineAddButton = !showCompactPopulatedState;
  const showHeaderAddButton = showCompactPopulatedState;
  const showEmptyState = !hasAnyTasks && !isAddingTask;
  const defaultListLabelL10nId = "newtab-widget-lists-name-default";
  const listsSizeClass =
    widgetSize === "large" ? "large-widget" : "medium-widget compact-widget";

  function renderAddTaskButton(iconOnly = false) {
    return (
      <button
        className={`lists-add-button${iconOnly ? " icon-only" : ""}`}
        disabled={isAtMaxListItemsLimit}
        onClick={handleShowTaskInput}
        type="button"
      >
        <span className="icon icon-add" />
        <span
          className={iconOnly ? "sr-only" : "button-label"}
          data-l10n-id="newtab-widget-lists-button-add-item"
        />
      </button>
    );
  }

  return (
    <article
      // @nova-cleanup(remove-conditional): Always apply col-4.
      className={`lists widget ${novaEnabled ? "col-4" : ""} ${listsSizeClass} ${isMaximized ? "is-maximized" : ""}${showEmptyState ? " is-empty" : ""}${hasVisibleTasks ? " has-visible-tasks" : ""}${isAddingTask ? " is-adding-task" : ""}${isCelebrating ? " is-celebrating" : ""}`}
      ref={el => {
        widgetRef.current = el;
        impressionRef(el);
      }}
    >
      {isCelebrating && celebrationFrame ? (
        <WidgetCelebration
          classNamePrefix="lists-celebration"
          celebrationFrame={celebrationFrame}
          celebrationId={celebrationId}
          headlineL10nId={LISTS_CELEBRATION.headlineL10nId}
          illustrationSrc={LISTS_CELEBRATION.illustrationSrc}
          onComplete={completeCelebration}
          subheadL10nId={LISTS_CELEBRATION.subheadL10nId}
        />
      ) : null}
      <div className="lists-header">
        <EditableText
          key={`${selected}-${isCreatingNewList ? "draft" : "saved"}`}
          value={isCreatingNewList ? "" : lists[selected]?.label || ""}
          onSave={handleListNameSave}
          isEditing={isEditing}
          setIsEditing={setIsEditing}
          onCancel={handleCancelNewList}
          type="list"
          maxLength={30}
          ariaLabelL10nId="newtab-widget-lists-menu-edit2"
          saveOnBlur={!isCreatingNewList}
          dataL10nId={
            isCreatingNewList
              ? "newtab-widget-lists-name-placeholder-new2"
              : listNamePlaceholder
          }
        >
          {renderListSwitcherOrTitle({
            currentListsCount,
            lists,
            onSelect: handleSelectList,
            selected,
            defaultListLabelL10nId,
            onCreateList: handleCreateNewListFromSwitcher,
            isAtMaxListsLimit,
            switcherButtonRef,
          })}
        </EditableText>
        {/* Hide the badge when user is editing task list title */}
        {!isEditing && badgeEnabled && badgeLabel && !isMediumSize && (
          <moz-badge
            data-l10n-id={(() => {
              if (badgeLabel === "New") {
                return "newtab-widget-lists-label-new";
              }
              if (badgeLabel === "Beta") {
                return "newtab-widget-lists-label-beta";
              }
              return "";
            })()}
          ></moz-badge>
        )}
        {showHeaderAddButton && renderAddTaskButton(true)}
        {ENABLE_COMPACT_COMPLETED_PREVIEW &&
          isMediumSize &&
          hasCompletedTasks && (
            <button
              aria-pressed={isShowingCompactCompleted}
              className={`lists-completed-button${isShowingCompactCompleted ? " is-active" : ""}`}
              onClick={() =>
                hasIncompleteTasks &&
                setShowCompactCompleted(currentValue => !currentValue)
              }
              type="button"
            >
              {/* Keep the compact completed-items toggle staged off until design comes up with "Completed" items for compact view. */}
              <span aria-hidden="true" className="lists-completed-button-label">
                C
              </span>
              <span
                className="sr-only"
                data-l10n-id="newtab-widget-lists-completed-list"
                data-l10n-args={JSON.stringify({
                  number: selectedList?.completed.length,
                })}
              />
            </button>
          )}
        <WidgetMenuButton
          className="lists-panel-button"
          menuId="lists-panel"
          l10nId="newtab-widget-lists-open-menu-button"
        />
        <panel-list className="panel-list-no-icons" id="lists-panel">
          <panel-item
            data-l10n-id="newtab-widget-lists-menu-edit"
            onClick={() => setIsEditing(true)}
          ></panel-item>
          <panel-item
            {...(isAtMaxListsLimit ? { disabled: true } : {})}
            data-l10n-id="newtab-widget-lists-menu-create"
            onClick={() => handleCreateNewList()}
            className="create-list"
          ></panel-item>
          <panel-item
            data-l10n-id="newtab-widget-lists-menu-delete"
            onClick={() => handleDeleteList()}
          ></panel-item>
          <panel-item
            data-l10n-id="newtab-widget-lists-menu-copy"
            onClick={() => handleCopyListToClipboard()}
          ></panel-item>
          <WidgetMenuFooter
            dispatch={dispatch}
            widgetId="lists"
            widgetEnabledMap={widgetEnabledMap}
            widgetName="lists"
            enabledPref="widgets.lists.enabled"
            widgetSize={widgetsMayBeMaximized ? widgetSize : "medium"}
            learnMoreL10nId="newtab-widget-lists-menu-learn-more"
            onLearnMore={handleListInteraction}
            sizeSubmenu={
              // @nova-cleanup(remove-conditional): Drop the novaEnabled check,
              // keep widgetsMayBeMaximized.
              novaEnabled &&
              widgetsMayBeMaximized && (
                <SizeSubmenu
                  submenuId="lists-size-submenu"
                  sizes={["medium", "large"]}
                  checkedSize={widgetSize}
                  onChangeSize={handleChangeSize}
                />
              )
            }
          />
        </panel-list>
      </div>
      {(showInlineAddButton || isAddingTask) && (
        <div className="lists-add-action">
          {showInlineAddButton && renderAddTaskButton()}
          <div className="add-task-container">
            <span
              className={`icon icon-add ${isAtMaxListItemsLimit ? "icon-disabled" : ""}`}
            />
            <input
              ref={inputRef}
              onBlur={() => saveTask()}
              onChange={e => setNewTask(e.target.value)}
              value={newTask}
              data-l10n-id="newtab-widget-lists-input-add-an-item2"
              data-l10n-attrs="placeholder,aria-label"
              className="add-task-input"
              onKeyDown={handleKeyDown}
              type="text"
              maxLength={100}
              disabled={isAtMaxListItemsLimit}
            />
          </div>
        </div>
      )}
      <div className="task-list-wrapper">
        {showEmptyState ? (
          <div className="empty-list">
            <img
              alt=""
              className="empty-list-illustration"
              height="66"
              src={LISTS_EMPTY_STATE_ILLUSTRATION}
              width="75"
            />
          </div>
        ) : (
          <moz-reorderable-list
            ref={reorderListRef}
            itemSelector="fieldset .task-type-tasks"
            dragSelector=".checkbox-wrapper:has(.task-label)"
          >
            <fieldset>
              {isMediumSize
                ? hasIncompleteTasks &&
                  selectedList.tasks.map((task, index) => (
                    <ListItem
                      type={TASK_TYPE.IN_PROGRESS}
                      task={task}
                      key={task.id}
                      updateTask={updateTask}
                      deleteTask={deleteTask}
                      moveTask={moveTask}
                      isValidUrl={isValidUrl}
                      isFirst={index === 0}
                      isLast={index === selectedList.tasks.length - 1}
                    />
                  ))
                : hasIncompleteTasks &&
                  selectedList.tasks.map((task, index) => (
                    <ListItem
                      type={TASK_TYPE.IN_PROGRESS}
                      task={task}
                      key={task.id}
                      updateTask={updateTask}
                      deleteTask={deleteTask}
                      moveTask={moveTask}
                      isValidUrl={isValidUrl}
                      isFirst={index === 0}
                      isLast={index === selectedList.tasks.length - 1}
                    />
                  ))}
              {showCompletedTasks && (
                <details
                  className="completed-task-wrapper"
                  open={selectedList?.tasks.length < 1}
                >
                  <summary>
                    <span
                      data-l10n-id="newtab-widget-lists-completed-list"
                      data-l10n-args={JSON.stringify({
                        number: lists[selected]?.completed.length,
                      })}
                      className="completed-title"
                    ></span>
                  </summary>
                  {selectedList.completed.map(completedTask => (
                    <ListItem
                      key={completedTask.id}
                      type={TASK_TYPE.COMPLETED}
                      task={completedTask}
                      deleteTask={deleteTask}
                      updateTask={updateTask}
                    />
                  ))}
                </details>
              )}
            </fieldset>
          </moz-reorderable-list>
        )}
      </div>
    </article>
  );
}

function ListItem({
  task,
  updateTask,
  deleteTask,
  moveTask,
  isValidUrl,
  type,
  isFirst = false,
  isLast = false,
}) {
  const [isEditing, setIsEditing] = useState(false);
  const [exiting, setExiting] = useState(false);
  const isCompleted = type === TASK_TYPE.COMPLETED;

  const prefersReducedMotion =
    typeof window !== "undefined" &&
    typeof window.matchMedia === "function" &&
    window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  function toggleCompleted(completed) {
    if (completed && !prefersReducedMotion) {
      setExiting(true);
    } else {
      updateTask({ ...task, completed }, type);
    }
  }

  function handleCheckboxChange(e) {
    toggleCompleted(e.target.checked);
  }

  // When the CSS transition finishes, dispatch the real “completed = true”
  function handleTransitionEnd(e) {
    // only fire once for the exit:
    if (e.propertyName === "opacity" && exiting) {
      updateTask({ ...task, completed: true }, type);
      setExiting(false);
    }
  }

  function handleSave(newValue) {
    const trimmedTask = newValue.trimEnd();
    if (trimmedTask && trimmedTask !== task.value) {
      updateTask(
        { ...task, value: newValue, isUrl: isValidUrl(trimmedTask) },
        type
      );
      setIsEditing(false);
    }
  }

  function handleDelete() {
    deleteTask(task, type);
  }

  const taskLabel = task.isUrl ? (
    <a
      href={task.value}
      rel="noopener noreferrer"
      target="_blank"
      className="task-label"
      title={task.value}
    >
      {task.value}
    </a>
  ) : (
    <label
      className="task-label"
      title={task.value}
      onClick={() =>
        isCompleted ? toggleCompleted(false) : setIsEditing(true)
      }
    >
      {task.value}
    </label>
  );

  return (
    <div
      className={`task-item task-type-${type} ${exiting ? " exiting" : ""}`}
      id={task.id}
      key={task.id}
      onTransitionEnd={handleTransitionEnd}
    >
      <div className="checkbox-wrapper" key={isEditing}>
        <moz-checkbox
          onChange={handleCheckboxChange}
          checked={task.completed || exiting || undefined}
          aria-label={task.value}
        />
        {isCompleted ? (
          taskLabel
        ) : (
          <EditableText
            isEditing={isEditing}
            setIsEditing={setIsEditing}
            value={task.value}
            onSave={handleSave}
            type="task"
            ariaLabelL10nId="newtab-widget-lists-input-menu-edit2"
          >
            {taskLabel}
          </EditableText>
        )}
      </div>
      <moz-button
        data-l10n-id="newtab-menu-section-tooltip"
        iconSrc="chrome://global/skin/icons/more.svg"
        menuId={`panel-task-${task.id}`}
        type="ghost"
      />
      <panel-list className="panel-list-no-icons" id={`panel-task-${task.id}`}>
        {!isCompleted && (
          <>
            {task.isUrl && (
              <panel-item
                data-l10n-id="newtab-widget-lists-input-menu-open-link"
                onClick={() => window.open(task.value, "_blank", "noopener")}
              ></panel-item>
            )}
            <panel-item
              {...(isFirst ? { disabled: true } : {})}
              onClick={() => moveTask(task, "up")}
              data-l10n-id="newtab-widget-lists-input-menu-move-up"
            ></panel-item>
            <panel-item
              {...(isLast ? { disabled: true } : {})}
              onClick={() => moveTask(task, "down")}
              data-l10n-id="newtab-widget-lists-input-menu-move-down"
            ></panel-item>
            <panel-item
              data-l10n-id="newtab-widget-lists-input-menu-edit"
              className="edit-item"
              onClick={() => setIsEditing(true)}
            ></panel-item>
          </>
        )}
        <panel-item
          data-l10n-id="newtab-widget-lists-input-menu-delete"
          className="delete-item"
          onClick={handleDelete}
        ></panel-item>
      </panel-list>
    </div>
  );
}

function EditableText({
  value,
  isEditing,
  setIsEditing,
  onSave,
  onCancel,
  children,
  type,
  dataL10nId = null,
  ariaLabelL10nId = null,
  maxLength = 100,
  saveOnBlur = true,
}) {
  const [tempValue, setTempValue] = useState(value);
  const inputRef = useRef(null);
  const wrapperRef = useRef(null);
  const previousFocusRef = useRef(null);
  const cancellingRef = useRef(false);

  // True if tempValue is empty, null/undefined, or only whitespace
  const showPlaceholder = (tempValue ?? "").trim() === "";
  const inputL10nId =
    showPlaceholder && dataL10nId ? dataL10nId : ariaLabelL10nId;
  const inputL10nAttrs =
    showPlaceholder && dataL10nId ? "placeholder,aria-label" : "aria-label";

  useEffect(() => {
    if (isEditing) {
      cancellingRef.current = false;
      previousFocusRef.current = document.activeElement;
      inputRef.current?.focus();
    }
  }, [isEditing]);

  useEffect(() => {
    if (!isEditing) {
      setTempValue(value);
    }
  }, [isEditing, value]);

  const handleRestoreFocus = () => {
    const target = previousFocusRef.current;
    if (target && document.contains(target)) {
      target.focus();
    }
  };

  function handleKeyDown(e) {
    if (e.key === "Enter") {
      onSave(tempValue.trim());
      setIsEditing(false);
    } else if (e.key === "Escape") {
      cancellingRef.current = true;
      setIsEditing(false);
      setTempValue(value);
      onCancel?.();
      handleRestoreFocus();
    }
  }

  function handleOnBlur(e) {
    // Skip save when cancelling via Escape or the clear button — the
    // restored focus would otherwise trip handleOnBlur into saving.
    if (cancellingRef.current) {
      cancellingRef.current = false;
      return;
    }
    // Skip save when focus moved to the cancel button so its click handler can run.
    if (e.relatedTarget && wrapperRef.current?.contains(e.relatedTarget)) {
      return;
    }
    if (!saveOnBlur) {
      if (tempValue.trim()) {
        return;
      }
      setIsEditing(false);
      onCancel?.();
      return;
    }
    onSave(tempValue.trim());
    setIsEditing(false);
  }

  function handleClear() {
    cancellingRef.current = true;
    setIsEditing(false);
    setTempValue(value);
    onCancel?.();
    handleRestoreFocus();
  }

  const input = (
    <input
      className={`edit-${type}`}
      ref={inputRef}
      type="text"
      value={tempValue}
      maxLength={maxLength}
      onChange={event => setTempValue(event.target.value)}
      onBlur={handleOnBlur}
      onKeyDown={handleKeyDown}
      {...(inputL10nId ? { "data-l10n-id": inputL10nId } : {})}
      {...(inputL10nId ? { "data-l10n-attrs": inputL10nAttrs } : {})}
    />
  );

  if (!isEditing) {
    return [children];
  }

  if (type === "list") {
    return (
      <div className="edit-list-wrapper" ref={wrapperRef}>
        {input}
        <moz-button
          className="edit-list-clear"
          type="icon ghost"
          size="small"
          iconSrc="chrome://global/skin/icons/close.svg"
          data-l10n-id="newtab-widget-lists-edit-clear"
          onClick={handleClear}
        />
      </div>
    );
  }

  return input;
}

export { Lists };
