/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  getPlacesSemanticHistoryManager:
    "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs",
  // Domain fallback / workaround for general-category queries (games, movies, etc.)
  SearchBrowsingHistoryDomainBoost:
    "moz-src:///browser/components/aiwindow/models/SearchBrowsingHistoryDomainBoost.sys.mjs",
});

/**
 * Convert ISO timestamp string to microseconds (moz_places format / PRTime).
 *
 * @param {string|null} iso
 * @returns {number|null}
 */
function isoToMicroseconds(iso) {
  if (!iso) {
    return null;
  }
  const ms = new Date(iso).getTime();
  return Number.isFinite(ms) ? ms * 1000 : null;
}

/**
 * A history row from the moz_places databases, normalized for usage.
 *
 * @typedef {object} HistoryRow
 * @property {string} title - Sanitized title (falls back to URL if missing).
 * @property {string} url - Page URL.
 * @property {string|null} visitDate - ISO timestamp of last visit, or null.
 * @property {number} visitCount - Number of visits (defaults to 0).
 * @property {number} relevanceScore - Ranking score (semantic relevance or frecency fallback).
 */

/**
 * Normalize a history row from either:
 * - semantic SQL result (mozIStorageRow), or
 * - Places history node (plain object from nsINavHistoryResultNode).
 *
 * @param {object} row
 * @param {boolean} [fromNode=false]  // true if row came from Places node
 * @returns {HistoryRow}              // normalized history entry
 */
function buildHistoryRow(row, fromNode = false) {
  let title, url, visitDateIso, visitCount, distance, frecency;

  if (!fromNode) {
    // from semantic / SQL result (mozIStorageRow)
    title = row.getResultByName("title");
    url = row.getResultByName("url");
    visitCount = row.getResultByName("visit_count");
    distance = row.getResultByName("distance");
    frecency = row.getResultByName("frecency");

    // convert last_visit_date to ISO format
    const lastVisitRaw = row.getResultByName("last_visit_date");
    // last_visit_date is in microseconds from moz_places
    if (typeof lastVisitRaw === "number") {
      visitDateIso = new Date(Math.round(lastVisitRaw / 1000)).toISOString();
    } else if (lastVisitRaw instanceof Date) {
      visitDateIso = lastVisitRaw.toISOString();
    } else {
      visitDateIso = null;
    }
  } else {
    // from basic / Places history node (nsINavHistoryResultNode)
    title = row.title;
    url = row.uri;
    visitCount = row.accessCount;
    frecency = row.frecency;

    // convert time to ISO format
    const lastVisitDate = lazy.PlacesUtils.toDate(row.time);
    visitDateIso = lastVisitDate ? lastVisitDate.toISOString() : null;
  }

  let relevanceScore;
  if (typeof distance === "number") {
    relevanceScore = 1 - distance;
  } else {
    relevanceScore = frecency;
  }

  return {
    title: sanitizeUntrustedContent(title || url),
    url,
    visitDate: visitDateIso, // ISO timestamp format
    visitCount: visitCount || 0,
    relevanceScore: relevanceScore || 0, // Use embedding's distance as relevance score when available
  };
}

/**
 * Hybrid merge of semantic + Places history search results using Reciprocal Rank Fusion (RRF).
 *
 * RRF combines multiple ranked result lists by assigning each entry a score
 * based on its rank in each list:
 *
 *   score += 1 / (k + rank)
 *
 * where `rank` is the 1-based position in the list and `k` is a constant that
 * dampens the impact of top-ranked results. Entries appearing in both lists
 * accumulate higher scores.
 *
 * This implementation:
 *   - Deduplicates results by URL.
 *   - Accumulates RRF scores across semantic and Places history rankings.
 *   - Merges missing metadata (title, visitDate, visitCount) from either source.
 *   - Sorts by fused RRF score, then by recency (visitDate), then by visitCount.
 *   - Returns at most `historyLimit` results with the fused score as relevanceScore.
 *
 * @param {HistoryRow[]} semanticRows - History entry results from semantic history search (ranked by distance).
 * @param {HistoryRow[]} keywordRows - History entry results from Places history search (ranked by frecency).
 * @param {number} historyLimit - Maximum number of history results to return.
 * @param {number} [k=60] - RRF constant controlling rank influence (larger values reduce top-rank dominance).
 * @returns {HistoryRow[]} - Fused, deduplicated, and ranked history results.
 */
function mergeHistoryResultsRRF(
  semanticRows,
  keywordRows,
  historyLimit,
  k = 60
) {
  const byUrl = new Map();

  for (let i = 0; i < semanticRows.length; i++) {
    const row = semanticRows[i];
    if (!byUrl.has(row.url)) {
      byUrl.set(row.url, { ...row, _rrf: 0 });
    }
    byUrl.get(row.url)._rrf += 1 / (k + i + 1);
  }

  for (let i = 0; i < keywordRows.length; i++) {
    const row = keywordRows[i];
    if (!byUrl.has(row.url)) {
      byUrl.set(row.url, { ...row, _rrf: 0 });
    }

    const entry = byUrl.get(row.url);
    entry._rrf += 1 / (k + i + 1);

    // Prefer Places metadata when available, since Places is the source of truth.
    if (row.title) {
      entry.title = row.title;
    }
    if (row.visitDate) {
      entry.visitDate = row.visitDate;
    }
    if (row.visitCount !== undefined && row.visitCount !== null) {
      entry.visitCount = row.visitCount;
    }
  }

  const entries = [...byUrl.values()];

  for (const entry of entries) {
    const ms = entry.visitDate ? new Date(entry.visitDate).getTime() : 0;
    entry._visitMs = Number.isFinite(ms) ? ms : 0;
  }

  // Sort by fused RRF score first, then break ties by newer visitDate,
  // then by higher visitCount.
  entries.sort((a, b) => {
    const rrfDiff = b._rrf - a._rrf;
    if (rrfDiff !== 0) {
      return rrfDiff;
    }

    if (b._visitMs !== a._visitMs) {
      return b._visitMs - a._visitMs;
    }

    return (b.visitCount || 0) - (a.visitCount || 0);
  });

  return entries.slice(0, historyLimit).map(({ _rrf, _visitMs, ...row }) => ({
    ...row,
    relevanceScore: _rrf, // Final fused score for hybrid results
  }));
}

/**
 * Hybrid browsing history search using semantic search and Places history search.
 *
 * This runs semantic search and Places history search independently, then
 * combines the two ranked result sets with Reciprocal Rank Fusion (RRF).
 *
 * If the fused results do not fill `historyLimit`, a domain-based fallback may
 * add more results for broad category queries (e.g. "games", "news") where
 * semantic embeddings over page titles are insufficient. This acts as a
 * temporary heuristic and only fills remaining slots without overriding the
 * fused ranking.
 *
 * @param {object} params
 * @param {string} params.searchTerm
 * @param {number|null} params.startTs
 * @param {number|null} params.endTs
 * @param {number} params.historyLimit
 * @param {number} params.distanceThreshold
 * @returns {Promise<HistoryRow[]>} - Fused, deduplicated, and ranked history results.
 */
async function searchBrowsingHistoryHybrid({
  searchTerm,
  startTs,
  endTs,
  historyLimit,
  distanceThreshold,
}) {
  // Fetch deeper from both sources, then fuse down to historyLimit.
  const hybridFetchLimit = Math.max(historyLimit * 3, 50);

  const [semanticRows, keywordRows] = await Promise.all([
    searchBrowsingHistorySemantic({
      searchTerm,
      startTs,
      endTs,
      historyLimit: hybridFetchLimit,
      distanceThreshold,
    }),
    searchBrowsingHistoryBasic({
      searchTerm,
      startTs,
      endTs,
      historyLimit: hybridFetchLimit,
    }),
  ]);

  let rows = mergeHistoryResultsRRF(semanticRows, keywordRows, historyLimit);

  // Domain fallback for general-category queries (games, movies, news, etc.)
  // Keep hybrid ranking primary, only top-up if we have room.
  if (rows.length < historyLimit) {
    const domains =
      lazy.SearchBrowsingHistoryDomainBoost.matchDomains(searchTerm);
    if (domains?.length) {
      const semanticManager = lazy.getPlacesSemanticHistoryManager();
      let conn = await semanticManager.getConnection();

      const domainRows =
        await lazy.SearchBrowsingHistoryDomainBoost.searchByDomains({
          conn,
          domains,
          startTs,
          endTs,
          historyLimit: Math.max(historyLimit * 2, 200), // extra for dedupe
          buildHistoryRow,
        });

      return lazy.SearchBrowsingHistoryDomainBoost.mergeDedupe(
        rows,
        domainRows,
        historyLimit
      );
    }
  }

  return rows;
}

/**
 * Plain time-range browsing history search without search term (no semantic search).
 *
 * @param {object} params
 * @param {number|null} params.startTs
 * @param {number|null} params.endTs
 * @param {number} params.historyLimit
 * @returns {Promise<HistoryRow[]>}
 */
async function searchBrowsingHistoryTimeRange({
  startTs,
  endTs,
  historyLimit,
}) {
  const results = [];
  await lazy.PlacesUtils.withConnectionWrapper(
    "SearchBrowsingHistory:searchBrowsingHistoryTimeRange",
    async db => {
      const stmt = await db.executeCached(
        `
          SELECT id,
                 title,
                 url,
                 NULL AS distance,
                 visit_count,
                 frecency,
                 last_visit_date
          FROM moz_places
          WHERE frecency <> 0
          AND (:startTs IS NULL OR last_visit_date >= :startTs)
          AND (:endTs IS NULL OR last_visit_date <= :endTs)
          ORDER BY last_visit_date DESC, frecency DESC
          LIMIT :limit
        `,
        {
          startTs,
          endTs,
          limit: historyLimit,
        }
      );

      for (let row of stmt) {
        results.push(row);
      }
    }
  );

  const rows = [];
  for (let row of results) {
    rows.push(buildHistoryRow(row));
  }
  return rows;
}

/**
 * Normalize tensor/output format from the embedder into a single vector.
 *
 * @param {Array|object} tensor
 * @returns {Array|Float32Array}
 */
function extractVectorFromTensor(tensor) {
  if (!tensor) {
    throw new Error("Unexpected empty tensor");
  }

  // Case 1: { output: ... } or { metrics, output }
  if (tensor.output) {
    if (
      Array.isArray(tensor.output) &&
      (Array.isArray(tensor.output[0]) || ArrayBuffer.isView(tensor.output[0]))
    ) {
      // output is an array of vectors, return the first
      return tensor.output[0];
    }
    // output is already a single vector
    return tensor.output;
  }

  // Case 2: tensor is nested like [[...]]
  if (
    Array.isArray(tensor) &&
    tensor.length === 1 &&
    Array.isArray(tensor[0])
  ) {
    tensor = tensor[0];
  }

  // Then we check if it's an array of arrays or just a single value.
  if (
    Array.isArray(tensor) &&
    (Array.isArray(tensor[0]) || ArrayBuffer.isView(tensor[0]))
  ) {
    return tensor[0];
  }

  return tensor;
}

/**
 * Semantic browsing history search using embeddings.
 *
 * This performs a two-stage retrieval for performance:
 * 1. Coarse search: over the quantized embeddings (`embedding_coarse`) to
 *    quickly select a dynamically sized candidate set (`coarseLimit`).
 *    This bounds the expensive cosine-distance computation.
 * 2. Refined search: computes the exact cosine distance for those candidates,
 *    applies the caller-provided `distanceThreshold`, and returns the best
 *    matches up to `historyLimit`.
 *
 * @param {object} params
 * @param {string} params.searchTerm
 * @param {number|null} params.startTs
 * @param {number|null} params.endTs
 * @param {number} params.historyLimit
 * @param {number} params.distanceThreshold
 * @returns {Promise<HistoryRow[]>} - Semantic history search results ranked by distance.
 */
async function searchBrowsingHistorySemantic({
  searchTerm,
  startTs,
  endTs,
  historyLimit,
  distanceThreshold,
}) {
  const semanticManager = lazy.getPlacesSemanticHistoryManager();
  await semanticManager.embedder.ensureEngine();

  // Embed search term
  let tensor = await semanticManager.embedder.embed(searchTerm);
  const vec = extractVectorFromTensor(tensor);
  const vector = lazy.PlacesUtils.tensorToSQLBindable(vec);

  // Coarse-stage candidate pool (dynamic)
  const coarseLimit = Math.max(historyLimit * 15, 200);

  let conn = await semanticManager.getConnection();
  const results = await conn.executeCached(
    `
    WITH coarse_matches AS (
      SELECT rowid,
             embedding
      FROM vec_history
      WHERE embedding_coarse match vec_quantize_binary(:vector)
      ORDER BY distance
      LIMIT :coarseLimit
    ),
    matches AS (
      SELECT url_hash, vec_distance_cosine(embedding, :vector) AS distance
      FROM vec_history_mapping
      JOIN coarse_matches USING (rowid)
      WHERE distance <= :distanceThreshold
      ORDER BY distance
      LIMIT :limit
    )
    SELECT id,
           title,
           url,
           distance,
           visit_count,
           frecency,
           last_visit_date
    FROM moz_places
    JOIN matches USING (url_hash)
    WHERE frecency <> 0
    AND (:startTs IS NULL OR last_visit_date >= :startTs)
    AND (:endTs IS NULL OR last_visit_date <= :endTs)
    ORDER BY distance
    `,
    {
      vector,
      distanceThreshold,
      limit: historyLimit,
      coarseLimit,
      startTs,
      endTs,
    }
  );

  const rows = [];
  for (let row of results) {
    rows.push(buildHistoryRow(row));
  }

  return rows;
}

/**
 * Browsing history search using the default Places history search.
 *
 * @param {object} params
 * @param {string} params.searchTerm
 * @param {number|null} params.startTs
 * @param {number|null} params.endTs
 * @param {number} params.historyLimit
 * @returns {Promise<HistoryRow[]>}
 */
async function searchBrowsingHistoryBasic({
  searchTerm,
  startTs = null,
  endTs = null,
  historyLimit,
}) {
  let root;
  let openedRoot = false;

  try {
    const currentHistory = lazy.PlacesUtils.history;
    const query = currentHistory.getNewQuery();
    const opts = currentHistory.getNewQueryOptions();

    // Use Places' built-in text filtering
    query.searchTerms = searchTerm;

    // Add time range filter
    if (startTs !== null) {
      query.beginTime = startTs;
      query.beginTimeReference = Ci.nsINavHistoryQuery.TIME_RELATIVE_EPOCH;
    }
    if (endTs !== null) {
      query.endTime = endTs;
      query.endTimeReference = Ci.nsINavHistoryQuery.TIME_RELATIVE_EPOCH;
    }

    // Simple URI results, ranked by frecency
    opts.resultType = Ci.nsINavHistoryQueryOptions.RESULTS_AS_URI;
    opts.sortingMode = Ci.nsINavHistoryQueryOptions.SORT_BY_FRECENCY_DESCENDING;
    opts.maxResults = historyLimit;
    opts.excludeQueries = false;
    opts.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY;

    const result = currentHistory.executeQuery(query, opts);
    root = result.root;

    if (!root.containerOpen) {
      root.containerOpen = true;
      openedRoot = true;
    }

    const rows = [];
    for (let i = 0; i < root.childCount && rows.length < historyLimit; i++) {
      const node = root.getChild(i);
      rows.push(buildHistoryRow(node, true));
    }
    return rows;
  } catch (error) {
    console.error("Error searching browser history:", error);
    return [];
  } finally {
    if (root && openedRoot) {
      root.containerOpen = false;
    }
  }
}

/**
 * @typedef {object} HistorySearchSummary
 * @property {string} searchTerm - The search term.
 * @property {number} count - The history count.
 * @property {HistoryRow[]} results - The history row results.
 * @property {string} [message] - A message if there are no results.
 * @property {string} [error] - An error message if there is an error.
 */

/**
 * Searches browser history using hybrid semantic search when possible,
 * otherwise Places history search or time-range filtering.
 *
 * Rules:
 *   - Empty searchTerm: time-range search (if start/end given) or recent history.
 *   - Non-empty searchTerm: hybrid semantic + Places history search when available,
 *     otherwise Places history search with time filtering.
 *
 * @param {object} params
 *  The search parameters.
 * @param {string} params.searchTerm
 *  The search string. If null or empty, text search is skipped and results are
 *  filtered by time range and sorted by last_visit_date and frecency.
 * @param {string|null} params.startTs
 *  Optional local ISO-8601 start timestamp (e.g. "2025-11-07T09:00:00").
 * @param {string|null} params.endTs
 *  Optional local ISO-8601 end timestamp (e.g. "2025-11-07T09:00:00").
 * @param {number} params.historyLimit
 *  Maximum number of history results to return.
 * @returns {Promise<HistorySearchSummary>}
 *  A promise resolving to an object with the search term and history results.
 *  Includes `count` when matches exist, a `message` when none are found, or an
 *  `error` string on failure.
 */
export async function searchBrowsingHistory({
  searchTerm = "",
  startTs = null,
  endTs = null,
  historyLimit = 15,
}) {
  /** @type {HistoryRow[]} */
  let rows = [];

  try {
    // Convert ISO timestamp strings to microseconds to match the format used in moz_places / PRTime
    const startUs = isoToMicroseconds(startTs);
    const endUs = isoToMicroseconds(endTs);

    const distanceThreshold = Services.prefs.getFloatPref(
      "places.semanticHistory.distanceThreshold",
      0.6
    );

    const semanticManager = lazy.getPlacesSemanticHistoryManager();

    // If semantic search cannot be used or we don't have enough entries, always
    // fall back to plain time-range search.
    const canUseSemantic =
      semanticManager.isEnabledForSmartWindow &&
      (await semanticManager.hasSufficientEntriesForSearching());

    if (!searchTerm?.trim()) {
      // Plain time-range search (no searchTerm)
      rows = await searchBrowsingHistoryTimeRange({
        startTs: startUs,
        endTs: endUs,
        historyLimit,
      });
    } else if (canUseSemantic) {
      // Hybrid search: semantic + Places history search
      rows = await searchBrowsingHistoryHybrid({
        searchTerm,
        startTs: startUs,
        endTs: endUs,
        historyLimit,
        distanceThreshold,
      });
    } else {
      // Fallback to Places history search with time window if semantic search is not enabled or insufficient records.
      rows = await searchBrowsingHistoryBasic({
        searchTerm,
        startTs: startUs,
        endTs: endUs,
        historyLimit,
      });
    }

    if (rows.length === 0) {
      return {
        searchTerm,
        count: 0,
        results: [],
        message: searchTerm
          ? `No browser history found for "${searchTerm}".`
          : "No browser history found in the requested time range.",
      };
    }

    // Return as JSON string with metadata
    return {
      searchTerm,
      count: rows.length,
      results: rows,
    };
  } catch (error) {
    console.error("Error searching browser history:", error);
    return {
      searchTerm,
      count: 0,
      results: [],
      error: `Error searching browser history: ${error.message}`,
    };
  }
}
