/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DMABUFTextureClientOGL.h"
#include "mozilla/widget/DMABufSurface.h"
#include "gfxPlatform.h"

namespace mozilla::layers {

using namespace gfx;

DMABUFTextureData::DMABUFTextureData(DMABufSurface* aSurface,
                                     BackendType aBackend)
    : mSurface(aSurface), mBackend(aBackend) {
  MOZ_ASSERT(mSurface);
}

DMABUFTextureData::~DMABUFTextureData() = default;

bool DMABUFTextureData::Serialize(SurfaceDescriptor& aOutDescriptor) {
  return mSurface->Serialize(aOutDescriptor);
}

void DMABUFTextureData::GetSubDescriptor(
    RemoteDecoderVideoSubDescriptor* const aOutDesc) {
  SurfaceDescriptor desc;
  if (!mSurface->Serialize(desc)) {
    return;
  }
  *aOutDesc = static_cast<SurfaceDescriptorDMABuf>(desc);
}

void DMABUFTextureData::FillInfo(TextureData::Info& aInfo) const {
  aInfo.size = gfx::IntSize(mSurface->GetWidth(), mSurface->GetHeight());
  aInfo.format = mSurface->GetFormat();
  aInfo.hasSynchronization = false;
  aInfo.supportsMoz2D = false;
  aInfo.canExposeMappedData = false;
}

bool DMABUFTextureData::Lock(OpenMode) {
  MOZ_DIAGNOSTIC_CRASH("Not implemented.");
  return false;
}

void DMABUFTextureData::Unlock() { MOZ_DIAGNOSTIC_CRASH("Not implemented."); }

already_AddRefed<DataSourceSurface> DMABUFTextureData::GetAsSurface() {
  // TODO: Update for debug purposes.
  return nullptr;
}

void DMABUFTextureData::Deallocate(LayersIPCChannel*) { mSurface = nullptr; }

void DMABUFTextureData::Forget(LayersIPCChannel*) { mSurface = nullptr; }

}  // namespace mozilla::layers
