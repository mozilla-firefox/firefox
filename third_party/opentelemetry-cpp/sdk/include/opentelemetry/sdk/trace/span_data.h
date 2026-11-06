// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/span_limits.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
/**
 * Class for storing events in SpanData.
 */
class SpanDataEvent
{
public:
  SpanDataEvent(
      std::string name,
      opentelemetry::common::SystemTimestamp timestamp,
      const opentelemetry::common::KeyValueIterable &attributes,
      std::uint32_t attribute_count_limit      = (std::numeric_limits<std::uint32_t>::max)(),
      std::size_t attribute_value_length_limit = (std::numeric_limits<std::size_t>::max)());

  /**
   * Get the name for this event
   * @return the name for this event
   */
  std::string GetName() const noexcept { return name_; }

  /**
   * Get the timestamp for this event
   * @return the timestamp for this event
   */
  opentelemetry::common::SystemTimestamp GetTimestamp() const noexcept { return timestamp_; }

  /**
   * Get the attributes for this event
   * @return the attributes for this event
   */
  const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &
  GetAttributes() const noexcept;

  /**
   * Get the number of attributes dropped due to the per-event attribute count limit.
   * @return the number of attributes dropped
   */
  std::uint32_t GetDroppedAttributesCount() const noexcept { return dropped_attributes_count_; }

private:
  std::string name_;
  opentelemetry::common::SystemTimestamp timestamp_;
  opentelemetry::sdk::common::AttributeMap attribute_map_;
  std::uint32_t dropped_attributes_count_{0};
};

/**
 * Class for storing links in SpanData.
 */
class SpanDataLink
{
public:
  SpanDataLink(
      opentelemetry::trace::SpanContext span_context,
      const opentelemetry::common::KeyValueIterable &attributes,
      std::uint32_t attribute_count_limit      = (std::numeric_limits<std::uint32_t>::max)(),
      std::size_t attribute_value_length_limit = (std::numeric_limits<std::size_t>::max)());

  /**
   * Get the attributes for this link
   * @return the attributes for this link
   */
  const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &
  GetAttributes() const noexcept;

  /**
   * Get the span context for this link
   * @return the span context for this link
   */
  const opentelemetry::trace::SpanContext &GetSpanContext() const noexcept { return span_context_; }

  /**
   * Get the number of attributes dropped due to the per-link
   * attribute count limit.
   * @return the number of attributes dropped
   */
  std::uint32_t GetDroppedAttributesCount() const noexcept { return dropped_attributes_count_; }

private:
  opentelemetry::trace::SpanContext span_context_;
  opentelemetry::sdk::common::AttributeMap attribute_map_;
  std::uint32_t dropped_attributes_count_{0};
};

/**
 * SpanData is a representation of all data collected by a span.
 */
class SpanData final : public Recordable
{
public:
  SpanData() = default;
  /**
   * Get the trace id for this span
   * @return the trace id for this span
   */
  opentelemetry::trace::TraceId GetTraceId() const noexcept { return span_context_.trace_id(); }

  /**
   * Get the span id for this span
   * @return the span id for this span
   */
  opentelemetry::trace::SpanId GetSpanId() const noexcept { return span_context_.span_id(); }

  /**
   * Get the span context for this span
   * @return the span context for this span
   */
  const opentelemetry::trace::SpanContext &GetSpanContext() const noexcept { return span_context_; }

  /**
   * Get the parent span id for this span
   * @return the span id for this span's parent
   */
  opentelemetry::trace::SpanId GetParentSpanId() const noexcept { return parent_span_id_; }

  /**
   * Get the name for this span
   * @return the name for this span
   */
  opentelemetry::nostd::string_view GetName() const noexcept { return name_; }

  /**
   * Get the trace flags for this span
   * @return the trace flags for this span
   */
  opentelemetry::trace::TraceFlags GetFlags() const noexcept { return flags_; }

  /**
   * Get the kind of this span
   * @return the kind of this span
   */
  opentelemetry::trace::SpanKind GetSpanKind() const noexcept { return span_kind_; }

  /**
   * Get the status for this span
   * @return the status for this span
   */
  opentelemetry::trace::StatusCode GetStatus() const noexcept { return status_code_; }

  /**
   * Get the status description for this span
   * @return the description of the the status of this span
   */
  opentelemetry::nostd::string_view GetDescription() const noexcept { return status_desc_; }

  /**
   * Get the attributes associated with the resource
   * @returns the attributes associated with the resource configured for TracerProvider
   */
  const opentelemetry::sdk::resource::Resource &GetResource() const noexcept;

  /**
   * Get the attributes associated with the resource
   * @returns the attributes associated with the resource configured for TracerProvider
   */
  const opentelemetry::sdk::trace::InstrumentationScope &GetInstrumentationScope() const noexcept;

  OPENTELEMETRY_DEPRECATED_MESSAGE("Please use GetInstrumentationScope instead")
  const opentelemetry::sdk::trace::InstrumentationScope &GetInstrumentationLibrary() const noexcept
  {
    return GetInstrumentationScope();
  }

  /**
   * Get the start time for this span
   * @return the start time for this span
   */
  opentelemetry::common::SystemTimestamp GetStartTime() const noexcept { return start_time_; }

  /**
   * Get the duration for this span
   * @return the duration for this span
   */
  std::chrono::nanoseconds GetDuration() const noexcept { return duration_; }

  /**
   * Get the attributes for this span
   * @return the attributes for this span
   */
  const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &
  GetAttributes() const noexcept;

  /**
   * Get the events associated with this span
   * @return the events associated with this span
   */
  const std::vector<SpanDataEvent> &GetEvents() const noexcept { return events_; }

  /**
   * Get the links associated with this span
   * @return the links associated with this span
   */
  const std::vector<SpanDataLink> &GetLinks() const noexcept { return links_; }

  void SetIdentity(const opentelemetry::trace::SpanContext &span_context,
                   opentelemetry::trace::SpanId parent_span_id) noexcept override;

  void SetAttribute(nostd::string_view key,
                    const opentelemetry::common::AttributeValue &value) noexcept override;

  void AddEvent(nostd::string_view name,
                opentelemetry::common::SystemTimestamp timestamp =
                    opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()),
                const opentelemetry::common::KeyValueIterable &attributes =
                    opentelemetry::common::KeyValueIterableView<std::map<std::string, int32_t>>(
                        {})) noexcept override;

  void AddLink(const opentelemetry::trace::SpanContext &span_context,
               const opentelemetry::common::KeyValueIterable &attributes) noexcept override;

  void SetStatus(opentelemetry::trace::StatusCode code,
                 nostd::string_view description) noexcept override;

  void SetName(nostd::string_view name) noexcept override;

  void SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept override;

  void SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept override;

  void SetResource(const opentelemetry::sdk::resource::Resource &resource) noexcept override;

  void SetStartTime(opentelemetry::common::SystemTimestamp start_time) noexcept override;

  void SetDuration(std::chrono::nanoseconds duration) noexcept override;

  void SetSpanLimits(const SpanLimits &limits) noexcept override;

  void SetInstrumentationScope(const InstrumentationScope &instrumentation_scope) noexcept override;

  std::uint32_t GetDroppedAttributesCount() const noexcept { return dropped_attributes_count_; }

  std::uint32_t GetDroppedEventsCount() const noexcept { return dropped_events_count_; }

  std::uint32_t GetDroppedLinksCount() const noexcept { return dropped_links_count_; }

private:
  opentelemetry::trace::SpanContext span_context_{false, false};
  opentelemetry::trace::SpanId parent_span_id_;
  opentelemetry::common::SystemTimestamp start_time_;
  std::chrono::nanoseconds duration_{0};
  std::string name_;
  opentelemetry::trace::StatusCode status_code_{opentelemetry::trace::StatusCode::kUnset};
  std::string status_desc_;
  opentelemetry::sdk::common::AttributeMap attribute_map_;
  std::vector<SpanDataEvent> events_;
  std::vector<SpanDataLink> links_;
  opentelemetry::trace::TraceFlags flags_;
  opentelemetry::trace::SpanKind span_kind_{opentelemetry::trace::SpanKind::kInternal};
  const opentelemetry::sdk::resource::Resource *resource_{nullptr};
  const InstrumentationScope *instrumentation_scope_{nullptr};
  SpanLimits limits_{SpanLimits::NoLimits()};
  std::uint32_t dropped_attributes_count_{0};
  std::uint32_t dropped_events_count_{0};
  std::uint32_t dropped_links_count_{0};
};
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
