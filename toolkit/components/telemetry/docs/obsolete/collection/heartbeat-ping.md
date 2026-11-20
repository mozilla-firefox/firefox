# "heartbeat" ping (obsolete)

This ping is submitted after a Firefox Heartbeat survey. Even if the user exits
the browser, closes the survey window, or ignores the survey, Heartbeat will
provide a ping to Telemetry for sending during the same session.

The payload contains the user's survey response (if any) as well as timestamps
of various Heartbeat events (survey shown, survey closed, link clicked, etc).

The ping will also report the "surveyId", "surveyVersion" and "testing"
Heartbeat survey parameters (if they are present in the survey config).
These "meta fields" will be repeated verbatim in the payload section.

The environment block, client ID and profile group ID are submitted with this ping.

Structure:

```js
{
  type: "heartbeat",
  version: 4,
  clientId: <UUID>,
  profileGroupId: <UUID>,
  environment: { /* ... */ }
  // ... common ping data
  payload: {
    version: 1,
    flowId: <string>,
    ... timestamps below ...
    offeredTS: <integer epoch timestamp>,
    learnMoreTS: <integer epoch timestamp>,
    votedTS: <integer epoch timestamp>,
    engagedTS: <integer epoch timestamp>,
    closedTS: <integer epoch timestamp>,
    expiredTS: <integer epoch timestamp>,
    windowClosedTS: <integer epoch timestamp>,
    // ... user's rating below
    score: <integer>,
    // ... survey meta fields below
    surveyId: <string>,
    surveyVersion: <integer>,
    testing: <boolean>
  }
}
```

Notes:

- Pings will **NOT** have all possible timestamps, timestamps are only reported for events that actually occurred.
- Timestamp meanings:
  : - offeredTS: when the survey was shown to the user
  - learnMoreTS: when the user clicked on the "Learn More" link
  - votedTS: when the user voted
  - engagedTS: when the user clicked on the survey-provided button (alternative to voting feature)
  - closedTS: when the Heartbeat notification bar was closed
  - expiredTS: indicates that the survey expired after 2 hours of no interaction (threshold regulated by "browser.uitour.surveyDuration" pref)
  - windowClosedTS: the user closed the entire Firefox window containing the survey, thus ending the survey. This timestamp will also be reported when the survey is ended by the browser being shut down.
- The surveyId/surveyVersion fields identify a specific survey (like a "1040EZ" tax paper form). The flowID is a UUID that uniquely identifies a single user's interaction with the survey. Think of it as a session token.
- The self-support page cannot include additional data in this payload. Only the the 4 flowId/surveyId/surveyVersion/testing fields are under the self-support page's control.

See also: {doc}`common ping fields <../../data/common-ping>`
