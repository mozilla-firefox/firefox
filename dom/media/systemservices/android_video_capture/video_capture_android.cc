/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video_capture_android.h"

#include "AndroidBridge.h"
#include "device_info_android.h"
#include "modules/utility/include/helpers_android.h"
#include "rtc_base/logging.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/time_utils.h"

static JavaVM* g_jvm_capture = NULL;
static jclass g_java_capturer_class = NULL;  // VideoCaptureAndroid.class.

namespace webrtc {

jobject JniCommon_allocateNativeByteBuffer(JNIEnv* env, jclass, jint size) {
  void* new_data = ::operator new(size);
  jobject byte_buffer = env->NewDirectByteBuffer(new_data, size);
  return byte_buffer;
}

void JniCommon_freeNativeByteBuffer(JNIEnv* env, jclass, jobject byte_buffer) {
  void* data = env->GetDirectBufferAddress(byte_buffer);
  ::operator delete(data);
}

// Called by Java when the camera has a new frame to deliver.
void JNICALL ProvideCameraFrame(JNIEnv* env, jobject, jint width, jint height,
                                jobject javaDataY, jint strideY,
                                jobject javaDataU, jint strideU,
                                jobject javaDataV, jint strideV, jint rotation,
                                jlong timeStamp, jlong context) {
  if (!context) {
    return;
  }

  webrtc::videocapturemodule::VideoCaptureAndroid* captureModule =
      reinterpret_cast<webrtc::videocapturemodule::VideoCaptureAndroid*>(
          context);
  uint8_t* dataY =
      reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(javaDataY));
  uint8_t* dataU =
      reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(javaDataU));
  uint8_t* dataV =
      reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(javaDataV));

  webrtc::scoped_refptr<I420Buffer> i420Buffer = I420Buffer::Copy(
      width, height, dataY, strideY, dataU, strideU, dataV, strideV);

  captureModule->OnIncomingFrame(i420Buffer, rotation, timeStamp);
}

int32_t SetCaptureAndroidVM(JavaVM* javaVM) {
  if (g_java_capturer_class) {
    return 0;
  }

  if (javaVM) {
    assert(!g_jvm_capture);
    g_jvm_capture = javaVM;
    AttachThreadScoped ats(g_jvm_capture);

    videocapturemodule::DeviceInfoAndroid::Initialize(g_jvm_capture);

    {
      jclass clsRef = mozilla::jni::GetClassRef(
          ats.env(), "org/webrtc/videoengine/VideoCaptureAndroid");
      g_java_capturer_class =
          static_cast<jclass>(ats.env()->NewGlobalRef(clsRef));
      ats.env()->DeleteLocalRef(clsRef);
      assert(g_java_capturer_class);

      JNINativeMethod native_methods[] = {
          {"ProvideCameraFrame",
           "(IILjava/nio/ByteBuffer;ILjava/nio/ByteBuffer;ILjava/nio/"
           "ByteBuffer;IIJJ)V",
           reinterpret_cast<void*>(&ProvideCameraFrame)}};
      if (ats.env()->RegisterNatives(g_java_capturer_class, native_methods,
                                     1) != 0)
        assert(false);
    }

    {
      jclass clsRef =
          mozilla::jni::GetClassRef(ats.env(), "org/webrtc/JniCommon");

      JNINativeMethod native_methods[] = {
          {"nativeAllocateByteBuffer", "(I)Ljava/nio/ByteBuffer;",
           reinterpret_cast<void*>(&JniCommon_allocateNativeByteBuffer)},
          {"nativeFreeByteBuffer", "(Ljava/nio/ByteBuffer;)V",
           reinterpret_cast<void*>(&JniCommon_freeNativeByteBuffer)}};
      if (ats.env()->RegisterNatives(clsRef, native_methods, 2) != 0)
        assert(false);
    }
  } else {
    if (g_jvm_capture) {
      AttachThreadScoped ats(g_jvm_capture);
      ats.env()->UnregisterNatives(g_java_capturer_class);
      ats.env()->DeleteGlobalRef(g_java_capturer_class);
      g_java_capturer_class = NULL;
      videocapturemodule::DeviceInfoAndroid::DeInitialize();
      g_jvm_capture = NULL;
    }
  }

  return 0;
}

namespace videocapturemodule {

webrtc::scoped_refptr<VideoCaptureModule> VideoCaptureImpl::Create(
    Clock* clock, const char* deviceUniqueIdUTF8) {
  webrtc::scoped_refptr<VideoCaptureAndroid> implementation(
      new webrtc::RefCountedObject<VideoCaptureAndroid>(clock));
  if (implementation->Init(deviceUniqueIdUTF8) != 0) {
    implementation = nullptr;
  }
  return implementation;
}

void VideoCaptureAndroid::OnIncomingFrame(
    webrtc::scoped_refptr<I420Buffer> buffer, int32_t degrees,
    int64_t captureTime) {
  MutexLock lock(&api_lock_);

  VideoRotation rotation =
      (degrees <= 45 || degrees > 315)    ? kVideoRotation_0
      : (degrees > 45 && degrees <= 135)  ? kVideoRotation_90
      : (degrees > 135 && degrees <= 225) ? kVideoRotation_180
      : (degrees > 225 && degrees <= 315) ? kVideoRotation_270
                                          : kVideoRotation_0;  // Impossible.

  // Historically, we have ignored captureTime. Why?
  VideoFrame captureFrame(std::move(buffer), 0, webrtc::TimeMillis(), rotation);

  DeliverCapturedFrame(captureFrame);
}

VideoCaptureAndroid::VideoCaptureAndroid(Clock* clock)
    : VideoCaptureImpl(clock),
      _deviceInfo(),
      _jCapturer(NULL),
      _captureStarted(false) {}

int32_t VideoCaptureAndroid::Init(const char* deviceUniqueIdUTF8) {
  const int nameLength = strlen(deviceUniqueIdUTF8);
  if (nameLength >= kVideoCaptureUniqueNameLength) return -1;

  RTC_DCHECK_RUN_ON(&api_checker_);
  // Store the device name
  RTC_LOG(LS_INFO) << "VideoCaptureAndroid::Init: " << deviceUniqueIdUTF8;
  _deviceUniqueId = new char[nameLength + 1];
  memcpy(_deviceUniqueId, deviceUniqueIdUTF8, nameLength + 1);

  AttachThreadScoped ats(g_jvm_capture);
  JNIEnv* env = ats.env();
  jmethodID factory =
      env->GetStaticMethodID(g_java_capturer_class, "create",
                             "(Ljava/lang/String;)"
                             "Lorg/webrtc/videoengine/VideoCaptureAndroid;");
  assert(factory);
  jstring j_deviceName = env->NewStringUTF(_deviceUniqueId);
  _jCapturer = env->NewGlobalRef(env->CallStaticObjectMethod(
      g_java_capturer_class, factory, j_deviceName));
  assert(_jCapturer);
  return 0;
}

VideoCaptureAndroid::~VideoCaptureAndroid() {
  // Ensure Java camera is released even if our caller didn't explicitly Stop.
  if (CaptureStarted()) {
    StopCapture();
  }
  AttachThreadScoped ats(g_jvm_capture);
  JNIEnv* env = ats.env();
  env->DeleteGlobalRef(_jCapturer);
}

int32_t VideoCaptureAndroid::StartCapture(
    const VideoCaptureCapability& capability) {
  AttachThreadScoped ats(g_jvm_capture);
  JNIEnv* env = ats.env();
  RTC_DCHECK_RUN_ON(&api_checker_);
  int width = 0;
  int height = 0;
  int min_mfps = 0;
  int max_mfps = 0;
  VideoCaptureCapability matchedCapability;

  if (_deviceInfo.GetBestMatchedCapability(_deviceUniqueId, capability,
                                           matchedCapability) < 0) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "s: GetBestMatchedCapability failed: "
                      << capability.width << "x" << capability.height;
    return -1;
  }

  if (CaptureStarted()) {
    if (_captureCapability == matchedCapability) {
      return 0;
    }
    if (StopCapture() < 0) {
      RTC_LOG(LS_WARNING) << __FUNCTION__
                          << "Stopping for capability change failed";
    }
  }

  width = matchedCapability.width;
  height = matchedCapability.height;
  _deviceInfo.GetMFpsRange(_deviceUniqueId, matchedCapability.maxFPS, &min_mfps,
                           &max_mfps);

  jmethodID j_start =
      env->GetMethodID(g_java_capturer_class, "startCapture", "(IIIIJ)Z");
  assert(j_start);
  jlong j_this = reinterpret_cast<intptr_t>(this);
  bool started = env->CallBooleanMethod(_jCapturer, j_start, width, height,
                                        min_mfps, max_mfps, j_this);
  if (started) {
    MutexLock lock(&api_lock_);
    _captureCapability = matchedCapability;
    _requestedCapability = capability;
    _captureStarted = true;
  }
  return started ? 0 : -1;
}

int32_t VideoCaptureAndroid::StopCapture() {
  AttachThreadScoped ats(g_jvm_capture);
  JNIEnv* env = ats.env();

  RTC_DCHECK_RUN_ON(&api_checker_);

  {
    MutexLock lock(&api_lock_);

    if (!_captureStarted) {
      return 0;
    }

    memset(&_requestedCapability, 0, sizeof(_requestedCapability));
    memset(&_captureCapability, 0, sizeof(_captureCapability));
    _captureStarted = false;
    // Exit critical section to avoid blocking camera thread inside
    // onIncomingFrame() call.
  }

  // try to stop the capturer.
  jmethodID j_stop =
      env->GetMethodID(g_java_capturer_class, "stopCapture", "()Z");
  return env->CallBooleanMethod(_jCapturer, j_stop) ? 0 : -1;
}

bool VideoCaptureAndroid::CaptureStarted() {
  MutexLock lock(&api_lock_);
  return _captureStarted;
}

int32_t VideoCaptureAndroid::CaptureSettings(VideoCaptureCapability& settings) {
  RTC_DCHECK_RUN_ON(&api_checker_);
  MutexLock lock(&api_lock_);
  settings = _requestedCapability;
  return 0;
}

}  // namespace videocapturemodule
}  // namespace webrtc
