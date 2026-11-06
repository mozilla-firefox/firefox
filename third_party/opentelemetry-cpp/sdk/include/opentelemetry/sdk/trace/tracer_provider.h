// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <mutex>
#include <vector>

#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/id_generator.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/random_id_generator.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/samplers/always_on.h"
#include "opentelemetry/sdk/trace/span_limits.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/tracer_provider.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

class OPENTELEMETRY_EXPORT TracerProvider final : public opentelemetry::trace::TracerProvider
{
public:
  /**
   * Initialize a new tracer provider with a specified sampler
   * @param processor The span processor for this tracer provider. This must
   * not be a nullptr.
   * @param resource  The resources for this tracer provider.
   * @param sampler The sampler for this tracer provider. This must
   * not be a nullptr.
   * @param id_generator The custom id generator for this tracer provider. This must
   * not be a nullptr
   * @param tracer_configurator Provides access to a function that computes the TracerConfig for
   * Tracers provided by this TracerProvider.
   * @param span_limits The span limits for this tracer provider.
   */
  explicit TracerProvider(
      std::unique_ptr<SpanProcessor> processor,
      const opentelemetry::sdk::resource::Resource &resource =
          opentelemetry::sdk::resource::Resource::Create({}),
      std::unique_ptr<Sampler> sampler = std::unique_ptr<AlwaysOnSampler>(new AlwaysOnSampler),
      std::unique_ptr<IdGenerator> id_generator =
          std::unique_ptr<IdGenerator>(new RandomIdGenerator()),
      std::unique_ptr<instrumentationscope::ScopeConfigurator<TracerConfig>> tracer_configurator =
          std::make_unique<instrumentationscope::ScopeConfigurator<TracerConfig>>(
              instrumentationscope::ScopeConfigurator<TracerConfig>::Builder(
                  TracerConfig::Default())
                  .Build()),
      SpanLimits span_limits = SpanLimits::NoLimits()) noexcept;

  explicit TracerProvider(
      std::vector<std::unique_ptr<SpanProcessor>> &&processors,
      const opentelemetry::sdk::resource::Resource &resource =
          opentelemetry::sdk::resource::Resource::Create({}),
      std::unique_ptr<Sampler> sampler = std::unique_ptr<AlwaysOnSampler>(new AlwaysOnSampler),
      std::unique_ptr<IdGenerator> id_generator =
          std::unique_ptr<IdGenerator>(new RandomIdGenerator()),
      std::unique_ptr<instrumentationscope::ScopeConfigurator<TracerConfig>> tracer_configurator =
          std::make_unique<instrumentationscope::ScopeConfigurator<TracerConfig>>(
              instrumentationscope::ScopeConfigurator<TracerConfig>::Builder(
                  TracerConfig::Default())
                  .Build()),
      SpanLimits span_limits = SpanLimits::NoLimits()) noexcept;

  /**
   * Initialize a new tracer provider with a specified context
   * @param context The owned tracer configuration/pipeline for this provider.
   */
  explicit TracerProvider(std::unique_ptr<TracerContext> context) noexcept;

  TracerProvider(const TracerProvider &)            = delete;
  TracerProvider(TracerProvider &&)                 = delete;
  TracerProvider &operator=(const TracerProvider &) = delete;
  TracerProvider &operator=(TracerProvider &&)      = delete;

  ~TracerProvider() override;

  /*
    Make sure GetTracer() helpers from the API are seen in overload resolution.
  */
  using opentelemetry::trace::TracerProvider::GetTracer;

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer(
      nostd::string_view name,
      nostd::string_view version,
      nostd::string_view schema_url,
      const opentelemetry::common::KeyValueIterable *attributes) noexcept override;
#else
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer(
      nostd::string_view name,
      nostd::string_view version    = "",
      nostd::string_view schema_url = "") noexcept override;
#endif

  /**
   * Attaches a span processor to list of configured processors for this tracer provider.
   * @param processor The new span processor for this tracer provider. This
   * must not be a nullptr.
   *
   * Note: This process may not receive any in-flight spans, but will get newly created spans.
   * Note: This method is not thread safe, and should ideally be called from main thread.
   */
  void AddProcessor(std::unique_ptr<SpanProcessor> processor) noexcept;

  /**
   * Update the TracerConfigurator for this provider, recreate and propagate the resulting
   * TracerConfig to all existing Tracers while new Tracers will use the updated configuration.
   *
   * @param tracer_configurator The new configurator.
   *
   * @note Calling the TracerProvider::GetTracer from within
   * the ScopeConfigurator<TracerConfig>::ComputeConfig
   * function (as a scope_matcher callback set with ScopeConfigurator<TracerConfig>::AddCondition)
   * is not supported and will result in a deadlock.
   */
  void UpdateTracerConfigurator(
      std::unique_ptr<instrumentationscope::ScopeConfigurator<TracerConfig>>
          tracer_configurator) noexcept;

  /**
   * Obtain the resource associated with this tracer provider.
   * @return The resource for this tracer provider.
   */
  const opentelemetry::sdk::resource::Resource &GetResource() const noexcept;

  /**
   * Obtain the span limits configured for this tracer provider.
   * @return The SpanLimits for this tracer provider.
   */
  const opentelemetry::sdk::trace::SpanLimits &GetSpanLimits() const noexcept;

  /**
   * Shutdown the span processor associated with this tracer provider.
   */
  bool Shutdown(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept;

  /**
   * Force flush the span processor associated with this tracer provider.
   */
  bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept;

private:
  // order of declaration is important here - tracers should destroy only after context.
  std::vector<std::shared_ptr<Tracer>> tracers_;
  std::shared_ptr<TracerContext> context_;
  std::mutex lock_;
};
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
