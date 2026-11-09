# Bounce Tracking Protection

Bounce Tracking Protection (BTP) is an anti-tracking feature in Gecko which
detects bounce trackers (navigational tracking) based on a set of heuristics. As
opposed to the [cookie purging](../cookie-purging/index.md) feature it does not
rely on a list of trackers which makes it more webcompat friendly while also
covering unknown bounce trackers.


## Standardization

The protection is a work item of the PrivacyCG. The implementation in Gecko
closely follows the
[Bounce Tracking Mitigations spec draft](https://privacycg.github.io/nav-tracking-mitigations/#bounce-tracking-mitigations).

Mozilla also has a [specification position on Bounce Tracking Mitigations](https://mozilla.github.io/standards-positions/#bounce-tracking-mitigations).


## Behavior

BTP detects bounce trackers by looking at navigation timing. It establishes the
concept of an extended navigation which can encompass a chain of short-lived
redirects. These short-lived redirects are commonly used by bounce trackers. Any
site that appears as an intermediary in such a short-lived redirect gets added
to a classification list. Classified bounce trackers have their cookies, site
data and cache purged periodically. In order to avoid false positives and
purging data that may be important for users, sites which the user directly
interacted with in the last 45 days are exempt from being classified or purged.

See
[Bounce Tracking Mitigations Explainer](https://github.com/privacycg/nav-tracking-mitigations/blob/main/bounce-tracking-explainer.md)
for a more detailed (albeit chromium-oriented) description of the feature and how trackers are classified.

<!---
    TODO: This section should be extended to describe the behavior and rely less on the chromium explainer.
    It should also talk about potential deviations from the spec.
-->

### Deviations from the spec

#### Initial host of an extended navigation

The spec's *process navigation start* algorithm derives the extended
navigation's initial host from the navigation's source document. Because the
initial host is exempt from classification, that lets a cross-site iframe which
navigates the top level nominate itself as the initial host and escape being
classified for its own bounce
([Bug 2060310](https://bugzilla.mozilla.org/show_bug.cgi?id=2060310)).

Gecko instead uses the site the extended navigation is leaving: the site of the most
recent non-initial document committed in the context being navigated. A context which
has never committed a document of its own is one the navigation opened
(`window.open`, `target="_blank"`), so the opener's top level document is used
instead. Both signals are read at commit time, so a navigation which starts before
the current document has finished loading still attributes that document.

Reading the opener rather than the navigation's initiator is load bearing. A context
opened by a cross-site frame holds an initial `about:blank` whose principal is
inherited from that frame, and the initiator can resolve back into the opened context
itself, so either would let the frame name itself again through an empty popup.

Because the derivation does not depend on the navigation having an initiator at all,
a navigation started by the browser rather than by content — address bar, bookmark,
session restore — also exempts the site it is leaving. This matches how the spec
attributes user activation, which already uses the top level traversable's active
document.

#### Bounce candidate of a navigation without user activation

For a navigation without transient activation, *process navigation start* adds
the host of the navigation's initiator origin to the bounce set. For an ordinary
client redirect the initiator is the document being navigated away from, but it
can also sit outside the context being navigated: a script in another tab, or a
cross-site frame allowed to navigate the top level without a gesture (e.g.
`sandbox="allow-top-navigation"`). Such an initiator was credited with a bounce
it never performed and could be classified and purged for it
([Bug 2061167](https://bugzilla.mozilla.org/show_bug.cgi?id=2061167)).

Gecko instead adds the site of the document currently committed in the context
being navigated, the same site the initial host derivation above starts from. It
deliberately does not fall back to the opener for a context which has nothing
committed yet, since that would name the initiating tab again; such a navigation
adds no candidate. A frame initiated top level navigation is therefore credited
to the page hosting the frame, which is the site actually being left.

#### Extended navigations across containers

The spec keeps one bounce tracking record per top level traversable, and state is
keyed by the traversable's storage partition. With per-site container association
(`privacy.containers.switchDuringNavigation.enabled`), a top level navigation, or
one of its redirects, to a site bound to a container switches the load into that
container and retargets it into a new tab. Later hops which are bound to nothing
stay in that container. A single extended navigation can therefore run in two
tabs and two containers, which the spec has no notion of.

Gecko handles this as follows:

- The record follows the load. When the retargeted load is about to commit in
  the new tab, `BounceTrackingState::OnLoadRetargeted` moves the record from
  the tab the load started in to the new tab, and restarts its pending client
  bounce timer there. The destination becomes the final host as usual. The tab
  left behind holds no record, so closing it, as happens for a blank tab which
  only existed for the load, classifies nothing. The move is skipped if that tab
  has started another navigation meanwhile.
- Every bounce is recorded with the `OriginAttributes` it ran in: a server side
  hop with those of its redirect, a client side hop with those of the tab being
  left. It is classified, exempted by user activation and purged in that
  container only.
- The initial and final host are exempt only in the container their document
  ran in. Having been on a site in one container does not exempt that site's
  bounces in another, which would otherwise let a tracker collect state in a
  container the user never visited it in.
- User activation recorded during the navigation does not move with the record,
  so it cannot feed the storage access heuristic of another container.

#### Comparison with Chromium

Chromium's implementation starts a redirect chain the same way. In
[`btm_bounce_detector.cc`](https://source.chromium.org/chromium/chromium/src/+/main:content/browser/btm/btm_bounce_detector.cc),
`BtmBounceDetector::DidStartNavigation` takes the chain start from the tab's last
committed URL and only consults the navigation's initiator when the tab has nothing
committed. `BtmServiceImpl::HandleRedirects` in
[`btm_service_impl.cc`](https://source.chromium.org/chromium/chromium/src/+/main:content/browser/btm/btm_service_impl.cc)
then skips a redirector whose site equals the chain's initial or final site, which is
what `RecordStatefulBounces` does with the initial and final host. A client redirect
is likewise recorded with the tab's last committed URL as the redirector, never the
navigation's initiator, and only once the tab has committed something.

Gecko is stricter in one case. Chromium's fallback is the initiator's origin as-is,
so for a context opened by a cross-site frame it resolves to the frame's own site and
the frame can still name itself. Gecko resolves the opener to its top level document
instead.

## Gecko Implementation

Work for the Gecko implementation in tracked under following meta-bug:
[Bug 1839915 - \[meta\] Bounce Tracking Protection](https://bugzilla.mozilla.org/show_bug.cgi?id=1839915).


A simplified UML diagram of the BTP implementation in Gecko. Note that some
classes and attributes have been omitted for readability. You can use the
diagram feature in Searchfox to view the full diagram
([example](https://searchfox.org/mozilla-central/query/default?q=class-diagram%3A%27mozilla%3A%3ABounceTrackingState%27+depth%3A2)).

```{mermaid}
classDiagram
    class BounceTrackingProtection {
        - mBounceTrackingPurgeTimer: nsITimer
        - mStorage: BounceTrackingProtectionStorage
    }

    note for BounceTrackingProtection "Singleton class to manage the feature."

    class BounceTrackingAllowList {
        <!-- [...] -->
    }

    class nsIBTPExceptionList {
        <!-- Bug 1930704: Empty classes lead to build errors. Adding a comment inside the class fixes the issue -->
        <!-- [...] -->
    }

    class BounceTrackingStateGlobal {
        - mUserActivation: nsTHashMap&lt;nsCStringHashKey, PRTime&gt;
        - mBounceTrackers: nsTHashMap&lt;nsCStringHashKey, PRTime&gt;
        - mOriginAttributes: OriginAttributes
    }

    note for BounceTrackingStateGlobal "Manages the global maps for bounce tracker candidates
                                        and user activation for a specific OriginAttributes dict."

    class BounceTrackingProtectionStorage {
        - mStateGlobal : nsTHashMap&lt;OriginAttributesHashKey, RefPtr&lt;BounceTrackingStateGlobal&gt;&gt;
        - mDatabaseFile : nsCOMPtr&lt;nsIFile&gt;
    }


    class BounceTrackingRecord {
        - mInitialHost: nsAutoCString
        - mInitialOriginAttributes: OriginAttributes
        - mBounceHosts: nsTHashMap&lt;OriginAttributesHashKey, nsTHashSet&lt;nsCStringHashKey&gt;&gt;
        - mFinalHost: nsAutoCString
        - mFinalOriginAttributes: OriginAttributes
    }

    note for BounceTrackingRecord "Encapsulates the per-tab navigation state
                                   during an extended navigation."

    class BounceTrackingState {
        - mBounceTrackingProtection: BounceTrackingProtection
        - mBounceTrackingRecord: BounceTrackingRecord
        - mClientBounceDetectionTimeout: nsReadOnlyTimer
        - mOriginAttributes: OriginAttributes
    }


    class BrowsingContextWebProgress {
        - mBounceTrackingState: BounceTrackingState
    }


    class DocumentLoadListener{
        <!-- [...] -->
    }

    note for BrowsingContextWebProgress "Every tab has a web progress
                                         and therefore a BounceTrackingState"

    BounceTrackingProtection *-- BounceTrackingProtectionStorage
    BounceTrackingState --o BounceTrackingProtection
    BounceTrackingState --> BounceTrackingProtection : RecordStatefulBounces() on extended nav end
    BounceTrackingState *-- BounceTrackingRecord
    BrowsingContextWebProgress *-- BounceTrackingState
    BounceTrackingStateGlobal --* BounceTrackingProtectionStorage
    BounceTrackingStateGlobal --> BounceTrackingProtectionStorage : Persists state changes in storage.
    BounceTrackingProtection --> BounceTrackingStateGlobal : Manages global state
    nsIBTPExceptionList --* BounceTrackingProtection
    nsIBTPExceptionList --> BounceTrackingProtection : Site host purge exceptions from RemoteSettings.
    BounceTrackingAllowList --* BounceTrackingProtection
    BounceTrackingAllowList --> BounceTrackingProtection :  Site host purge exceptions from PermissionManager.
    BrowsingContextWebProgress --> BounceTrackingState : Navigation signals
    DocumentLoadListener --> BounceTrackingState : Navigation signals, OnLoadRetargeted()
```

## Preferences

The feature can be enabled and it's behavior can be adjusted using the
`privacy.bounceTrackingProtection.*` prefs. See
[StaticPrefList.yaml](https://searchfox.org/mozilla-central/rev/ec342a3d481d9ac3324d1041e05eefa6b61392d2/modules/libpref/init/StaticPrefList.yaml#15125-15180)
for a list of prefs with descriptions.

The main feature pref is `privacy.bounceTrackingProtection.mode` where `0` is
fully disabled and `1` is fully enabled. See
[nsIBounceTrackingProtection.idl](https://searchfox.org/mozilla-central/rev/ec342a3d481d9ac3324d1041e05eefa6b61392d2/toolkit/components/antitracking/bouncetrackingprotection/nsIBounceTrackingProtection.idl#10-42)
for a full list of options.

# Nimbus Integration
A subset of the BTP prefs can be controlled via Nimbus. See definition here:
[FeatureManifest.yaml](https://searchfox.org/firefox-main/source/toolkit/components/nimbus/FeatureManifest.yaml#:~:text=bounceTrackingProtection).

## Logging

BTP has a logger which can be enabled by starting Firefox with the `MOZ_LOG`
environment variable. Use `MOZ_LOG=BounceTrackingProtection:5` for verbose
logging for every navigation and `MOZ_LOG=BounceTrackingProtection:3` for more
concise logging focused on classification and purging.

### Console Messages

You can check the developer tools console for warning messages which will
be logged when a site gets classified. Example:

> “bounce-tracking-demo-tracker-server.glitch.me” has been classified as a
> bounce tracker. If it does not receive user activation within the next 3,600
> seconds it will have its state purged.


When a site has recently been purged (since last restart), upon next visit,
Firefox will also log a warning to the website console:
> The state of “bounce-tracking-demo-tracker-server.glitch.me” was recently purged because it was detected as a bounce tracker.

## Testing
When testing sites to ensure they don't get purged for bounce tracking behavior
you can use both logging (as described above) to observe classification and
direct calls to the feature via the Browser Toolbox to trigger the purging
early.

The snippets in the following section need to be executed in the [Browser
Toolbox]. Note that while the toolbox looks like the regular devtools it's a
special console used to debug Firefox itself rather than websites.

[Browser Toolbox]: /devtools-user/browser_toolbox/index.md

### Print the list of classified bounce trackers

To get a list of all currently classified bounce trackers use the following snippet:
```javascript
await Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection).testGetUserActivationHosts({})
```
This prints the list excluding private browsing and tab containers. For those
you need to pass in an `OriginAttributes` object. See
[nsIBounceTrackingProtection](https://searchfox.org/mozilla-central/rev/ec342a3d481d9ac3324d1041e05eefa6b61392d2/toolkit/components/antitracking/bouncetrackingprotection/nsIBounceTrackingProtection.idl#90)
for more documentation.

Sites which have been purged or which receive user interaction are automatically
removed from this list.

Note that just because a site is in this list it doesn't mean that it gets
classified. Once classified there is a grace period of 1h in which the site may
receive user interaction. If the user interacts with the site in that time
window it is removed from the bounce tracker list and exempt from purging for 45
days.

### Trigger a purge of all classified trackers:
Before navigating to the site set
`privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec` to `0` or a low
number. This controls how fast after (classified) bounce a site may be purged.
If you don't update this pref you need to wait up to 1h for a site to be purged.

Purges normally run every hour. To trigger a purge manually you can use the
following snippet:
```javascript
await Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection).testRunPurgeBounceTrackers();
```
The return value will be an array of sites that have been purged. Note that
purging applies for the entire domain (eTLD+1).

### List recently purged sites
You can obtain a list of recently purged sites (since the last restart) by
calling:
```javascript
await Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection).testGetRecentlyPurgedTrackers({});
```
This only shows sites which have been purged in normal browsing. If you want
data from private browsing or containers you need to pass in a non-default
`OriginAttributes` object, e.g. `{ privateBrowsingId: 1 }`.

There is also `hasRecentlyPurgedSite` which can be used to check if a specific
site has been recently purged (across all OriginAttributes contexts).

## Test Page
https://bounce-tracking-demo.glitch.me/ is a demo page with two links that
exhibit bounce tracking behaviour. You can use it combined with the methods
above to verify that the mechanism is running.
