# Firefox Network Scheduling and Prioritization

*Please note that this document is a first draft and is subject to further refinement and updates.*
*Refers to Fx 132+*


## Scheduling
Firefox employs several techniques to orchestrate network request scheduling:

### DOM Preload Scanner (Speculative Loader)
- Runs on a background thread, scanning HTML for resource URLs to preload.
- Adds discovered resources to a speculative load queue.
- See [MDN Documentation on HTML Parser Threading (archived)](https://web.archive.org/web/20201021003137/https://developer.mozilla.org/en-US/docs/Mozilla/Gecko/HTML_parser_threading).

### DOM Parser (Non-Speculative)
- Makes requests for elements as the DOM tree is constructed.

### Class of Service
- Categorizes requests based on context or request target. See [nsIClassOfService.idl](https://searchfox.org/mozilla-central/rev/f549a50b1e39b1e6bea19912d92545c4c0a06b7b/netwerk/base/nsIClassOfService.idl#7-15)
- Categories (e.g. [Leader, Normal, Follower, Speculative, etc](https://searchfox.org/mozilla-central/rev/f549a50b1e39b1e6bea19912d92545c4c0a06b7b/netwerk/base/nsIClassOfService.idl#69-102)) can affect both network and cache behaviours
- May defer scheduling of certain requests (e.g., trackers classified as `ClassOfService::Tail`). For detailed information about resource tailing, see [Firefox Resource Tailing Documentation](resource_tailing.md).
- Also defines [base urgency for a request](https://searchfox.org/mozilla-central/rev/f2c181a7ab3bfea4d2266521e6eac713630479b3/netwerk/protocol/http/nsHttpHandler.cpp#794-818)

## Priority
As HTTP/1.1 does not feature a prioritization system (sequential requests), Firefox uses `supportsPriority` and `classOfService` to order requests.
With HTTP/2 and HTTP/3 utilizing a single, multiplexed connection to each host, request priority becomes crucial due to bandwidth limitations. Priority is expressed using the **Extensible Prioritization Scheme**, which includes:

- **Urgency**: Ranges from `0` (highest priority) to `7` (lowest priority). Resources with a lower numerical urgency are delivered before those with higher urgencies. For example, all resources with urgency `2` are transferred before those with urgency `3` begin.
- **Incremental**: A boolean indicating whether bandwidth should be split between this resource and others of the same urgency. The incremental flag determines if resources of the same urgency are sent sequentially (`i` not present) or incrementally (`i`).

These priorities are calculated based on the following factors:
- Resource type and its placement within the document or viewport.
- Assigned [Class of Service](https://searchfox.org/mozilla-central/rev/f2c181a7ab3bfea4d2266521e6eac713630479b3/netwerk/protocol/http/nsHttpHandler.cpp#794-818)
- Use of the [SupportsPriority](https://searchfox.org/mozilla-central/rev/f549a50b1e39b1e6bea19912d92545c4c0a06b7b/xpcom/threads/nsISupportsPriority.idl#8-16) interface.
- Application of PriorityHints (e.g., `fetchpriority="high"`) [adjustments](https://searchfox.org/mozilla-central/rev/1e8cec3727d6e09f4af41bb3d202b7a4c326ed84/modules/libpref/init/StaticPrefList.yaml#12615-12863) which is implemented via `SupportsPriority`
- Backgrounded tabs will have their priorities lowered.

## Resource Scheduling and Priority Table

| **Resource Type**                    | **Class of Service**                    | **supportsPriority**                                                                 | **Urgency**                                   | **Incremental** | **Notes**                                                                 |
|--------------------------------------|------------------------------------------|--------------------------------------------------------------------------------------|-----------------------------------------------|-----------------|---------------------------------------------------------------------------|
| **HTML, Root Document**              | `UrgentStart (64)`                       | `PRIORITY_HIGHEST, -20`                                                              | `0`                                           | `true`          |                                                                           |
| **CSS (`<head>`, Render-Blocking)**  | `Leader (1)`                             | `PRIORITY_NORMAL, 0`<br>`fetchpriority=high: PRIORITY_HIGH, -20`<br>`fetchpriority=low: PRIORITY_LOW, 0` | `2` <br>`fetchpriority=high: 0`<br>`fetchpriority=low: 2` | `false`         |                                                                           |
| **CSS (rel=preload)**                | `Leader (1)`                             | `PRIORITY_HIGHEST, -20`<br>`fetchpriority=high: PRIORITY_HIGH, -20`<br>`fetchpriority=low: PRIORITY_LOW, -10`| `0` <br>`fetchpriority=high: 0`<br>`fetchpriority=low: 1` | `false`         |                                                                         |
| **CSS (Body)**                       | `Leader (1)`                             | `PRIORITY_NORMAL, 0` <br>`fetchpriority=high: PRIORITY_HIGH, -20`<br>`fetchpriority=low: PRIORITY_NORMAL, 0` | `2` <br>`fetchpriority=high: 0`<br>`fetchpriority=low: 2`| `false`         |                                                                          |
| **JavaScript (Blocking)**            | `Leader (1)`                             | `PRIORITY_NORMAL, 0` <br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOW, 10` | `2` <br>`fetchpriority=high: 1`<br>`fetchpriority=low: 3` | `false`         |                                                                           |
| **JavaScript (rel=preload)**         | `Unblocked (16)`                         | `PRIORITY_HIGHEST, -20` <br>`fetchpriority=high: PRIORITY_HIGH, -20`<br>`fetchpriority=low: PRIORITY_LOW, 10`| `1` <br>`fetchpriority=high: 1`<br>`fetchpriority=low: 4` | `false`         |                                                                         |
| **JavaScript (Async)**               | `TailAllowed (512), Unblocked (16)`      | `PRIORITY_NORMAL, 0` <br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOW, 10` | `3`<br>`fetchpriority=high: 2`<br>`fetchpriority=low: 4` | `false`         |                                                                           |
| **JavaScript (Defer)**               | `Unblocked (16)`                         | `PRIORITY_NORMAL, 0` <br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOW, 10` | `3`<br>`fetchpriority=high: 2`<br>`fetchpriority=low: 4` | `false`         |                                                                           |
| **Font @font-face**                  | `TailForbidden (1024)`                   | `PRIORITY_HIGH, -10`                                                                 | `3`                                           | `false`         |  Urgency affected by `TailForbidden` CoS. fetchpriority applied via rel="preload"    |
| **Font (rel=preload)**               | `TailForbidden (1024), Unblocked (16)`   | `PRIORITY_HIGH, -10`<br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOW, 10` | `2`<br>`fetchpriority=high: 2`<br>`fetchpriority=low: 4` | `false`         |                                                                           |
| **Image**                            | `(0)`                                    | `PRIORITY_LOW, 10`<br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOWEST, 20` | `5`<br>`fetchpriority=high: 3`<br>`fetchpriority=low: 6` | `true`          |                                                                           |
| **Image (rel=preload)**              | `(0)`                                    | `PRIORITY_LOW, 10`<br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOWEST, 20` | `4`<br>`fetchpriority=high: 3`<br>`fetchpriority=low: 5` | `true`          |                                                                           |
| **Image (About to Be Rendered)**     | `(0)`                                    | `PRIORITY_HIGH, -10`                                                                 | `3`                         | `true`          | See: [image_layout_network_priority](https://searchfox.org/mozilla-central/rev/a13db27562f9237db97e2ea5b01dc879d5b55b74/modules/libpref/init/StaticPrefList.yaml#7429-7431) and [bug 1915817](https://bugzilla.mozilla.org/show_bug.cgi?id=1915817)                                   |
| **Fetch**                            | `(0)`                                    | `PRIORITY_NORMAL, 0`<br>`fetchpriority=high: PRIORITY_HIGH, -10`<br>`fetchpriority=low: PRIORITY_LOW, 10` | `4`<br>`fetchpriority=high: 3`<br>`fetchpriority=low: 5` | `true`          |                                                                           |
| **Tracker (Script)**                 | `Tail (256), Unblocked (16)`             | `PRIORITY_NORMAL, 0`                                                                 | `3`                                           | `false`         | Request is tailed, i.e., deferred by a constant multiplied by the number of pending requests. |
