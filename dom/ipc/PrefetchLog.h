/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PrefetchLog_h
#define mozilla_dom_PrefetchLog_h

#include "mozilla/Logging.h"

// Log module for Speculation Rules prefetch (DOM + Necko).
//
// To enable:
//
//   MOZ_LOG=SpeculationRules:5
//   MOZ_LOG_FILE=speculation-rules.log
//
// Level 5 (Verbose) prints lifecycle + algorithm decisions; lower levels
// scope to warnings/errors. See Mozilla Logging.h for level semantics.
//
// Spec: https://wicg.github.io/nav-speculation/prefetch.html

namespace mozilla::dom {

extern LazyLogModule gSpeculationRulesLog;

}  // namespace mozilla::dom

// Convenience macros mirroring the netwerk/protocol/http style:
//   LOG_SPECRULES(("foo %s", bar));        // Debug level
//   LOG_SPECRULES_V(("verbose %s", bar));  // Verbose level
#define LOG_SPECRULES(args) \
  MOZ_LOG(mozilla::dom::gSpeculationRulesLog, mozilla::LogLevel::Debug, args)
#define LOG_SPECRULES_FMT(...)                                              \
  MOZ_LOG_FMT(mozilla::dom::gSpeculationRulesLog, mozilla::LogLevel::Debug, \
              __VA_ARGS__)
#define LOG_SPECRULES_V(args) \
  MOZ_LOG(mozilla::dom::gSpeculationRulesLog, mozilla::LogLevel::Verbose, args)
#define LOG_SPECRULES_WARN(args) \
  MOZ_LOG(mozilla::dom::gSpeculationRulesLog, mozilla::LogLevel::Warning, args)
#define LOG_SPECRULES_ERROR(args) \
  MOZ_LOG(mozilla::dom::gSpeculationRulesLog, mozilla::LogLevel::Error, args)

#define LOG_SPECRULES_ENABLED() \
  MOZ_LOG_TEST(mozilla::dom::gSpeculationRulesLog, mozilla::LogLevel::Debug)

#endif  // mozilla_dom_PrefetchLog_h
