// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/content_video_view_overlay.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "gpu/ipc/common/gpu_surface_lookup.h"

namespace media {

ContentVideoViewOverlay::ContentVideoViewOverlay(
    int surface_id,
    const AndroidOverlay::Config& config)
    : surface_id_(surface_id), config_(config), weak_factory_(this) {
  if (ContentVideoViewOverlayAllocator::GetInstance()->AllocateSurface(this)) {
    // We have the surface -- post a callback to our OnSurfaceAvailable.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&ContentVideoViewOverlay::OnSurfaceAvailable,
                              weak_factory_.GetWeakPtr(), true));
  }
}

ContentVideoViewOverlay::~ContentVideoViewOverlay() {
  // Deallocate the surface.  It's okay if we don't own it.
  // Note that this only happens once any codec is done with us.
  ContentVideoViewOverlayAllocator::GetInstance()->DeallocateSurface(this);
}

void ContentVideoViewOverlay::ScheduleLayout(const gfx::Rect& rect) {
  NOTIMPLEMENTED();
}

const base::android::JavaRef<jobject>& ContentVideoViewOverlay::GetJavaSurface()
    const {
  return surface_.j_surface();
}

void ContentVideoViewOverlay::OnSurfaceAvailable(bool success) {
  if (!success) {
    // Notify that the surface won't be available.
    config_.failed_cb.Run(this);
    // |this| may be deleted.
    return;
  }

  // Get the surface and notify our client.
  surface_ =
      gpu::GpuSurfaceLookup::GetInstance()->AcquireJavaSurface(surface_id_);

  // If no surface was returned, then fail instead.
  if (surface_.IsEmpty()) {
    config_.failed_cb.Run(this);
    // |this| may be deleted.
    return;
  }

  config_.ready_cb.Run(this);
}

void ContentVideoViewOverlay::OnSurfaceDestroyed() {
  config_.destroyed_cb.Run(this);
  // |this| may be deleted, or deletion might be posted elsewhere.
}

int32_t ContentVideoViewOverlay::GetSurfaceId() {
  return surface_id_;
}

}  // namespace media
