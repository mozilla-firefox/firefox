/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <math.h>

#include "gfxVR.h"

using namespace mozilla;
using namespace mozilla::gfx;

Matrix4x4 VRFieldOfView::ConstructProjectionMatrix(float zNear, float zFar,
                                                   bool rightHanded) const {
  float upTan = tan(upDegrees * kRadPerDegree);
  float downTan = tan(downDegrees * kRadPerDegree);
  float leftTan = tan(leftDegrees * kRadPerDegree);
  float rightTan = tan(rightDegrees * kRadPerDegree);

  float handednessScale = rightHanded ? -1.0 : 1.0;

  float pxscale = 2.0f / (leftTan + rightTan);
  float pxoffset = (leftTan - rightTan) * pxscale * 0.5;
  float pyscale = 2.0f / (upTan + downTan);
  float pyoffset = (upTan - downTan) * pyscale * 0.5;

  Matrix4x4 mobj;
  float* m = &mobj._11;

  m[0 * 4 + 0] = pxscale;
  m[2 * 4 + 0] = pxoffset * handednessScale;

  m[1 * 4 + 1] = pyscale;
  m[2 * 4 + 1] = -pyoffset * handednessScale;

  m[2 * 4 + 2] = zFar / (zNear - zFar) * -handednessScale;
  m[3 * 4 + 2] = (zFar * zNear) / (zNear - zFar);

  m[2 * 4 + 3] = handednessScale;
  m[3 * 4 + 3] = 0.0f;

  return mobj;
}

void VRHMDSensorState::CalcViewMatrices(
    const gfx::Matrix4x4* aHeadToEyeTransforms) {
  gfx::Matrix4x4 matHead;
  if (flags & VRDisplayCapabilityFlags::Cap_Orientation) {
    matHead.SetRotationFromQuaternion(
        gfx::Quaternion(-pose.orientation[0], -pose.orientation[1],
                        -pose.orientation[2], pose.orientation[3]));
  }
  matHead.PreTranslate(-pose.position[0], -pose.position[1], -pose.position[2]);

  gfx::Matrix4x4 matView =
      matHead * aHeadToEyeTransforms[VRDisplayState::Eye_Left];
  matView.Normalize();
  memcpy(leftViewMatrix.data(), matView.components, sizeof(matView.components));
  matView = matHead * aHeadToEyeTransforms[VRDisplayState::Eye_Right];
  matView.Normalize();
  memcpy(rightViewMatrix.data(), matView.components,
         sizeof(matView.components));
}

const IntSize VRDisplayInfo::SuggestedEyeResolution() const {
  return IntSize(mDisplayState.eyeResolution.width,
                 mDisplayState.eyeResolution.height);
}

const Point3D VRDisplayInfo::GetEyeTranslation(uint32_t whichEye) const {
  return Point3D(mDisplayState.eyeTranslation[whichEye].x,
                 mDisplayState.eyeTranslation[whichEye].y,
                 mDisplayState.eyeTranslation[whichEye].z);
}

const Size VRDisplayInfo::GetStageSize() const {
  return Size(mDisplayState.stageSize.width, mDisplayState.stageSize.height);
}

const Matrix4x4 VRDisplayInfo::GetSittingToStandingTransform() const {
  Matrix4x4 m;
  // If we could replace Matrix4x4 with a pod type, we could
  // use it directly from the VRDisplayInfo struct.
  memcpy(m.components, mDisplayState.sittingToStandingTransform.data(),
         sizeof(float) * 16);
  return m;
}
