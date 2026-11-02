/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCETYPES_H_
#define TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCETYPES_H_

#include <cstdint>

#include "ipc/EnumSerializer.h"
#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/HWInferenceBinding.h"

namespace mozilla::hwinference {

// Outcome of a PHWInference::InstallModel request. Denied means the task's
// resolver refused the download, e.g. the user dismissed the doorhanger;
// Failed means it was allowed but did not complete.
enum class ModelInstallResult : uint8_t {
  Installed,
  Denied,
  Failed,
};

}  // namespace mozilla::hwinference

namespace IPC {

template <>
struct ParamTraits<mozilla::hwinference::ModelInstallResult>
    : public ContiguousEnumSerializerInclusive<
          mozilla::hwinference::ModelInstallResult,
          mozilla::hwinference::ModelInstallResult::Installed,
          mozilla::hwinference::ModelInstallResult::Failed> {};

template <>
struct ParamTraits<mozilla::dom::HWInferenceDeviceKind>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::HWInferenceDeviceKind> {};

template <>
struct ParamTraits<mozilla::dom::HWInferenceBackend>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::HWInferenceBackend> {};

}  // namespace IPC

#endif  // TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCETYPES_H_
