/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_INSTANCE_H_
#define GPU_INSTANCE_H_

#include "ObjectModel.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/layers/BuildConstants.h"
#include "nsCOMPtr.h"

namespace mozilla {
class ErrorResult;
namespace dom {
class Promise;
struct GPURequestAdapterOptions;
}  // namespace dom

namespace webgpu {
class Adapter;
class GPUAdapter;
class Instance;
class WebGPUChild;

class WGSLLanguageFeatures final : public nsWrapperCache,
                                   public ChildOf<Instance> {
 public:
  GPU_DECL_CYCLE_COLLECTION(WGSLLanguageFeatures)

 public:
  explicit WGSLLanguageFeatures(Instance* const aParent) : ChildOf(aParent) {}

  void Add(const nsAString& feature, ErrorResult& aRv) {
    dom::WGSLLanguageFeatures_Binding::SetlikeHelpers::Add(this, feature, aRv);
  }

 protected:
  virtual ~WGSLLanguageFeatures() = default;

 public:
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return dom::WGSLLanguageFeatures_Binding::Wrap(aCx, this, aGivenProto);
  }
};

class Instance final : public nsWrapperCache {
 public:
  GPU_DECL_CYCLE_COLLECTION(Instance)
  GPU_DECL_JS_WRAP(Instance)

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  /// If WebGPU is disabled by `dom.webgpu.enabled` or
  /// `dom.webgpu.service-workers-enabled`, return a static string explaining
  /// why. Otherwise, return nullptr.
  static const char* DisabledByPrefWithExplanation();

  static bool PrefEnabled();
  static bool PrefEnabled(JSContext* aCx, JSObject* aObj) {
    return PrefEnabled();
  }
  static bool ExternalTexturePrefEnabled(JSContext* aCx, JSObject* aObj);

  static already_AddRefed<Instance> Create(nsIGlobalObject* aGlobal);

  already_AddRefed<dom::Promise> RequestAdapter(
      const dom::GPURequestAdapterOptions& aOptions, ErrorResult& aRv);

  dom::GPUTextureFormat GetPreferredCanvasFormat() const {
    // Changing implementation in a way that increases fingerprinting surface?
    // Please create a bug in [Core::Privacy: Anti
    // Tracking](https://bugzilla.mozilla.org/enter_bug.cgi?product=Core&component=Privacy%3A%20Anti-Tracking)
    if (kIsAndroid) {
      return dom::GPUTextureFormat::Rgba8unorm;
    }
    return dom::GPUTextureFormat::Bgra8unorm;
  };

 private:
  explicit Instance(nsIGlobalObject* aGlobal);
  virtual ~Instance() = default;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<WGSLLanguageFeatures> mWgslLanguageFeatures;

 public:
  already_AddRefed<WGSLLanguageFeatures> WgslLanguageFeatures() const {
    RefPtr<WGSLLanguageFeatures> features = mWgslLanguageFeatures;
    return features.forget();
  }
};

}  // namespace webgpu
}  // namespace mozilla

#endif  // GPU_INSTANCE_H_
