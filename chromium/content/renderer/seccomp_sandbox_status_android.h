// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_SECCOMP_SANDBOX_STATUS_ANDROID_H_
#define CONTENT_RENDERER_SECCOMP_SANDBOX_STATUS_ANDROID_H_

#include "content/public/renderer/seccomp_sandbox_status_android.h"

namespace content {

void SetSeccompSandboxStatus(SeccompSandboxStatus status);

}  // namespace content

#endif  // CONTENT_RENDERER_SECCOMP_SANDBOX_STATUS_ANDROID_H_
