/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRPuppetCommandBuffer.h"

#include "mozilla/ClearOnShutdown.h"
#include "prthread.h"

namespace mozilla::gfx {

static StaticRefPtr<VRPuppetCommandBuffer> sVRPuppetCommandBufferSingleton;

/* static */
VRPuppetCommandBuffer& VRPuppetCommandBuffer::Get() {
  if (sVRPuppetCommandBufferSingleton == nullptr) {
    sVRPuppetCommandBufferSingleton = new VRPuppetCommandBuffer();
    ClearOnShutdown(&sVRPuppetCommandBufferSingleton);
  }
  return *sVRPuppetCommandBufferSingleton;
}

/* static */
bool VRPuppetCommandBuffer::IsCreated() {
  return sVRPuppetCommandBufferSingleton != nullptr;
}

VRPuppetCommandBuffer::VRPuppetCommandBuffer()
    : mMutex("VRPuppetCommandBuffer::mMutex") {
  MOZ_COUNT_CTOR(VRPuppetCommandBuffer);
  MOZ_RELEASE_ASSERT(sVRPuppetCommandBufferSingleton == nullptr);
  Reset();
}

VRPuppetCommandBuffer::~VRPuppetCommandBuffer() {
  MOZ_COUNT_DTOR(VRPuppetCommandBuffer);
}

void VRPuppetCommandBuffer::Submit(const nsTArray<uint64_t>& aBuffer) {
  MutexAutoLock lock(mMutex);
  mBuffer.AppendElements(aBuffer);
  mEnded = false;
  mEndedWithTimeout = false;
}

bool VRPuppetCommandBuffer::HasEnded() {
  MutexAutoLock lock(mMutex);
  return mEnded;
}

void VRPuppetCommandBuffer::Reset() {
  MutexAutoLock lock(mMutex);
  memset(&mPendingState, 0, sizeof(VRSystemState));
  memset(&mCommittedState, 0, sizeof(VRSystemState));
  for (int iControllerIdx = 0; iControllerIdx < kVRControllerMaxCount;
       iControllerIdx++) {
    for (int iHaptic = 0; iHaptic < kNumPuppetHaptics; iHaptic++) {
      mHapticPulseRemaining[iControllerIdx][iHaptic] = 0.0f;
      mHapticPulseIntensity[iControllerIdx][iHaptic] = 0.0f;
    }
  }
  mDataOffset = 0;
  mPresentationRequested = false;
  mFrameSubmitted = false;
  mFrameAccepted = false;
  mTimeoutDuration = 10.0f;
  mWaitRemaining = 0.0f;
  mBlockedTime = 0.0f;
  mTimerElapsed = 0.0f;
  mEnded = true;
  mEndedWithTimeout = false;
  mLastRunTimestamp = TimeStamp();
  mTimerSamples.Clear();
  mBuffer.Clear();
}

bool VRPuppetCommandBuffer::RunCommand(uint64_t aCommand, double aDeltaTime) {
  /**
   * Run a single command.  If the command is blocking on a state change and
   * can't be executed, return false.
   *
   * VRPuppetCommandBuffer::RunCommand is only called by
   *VRPuppetCommandBuffer::Run(), which is already holding the mutex.
   *
   * Note that VRPuppetCommandBuffer::RunCommand may potentially be called >1000
   *times per frame. It might not hurt to add an assert here, but we should
   *avoid adding code which may potentially malloc (eg string handling) even for
   *debug builds here.  This function will need to be reasonably fast, even in
   *debug builds which will be using it during tests.
   **/
  switch ((VRPuppet_Command)(aCommand & 0xff00000000000000)) {
    case VRPuppet_Command::VRPuppet_End:
      CompleteTest(false);
      break;
    case VRPuppet_Command::VRPuppet_ClearAll:
      memset(&mPendingState, 0, sizeof(VRSystemState));
      memset(&mCommittedState, 0, sizeof(VRSystemState));
      mPresentationRequested = false;
      mFrameSubmitted = false;
      mFrameAccepted = false;
      break;
    case VRPuppet_Command::VRPuppet_ClearController: {
      uint8_t controllerIdx = aCommand & 0x00000000000000ff;
      if (controllerIdx < kVRControllerMaxCount) {
        mPendingState.controllerState[controllerIdx].Clear();
      }
    } break;
    case VRPuppet_Command::VRPuppet_Timeout:
      mTimeoutDuration = (double)(aCommand & 0x00000000ffffffff) / 1000.0f;
      break;
    case VRPuppet_Command::VRPuppet_Wait:
      if (mWaitRemaining == 0.0f) {
        mWaitRemaining = (double)(aCommand & 0x00000000ffffffff) / 1000.0f;
        // Wait timer started, block
        return false;
      }
      mWaitRemaining -= aDeltaTime;
      if (mWaitRemaining > 0.0f) {
        // Wait timer still running, block
        return false;
      }
      // Wait timer has elapsed, unblock
      mWaitRemaining = 0.0f;
      break;
    case VRPuppet_Command::VRPuppet_WaitSubmit:
      if (!mFrameSubmitted) {
        return false;
      }
      break;
    case VRPuppet_Command::VRPuppet_CaptureFrame:
      // TODO - Capture the frame and record the output (Bug 1555180)
      break;
    case VRPuppet_Command::VRPuppet_AcknowledgeFrame:
      mFrameSubmitted = false;
      mFrameAccepted = true;
      break;
    case VRPuppet_Command::VRPuppet_RejectFrame:
      mFrameSubmitted = false;
      mFrameAccepted = false;
      break;
    case VRPuppet_Command::VRPuppet_WaitPresentationStart:
      if (!mPresentationRequested) {
        return false;
      }
      break;
    case VRPuppet_Command::VRPuppet_WaitPresentationEnd:
      if (mPresentationRequested) {
        return false;
      }
      break;
    case VRPuppet_Command::VRPuppet_WaitHapticIntensity: {
      // 0x0800cchhvvvvvvvv - VRPuppet_WaitHapticIntensity(c, h, v)
      uint8_t iControllerIdx = (aCommand & 0x0000ff0000000000) >> 40;
      if (iControllerIdx >= kVRControllerMaxCount) {
        // Puppet test is broken, ensure it fails
        return false;
      }
      uint8_t iHapticIdx = (aCommand & 0x000000ff00000000) >> 32;
      if (iHapticIdx >= kNumPuppetHaptics) {
        // Puppet test is broken, ensure it fails
        return false;
      }
      uint32_t iHapticIntensity =
          aCommand & 0x00000000ffffffff;  // interpreted as 16.16 fixed point

      SimulateHaptics(aDeltaTime);
      uint64_t iCurrentIntensity =
          round(mHapticPulseIntensity[iControllerIdx][iHapticIdx] *
                (1 << 16));  // convert to 16.16 fixed point
      if (iCurrentIntensity > 0xffffffff) {
        iCurrentIntensity = 0xffffffff;
      }
      if (iCurrentIntensity != iHapticIntensity) {
        return false;
      }
    } break;

    case VRPuppet_Command::VRPuppet_StartTimer:
      mTimerElapsed = 0.0f;
      break;
    case VRPuppet_Command::VRPuppet_StopTimer:
      mTimerSamples.AppendElements(mTimerElapsed);
      // TODO - Return the timer samples to Javascript once the command buffer
      // is complete (Bug 1555182)
      break;

    case VRPuppet_Command::VRPuppet_UpdateDisplay:
      mDataOffset = (uint8_t*)&mPendingState.displayState -
                    (uint8_t*)&mPendingState + (aCommand & 0x00000000ffffffff);
      break;
    case VRPuppet_Command::VRPuppet_UpdateSensor:
      mDataOffset = (uint8_t*)&mPendingState.sensorState -
                    (uint8_t*)&mPendingState + (aCommand & 0x00000000ffffffff);
      break;
    case VRPuppet_Command::VRPuppet_UpdateControllers:
      mDataOffset = (uint8_t*)&mPendingState.controllerState -
                    (uint8_t*)&mPendingState + (aCommand & 0x00000000ffffffff);
      break;
    case VRPuppet_Command::VRPuppet_Commit:
      memcpy(&mCommittedState, &mPendingState, sizeof(VRSystemState));
      break;

    case VRPuppet_Command::VRPuppet_Data7:
      WriteData((aCommand & 0x00ff000000000000) >> 48);
      [[fallthrough]];
      // Purposefully, no break
    case VRPuppet_Command::VRPuppet_Data6:
      WriteData((aCommand & 0x0000ff0000000000) >> 40);
      [[fallthrough]];
      // Purposefully, no break
    case VRPuppet_Command::VRPuppet_Data5:
      WriteData((aCommand & 0x000000ff00000000) >> 32);
      [[fallthrough]];
      // Purposefully, no break
    case VRPuppet_Command::VRPuppet_Data4:
      WriteData((aCommand & 0x00000000ff000000) >> 24);
      [[fallthrough]];
      // Purposefully, no break
    case VRPuppet_Command::VRPuppet_Data3:
      WriteData((aCommand & 0x0000000000ff0000) >> 16);
      [[fallthrough]];
      // Purposefully, no break
    case VRPuppet_Command::VRPuppet_Data2:
      WriteData((aCommand & 0x000000000000ff00) >> 8);
      [[fallthrough]];
      // Purposefully, no break
    case VRPuppet_Command::VRPuppet_Data1:
      WriteData(aCommand & 0x00000000000000ff);
      break;
  }
  return true;
}

void VRPuppetCommandBuffer::WriteData(uint8_t aData) {
  if (mDataOffset && mDataOffset < sizeof(VRSystemState)) {
    ((uint8_t*)&mPendingState)[mDataOffset++] = aData;
  }
}

void VRPuppetCommandBuffer::Run() {
  MutexAutoLock lock(mMutex);
  TimeStamp now = TimeStamp::Now();
  double deltaTime = 0.0f;
  if (!mLastRunTimestamp.IsNull()) {
    deltaTime = (now - mLastRunTimestamp).ToSeconds();
  }
  mLastRunTimestamp = now;
  mTimerElapsed += deltaTime;
  size_t transactionLength = 0;
  while (transactionLength < mBuffer.Length() && !mEnded) {
    if (RunCommand(mBuffer[transactionLength], deltaTime)) {
      mBlockedTime = 0.0f;
      transactionLength++;
    } else {
      mBlockedTime += deltaTime;
      if (mBlockedTime > mTimeoutDuration) {
        CompleteTest(true);
      }
      // If a command is blocked, we don't increment transactionLength,
      // allowing the command to be retried on the next cycle
      break;
    }
  }
  mBuffer.RemoveElementsAt(0, transactionLength);
}

void VRPuppetCommandBuffer::Run(VRSystemState& aState) {
  Run();
  // We don't want to stomp over some members
  bool bEnumerationCompleted = aState.enumerationCompleted;
  bool bShutdown = aState.displayState.shutdown;
  uint32_t minRestartInterval = aState.displayState.minRestartInterval;

  // Overwrite it all
  memcpy(&aState, &mCommittedState, sizeof(VRSystemState));

  // Restore the members
  aState.enumerationCompleted = bEnumerationCompleted;
  aState.displayState.shutdown = bShutdown;
  aState.displayState.minRestartInterval = minRestartInterval;
}

void VRPuppetCommandBuffer::StartPresentation() {
  mPresentationRequested = true;
  Run();
}

void VRPuppetCommandBuffer::StopPresentation() {
  mPresentationRequested = false;
  Run();
}

bool VRPuppetCommandBuffer::SubmitFrame() {
  // Emulate blocking behavior of various XR API's as
  // described by puppet script
  mFrameSubmitted = true;
  mFrameAccepted = false;
  while (true) {
    Run();
    if (!mFrameSubmitted || mEnded) {
      break;
    }
    PR_Sleep(PR_INTERVAL_NO_WAIT);  // Yield
  }

  return mFrameAccepted;
}

void VRPuppetCommandBuffer::VibrateHaptic(uint32_t aControllerIdx,
                                          uint32_t aHapticIndex,
                                          float aIntensity, float aDuration) {
  if (aHapticIndex >= kNumPuppetHaptics ||
      aControllerIdx >= kVRControllerMaxCount) {
    return;
  }

  // We must Run() before and after updating haptic state to avoid script
  // deadlocks
  // The deadlocks would be caused by scripts that include two
  // VRPuppet_WaitHapticIntensity commands.  If
  // VRPuppetCommandBuffer::VibrateHaptic() is called twice without advancing
  // through the command buffer with VRPuppetCommandBuffer::Run() in between,
  // the first VRPuppet_WaitHapticInensity may not see the transient value that
  // it is waiting for, thus blocking forever and deadlocking the script.
  Run();
  mHapticPulseRemaining[aControllerIdx][aHapticIndex] = aDuration;
  mHapticPulseIntensity[aControllerIdx][aHapticIndex] = aIntensity;
  Run();
}

void VRPuppetCommandBuffer::StopVibrateHaptic(uint32_t aControllerIdx) {
  if (aControllerIdx >= kVRControllerMaxCount) {
    return;
  }
  // We must Run() before and after updating haptic state to avoid script
  // deadlocks
  Run();
  for (int iHaptic = 0; iHaptic < kNumPuppetHaptics; iHaptic++) {
    mHapticPulseRemaining[aControllerIdx][iHaptic] = 0.0f;
    mHapticPulseIntensity[aControllerIdx][iHaptic] = 0.0f;
  }
  Run();
}

void VRPuppetCommandBuffer::StopAllHaptics() {
  // We must Run() before and after updating haptic state to avoid script
  // deadlocks
  Run();
  for (int iControllerIdx = 0; iControllerIdx < kVRControllerMaxCount;
       iControllerIdx++) {
    for (int iHaptic = 0; iHaptic < kNumPuppetHaptics; iHaptic++) {
      mHapticPulseRemaining[iControllerIdx][iHaptic] = 0.0f;
      mHapticPulseIntensity[iControllerIdx][iHaptic] = 0.0f;
    }
  }
  Run();
}

void VRPuppetCommandBuffer::SimulateHaptics(double aDeltaTime) {
  for (int iControllerIdx = 0; iControllerIdx < kVRControllerMaxCount;
       iControllerIdx++) {
    for (int iHaptic = 0; iHaptic < kNumPuppetHaptics; iHaptic++) {
      if (mHapticPulseIntensity[iControllerIdx][iHaptic] > 0.0f) {
        mHapticPulseRemaining[iControllerIdx][iHaptic] -= aDeltaTime;
        if (mHapticPulseRemaining[iControllerIdx][iHaptic] <= 0.0f) {
          mHapticPulseRemaining[iControllerIdx][iHaptic] = 0.0f;
          mHapticPulseIntensity[iControllerIdx][iHaptic] = 0.0f;
        }
      }
    }
  }
}

void VRPuppetCommandBuffer::CompleteTest(bool aTimedOut) {
  mEndedWithTimeout = aTimedOut;
  mEnded = true;
}

/**
 *  Generates a sequence of VRPuppet_Data# commands, as described
 *  in VRPuppetCommandBuffer.h, to encode the changes to be made to
 *  a "destination" structure to match the "source" structure.
 *  As the commands are encoded, the destination structure is updated
 *  to match the source.
 *
 * @param aBuffer
 *     The buffer in which the commands will be appended.
 * @param aSrcStart
 *     Byte pointer to the start of the structure that
 *     will be copied from.
 * @param aDstStart
 *     Byte pointer to the start of the structure that
 *     will be copied to.
 * @param aLength
 *     Length of the structure that will be copied.
 * @param aUpdateCommand
 *     VRPuppet_... command indicating which structure is being
 *     copied:
 *     VRPuppet_Command::VRPuppet_UpdateDisplay:
 *         A single VRDisplayState struct
 *     VRPuppet_Command::VRPuppet_UpdateSensor:
 *         A single VRHMDSensorState struct
 *     VRPuppet_Command::VRPuppet_UpdateControllers:
 *         An array of VRControllerState structs with a
 *         count of kVRControllerMaxCount
 */
void VRPuppetCommandBuffer::EncodeStruct(nsTArray<uint64_t>& aBuffer,
                                         uint8_t* aSrcStart, uint8_t* aDstStart,
                                         size_t aLength,
                                         VRPuppet_Command aUpdateCommand) {
  // Naive implementation, but will not be executed in realtime, so will not
  // affect test timer results. Could be improved to avoid unaligned reads and
  // to use SSE.

  // Pointer to source byte being compared+copied
  uint8_t* src = aSrcStart;

  // Pointer to destination byte being compared+copied
  uint8_t* dst = aDstStart;

  // Number of bytes packed into bufData
  uint8_t bufLen = 0;

  // 64-bits to be interpreted as up to 7 separate bytes
  // This will form the lower 56 bits of the command
  uint64_t bufData = 0;

  // purgebuffer takes the bytes stored in bufData and generates a VRPuppet
  // command representing those bytes as "VRPuppet Data".
  // VRPUppet_Data1 encodes 1 byte
  // VRPuppet_Data2 encodes 2 bytes
  // and so on, until..
  // VRPuppet_Data7 encodes 7 bytes
  // This command is appended to aBuffer, then bufLen and bufData are reset
  auto purgeBuffer = [&]() {
    // Only emit a command if there are data bytes in bufData
    if (bufLen > 0) {
      MOZ_RELEASE_ASSERT(bufLen <= 7);
      uint64_t command = (uint64_t)VRPuppet_Command::VRPuppet_Data1;
      command += ((uint64_t)VRPuppet_Command::VRPuppet_Data2 -
                  (uint64_t)VRPuppet_Command::VRPuppet_Data1) *
                 (bufLen - 1);
      command |= bufData;
      aBuffer.AppendElement(command);
      bufLen = 0;
      bufData = 0;
    }
  };

  // Loop through the bytes of the structs.
  // While copying the struct at aSrcStart to aDstStart,
  // the differences are encoded as VRPuppet commands and
  // appended to aBuffer.
  for (size_t i = 0; i < aLength; i++) {
    if (*src != *dst) {
      // This byte is different

      // Copy the byte to the destination
      *dst = *src;

      if (bufLen == 0) {
        // This is the start of a new span of changed bytes

        // Output a command to specify the offset of the
        // span.
        aBuffer.AppendElement((uint64_t)aUpdateCommand + i);

        // Store this first byte in bufData.
        // We will batch up to 7 bytes in one VRPuppet_DataXX
        // command, so we won't emit it yet.
        bufLen = 1;
        bufData = *src;
      } else if (bufLen <= 6) {
        // This is the continuation of a span of changed bytes.
        // There is room to add more bytes to bufData.

        // Store the next byte in bufData.
        // We will batch up to 7 bytes in one VRPuppet_DataXX
        // command, so we won't emit it yet.
        bufData = (bufData << 8) | *src;
        bufLen++;
      } else {
        MOZ_RELEASE_ASSERT(bufLen == 7);
        // This is the continuation of a span of changed bytes.
        // There are already 7 bytes in bufData, so we must emit
        // the VRPuppet_Data7 command for the prior bytes before
        // starting a new command.
        aBuffer.AppendElement((uint64_t)VRPuppet_Command::VRPuppet_Data7 +
                              bufData);

        // Store this byte to be included in the next VRPuppet_DataXX
        // command.
        bufLen = 1;
        bufData = *src;
      }
    } else {
      // This byte is the same.
      // If there are bytes in bufData, the span has now ended and we must
      // emit a VRPuppet_DataXX command for the accumulated bytes.
      // purgeBuffer will not emit any commands if there are no bytes
      // accumulated.
      purgeBuffer();
    }
    // Advance to the next source and destination byte.
    ++src;
    ++dst;
  }
  // In the event that the very last byte of the structs differ, we must
  // ensure that the accumulated bytes are emitted as a VRPuppet_DataXX
  // command.
  purgeBuffer();
}

}  // namespace mozilla::gfx
