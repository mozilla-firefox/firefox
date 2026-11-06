// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "opentelemetry/exporters/otlp/otlp_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_populate_attribute_utils.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"  // IWYU pragma: keep
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"           // IWYU pragma: keep
#include "opentelemetry/proto/trace/v1/trace.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"  // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{
struct InstrumentationScopePointerHasher
{
  std::size_t operator()(const opentelemetry::sdk::instrumentationscope::InstrumentationScope
                             *instrumentation) const noexcept
  {
    if (instrumentation == nullptr)
    {
      return 0;
    }

    return instrumentation->HashCode();
  }
};

struct InstrumentationScopePointerEqual
{
  bool operator()(
      const opentelemetry::sdk::instrumentationscope::InstrumentationScope *left,
      const opentelemetry::sdk::instrumentationscope::InstrumentationScope *right) const noexcept
  {
    if (left == right)
    {
      return true;
    }

    if (left == nullptr || right == nullptr)
    {
      return false;
    }

    return *left == *right;
  }
};
}  // namespace

void OtlpRecordableUtils::PopulateRequest(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &spans,
    proto::collector::trace::v1::ExportTraceServiceRequest *request) noexcept
{
  if (nullptr == request)
  {
    return;
  }

  using ScopeSpansMap =
      std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                         proto::trace::v1::ScopeSpans *, InstrumentationScopePointerHasher,
                         InstrumentationScopePointerEqual>;
  struct ResourceSpansEntry
  {
    proto::trace::v1::ResourceSpans *resource_spans = nullptr;
    ScopeSpansMap scope_spans;
  };
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceSpansEntry>
      resource_spans_index;

  for (const auto &recordable : spans)
  {
    const auto *otlp_recordable = static_cast<const OtlpRecordable *>(recordable.get());
    const auto *resource        = otlp_recordable->GetResource();
    const auto *instrumentation = otlp_recordable->GetInstrumentationScope();

    // Find or create the ResourceSpans entry for this recordable's resource
    auto &resource_entry = resource_spans_index[resource];
    if (resource_entry.resource_spans == nullptr)
    {
      resource_entry.resource_spans = request->add_resource_spans();
      if (resource != nullptr)
      {
        // Populate the resource attributes and schema url
        OtlpPopulateAttributeUtils::PopulateAttribute(
            resource_entry.resource_spans->mutable_resource(), *resource);
        resource_entry.resource_spans->set_schema_url(resource->GetSchemaURL());
      }
    }

    // Find or create the ScopeSpans entry for this recordable's instrumentation scope
    auto &scope_spans = resource_entry.scope_spans[instrumentation];
    if (scope_spans == nullptr)
    {
      scope_spans = resource_entry.resource_spans->add_scope_spans();
      if (instrumentation != nullptr)
      {
        // Populate the instrumentation scope attributes and schema url
        proto::common::v1::InstrumentationScope *instrumentation_scope_proto =
            scope_spans->mutable_scope();
        instrumentation_scope_proto->set_name(instrumentation->GetName());
        instrumentation_scope_proto->set_version(instrumentation->GetVersion());
        OtlpPopulateAttributeUtils::PopulateAttribute(instrumentation_scope_proto,
                                                      *instrumentation);

        scope_spans->set_schema_url(instrumentation->GetSchemaURL());
      }
    }

    // The recordable span can only be copied here since the request message is Arena allocated.
    scope_spans->add_spans()->CopyFrom(otlp_recordable->span());
  }
}

void OtlpRecordableUtils::PopulateRequest(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> &logs,
    proto::collector::logs::v1::ExportLogsServiceRequest *request) noexcept
{
  if (nullptr == request)
  {
    return;
  }

  using ScopeLogsMap =
      std::unordered_map<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *,
                         proto::logs::v1::ScopeLogs *, InstrumentationScopePointerHasher,
                         InstrumentationScopePointerEqual>;
  struct ResourceLogsEntry
  {
    proto::logs::v1::ResourceLogs *resource_logs = nullptr;
    ScopeLogsMap scope_logs;
  };
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, ResourceLogsEntry>
      resource_logs_index;

  for (const auto &recordable : logs)
  {
    const auto *otlp_recordable = static_cast<const OtlpLogRecordable *>(recordable.get());
    const auto *instrumentation = &otlp_recordable->GetInstrumentationScope();
    const auto *resource        = &otlp_recordable->GetResource();

    // Find or create the ResourceLogs entry for this recordable's resource
    auto &resource_entry = resource_logs_index[resource];
    if (resource_entry.resource_logs == nullptr)
    {
      resource_entry.resource_logs = request->add_resource_logs();
      if (resource != nullptr)
      {
        // Populate the resource attributes and schema url
        OtlpPopulateAttributeUtils::PopulateAttribute(
            resource_entry.resource_logs->mutable_resource(), *resource);
        resource_entry.resource_logs->set_schema_url(resource->GetSchemaURL());
      }
    }

    // Find or create the ScopeLogs entry for this recordable's instrumentation scope
    auto &scope_logs = resource_entry.scope_logs[instrumentation];
    if (scope_logs == nullptr)
    {
      scope_logs = resource_entry.resource_logs->add_scope_logs();
      if (instrumentation != nullptr)
      {
        auto proto_scope = scope_logs->mutable_scope();
        proto_scope->set_name(instrumentation->GetName());
        proto_scope->set_version(instrumentation->GetVersion());
        OtlpPopulateAttributeUtils::PopulateAttribute(proto_scope, *instrumentation);
        scope_logs->set_schema_url(instrumentation->GetSchemaURL());
      }
    }

    // The recordable log can only be copied here since the request message is Arena allocated.
    scope_logs->add_log_records()->CopyFrom(otlp_recordable->log_record());
  }
}
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
