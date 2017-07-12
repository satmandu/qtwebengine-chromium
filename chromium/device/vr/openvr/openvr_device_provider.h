// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_OPENVR_DEVICE_PROVIDER_H
#define DEVICE_VR_OPENVR_DEVICE_PROVIDER_H

#include <memory>

#include "base/callback.h"
#include "base/macros.h"
#include "device/vr/vr_device_provider.h"
#include "device/vr/vr_export.h"

namespace device {

class OpenVRDeviceProvider : public VRDeviceProvider {
 public:
  OpenVRDeviceProvider();
  ~OpenVRDeviceProvider() override;

  void GetDevices(std::vector<VRDevice*>* devices) override;
  void Initialize() override;

  void SetListeningForActivate(bool listening) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(OpenVRDeviceProvider);
};

}  // namespace device

#endif  // DEVICE_VR_OPENVR_DEVICE_PROVIDER_H