/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

export const HISTORY = "history";
export const CONVERSATION = "conversation";
// Memories from a user's direct NL query to the assistant.
export const CONVERSATION_USER_REQUEST = "user_request";
// Storage tag for memories generated from unified cross-modal session bundles.
export const SESSION = "session";

/**
 * Memory types
 */
export const MEMORY_TYPE_PROFILE_FACT = "profile_fact";
export const MEMORY_TYPE_DURABLE_MEMORY = "durable_memory";
export const MEMORY_TYPE_LONG_TERM_MEMORY = "long_term_memory";
export const MEMORY_TYPE_SHORT_TERM_MEMORY = "short_term_memory";
export const MEMORY_TYPES = Object.freeze([
  MEMORY_TYPE_PROFILE_FACT,
  MEMORY_TYPE_DURABLE_MEMORY,
  MEMORY_TYPE_LONG_TERM_MEMORY,
  MEMORY_TYPE_SHORT_TERM_MEMORY,
]);

/**
 * Memory strength constants
 */
export const MEMORY_STRENGTH_EVIDENCE_WEIGHT = 0.3;
export const MEMORY_STRENGTH_EVIDENCE_CAP = 10;
export const MEMORY_STRENGTH_LIFETIME_ACCESSED_WEIGHT = 5;
export const MEMORY_STRENGTH_LIFETIME_ACCESSED_HALFLIFE = 21;
export const MEMORY_STRENGTH_MERGE_COUNT_WEIGHT = 4.5;
export const MEMORY_STRENGTH_MERGE_COUNT_HALFLIFE = 45;
export const MEMORY_STRENGTH_FLOOR = 4;
export const MEMORY_STRENGTH_USER_REQUEST_MODIFIER = 11;
export const MEMORY_STRENGTH_PRECISION = 100;

export const MEMORY_TYPE_SHORT_TERM_TO_LONG_TERM_AGE_THRESHOLD = 14;
export const MEMORY_TYPE_SHORT_TERM_TO_LONG_TERM_STRENGTH_THRESHOLD = 20;
export const MEMORY_TYPE_LONG_TERM_TO_DURABLE_AGE_THRESHOLD = 45;
export const MEMORY_TYPE_LONG_TERM_TO_DURABLE_STRENGTH_THRESHOLD = 40;

// Promotion ladder for memory types, ordered strongest-first
export const MEMORY_TYPE_TIERS = Object.freeze([
  Object.freeze({
    type: MEMORY_TYPE_DURABLE_MEMORY,
    minStrength: MEMORY_TYPE_LONG_TERM_TO_DURABLE_STRENGTH_THRESHOLD,
    minAgeDays: MEMORY_TYPE_LONG_TERM_TO_DURABLE_AGE_THRESHOLD,
  }),
  Object.freeze({
    type: MEMORY_TYPE_LONG_TERM_MEMORY,
    minStrength: MEMORY_TYPE_SHORT_TERM_TO_LONG_TERM_STRENGTH_THRESHOLD,
    minAgeDays: MEMORY_TYPE_SHORT_TERM_TO_LONG_TERM_AGE_THRESHOLD,
  }),
]);

// Memory types list, strongest first
export const MEMORY_TYPE_LADDER = Object.freeze([
  ...MEMORY_TYPE_TIERS.map(tier => tier.type),
  MEMORY_TYPE_SHORT_TERM_MEMORY,
]);

/**
 * Memory merge constants
 */
export const MEMORY_MERGE_MIN_MEMORY_COUNT = 5;

/**
 * Memory generation constants
 */
export const MAX_SESSIONS_FIRST_RUN = 50;
export const MAX_SESSIONS_DELTA_RUN = 30;

/**
 * Memories generation run telemetry
 */
export const GENERATION_RUN_OUTCOME = Object.freeze({
  SUCCESS: "success",
  NO_SESSIONS: "no_sessions",
  NO_CANDIDATES: "no_candidates",
  ALL_FILTERED: "all_filtered",
  ERROR: "error",
});
export const GENERATION_RUN_ERROR_STAGE = Object.freeze({
  SOURCES: "sources",
  GENERATION: "generation",
  FILTER: "filter",
  SAVE: "save",
  MERGE: "merge",
  OTHER: "other",
});
export const GENERATION_RUN_ERROR_TYPE = Object.freeze({
  RATE_LIMITED: "rate_limited",
  TRANSIENT: "transient",
  SERVER_REJECTED: "server_rejected",
  FIREFOX_INTERNAL: "firefox_internal",
});
export const GENERATION_RUN_MERGE_STATUS = Object.freeze({
  NOT_REACHED: "not_reached",
  NOT_RUN: "not_run",
  BELOW_THRESHOLD: "below_threshold",
  RAN: "ran",
});

/**
 * Schema for memories generation telemetry statistics sent through a Glean event.
 *
 * @typedef {object} MemoriesGenerationRunStats
 * @property {boolean} [generation_triggered] The scheduler decided to generate memories
 * @property {number} [memories_before] Stored memories when generation started
 * @property {boolean} [is_first_run] No earlier run had processed any activity
 * @property {number} [history_rows_pulled] History rows read
 * @property {number} [history_rows_kept] History rows left after filtering
 * @property {number} [chat_messages_pulled] Chat messages read
 * @property {number} [chat_messages_kept] Chat messages left after filtering
 * @property {number} [sessions_built] Sessions built from the kept rows and messages
 * @property {number} [sessions_skipped] Sessions dropped by the heuristic gate
 * @property {number} [sessions_processed] Sessions sent for generation after the gate and cap
 * @property {number} [sessions_processed_events] Count of visits, searches and chat messages across processed sessions
 * @property {number} [sessions_processed_events_min] Count of events in the smallest processed session
 * @property {number} [sessions_processed_events_max] Count of events in the largest processed session
 * @property {number} [generation_batches] Generation batches attempted
 * @property {number} [generation_batches_skipped] Batches skipped after a non-retryable error
 * @property {number} [generation_candidates] Candidate memories generated
 * @property {number} [filter_dropped] Candidates dropped by the quality and sensitivity filter
 * @property {number} [save_new] Candidates saved as new memories
 * @property {number} [save_updated] Candidates saved as updates to stored memories
 * @property {string} [merge_status] One of {@link GENERATION_RUN_MERGE_STATUS}
 * @property {number} [merge_groups_proposed] Merge groups the model proposed
 * @property {number} [merge_groups_saved] Valid merge groups saved as merged memories
 * @property {number} [merge_components_removed] Memories deleted because they were merged
 * @property {number} [memories_after] Stored memories when the run ended
 * @property {string} [outcome] One of {@link GENERATION_RUN_OUTCOME}
 * @property {string} [error_stage] One of {@link GENERATION_RUN_ERROR_STAGE}
 * @property {string} [error_type] One of {@link GENERATION_RUN_ERROR_TYPE}
 */

/**
 * Journeys generation run telemetry
 */
export const JOURNEYS_GENERATION_RUN_OUTCOME = Object.freeze({
  SUCCESS: GENERATION_RUN_OUTCOME.SUCCESS,
  NO_MEMORIES: "no_memories",
  NO_CANDIDATES: GENERATION_RUN_OUTCOME.NO_CANDIDATES,
  ERROR: GENERATION_RUN_OUTCOME.ERROR,
});

/**
 * Schema for journeys generation telemetry statistics sent through a Glean event.
 *
 * @typedef {object} JourneysGenerationRunStats
 * @property {number} [memories_eligible] Memories that can have a journey and have none yet, before the cap
 * @property {number} [journey_memory_candidates] Eligible memories sent for generation after the cap and URL resolution
 * @property {number} [generation_batches] Generation batches attempted
 * @property {number} [generation_batches_skipped] Batches skipped after a non-retryable error
 * @property {number} [journeys_generated] Valid journeys the model returned
 * @property {number} [save_new] Journeys saved
 * @property {number} [journeys_eligible_after] Stored journeys that can be suggested when the run ended
 * @property {string} [outcome] One of {@link JOURNEYS_GENERATION_RUN_OUTCOME}
 * @property {string} [error_stage] One of {@link GENERATION_RUN_ERROR_STAGE}
 * @property {string} [error_type] One of {@link GENERATION_RUN_ERROR_TYPE}
 */

/**
 * Memories maintenance run telemetry
 */
export const MAINTENANCE_RUN_OUTCOME = Object.freeze({
  SUCCESS: GENERATION_RUN_OUTCOME.SUCCESS,
  ERROR: GENERATION_RUN_OUTCOME.ERROR,
});

/**
 * Schema for memories maintenance telemetry statistics sent through a Glean event.
 *
 * @typedef {object} MemoriesMaintenanceRunStats
 * @property {number} [memories_before] Stored memories when maintenance started, soft-deleted included
 * @property {number} [soft_deleted_removed] Soft-deleted memories hard-deleted
 * @property {number} [memories_skipped] Live memories not aged because they were aged less than a day ago
 * @property {number} [memories_aged] Live memories aged, including those then deleted by decay
 * @property {number} [promoted_to_long_term] Aged memories changed from short term to long term
 * @property {number} [promoted_to_durable] Aged memories changed from short or long term to durable
 * @property {number} [demoted_to_long_term] Aged memories changed from durable to long term
 * @property {number} [demoted_to_short_term] Aged memories changed from durable or long term to short term
 * @property {number} [memories_decayed] Aged memories hard-deleted by decay
 * @property {number} [memories_after] Stored memories when the run ended, soft-deleted included
 * @property {string} [outcome] One of {@link MAINTENANCE_RUN_OUTCOME}
 * @property {string} [error_type] One of {@link GENERATION_RUN_ERROR_TYPE}
 */

/**
 * Schema for journeys maintenance telemetry statistics sent through a Glean event.
 *
 * @typedef {object} JourneysMaintenanceRunStats
 * @property {number} [journeys_before] Stored journeys when maintenance started, dismissed and snoozed included
 * @property {number} [journeys_removed] Journeys deleted because their memory is gone
 * @property {number} [journeys_after] Stored journeys when the run ended, dismissed and snoozed included
 * @property {number} [journeys_eligible_after] Stored journeys that can be suggested when the run ended
 * @property {string} [outcome] One of {@link MAINTENANCE_RUN_OUTCOME}
 * @property {string} [error_type] One of {@link GENERATION_RUN_ERROR_TYPE}
 */

/**
 * Memory frecency constants
 */
export const MEMORY_FRECENCY_MAX_DAYS = 7;
export const MEMORY_FRECENCY_DAY_HALFLIFE = 3;

/**
 * Memory sensitivity types
 */
export const MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE = "not_sensitive";
export const MEMORY_SENSITIVITY_CATEGORY_SENSITIVE = "sensitive";
export const MEMORY_SENSIVITITY_CATEGORIES = Object.freeze([
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
]);

export const MEMORY_DECAY_THRESHOLD = 0.05;

/**
 * Memory categories
 */
export const CATEGORIES = "categories";
export const CATEGORIES_LIST = [
  "Arts & Entertainment",
  "Autos & Vehicles",
  "Beauty & Fitness",
  "Books & Literature",
  "Business & Industrial",
  "Computers & Electronics",
  "Food & Drink",
  "Games",
  "Hobbies & Leisure",
  "Home & Garden",
  "Internet & Telecom",
  "Jobs & Education",
  "Law & Government",
  "News",
  "Online Communities",
  "People & Society",
  "Pets & Animals",
  "Real Estate",
  "Reference",
  "Science",
  "Shopping",
  "Sports",
  "Travel & Transportation",
];

/**
 * Memory intents
 */
export const INTENTS = "intents";
export const INTENTS_LIST = [
  "Research / Learn",
  "Compare / Evaluate",
  "Plan / Organize",
  "Buy / Acquire",
  "Create / Produce",
  "Communicate / Share",
  "Monitor / Track",
  "Entertain / Relax",
  "Resume / Revisit",
];

/**
 * Memory retrieval constants
 */
export const DEFAULT_RELEVANT_MEMORIES_TOP_K = 5;
export const DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD = 0.04;
export const DEFAULT_RELEVANT_MEMORIES_MESSAGE_COUNT = 3;

// if generate memories is enabled. This is used by
// - MemoriesScheduler
export const PREF_GENERATE_MEMORIES_FROM_HISTORY =
  "browser.smartwindow.memories.generateFromHistory";
export const PREF_GENERATE_MEMORIES_FROM_CONVERSATION =
  "browser.smartwindow.memories.generateFromConversation";

// Important! Changing or removing this value requires a security review.
//
// Memories are generated from conversations that may contain private and untrusted content.
// However, memories are easily applied to conversations, and if they are marked with untrusted
// content flags then some tool calls may no longer be allowed for that conversation. In order to
// relax this restriction, we enforce a limit for each memory to be no larger than 100 characters.
// This limits the potential for prompt injection. The memories are already told to be short by the
// model requiring 4-10 words, but this programmatic check ensures that memories adhere to these
// requirements.
export const MAX_MEMORY_SUMMARY_LENGTH = 100;

/**
 * Session gate heuristics
 *
 * Used by MemoriesSessionGate.runHeuristicGate to decide whether a session
 * is worth running through the LLM memory pipeline. The goal is to filter
 * structurally-trivial sessions (auth pages, redirects, greeting-only chats)
 * before paying LLM cost. Sensitive content is already filtered upstream by
 * SensitiveInfoDetector at row level — do not duplicate that here.
 */

// Heuristic gate decisions.
export const GATE_KEEP = "KEEP";
export const GATE_SKIP = "SKIP";

// Page titles too generic to be informative on their own.
export const GENERIC_TITLES = new Set([
  "Google Docs",
  "Google Sheets",
  "Google Slides",
  "Google Drive",
  "Home",
  "New Tab",
  "Access Denied",
  "Untitled",
  "Gmail",
  "Sign in",
  "Loading...",
  "Google",
  "YouTube",
]);

// Title patterns that indicate an auth or intermediate-redirect step rather
// than real user-facing content. Catches new auth domains we haven't listed
// in SKIP_ONLY_DOMAINS.
export const NAV_TITLE_PATTERNS = [
  /^sign[- ]?in/i,
  /^log[- ]?in/i,
  /^continue (with|to)/i,
  /^authoriz/i,
  /^two[- ]?factor/i,
  /^device activation/i,
  /^verify your/i,
  /^connecting\.\.\./i,
  /^loading\.\.\./i,
];

// Hosts where a session of only-these-hosts and no extracted queries
// indicates pure navigation: auth flows, shorteners, click-trackers.
// Do NOT include search engines here — see SEARCH_ENGINE_DOMAINS.
export const SKIP_ONLY_DOMAINS = new Set([
  // Major auth / SSO endpoints
  "accounts.google.com",
  "login.microsoftonline.com",
  "login.live.com",
  "signin.aws.amazon.com",
  "appleid.apple.com",
  "accounts.spotify.com",
  "accounts.firefox.com",
  "auth0.com",
  "okta.com",
  "duosecurity.com",
  "idp.iam.mozilla.com",
  "auth.mozilla.auth0.com",
  // URL shorteners
  "t.co",
  "bit.ly",
  "tinyurl.com",
  "goo.gl",
  "ow.ly",
  "buff.ly",
  "rebrand.ly",
  "is.gd",
  "cutt.ly",
  "lnkd.in",
  // Click-tracker intermediaries
  "l.facebook.com",
  "lm.facebook.com",
  "out.reddit.com",
  "link.medium.com",
]);

// Search engine hosts. Sessions consisting only of these hosts pass the
// gate when search queries are present (user expressed intent) and fail
// when they are absent (pure SERP bounce).
export const SEARCH_ENGINE_DOMAINS = new Set([
  "google.com",
  "www.google.com",
  "bing.com",
  "www.bing.com",
  "duckduckgo.com",
  "www.duckduckgo.com",
  "search.yahoo.com",
  "www.ecosia.org",
  "startpage.com",
  "www.startpage.com",
  "search.brave.com",
  "perplexity.ai",
  "www.perplexity.ai",
]);

// Chat message bodies that carry no meaningful intent.
export const TRIVIAL_MESSAGES = new Set([
  "hi",
  "hello",
  "hey",
  "thanks",
  "thank you",
  "ok",
  "okay",
  "bye",
]);

// Minimum lengths below which a title / chat message is treated as having no
// extractable content.
export const MIN_TITLE_LENGTH = 10;
export const MIN_MESSAGE_LENGTH = 5;
