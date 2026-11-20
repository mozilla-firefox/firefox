# MozPerftest

**MozPerftest** can be used to run performance tests.

```{toctree}
running
tools
writing
developing
vision
```

The following documents all testing we have for MozPerftest.
If the owner does not specify the Usage and Description, it's marked N/A.

## accessible/tests/browser/performance

Performance tests for the accessibility engine

::::{dropdown} browser_addManyIds.js
:class-container: anchor-id-browser-addmanyids-js


:owner: Accessibility Team
:name: browser_addManyIds.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when adding 10000 IDs to nodes in DOM.**
::::

::::{dropdown} browser_addManyNodes.js
:class-container: anchor-id-browser-addmanynodes-js


:owner: Accessibility Team
:name: browser_addManyNodes.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when adding 100000 nodes to DOM.**
::::

::::{dropdown} browser_bounds.js
:class-container: anchor-id-browser-bounds-js


:owner: Accessibility Team
:name: browser_bounds.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when computing bounds for a framed (non-display:contents) accessible with 10000 children. Baseline counterpart to browser_displayContentsBounds.js.**
::::

::::{dropdown} browser_displayContentsBounds.js
:class-container: anchor-id-browser-displaycontentsbounds-js


:owner: Accessibility Team
:name: browser_displayContentsBounds.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when computing bounds for a display:contents accessible with 10000 children.**
::::

::::{dropdown} browser_implicitRoleARIAExpanded.js
:class-container: anchor-id-browser-implicitroleariaexpanded-js


:owner: Accessibility Team
:name: browser_implicitRoleARIAExpanded.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when adding 10000 elements with implicit ARIA roles**
::::

::::{dropdown} browser_panManySVGShapes.js
:class-container: anchor-id-browser-panmanysvgshapes-js


:owner: Accessibility Team
:name: browser_panManySVGShapes.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when panning an SVG workspace containing 2000 transformed groups, each with an ARIA labelled path. Modelled on the Blockly workspace in bug 2059322.**
::::

::::{dropdown} browser_reflowPseudoelements.js
:class-container: anchor-id-browser-reflowpseudoelements-js


:owner: Accessibility Team
:name: browser_reflowPseudoelements.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,alertThreshold:5,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when reflowing a table containing 15000 pseudoelements.**
::::

::::{dropdown} browser_removeManySpellingErrors.js
:class-container: anchor-id-browser-removemanyspellingerrors-js


:owner: Accessibility Team
:name: browser_removeManySpellingErrors.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when removing 500 spelling errors from content editable.**
::::

::::{dropdown} browser_testCSSVisibilityChange.js
:class-container: anchor-id-browser-testcssvisibilitychange-js


:owner: Accessibility Team
:name: browser_testCSSVisibilityChange.js
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:A11Y_TotalTime,unit:ms,shouldAlert:True, name:A11Y_DoInitialUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_parent,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_parent,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_parent,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_RecvCache_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_parent,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_parent,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_SendCache_parent,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_parent,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_parent,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_parent,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_parent,unit:iterations,shouldAlert:False, name:A11Y_DoInitialUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedNode_content,unit:ms,shouldAlert:False, name:A11Y_ContentRemovedAcc_content,unit:ms,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_content,unit:ms,shouldAlert:False, name:A11Y_ShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_RecvCache_content,unit:ms,shouldAlert:False, name:A11Y_ProcessShowEvent_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceEvents_content,unit:ms,shouldAlert:False, name:A11Y_CoalesceMutationEvents_content,unit:ms,shouldAlert:False, name:A11Y_ProcessHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_SendCache_content,unit:ms,shouldAlert:False, name:A11Y_WillRefresh_content,unit:ms,shouldAlert:False, name:A11Y_AccessibilityServiceInit_content,unit:ms,shouldAlert:False, name:A11Y_PlatformShowHideEvent_content,unit:ms,shouldAlert:False, name:A11Y_DoInitialUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessQueuedCacheUpdate_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedNode_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ContentRemovedAcc_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PruneOrInsertSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShutdownChildrenInSubtree_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_RecvCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessShowEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_CoalesceMutationEvents_Count_content,unit:iterations,shouldAlert:False, name:A11Y_ProcessHideEvent_Count_content,unit:iterations,shouldAlert:False, name:A11Y_SendCache_Count_content,unit:iterations,shouldAlert:False, name:A11Y_WillRefresh_Count_content,unit:iterations,shouldAlert:False, name:A11Y_AccessibilityServiceInit_Count_content,unit:iterations,shouldAlert:False, name:A11Y_PlatformShowHideEvent_Count_content,unit:iterations,shouldAlert:False
 --try-platform linux, mac, win
 --verbose

**Audit a11y performance when changing the CSS visibility of subtree's ancestor.**
::::


## browser/base/content/test

Performance tests from the 'browser/base/content/test' folder.

::::{dropdown} Dom-size
:class-container: anchor-id-dom-size


:owner: Browser Front-end team
:name: Dom-size

**Measures the size of the DOM**
::::


## browser/components/aiwindow/ui/test/browser_perf

Performance tests for Smart Window on Firefox Desktop

::::{dropdown} browser_smartwindow_intent_perf.js
:class-container: anchor-id-browser-smartwindow-intent-perf-js


:owner: GenAI Team
:name: browser_smartwindow_intent_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:SMART-INTENT-default-intent-latency-first-use,unit:ms,shouldAlert:False, name:SMART-INTENT-default-intent-latency-cold,unit:ms,shouldAlert:True, name:SMART-INTENT-default-intent-latency-warm,unit:ms,shouldAlert:True, name:SMART-INTENT-default-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:SMART-INTENT-default-engine-creation-time-cold,unit:ms,shouldAlert:True, name:SMART-INTENT-default-engine-run-time-first-use,unit:ms,shouldAlert:False, name:SMART-INTENT-default-engine-run-time-cold,unit:ms,shouldAlert:True, name:SMART-INTENT-default-engine-run-time-warm,unit:ms,shouldAlert:True, name:SMART-INTENT-default-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:SMART-INTENT-default-memory-before-run-cold,unit:MiB,shouldAlert:True, name:SMART-INTENT-default-memory-before-run-warm,unit:MiB,shouldAlert:True, name:SMART-INTENT-default-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:SMART-INTENT-default-memory-after-run-cold,unit:MiB,shouldAlert:True, name:SMART-INTENT-default-memory-after-run-warm,unit:MiB,shouldAlert:True, name:SMART-INTENT-default-peak-memory,unit:MiB,shouldAlert:True, name:SMART-INTENT-en-fr-intent-latency-first-use,unit:ms,shouldAlert:False, name:SMART-INTENT-en-fr-intent-latency-cold,unit:ms,shouldAlert:True, name:SMART-INTENT-en-fr-intent-latency-warm,unit:ms,shouldAlert:True, name:SMART-INTENT-en-fr-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:SMART-INTENT-en-fr-engine-creation-time-cold,unit:ms,shouldAlert:True, name:SMART-INTENT-en-fr-engine-run-time-first-use,unit:ms,shouldAlert:False, name:SMART-INTENT-en-fr-engine-run-time-cold,unit:ms,shouldAlert:True, name:SMART-INTENT-en-fr-engine-run-time-warm,unit:ms,shouldAlert:True, name:SMART-INTENT-en-fr-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:SMART-INTENT-en-fr-memory-before-run-cold,unit:MiB,shouldAlert:True, name:SMART-INTENT-en-fr-memory-before-run-warm,unit:MiB,shouldAlert:True, name:SMART-INTENT-en-fr-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:SMART-INTENT-en-fr-memory-after-run-cold,unit:MiB,shouldAlert:True, name:SMART-INTENT-en-fr-memory-after-run-warm,unit:MiB,shouldAlert:True, name:SMART-INTENT-en-fr-peak-memory,unit:MiB,shouldAlert:True
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**User-perceived latency and inference memory for the Smart Window intent classifier, driven through the production smartbar flow**
::::

::::{dropdown} browser_smartwindow_perf.js
:class-container: anchor-id-browser-smartwindow-perf-js


:owner: GenAI Team
:name: browser_smartwindow_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:ttft-overhead,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**User-perceived responsiveness of Smart Window across window states and profile sizes**
::::


## browser/components/genai/tests/browser

Performance tests for GenAI features on Firefox Desktop

::::{dropdown} browser_link_preview_perf.js
:class-container: anchor-id-browser-link-preview-perf-js


:owner: GenAI Team
:name: browser_link_preview_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:LINKPREVIEW-first-keypoint-latency-first-use,unit:ms,shouldAlert:False, name:LINKPREVIEW-first-keypoint-latency-cold,unit:ms,shouldAlert:True, name:LINKPREVIEW-keypoints-complete-latency-first-use,unit:ms,shouldAlert:False, name:LINKPREVIEW-keypoints-complete-latency-cold,unit:ms,shouldAlert:True, name:LINKPREVIEW-peak-memory,unit:MiB,shouldAlert:True, name:LINKPREVIEW-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:LINKPREVIEW-engine-creation-time-cold,unit:ms,shouldAlert:True, name:LINKPREVIEW-engine-run-time-first-use,unit:ms,shouldAlert:False, name:LINKPREVIEW-engine-run-time-cold,unit:ms,shouldAlert:True, name:LINKPREVIEW-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:LINKPREVIEW-memory-after-run-cold,unit:MiB,shouldAlert:True, name:LINKPREVIEW-time-to-first-token-first-use,unit:ms,shouldAlert:False, name:LINKPREVIEW-time-to-first-token-cold,unit:ms,shouldAlert:True, name:LINKPREVIEW-decoding-time-first-use,unit:ms,shouldAlert:False, name:LINKPREVIEW-decoding-time-cold,unit:ms,shouldAlert:True, name:LINKPREVIEW-tokens-per-second-first-use,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:LINKPREVIEW-tokens-per-second-cold,unit:tokens/s,shouldAlert:True,lowerIsBetter:False
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**User-perceived latency and inference memory for Link Preview key points, driven through the production UI flow**
::::


## browser/components/tabbrowser/test/browser/smarttabgrouping/performance

End-to-end performance tests for Smart Tab Grouping

::::{dropdown} browser_smart_tab_grouping_perf.js
:class-container: anchor-id-browser-smart-tab-grouping-perf-js


:owner: GenAI Team
:name: browser_smart_tab_grouping_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:STG-E2E-embedder-engine-creation-time-cold,unit:ms,shouldAlert:True, name:STG-E2E-embedder-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:STG-E2E-embedder-engine-run-time-cold,unit:ms,shouldAlert:True, name:STG-E2E-embedder-engine-run-time-first-use,unit:ms,shouldAlert:False, name:STG-E2E-embedder-engine-run-time-warm,unit:ms,shouldAlert:True, name:STG-E2E-embedder-memory-after-run-cold,unit:MiB,shouldAlert:True, name:STG-E2E-embedder-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:STG-E2E-embedder-memory-after-run-warm,unit:MiB,shouldAlert:True, name:STG-E2E-embedder-memory-before-run-cold,unit:MiB,shouldAlert:True, name:STG-E2E-embedder-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:STG-E2E-embedder-memory-before-run-warm,unit:MiB,shouldAlert:True, name:STG-E2E-labelLatency-cold,unit:ms,shouldAlert:True, name:STG-E2E-labelLatency-first-use,unit:ms,shouldAlert:False, name:STG-E2E-labelLatency-warm,unit:ms,shouldAlert:True, name:STG-E2E-peak-memory,unit:MiB,shouldAlert:True, name:STG-E2E-suggestLatency-cold,unit:ms,shouldAlert:True, name:STG-E2E-suggestLatency-first-use,unit:ms,shouldAlert:False, name:STG-E2E-suggestLatency-warm,unit:ms,shouldAlert:True, name:STG-E2E-topic-engine-creation-time-cold,unit:ms,shouldAlert:True, name:STG-E2E-topic-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:STG-E2E-topic-engine-run-time-cold,unit:ms,shouldAlert:True, name:STG-E2E-topic-engine-run-time-first-use,unit:ms,shouldAlert:False, name:STG-E2E-topic-engine-run-time-warm,unit:ms,shouldAlert:True, name:STG-E2E-topic-memory-after-run-cold,unit:MiB,shouldAlert:True, name:STG-E2E-topic-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:STG-E2E-topic-memory-after-run-warm,unit:MiB,shouldAlert:True, name:STG-E2E-topic-memory-before-run-cold,unit:MiB,shouldAlert:True, name:STG-E2E-topic-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:STG-E2E-topic-memory-before-run-warm,unit:MiB,shouldAlert:True
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**End-to-end Smart Tab Grouping latency, measured through the tab group menu**
::::


## browser/components/translations/tests/browser

Performance tests for Translations models on Firefox Desktop

::::{dropdown} Full-Page Translations Base Model
:class-container: anchor-id-full-page-translations-base-model


:owner: Translations Team
:name: Full-Page Translations Base Model
:Default options:

::

 --perfherder
 --perfherder-metrics name:engine-init-time,unit:ms,shouldAlert:True,lowerIsBetter:True, name:words-per-second,unit:WPS,shouldAlert:True,lowerIsBetter:False, name:tokens-per-second,unit:TPS,shouldAlert:True,lowerIsBetter:False, name:peak-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:post-gc-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:peak-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:post-gc-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:total-translation-time,unit:s,shouldAlert:True,lowerIsBetter:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Tests the performance of Full Page Translations with a base-architecture model**
::::

::::{dropdown} Full-Page Translations BaseMemory Model
:class-container: anchor-id-full-page-translations-basememory-model


:owner: Translations Team
:name: Full-Page Translations BaseMemory Model
:Default options:

::

 --perfherder
 --perfherder-metrics name:engine-init-time,unit:ms,shouldAlert:True,lowerIsBetter:True, name:words-per-second,unit:WPS,shouldAlert:True,lowerIsBetter:False, name:tokens-per-second,unit:TPS,shouldAlert:True,lowerIsBetter:False, name:peak-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:post-gc-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:peak-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:post-gc-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:total-translation-time,unit:s,shouldAlert:True,lowerIsBetter:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Tests the performance of Full Page Translations with a base-memory-architecture model**
::::

::::{dropdown} Full-Page Translations Tiny Model
:class-container: anchor-id-full-page-translations-tiny-model


:owner: Translations Team
:name: Full-Page Translations Tiny Model
:Default options:

::

 --perfherder
 --perfherder-metrics name:engine-init-time,unit:ms,shouldAlert:True,lowerIsBetter:True, name:words-per-second,unit:WPS,shouldAlert:True,lowerIsBetter:False, name:tokens-per-second,unit:TPS,shouldAlert:True,lowerIsBetter:False, name:peak-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:post-gc-parent-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:peak-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:post-gc-inference-process-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:total-translation-time,unit:s,shouldAlert:True,lowerIsBetter:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Tests the performance of Full Page Translations with a tiny-architecture model**
::::


## browser/components/urlbar/tests/browser-ml

Performance tests for urlbar features on Firefox Desktop

::::{dropdown} browser_quicksuggest_ml_perf.js
:class-container: anchor-id-browser-quicksuggest-ml-perf-js


:owner: GenAI Team
:name: browser_quicksuggest_ml_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:MLSUGGEST-suggestion-latency-first-use,unit:ms,shouldAlert:False, name:MLSUGGEST-suggestion-latency-warm,unit:ms,shouldAlert:True, name:MLSUGGEST-peak-memory,unit:MiB,shouldAlert:True, name:MLSUGGEST-intent-engine-run-time-first-use,unit:ms,shouldAlert:False, name:MLSUGGEST-intent-engine-run-time-warm,unit:ms,shouldAlert:True, name:MLSUGGEST-intent-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:MLSUGGEST-intent-memory-after-run-warm,unit:MiB,shouldAlert:True, name:MLSUGGEST-ner-engine-run-time-first-use,unit:ms,shouldAlert:False, name:MLSUGGEST-ner-engine-run-time-warm,unit:ms,shouldAlert:True, name:MLSUGGEST-ner-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:MLSUGGEST-ner-memory-after-run-warm,unit:MiB,shouldAlert:True
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**User-perceived latency and inference memory for ML-backed Firefox Suggest, driven through the production urlbar flow**
::::

::::{dropdown} browser_urlbar_semantic_history_perf.js
:class-container: anchor-id-browser-urlbar-semantic-history-perf-js


:owner: GenAI Team
:name: browser_urlbar_semantic_history_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:SEMANTICHISTORY-indexing-engine-run-time,unit:ms,shouldAlert:True, name:SEMANTICHISTORY-indexing-memory-after-run,unit:MiB,shouldAlert:True, name:SEMANTICHISTORY-semantic-result-latency,unit:ms,shouldAlert:True, name:SEMANTICHISTORY-peak-memory,unit:MiB,shouldAlert:True, name:SEMANTICHISTORY-engine-creation-time,unit:ms,shouldAlert:True, name:SEMANTICHISTORY-engine-run-time,unit:ms,shouldAlert:True, name:SEMANTICHISTORY-memory-after-run,unit:MiB,shouldAlert:True
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**User-perceived latency and inference memory for semantic history search, driven through history ingestion and the production urlbar flow**
::::


## browser/extensions/formautofill/test/browser/performance

Performance tests for Form Autofill on Firefox Desktop

::::{dropdown} browser_formautofill_ml_perf.js
:class-container: anchor-id-browser-formautofill-ml-perf-js


:owner: Form Autofill Team
:name: browser_formautofill_ml_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:FORM-AUTOFILL-single-engine-focus-to-identification-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-single-engine-focus-to-identification-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-single-engine-focus-to-identification-time-warm,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-single-engine-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-single-engine-engine-creation-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-single-engine-engine-run-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-single-engine-engine-run-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-single-engine-engine-run-time-warm,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-single-engine-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:FORM-AUTOFILL-single-engine-memory-before-run-cold,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-single-engine-memory-before-run-warm,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-single-engine-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:FORM-AUTOFILL-single-engine-memory-after-run-cold,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-single-engine-memory-after-run-warm,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-single-engine-peak-memory,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-focus-to-identification-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-double-engine-focus-to-identification-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-focus-to-identification-time-warm,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-peak-memory,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-double-engine-encoder-engine-creation-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-engine-run-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-double-engine-encoder-engine-run-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-engine-run-time-warm,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:FORM-AUTOFILL-double-engine-encoder-memory-before-run-cold,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-memory-before-run-warm,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:FORM-AUTOFILL-double-engine-encoder-memory-after-run-cold,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-encoder-memory-after-run-warm,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-double-engine-head-engine-creation-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-engine-run-time-first-use,unit:ms,shouldAlert:False, name:FORM-AUTOFILL-double-engine-head-engine-run-time-cold,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-engine-run-time-warm,unit:ms,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:FORM-AUTOFILL-double-engine-head-memory-before-run-cold,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-memory-before-run-warm,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:FORM-AUTOFILL-double-engine-head-memory-after-run-cold,unit:MiB,shouldAlert:True, name:FORM-AUTOFILL-double-engine-head-memory-after-run-warm,unit:MiB,shouldAlert:True
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Integrated ML Autofill performance for the single- and double-engine classifiers**
::::


## dom/media/webspeech/recognition/test

Performance tests for on-device speech recognition

::::{dropdown} browser_speech_recognition_perf.js
:class-container: anchor-id-browser-speech-recognition-perf-js


:owner: Media Playback Team
:name: browser_speech_recognition_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:engine-ready-latency,unit:ms,shouldAlert:False, name:first-result-latency,unit:ms,shouldAlert:False, name:finalization-latency,unit:ms,shouldAlert:False, name:real-time-factor,unit:X,shouldAlert:False,lowerIsBetter:False, name:memory-after-init,unit:MB,shouldAlert:False, name:memory-mid-recognition,unit:MB,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Latency, real-time factor and memory of on-device speech recognition**
::::


## dom/serviceworkers/test/performance

Performance tests running through Mochitest for Service Workers

::::{dropdown} Service Worker Caching
:class-container: anchor-id-service-worker-caching


:owner: DOM LWS
:name: Service Worker Caching
:Default options:

::

 --perfherder
 --perfherder-metrics name:No cache,unit:ms,shouldAlert:True, name:Cached,unit:ms,shouldAlert:True, name:No cache again,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test service worker caching.**
::::

::::{dropdown} Service Worker Fetch
:class-container: anchor-id-service-worker-fetch


:owner: DOM LWS
:name: Service Worker Fetch
:Default options:

::

 --perfherder
 --perfherder-metrics name:Cold fetch,unit:ms,shouldAlert:True, name:Undisturbed fetch,unit:ms,shouldAlert:True, name:Intercepted fetch,unit:ms,shouldAlert:True, name:Liberated fetch,unit:ms,shouldAlert:True, name:Undisturbed XHR,unit:ms,shouldAlert:True, name:Intercepted XHR,unit:ms,shouldAlert:True, name:Liberated XHR,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test cold and warm fetches.**
::::

::::{dropdown} Service Worker Registration
:class-container: anchor-id-service-worker-registration


:owner: DOM LWS
:name: Service Worker Registration
:Default options:

::

 --perfherder
 --perfherder-metrics name:Registration,unit:ms,shouldAlert:True, name:Registration Internals,unit:ms,shouldAlert:True, name:Activation,unit:ms,shouldAlert:True, name:Unregistration,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test registration, activation, and unregistration.**
::::

::::{dropdown} Service Worker Update
:class-container: anchor-id-service-worker-update


:owner: DOM LWS
:name: Service Worker Update
:Default options:

::

 --perfherder
 --perfherder-metrics name:Vacuous update,unit:ms,shouldAlert:True, name:Server update,unit:ms,shouldAlert:True, name:Main callback,unit:ms,shouldAlert:True, name:SW callback,unit:ms,shouldAlert:True, name:Update internals,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test updating.**
::::


## dom/webgpu/tests/mochitest

Performance tests from the 'dom/webgpu/tests/mochitest' folder.

::::{dropdown} Queue Write
:class-container: anchor-id-queue-write


:owner: Graphics Team
:name: Queue Write
:Default options:

::

 --perfherder
 --perfherder-metrics name:writeBuffer Time,unit:ms, name:writeTexture Time,unit:ms
 --manifest perftest.toml
 --manifest-flavor plain

**Test the performance of Queue.writeBuffer and Queue.writeTexture**
::::


## intl/benchmarks/test/xpcshell

Performance tests running through XPCShell for Intl code

::::{dropdown} IntlDateTimeFormat
:class-container: anchor-id-intldatetimeformat


:owner: Internationalization Team
:name: Intl.DateTimeFormat
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.DateTimeFormat constructor iterations,unit:iterations, name:Intl.DateTimeFormat constructor accumulatedTime,unit:ms, name:Intl.DateTimeFormat constructor perCallTime,unit:ms, name:Intl.DateTimeFormat.prototype.format iterations,unit:iterations, name:Intl.DateTimeFormat.prototype.format accumulatedTime,unit:ms, name:Intl.DateTimeFormat.prototype.format perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.DateTimeFormat implementation.**
::::

::::{dropdown} IntlLocale
:class-container: anchor-id-intllocale


:owner: Internationalization Team
:name: Intl.Locale
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.Locale constructor iterations,unit:iterations, name:Intl.Locale constructor accumulatedTime,unit:ms, name:Intl.Locale constructor perCallTime,unit:ms, name:Intl.Locale.prototype accessors iterations,unit:iterations, name:Intl.Locale.prototype accessors accumulatedTime,unit:ms, name:Intl.Locale.prototype accessors perCallTime,unit:ms, name:Intl.Locale.maximize operation iterations,unit:iterations, name:Intl.Locale.maximize operation accumulatedTime,unit:ms, name:Intl.Locale.maximize operation perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.Locale implementation.**
::::

::::{dropdown} IntlNumberFormat
:class-container: anchor-id-intlnumberformat


:owner: Internationalization Team
:name: Intl.NumberFormat
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.NumberFormat constructor iterations,unit:iterations, name:Intl.NumberFormat constructor accumulatedTime,unit:ms, name:Intl.NumberFormat constructor perCallTime,unit:ms, name:Intl.NumberFormat.prototype.format iterations,unit:iterations, name:Intl.NumberFormat.prototype.format accumulatedTime,unit:ms, name:Intl.NumberFormat.prototype.format perCallTime,unit:ms, name:Intl.NumberFormat.prototype.formatToParts iterations,unit:iterations, name:Intl.NumberFormat.prototype.formatToParts accumulatedTime,unit:ms, name:Intl.NumberFormat.prototype.formatToParts perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.NumberFormat implementation.**
::::

::::{dropdown} IntlPluralRules
:class-container: anchor-id-intlpluralrules


:owner: Internationalization Team
:name: Intl.PluralRules
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.PluralRules constructor iterations,unit:iterations, name:Intl.PluralRules constructor accumulatedTime,unit:ms, name:Intl.PluralRules constructor perCallTime,unit:ms, name:Intl.PluralRules.prototype.select iterations,unit:iterations, name:Intl.PluralRules.prototype.select accumulatedTime,unit:ms, name:Intl.PluralRules.prototype.select perCallTime,unit:ms, name:Intl.PluralRules pluralCategories iterations,unit:iterations, name:Intl.PluralRules pluralCategories accumulatedTime,unit:ms, name:Intl.PluralRules pluralCategories perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.PluralRules implementation.**
::::


## netwerk/test/perf

Performance tests from the 'network/test/perf' folder.

::::{dropdown} cloudflare
:class-container: anchor-id-cloudflare


:owner: Network Team
:name: cloudflare

**User-journey live site test for Cloudflare blog.**
::::

::::{dropdown} controlled
:class-container: anchor-id-controlled


:owner: Network Team
:name: controlled
:tags: throttlable

**User-journey live site test for controlled server**
::::

::::{dropdown} facebook-scroll
:class-container: anchor-id-facebook-scroll


:owner: Network Team
:name: facebook-scroll

**Measures the number of requests per second after a scroll.**
::::

::::{dropdown} g-image
:class-container: anchor-id-g-image


:owner: Network Team
:name: g-image

**Measures the number of images per second after a scroll.**
::::

::::{dropdown} g-search
:class-container: anchor-id-g-search


:owner: Network Team
:name: g-search

**User-journey live site test for google search**
::::

::::{dropdown} lq-fetch
:class-container: anchor-id-lq-fetch


:owner: Network Team
:name: lq-fetch

**Measures the amount of time it takes to load a set of images.**
::::

::::{dropdown} youtube-noscroll
:class-container: anchor-id-youtube-noscroll


:owner: Network Team
:name: youtube-noscroll

**Measures quality of the video being played.**
::::

::::{dropdown} youtube-scroll
:class-container: anchor-id-youtube-scroll


:owner: Network Team
:name: youtube-scroll

**Measures quality of the video being played.**
::::


## netwerk/test/unit

Performance tests from the 'netwerk/test/unit' folder.

::::{dropdown} DNS Benchmark
:class-container: anchor-id-dns-benchmark


:owner: Network Team
:name: DNS Benchmark
:Default options:

::

 --perfherder

**Benchmark for DNS resolver lock contention under low and high concurrency.**

This test measures DNS resolver throughput by comparing baseline resolution times with low concurrency (within the thread pool) against performance under high concurrency (far exceeding the thread pool). It validates that the resolver scales with available threads rather than serializing all requests.
::::

::::{dropdown} TRR Benchmark
:class-container: anchor-id-trr-benchmark


:owner: Network Team
:name: TRR Benchmark
:Default options:

::

 --perfherder
 --perfherder-metrics name:average,unit:ms,shouldAlert:True,lowerIsBetter:True

**Benchmark for TRR (Trusted Recursive Resolver) DNS lookup performance under various load conditions.**

This test measures TRR DNS lookup performance by comparing baseline DNS resolution times with low contention against performance under high contention with many concurrent requests. It validates that TRR requests maintain acceptable performance even when the network is busy with other requests.
::::

::::{dropdown} http3 raw
:class-container: anchor-id-http3-raw


:owner: Network Team
:name: http3 raw
:tags: network,http3,quic
:Default options:

::

 --perfherder
 --perfherder-metrics name:speed,unit:bps
 --xpcshell-cycles 13
 --verbose
 --try-platform linux, mac

**XPCShell tests that verifies the lib integration against a local server**
::::


## testing/performance

Performance tests from the 'testing/performance' folder.

::::{dropdown} BBC Link
:class-container: anchor-id-bbc-link


:owner: Performance Team
:name: BBC Link

**Measures time to load BBC homepage**
::::

::::{dropdown} Facebook
:class-container: anchor-id-facebook


:owner: Performance Team
:name: Facebook

**Measures time to log in to Facebook**
::::

::::{dropdown} JSConf (cold)
:class-container: anchor-id-jsconf-cold


:owner: Performance Team
:name: JSConf (cold)

**Measures time to load JSConf page (cold)**
::::

::::{dropdown} JSConf (warm)
:class-container: anchor-id-jsconf-warm


:owner: Performance Team
:name: JSConf (warm)

**Measures time to load JSConf page (warm)**
::::

::::{dropdown} Politico Link
:class-container: anchor-id-politico-link


:owner: Performance Team
:name: Politico Link

**Measures time to load Politico homepage**
::::

::::{dropdown} YouTube Link
:class-container: anchor-id-youtube-link


:owner: Performance Team
:name: YouTube Link

**Measures time to load YouTube video**
::::

::::{dropdown} pageload
:class-container: anchor-id-pageload


:owner: Performance Team
:name: pageload

**Measures time to load mozilla page**
::::

::::{dropdown} perfstats
:class-container: anchor-id-perfstats


:owner: Performance Team
:name: perfstats

**Collect perfstats for the given site**

This test launches browsertime with the perfStats option (will collect low-overhead timings, see Bug 1553254). The test currently runs a short user journey. A selection of popular sites are visited, first as cold pageloads, and then as warm.
::::


## toolkit/components/ml/tests/browser

Performance tests running through Mochitest for ML Models

::::{dropdown} ML Semantic History Search
:class-container: anchor-id-ml-semantic-history-search


:owner: GenAI Team
:name: ML Semantic History Search
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:True, name:memory,unit:MB,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform mac, win

**Test for latency for ML Semantic Search History Feature**
::::

::::{dropdown} ML Smart Tab Clustering
:class-container: anchor-id-ml-smart-tab-clustering


:owner: GenAI Team
:name: ML Smart Tab Clustering
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Testing Smart Tab Clustering**
::::

::::{dropdown} browser_ml_autofill_perf.js
:class-container: anchor-id-browser-ml-autofill-perf-js


:owner: GenAI Team
:name: browser_ml_autofill_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:AUTOFILL-pipeline-ready-latency,unit:ms,shouldAlert:False, name:AUTOFILL-initialization-latency,unit:ms,shouldAlert:False, name:AUTOFILL-model-run-latency,unit:ms,shouldAlert:False, name:AUTOFILL-total-memory-usage,unit:MiB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False, name:AUTOFILL-two-engine-concurrent-init-latency,unit:ms,shouldAlert:False, name:AUTOFILL-two-engine-e2e-run-latency,unit:ms,shouldAlert:False, name:AUTOFILL-two-engine-total-memory-usage,unit:MiB,shouldAlert:False, name:AUTOFILL-encoder-pipeline-ready-latency,unit:ms,shouldAlert:False, name:AUTOFILL-encoder-initialization-latency,unit:ms,shouldAlert:False, name:AUTOFILL-encoder-model-run-latency,unit:ms,shouldAlert:False, name:AUTOFILL-head-pipeline-ready-latency,unit:ms,shouldAlert:False, name:AUTOFILL-head-initialization-latency,unit:ms,shouldAlert:False, name:AUTOFILL-head-model-run-latency,unit:ms,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Latency for the ML Autofill model (default single-model and opt-in two-engine)**
::::

::::{dropdown} browser_ml_engine_multi_perf.js
:class-container: anchor-id-browser-ml-engine-multi-perf-js


:owner: GenAI Team
:name: browser_ml_engine_multi_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Testing model execution concurrently**
::::

::::{dropdown} browser_ml_engine_perf.js
:class-container: anchor-id-browser-ml-engine-perf-js


:owner: GenAI Team
:name: browser_ml_engine_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for ml models**
::::

::::{dropdown} browser_ml_llama_smollm2_smoke.js
:class-container: anchor-id-browser-ml-llama-smollm2-smoke-js


:owner: GenAI Team
:name: browser_ml_llama_smollm2_smoke.js
:Default options:

::

 --perfherder
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Structural smoke for the native llama.cpp backend with SmolLM2 360M Instruct.**
::::

::::{dropdown} browser_ml_llama_summarizer_perf.js
:class-container: anchor-id-browser-ml-llama-summarizer-perf-js


:owner: GenAI Team
:name: browser_ml_llama_summarizer_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Summarization latency and memory for the SmolLM2 and Qwen3 models on llama.cpp**
::::

::::{dropdown} browser_ml_smart_tab_perf.js
:class-container: anchor-id-browser-ml-smart-tab-perf-js


:owner: GenAI Team
:name: browser_ml_smart_tab_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Testing Smart Tab Models**
::::

::::{dropdown} browser_ml_suggest_feature_perf.js
:class-container: anchor-id-browser-ml-suggest-feature-perf-js


:owner: GenAI Team
:name: browser_ml_suggest_feature_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:True, name:memory,unit:MiB,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for ML suggest Feature**
::::

::::{dropdown} browser_ml_suggest_inference.js
:class-container: anchor-id-browser-ml-suggest-inference-js


:owner: GenAI Team
:name: browser_ml_suggest_inference.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:inference-pipeline-ready-latency,unit:ms,shouldAlert:False, name:inference-initialization-latency,unit:ms,shouldAlert:False, name:inference-model-run-latency,unit:ms,shouldAlert:False, name:inference-total-memory-usage,unit:MiB,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for ML suggest Inference Model**
::::


## toolkit/components/pdfjs/test

Performance tests for PDF.js features on Firefox Desktop

::::{dropdown} browser_pdfjs_alttext_perf.js
:class-container: anchor-id-browser-pdfjs-alttext-perf-js


:owner: GenAI Team
:name: browser_pdfjs_alttext_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:PDFJS-ALT-TEXT-alt-text-latency-first-use,unit:ms,shouldAlert:False, name:PDFJS-ALT-TEXT-alt-text-latency-cold,unit:ms,shouldAlert:True, name:PDFJS-ALT-TEXT-alt-text-latency-warm,unit:ms,shouldAlert:True, name:PDFJS-ALT-TEXT-engine-creation-time-first-use,unit:ms,shouldAlert:False, name:PDFJS-ALT-TEXT-engine-creation-time-cold,unit:ms,shouldAlert:True, name:PDFJS-ALT-TEXT-engine-run-time-first-use,unit:ms,shouldAlert:False, name:PDFJS-ALT-TEXT-engine-run-time-cold,unit:ms,shouldAlert:True, name:PDFJS-ALT-TEXT-engine-run-time-warm,unit:ms,shouldAlert:True, name:PDFJS-ALT-TEXT-memory-before-run-first-use,unit:MiB,shouldAlert:False, name:PDFJS-ALT-TEXT-memory-before-run-cold,unit:MiB,shouldAlert:True, name:PDFJS-ALT-TEXT-memory-before-run-warm,unit:MiB,shouldAlert:True, name:PDFJS-ALT-TEXT-memory-after-run-first-use,unit:MiB,shouldAlert:False, name:PDFJS-ALT-TEXT-memory-after-run-cold,unit:MiB,shouldAlert:True, name:PDFJS-ALT-TEXT-memory-after-run-warm,unit:MiB,shouldAlert:True, name:PDFJS-ALT-TEXT-peak-memory,unit:MiB,shouldAlert:True
 --verbose
 --ml-services
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**User-perceived latency and inference memory for PDF.js alt-text generation, driven through the production image-adding flow**
::::


## toolkit/components/pdfjs/test/perf

Performance tests for the PDF.js viewer UI on Firefox Desktop, using PDFs with empty pages

::::{dropdown} browser_pdfjs_ui_interaction_perf.js
:class-container: anchor-id-browser-pdfjs-ui-interaction-perf-js


:owner: PDF.js Team
:name: browser_pdfjs_ui_interaction_perf.js
:Default options:

::

 --extra-args setpref=layout.frame_rate=0, setpref=docshell.event_starvation_delay_hint=1
 --perfherder
 --perfherder-metrics name:PDFJS-UI-scroll-100p,unit:ms,shouldAlert:True, name:PDFJS-UI-zoom-100p,unit:ms,shouldAlert:True, name:PDFJS-UI-goto-1000p,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Time for the PDF.js viewer to scroll, zoom and go to pages in PDFs with 100 and 1000 empty pages**
::::

::::{dropdown} browser_pdfjs_ui_open_perf.js
:class-container: anchor-id-browser-pdfjs-ui-open-perf-js


:owner: PDF.js Team
:name: browser_pdfjs_ui_open_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:PDFJS-UI-first-open-pagesinit,unit:ms,shouldAlert:False, name:PDFJS-UI-first-open-pagerendered,unit:ms,shouldAlert:False, name:PDFJS-UI-open-1p-pagesinit,unit:ms,shouldAlert:True, name:PDFJS-UI-open-1p-pagerendered,unit:ms,shouldAlert:True, name:PDFJS-UI-open-1p-render-delay,unit:ms,shouldAlert:False, name:PDFJS-UI-open-1000p-pagesinit,unit:ms,shouldAlert:True, name:PDFJS-UI-open-1000p-pagerendered,unit:ms,shouldAlert:True, name:PDFJS-UI-open-1000p-pagesloaded,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Time for the PDF.js viewer to open PDFs with 1 and 1000 empty pages, from the navigation start**
::::


## toolkit/components/places/tests/browser/performance

Performance tests for Toolkit: Places

::::{dropdown} Calculate Frecency Speed
:class-container: anchor-id-calculate-frecency-speed


:owner: Places
:name: Calculate Frecency Speed
:Default options:

::

 --extra-args headless
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --perfherder
 --perfherder-metrics name:PlacesCalculateFrecencyHighFrequency,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyLowFrequency,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyChunked,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyBookmarkOnly,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyRedirectChain,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyInteractions,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyManyVisits,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyVeryManyVisits,unit:ms,shouldAlert:False, name:PlacesCalculateFrecencyWorstCase,unit:ms,shouldAlert:False
 --try-platform linux, mac
 --verbose

**Audits the speed of running calculate_frecency.**
::::


## toolkit/components/url-classifier/tests/performance

Performance tests for the URL Classifier

::::{dropdown} UrlClassifierExceptionListLookup
:class-container: anchor-id-urlclassifierexceptionlistlookup


:owner: Privacy Team
:name: UrlClassifier.ExceptionListLookup
:tags: url-classifier
:Default options:

::

 --perfherder
 --perfherder-metrics name:UrlClassifier.ExceptionListLookup iterations,unit:iterations, name:UrlClassifier.ExceptionListLookup accumulatedTime,unit:ms, name:UrlClassifier.ExceptionListLookup perCallTime,unit:ms
 --verbose

**Test the speed of nsIUrlClassifierExceptionList#matches.**
::::


If you have any questions, please see this [wiki page](https://wiki.mozilla.org/TestEngineering/Performance#Where_to_find_us).
