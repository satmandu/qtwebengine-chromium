// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/android/gvr/gvr_delegate.h"

#include "base/trace_event/trace_event.h"
#include "device/vr/vr_math.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr_types.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/transform_util.h"

namespace device {

namespace {
// Default downscale factor for computing the recommended WebVR
// renderWidth/Height from the 1:1 pixel mapped size. Using a rather
// aggressive downscale due to the high overhead of copying pixels
// twice before handing off to GVR. For comparison, the polyfill
// uses approximately 0.55 on a Pixel XL.
static constexpr float kWebVrRecommendedResolutionScale = 0.5;

// TODO(mthiesse): If gvr::PlatformInfo().GetPosePredictionTime() is ever
// exposed, use that instead (it defaults to 50ms on most platforms).
static constexpr int64_t kPredictionTimeWithoutVsyncNanos = 50000000;

// Time offset used for calculating angular velocity from a pair of predicted
// poses. The precise value shouldn't matter as long as it's nonzero and much
// less than a frame.
static constexpr int64_t kAngularVelocityEpsilonNanos = 1000000;

void GvrMatToMatf(const gvr::Mat4f& in, vr::Mat4f* out) {
  // If our std::array implementation doesn't have any non-data members, we can
  // just cast the gvr matrix to an std::array.
  static_assert(sizeof(in) == sizeof(*out),
                "Cannot reinterpret gvr::Mat4f as vr::Matf");
  *out = *reinterpret_cast<vr::Mat4f*>(const_cast<gvr::Mat4f*>(&in));
}

gfx::Vector3dF GetAngularVelocityFromPoses(vr::Mat4f head_mat,
                                           vr::Mat4f head_mat_2,
                                           double epsilon_seconds) {
  // The angular velocity is a 3-element vector pointing along the rotation
  // axis with magnitude equal to rotation speed in radians/second, expressed
  // in the seated frame of reference.
  //
  // The 1.1 spec isn't very clear on details, clarification requested in
  // https://github.com/w3c/webvr/issues/212 . For now, assuming that we
  // want a vector in the sitting reference frame.
  //
  // Assuming that pose prediction is simply based on adding a time * angular
  // velocity rotation to the pose, we can approximate the angular velocity
  // from the difference between two successive poses. This is a first order
  // estimate that assumes small enough rotations so that we can do linear
  // approximation.
  //
  // See:
  // https://en.wikipedia.org/wiki/Angular_velocity#Calculation_from_the_orientation_matrix

  vr::Mat4f delta_mat;
  vr::Mat4f inverse_head_mat;
  // Calculate difference matrix, and inverse head matrix rotation.
  // For the inverse rotation, just transpose the 3x3 subsection.
  //
  // Assume that epsilon is nonzero since it's based on a compile-time constant
  // provided by the caller.
  for (int j = 0; j < 3; ++j) {
    for (int i = 0; i < 3; ++i) {
      delta_mat[j][i] = (head_mat_2[j][i] - head_mat[j][i]) / epsilon_seconds;
      inverse_head_mat[j][i] = head_mat[i][j];
    }
    delta_mat[j][3] = delta_mat[3][j] = 0.0;
    inverse_head_mat[j][3] = inverse_head_mat[3][j] = 0.0;
  }
  delta_mat[3][3] = 1.0;
  inverse_head_mat[3][3] = 1.0;
  vr::Mat4f omega_mat;
  vr::MatrixMul(delta_mat, inverse_head_mat, &omega_mat);
  gfx::Vector3dF omega_vec(-omega_mat[2][1], omega_mat[2][0], -omega_mat[1][0]);

  // Rotate by inverse head matrix to bring into seated space.
  return vr::MatrixVectorRotate(inverse_head_mat, omega_vec);
}

}  // namespace

/* static */
mojom::VRPosePtr GvrDelegate::VRPosePtrFromGvrPose(const vr::Mat4f& head_mat) {
  mojom::VRPosePtr pose = mojom::VRPose::New();

  pose->orientation.emplace(4);

  gfx::Transform inv_transform(
      head_mat[0][0], head_mat[0][1], head_mat[0][2], head_mat[0][3],
      head_mat[1][0], head_mat[1][1], head_mat[1][2], head_mat[1][3],
      head_mat[2][0], head_mat[2][1], head_mat[2][2], head_mat[2][3],
      head_mat[3][0], head_mat[3][1], head_mat[3][2], head_mat[3][3]);

  gfx::Transform transform;
  if (inv_transform.GetInverse(&transform)) {
    gfx::DecomposedTransform decomposed_transform;
    gfx::DecomposeTransform(&decomposed_transform, transform);

    pose->orientation.value()[0] = decomposed_transform.quaternion[0];
    pose->orientation.value()[1] = decomposed_transform.quaternion[1];
    pose->orientation.value()[2] = decomposed_transform.quaternion[2];
    pose->orientation.value()[3] = decomposed_transform.quaternion[3];

    pose->position.emplace(3);
    pose->position.value()[0] = decomposed_transform.translate[0];
    pose->position.value()[1] = decomposed_transform.translate[1];
    pose->position.value()[2] = decomposed_transform.translate[2];
  }

  return pose;
}

/* static */
void GvrDelegate::GetGvrPoseWithNeckModel(gvr::GvrApi* gvr_api,
                                          vr::Mat4f* out) {
  gvr::ClockTimePoint target_time = gvr::GvrApi::GetTimePointNow();
  target_time.monotonic_system_time_nanos += kPredictionTimeWithoutVsyncNanos;

  gvr::Mat4f head_mat = gvr_api->ApplyNeckModel(
      gvr_api->GetHeadSpaceFromStartSpaceRotation(target_time), 1.0f);

  GvrMatToMatf(head_mat, out);
}

/* static */
mojom::VRPosePtr GvrDelegate::GetVRPosePtrWithNeckModel(
    gvr::GvrApi* gvr_api,
    vr::Mat4f* head_mat_out) {
  gvr::ClockTimePoint target_time = gvr::GvrApi::GetTimePointNow();
  target_time.monotonic_system_time_nanos += kPredictionTimeWithoutVsyncNanos;

  gvr::Mat4f gvr_head_mat = gvr_api->ApplyNeckModel(
      gvr_api->GetHeadSpaceFromStartSpaceRotation(target_time), 1.0f);

  vr::Mat4f* head_mat_ptr = head_mat_out;
  vr::Mat4f head_mat;
  if (!head_mat_ptr)
    head_mat_ptr = &head_mat;
  GvrMatToMatf(gvr_head_mat, head_mat_ptr);

  mojom::VRPosePtr pose = GvrDelegate::VRPosePtrFromGvrPose(*head_mat_ptr);

  // Get a second pose a bit later to calculate angular velocity.
  target_time.monotonic_system_time_nanos += kAngularVelocityEpsilonNanos;
  gvr::Mat4f gvr_head_mat_2 =
      gvr_api->GetHeadSpaceFromStartSpaceRotation(target_time);
  vr::Mat4f head_mat_2;
  GvrMatToMatf(gvr_head_mat_2, &head_mat_2);

  // Add headset angular velocity to the pose.
  pose->angularVelocity.emplace(3);
  double epsilon_seconds = kAngularVelocityEpsilonNanos * 1e-9;
  gfx::Vector3dF angular_velocity =
      GetAngularVelocityFromPoses(*head_mat_ptr, head_mat_2, epsilon_seconds);
  pose->angularVelocity.value()[0] = angular_velocity.x();
  pose->angularVelocity.value()[1] = angular_velocity.y();
  pose->angularVelocity.value()[2] = angular_velocity.z();

  return pose;
}

/* static */
gfx::Size GvrDelegate::GetRecommendedWebVrSize(gvr::GvrApi* gvr_api) {
  // Pick a reasonable default size for the WebVR transfer surface
  // based on a downscaled 1:1 render resolution. This size will also
  // be reported to the client via CreateVRDisplayInfo as the
  // client-recommended renderWidth/renderHeight and for the GVR
  // framebuffer. If the client chooses a different size or resizes it
  // while presenting, we'll resize the transfer surface and GVR
  // framebuffer to match.
  gvr::Sizei render_target_size =
      gvr_api->GetMaximumEffectiveRenderTargetSize();

  gfx::Size webvr_size(
      render_target_size.width * kWebVrRecommendedResolutionScale,
      render_target_size.height * kWebVrRecommendedResolutionScale);

  // Ensure that the width is an even number so that the eyes each
  // get the same size, the recommended renderWidth is per eye
  // and the client will use the sum of the left and right width.
  //
  // TODO(klausw,crbug.com/699350): should we round the recommended
  // size to a multiple of 2^N pixels to be friendlier to the GPU? The
  // exact size doesn't matter, and it might be more efficient.
  webvr_size.set_width(webvr_size.width() & ~1);
  return webvr_size;
}

/* static */
mojom::VRDisplayInfoPtr GvrDelegate::CreateVRDisplayInfo(
    gvr::GvrApi* gvr_api,
    gfx::Size recommended_size,
    uint32_t device_id) {
  TRACE_EVENT0("input", "GvrDelegate::CreateVRDisplayInfo");

  mojom::VRDisplayInfoPtr device = mojom::VRDisplayInfo::New();

  device->index = device_id;

  device->capabilities = mojom::VRDisplayCapabilities::New();
  device->capabilities->hasPosition = false;
  device->capabilities->hasExternalDisplay = false;
  device->capabilities->canPresent = true;

  std::string vendor = gvr_api->GetViewerVendor();
  std::string model = gvr_api->GetViewerModel();
  device->displayName = vendor + " " + model;

  gvr::BufferViewportList gvr_buffer_viewports =
      gvr_api->CreateEmptyBufferViewportList();
  gvr_buffer_viewports.SetToRecommendedBufferViewports();

  device->leftEye = mojom::VREyeParameters::New();
  device->rightEye = mojom::VREyeParameters::New();
  for (auto eye : {GVR_LEFT_EYE, GVR_RIGHT_EYE}) {
    mojom::VREyeParametersPtr& eye_params =
        (eye == GVR_LEFT_EYE) ? device->leftEye : device->rightEye;
    eye_params->fieldOfView = mojom::VRFieldOfView::New();
    eye_params->offset.resize(3);
    eye_params->renderWidth = recommended_size.width() / 2;
    eye_params->renderHeight = recommended_size.height();

    gvr::BufferViewport eye_viewport = gvr_api->CreateBufferViewport();
    gvr_buffer_viewports.GetBufferViewport(eye, &eye_viewport);
    gvr::Rectf eye_fov = eye_viewport.GetSourceFov();
    eye_params->fieldOfView->upDegrees = eye_fov.top;
    eye_params->fieldOfView->downDegrees = eye_fov.bottom;
    eye_params->fieldOfView->leftDegrees = eye_fov.left;
    eye_params->fieldOfView->rightDegrees = eye_fov.right;

    gvr::Mat4f eye_mat = gvr_api->GetEyeFromHeadMatrix(eye);
    eye_params->offset[0] = -eye_mat.m[0][3];
    eye_params->offset[1] = -eye_mat.m[1][3];
    eye_params->offset[2] = -eye_mat.m[2][3];
  }

  return device;
}

}  // namespace device
