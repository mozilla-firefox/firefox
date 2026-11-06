// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <exception>
#include <mutex>
#include <ostream>
#include <utility>
#include <vector>

#include "opentelemetry/common/key_value_iterable.h"  // IWYU pragma: keep
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/instrumentationscope/scope_configurator.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/id_generator.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/span_limits.h"
#include "opentelemetry/sdk/trace/tracer.h"
#include "opentelemetry/sdk/trace/tracer_config.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{
namespace resource  = opentelemetry::sdk::resource;
namespace trace_api = opentelemetry::trace;

namespace
{

nostd::shared_ptr<trace_api::Tracer> CreateNoopTracerFallback()
{
  // Construct as NoopTracer so enable_shared_from_this is initialized; StartSpan
  // relies on shared_from_this() for the static noop span.
  nostd::shared_ptr<trace_api::NoopTracer> tracer(new trace_api::NoopTracer());
  return nostd::shared_ptr<trace_api::Tracer>(std::move(tracer));
}

void LogGetTracerConstructionFailure(const char *detail) noexcept
{
#if OPENTELEMETRY_HAVE_EXCEPTIONS
  try
  {
#endif
    OTEL_INTERNAL_LOG_ERROR("[TracerProvider::GetTracer] Failed to construct tracer: "
                            << detail << "; returning noop tracer.");
#if OPENTELEMETRY_HAVE_EXCEPTIONS
  }
  catch (const std::exception &)  // NOLINT(bugprone-empty-catch)
  {
    // Logging can throw (typically std::bad_alloc from the string stream).
    // Swallow it so the noexcept GetTracer path cannot throw.
  }
#endif
}

}  // namespace

TracerProvider::TracerProvider(std::unique_ptr<TracerContext> context)
    : context_(std::move(context)), noop_tracer_(CreateNoopTracerFallback())
{
  OTEL_INTERNAL_LOG_DEBUG("[TracerProvider] TracerProvider created.");
  initialized_ = true;
}

TracerProvider::TracerProvider(
    std::unique_ptr<SpanProcessor> processor,
    const resource::Resource &resource,
    std::unique_ptr<Sampler> sampler,
    std::unique_ptr<IdGenerator> id_generator,
    std::unique_ptr<instrumentationscope::ScopeConfigurator<TracerConfig>> tracer_configurator,
    SpanLimits span_limits)
    : noop_tracer_(CreateNoopTracerFallback())
{
  std::vector<std::unique_ptr<SpanProcessor>> processors;
  processors.push_back(std::move(processor));
  context_ = std::make_shared<TracerContext>(std::move(processors), resource, std::move(sampler),
                                             std::move(id_generator),
                                             std::move(tracer_configurator), span_limits);
  // Context construction can throw, so mark initialized only after it succeeds.
  initialized_ = true;  // NOLINT(cppcoreguidelines-prefer-member-initializer)
}

TracerProvider::TracerProvider(
    std::vector<std::unique_ptr<SpanProcessor>> &&processors,
    const resource::Resource &resource,
    std::unique_ptr<Sampler> sampler,
    std::unique_ptr<IdGenerator> id_generator,
    std::unique_ptr<instrumentationscope::ScopeConfigurator<TracerConfig>> tracer_configurator,
    SpanLimits span_limits)
    : context_(std::make_shared<TracerContext>(std::move(processors),
                                               resource,
                                               std::move(sampler),
                                               std::move(id_generator),
                                               std::move(tracer_configurator),
                                               span_limits)),
      noop_tracer_(CreateNoopTracerFallback()),
      initialized_(true)
{}

TracerProvider::~TracerProvider()
{
  // Tracer hold the shared pointer to the context. So we can not use destructor of TracerContext to
  // Shutdown and flush all pending recordables when we have more than one tracer.These recordables
  // may use the raw pointer of instrumentation_scope_ in Tracer
  if (context_)
  {
    context_->Shutdown();
  }
}

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
nostd::shared_ptr<trace_api::Tracer> TracerProvider::GetTracer(
    nostd::string_view name,
    nostd::string_view version,
    nostd::string_view schema_url,
    const opentelemetry::common::KeyValueIterable *attributes) noexcept
#else
nostd::shared_ptr<trace_api::Tracer> TracerProvider::GetTracer(
    nostd::string_view name,
    nostd::string_view version,
    nostd::string_view schema_url) noexcept
#endif
{
#if OPENTELEMETRY_ABI_VERSION_NO < 2
  const opentelemetry::common::KeyValueIterable *attributes = nullptr;
#endif

  if (name.data() == nullptr)
  {
    OTEL_INTERNAL_LOG_ERROR("[TracerProvider::GetTracer] Library name is null.");
    name = "";
  }
  else if (name == "")
  {
    OTEL_INTERNAL_LOG_ERROR("[TracerProvider::GetTracer] Library name is empty.");
  }

#if OPENTELEMETRY_HAVE_EXCEPTIONS
  try
  {
#endif
    // Release the lock before construction-failure diagnostics can re-enter the provider.
    const std::lock_guard<std::mutex> guard(lock_);

    for (auto &tracer : tracers_)
    {
      auto &tracer_scope = tracer->GetInstrumentationScope();
      if (tracer_scope.equal(name, version, schema_url, attributes))
      {
        return nostd::shared_ptr<trace_api::Tracer>{tracer};
      }
    }

    // GetTracer is noexcept and does not surface an error, so callers cannot
    // recover after a construction failure. Retrying would re-throw, catch, and
    // log on hot paths such as GetTracer(...)->StartSpan(...). After the first
    // failure, treat the provider as non-functional for new tracers.
    if (!initialized_)
    {
      return noop_tracer_;
    }

    initialized_ = false;
    instrumentationscope::InstrumentationScopeAttributes attrs_map(attributes);
    auto scope =
        instrumentationscope::InstrumentationScope::Create(name, version, schema_url, attrs_map);

    auto tracer = std::make_shared<Tracer>(context_, std::move(scope));
    tracers_.push_back(tracer);
    nostd::shared_ptr<trace_api::Tracer> result{tracer};
    initialized_ = true;
    return result;
#if OPENTELEMETRY_HAVE_EXCEPTIONS
  }
  catch (const std::exception &ex)
  {
    LogGetTracerConstructionFailure(ex.what());
    return noop_tracer_;
  }
  // User-provided scope configurators can throw any exception type, not just
  // std::exception. Catch everything so GetTracer stays noexcept.
  catch (...)
  {
    LogGetTracerConstructionFailure("unknown exception");
    return noop_tracer_;
  }
#endif
}

void TracerProvider::AddProcessor(std::unique_ptr<SpanProcessor> processor) noexcept
{
  context_->AddProcessor(std::move(processor));
}

void TracerProvider::UpdateTracerConfigurator(
    std::unique_ptr<instrumentationscope::ScopeConfigurator<TracerConfig>>
        tracer_configurator) noexcept
{
  if (!tracer_configurator)
  {
    OTEL_INTERNAL_LOG_ERROR(
        "[TracerProvider::UpdateTracerConfigurator] tracer_configurator must not be null, "
        "ignoring.");
    return;
  }

  // Lock the provider mutex to ensure that calls to GetTracer are exclusive with respect to the
  // TracerConfigurator update and corresponding TracerConfig updates. This ensures that a Tracer
  // will never be returned from GetTracer with a TracerConfig that is out of date with respect to
  // the provider-level TracerConfigurator.
  const std::lock_guard<std::mutex> guard(lock_);
  context_->SetTracerConfigurator(std::move(tracer_configurator));

  // The only way to set the TracerConfig of a tracer is on Tracer construction in
  // TracerProvider::GetTracer or through Tracer::UpdateTracerConfig (which is private and only
  // accessed by TracerProvider).
  for (auto &tracer : tracers_)
  {
    TracerConfig new_config =
        context_->GetTracerConfigurator().ComputeConfig(tracer->GetInstrumentationScope());
    tracer->UpdateTracerConfig(new_config);
  }
}

const resource::Resource &TracerProvider::GetResource() const noexcept
{
  return context_->GetResource();
}

const opentelemetry::sdk::trace::SpanLimits &TracerProvider::GetSpanLimits() const noexcept
{
  return context_->GetSpanLimits();
}

bool TracerProvider::Shutdown(std::chrono::microseconds timeout) noexcept
{
  return context_->Shutdown(timeout);
}

bool TracerProvider::ForceFlush(std::chrono::microseconds timeout) noexcept
{
  return context_->ForceFlush(timeout);
}
}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
