// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_ANDROID_GVR_DELEGATE_PROVIDER_H
#define DEVICE_VR_ANDROID_GVR_DELEGATE_PROVIDER_H

#include "device/vr/android/gvr/gvr_device_provider.h"
#include "device/vr/vr_export.h"
#include "device/vr/vr_service.mojom.h"

namespace device {

class GvrDelegate;
class GvrDeviceProvider;

class DEVICE_VR_EXPORT GvrDelegateProvider {
 public:
  static void SetInstance(
      const base::Callback<GvrDelegateProvider*()>& provider_callback);
  static GvrDelegateProvider* GetInstance();

  virtual void SetDeviceProvider(GvrDeviceProvider* device_provider) = 0;
  virtual void ClearDeviceProvider() = 0;
  virtual void RequestWebVRPresent(
      mojom::VRSubmitFrameClientPtr submit_client,
      const base::Callback<void(bool)>& callback) = 0;
  virtual void ExitWebVRPresent() = 0;
  virtual GvrDelegate* GetDelegate() = 0;
  virtual void SetListeningForActivate(bool listening) = 0;

 protected:
  virtual ~GvrDelegateProvider() {}

 private:
  static base::Callback<GvrDelegateProvider*()> delegate_provider_;
};

}  // namespace device

#endif  // DEVICE_VR_ANDROID_GVR_DELEGATE_PROVIDER_H
