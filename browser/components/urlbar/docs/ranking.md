# Ranking

Before results appear in the UrlbarView, they are fetched from providers.

Each [UrlbarProvider](/browser/urlbar/overview.md#urlbarprovider)
implements its own internal ranking and returns sorted results.

Externally all the results are ranked by the {searchfox}`UrlbarMuxer <browser/components/urlbar/UrlbarMuxerStandard.sys.mjs>`
according to an hardcoded list of groups and sub-groups.

:::{NOTE}
Preferences can influence the groups order, for example by putting
Firefox Suggest before Search Suggestions.
:::

The Places provider returns history and bookmark results using an internal
ranking algorithm called **Frecency**.

## Frecency implementation

Frecency is a term derived from `frequency` and `recency`, its scope is to
provide a ranking algorithm that gives importance both to how often a page is
accessed and when it was last visited. Additionally, it accounts for the type of
each visit through a bonus system.

Frecency uses a double exponential decay.

Given a url, we identify a reference time, t{sub}`ref`, that will be either
the last visit date, or the last bookmark addition date (for pages not having
visits).

All the dates we use will be expressed as the number of days from the epoch.

Since calculating a score from all of the visits every time a new visit is added
would be expensive, a sample of the last
`places.frecency.pages.numSampledVisits` visits is used.

For each visit a *weight* is determined out of four buckets:

1. Very High (`places.frecency.pages.veryHighWeight`): This is used when a
   page is in the high bucket and has interesting interaction data.
2. High (`places.frecency.pages.highWeight`): This is for bookmarked or typed
   pages. Due to the current definition of typed, this includes URLs clicked on
   the Firefox UI (not the web), except searches.
3. Medium (`places.frecency.pages.mediumWeight`): This is for normal browsing,
   like clicking links on pages or downloads.
4. Low (`places.frecency.pages.lowWeight`): This is for non-typed redirect
   sources, visits across HTML frames (non top level) or reloads.

The score for the single visit is then calculated as:

$$
score_{visit} = weight_{visit} * e^{-\lambda(t_{ref}-t_{visit})}
$$

With half-life defined as:

$$
\lambda = \frac{\ln 2}{30}
$$

The total score is the average score, multiplied by the total number of visits:

$$
score_{total} = \frac{\sum score_{visit}}{samplecount} * visitcount
$$

To keep into account recent, this score is decayed daily using the already
defined half-life. To avoid having to multiply each score daily by an
exponential, we instead store in the database (`moz_places`) the number of days
from epoch after which the calculated score would become 1, by decaying it every
day.

$$
frecency = t_{ref} + \frac{\ln(score)}{\lambda}
$$

### Interactions

Page view duration, keypress count, and scroll distance are tracked when a page
loads. All data is stored locally and never transmitted.

A page visit is classified as **interesting** if either criterion is met:

- Page view time ≥ `places.frecency.pages.interactions.viewTimeSeconds`.
- Page view time ≥ `places.frecency.pages.interactions.viewTimeIfManyKeypressesSeconds`
  AND keypress count ≥ `places.frecency.pages.interactions.manyKeypresses`.

When a visit is classified as interesting, its scoring bucket is promoted. For
example, a visit initially categorized in the medium bucket is promoted to the
high bucket if it has an interesting interaction.

Interactions are stored separately from visits. To associate them, the query
checks whether a visit was recorded within `places.frecency.pages.interactions.maxVisitGapSeconds`
of a recorded interaction. If a matching visit exists within this time window,
the interaction is paired with it. Otherwise, the interaction becomes a
**virtual visit**.

A single visit can generate multiple interactions. When a user switches away
from a tab, the current interaction ends if the user does not return within
`browser.places.interactions.breakupIfNoUpdatesForSeconds`. Returning to the
tab after this threshold creates a new interaction, even though the original
visit remains the same.

### How frecency for a page is calculated

```{mermaid}
:align: center
:caption: Frecency calculation flow

flowchart TD
    start([Page URL Input])
    subgraph gather ["Gather"]
        visits[Gather recent visits<br/>up to sample limit]
        interactions[Gather interesting<br/>interactions]
        pair[Match interactions<br/>with visits by timestamp]
        virtual{Unpaired<br/>interaction?}
        generate[Create virtual visit<br/>from interaction]
        combine[Combine visits<br/>with virtual visits]
        hasVisits{Any visits<br/>collected?}
        isBookmarked{Is URL<br/>bookmarked?}
        bookmarkFallback[High<br/>Bucket]
        noData[frecency = 0]
    end
    subgraph classify ["Classify"]
        visitType{Visit<br/>Type?}
        bookmark_typed[Bookmark/Typed]
        regular_click[Regular Link]
        redirect_auto[Redirect/Sponsored]
        bookmarkInteraction{Interesting<br/>interaction?}
        regularInteraction{Interesting<br/>interaction?}
        score1[Very High<br/>Bucket]
        score2[High<br/>Bucket]
        score3[Medium<br/>Bucket]
        score4[Low<br/>Bucket]
    end
    calcDecay[Apply exponential decay<br/>based on visit age]
    aggregate[Sum decayed scores and<br/> normalize by visit count]
    project[frecency = days until<br/>score decays to 1]
    %% Main
    start --> visits & interactions
    visits --> pair
    interactions --> pair
    pair --> virtual
    virtual -->|Yes| generate
    virtual -->|No| combine
    generate --> combine
    combine --> hasVisits
    hasVisits -->|Yes| visitType
    hasVisits -->|No| isBookmarked
    isBookmarked -->|Yes| bookmarkFallback
    isBookmarked -->|No| noData
    bookmarkFallback --> calcDecay
    %% Classification
    visitType -->|Bookmark/Typed| bookmark_typed
    visitType -->|Regular| regular_click
    visitType -->|Redirect| redirect_auto
    bookmark_typed --> bookmarkInteraction
    regular_click --> regularInteraction
    redirect_auto --> score4
    bookmarkInteraction -->|Yes| score1
    bookmarkInteraction -->|No| score2
    regularInteraction -->|Yes| score2
    regularInteraction -->|No| score3
    %% Decay
    score1 & score2 & score3 & score4 --> calcDecay
    calcDecay --> aggregate
    %% Final
    aggregate --> project
    %% Styling
    style score1 fill:#2e7d32,stroke:#1b5e20,stroke-width:3px,color:#fff
    style score2 fill:#8bc34a,stroke:#558b2f,stroke-width:2px,color:#1a1a1a
    style score3 fill:#ffc107,stroke:#f57f17,stroke-width:2px,color:#1a1a1a
    style bookmarkFallback fill:#8bc34a,stroke:#558b2f,stroke-width:2px,color:#1a1a1a
    style score4 fill:#ff9800,stroke:#e65100,stroke-width:2px,color:#1a1a1a
    style project fill:#1976d2,stroke:#0d47a1,stroke-width:3px,color:#fff
    style noData fill:#1976d2,stroke:#0d47a1,stroke-width:3px,color:#fff
```

1. **Gather visits and interactions:** For a given URL, collect recent visits
   (up to the sample limit) and any interesting interactions.
2. **Match and create virtual visits:** Pair each interesting interaction with
   its corresponding visit by timestamp. If an interaction has no paired visit,
   create a "virtual visit" from that interaction. Combine all actual visits
   with these virtual visits for scoring.
3. **Check visits exist:** If no virtual or non-virtual visits were collected,
   check whether the URL is bookmarked. If bookmarked, assign it to the High
   bucket, otherwise set frecency to 0.
4. **Classify visits into buckets:** Categorize each visit based on its type and
   whether it has an interesting interaction.
5. **Apply exponential decay:** Reduce each visit's score based on its age
   using the time difference between the most recent visit in the sample vs.
   the visit timestamp.
6. **Aggregate scores:** Sum all decayed scores and normalize by dividing by the
   total visit count to get an average decayed score.
7. **Project to frecency value:** Convert the aggregated score into a timestamp
   representing the number of days from the Unix epoch until the score would
   decay to 1. This final value is the store frecency value.

### When frecency for a page is calculated

Operations that may influence the frecency score are:

- Adding visits
- Removing visits
- Adding bookmarks
- Removing bookmarks
- Changing the url of a bookmark
- An interesting interaction is recorded

Frecency is recalculated:

- Immediately, when a new visit is added. The user expectation here is that the
  page appears in search results after being visited. This is also valid for
  any History API that allows to add visits.
- In background on idle times, in any other case. In most cases having a
  temporarily stale value is not a problem, the main concern would be privacy
  when removing history of a page, but removing whole history will either
  completely remove the page or, if it's bookmarked, it will still be relevant.
  In this case, when a change influencing frecency happens, the `recalc_frecency`
  database field for the page is set to `1`.

Recalculation is done by the {searchfox}`PlacesFrecencyRecalculator <toolkit/components/places/PlacesFrecencyRecalculator.sys.mjs>` module.
The Recalculator is notified when `PlacesUtils.history.shouldStartFrecencyRecalculation`
value changes from false to true, that means there's values to recalculate.
A DeferredTask is armed, that will look for a user idle opportunity
in the next 5 minutes, otherwise it will run when that time elapses.
Once all the outdated values have been recalculated
`PlacesUtils.history.shouldStartFrecencyRecalculation` is set back to false
until the next operation invalidating a frecency.
The recalculation task is also armed on the `idle-daily` notification.

When the task is executed, it recalculates frecency of a chunk of pages. If
there are more pages left to recalculate, the task is re-armed. After frecency
of a page is recalculated, its `recalc_frecency` field is set back to `0`.

## Adaptive Input History

Input History (also known as Adaptive History) is a feature that allows to
find urls that the user previously picked. To do so, it associates search strings
with picked urls.

Adaptive results are usually presented before frecency derived results, making
them appear as having an infinite frecency.

When the user types a given string, and picks a result from the address bar, that
relation is stored and increases a use_count field for the given string.
The use_count field asymptotically approaches a max of `10` (the update is
done as `use_count * .9 + 1`).

On querying, all the search strings that start with the input string are matched,
a rank is calculated per each page as `ROUND(MAX(use_count) * (1 + (input = :search_string)), 1)`,
so that results perfectly matching the search string appear at the top.
Results with the same rank are additionally sorted by descending frecency.

On daily idles, when frecency is decayed, also input history gets decayed, in
particular the use_count field is multiplied by a decay rate of `0.975`.
After decaying, any entry that has a `use_count < 0.975^90 (= 0.1)` is removed,
thus entries are removed if unused for 90 days.

## Why legacy frecency scoring was replaced

{doc}`ranking-legacy` had the following issues:

1. The score depended on the time at which it is calculated, making the user
   experience more unstable. For example removing a bookmark (affects the score
   negatively) will recalculate the score to a value higher than another
   untouched bookmarked url, just because it was recalculated later.
2. All the scores must be decayed every day to keep recency into account. It is
   an expensive operation.
3. It has far too many coefficients, making it impossible to study for
   optimizing those coefficients.
4. Changing the coefficients doesn't recalculate all the scores, making
   experimentation more complicated.

```{toctree}
:hidden:
:maxdepth: 1

ranking-legacy
```
