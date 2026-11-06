// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include <unordered_map>
#include <utility>
#include <vector>
#include "opentelemetry/nostd/variant.h"

OPENTELEMETRY_BEGIN_NAMESPACE

namespace sdk
{
namespace instrumentationscope
{

nostd::unique_ptr<InstrumentationScope> InstrumentationScope::Create(
    nostd::string_view name,
    nostd::string_view version,
    nostd::string_view schema_url,
    InstrumentationScopeAttributes &&attributes)
{
  return nostd::unique_ptr<InstrumentationScope>(
      new InstrumentationScope{name, version, schema_url, std::move(attributes)});
}

nostd::unique_ptr<InstrumentationScope> InstrumentationScope::Create(
    nostd::string_view name,
    nostd::string_view version,
    nostd::string_view schema_url,
    const InstrumentationScopeAttributes &attributes)
{
  return nostd::unique_ptr<InstrumentationScope>(new InstrumentationScope{
      name, version, schema_url, InstrumentationScopeAttributes(attributes)});
}

std::size_t InstrumentationScope::HashCode() const noexcept
{
  return hash_code_;
}

bool InstrumentationScope::operator==(const InstrumentationScope &other) const noexcept
{
  return this->name_ == other.name_ && this->version_ == other.version_ &&
         this->schema_url_ == other.schema_url_ && this->attributes_ == other.attributes_;
}

bool InstrumentationScope::equal(
    const nostd::string_view name,
    const nostd::string_view version,
    const nostd::string_view schema_url,
    const opentelemetry::common::KeyValueIterable *attributes) const noexcept
{

  if (this->name_ != name || this->version_ != version || this->schema_url_ != schema_url)
  {
    return false;
  }

  if (attributes == nullptr)
  {
    if (attributes_.empty())
    {
      return true;
    }
    return false;
  }

  return attributes_.EqualTo(*attributes);
}

const std::string &InstrumentationScope::GetName() const noexcept
{
  return name_;
}
const std::string &InstrumentationScope::GetVersion() const noexcept
{
  return version_;
}
const std::string &InstrumentationScope::GetSchemaURL() const noexcept
{
  return schema_url_;
}
const InstrumentationScopeAttributes &InstrumentationScope::GetAttributes() const noexcept
{
  return attributes_;
}

void InstrumentationScope::SetAttribute(nostd::string_view key,
                                        const opentelemetry::common::AttributeValue &value) noexcept
{
  attributes_[std::string(key)] =
      nostd::visit(opentelemetry::sdk::common::AttributeConverter(), value);
}

InstrumentationScope::InstrumentationScope(nostd::string_view name,
                                           nostd::string_view version,
                                           nostd::string_view schema_url,
                                           InstrumentationScopeAttributes &&attributes)
    : name_(name), version_(version), schema_url_(schema_url), attributes_(std::move(attributes))
{
  std::string hash_data;
  hash_data.reserve(name_.size() + version_.size() + schema_url_.size());
  hash_data += name_;
  hash_data += version_;
  hash_data += schema_url_;
  hash_code_ = std::hash<std::string>{}(hash_data);
}

}  // namespace instrumentationscope
}  // namespace sdk

OPENTELEMETRY_END_NAMESPACE
