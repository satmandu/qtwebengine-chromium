// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_PUBLIC_CPP_BITMAP_CHILD_CHILD_SHARED_BITMAP_MANAGER_H_
#define SERVICES_UI_PUBLIC_CPP_BITMAP_CHILD_CHILD_SHARED_BITMAP_MANAGER_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/shared_memory.h"
#include "cc/ipc/shared_bitmap_manager.mojom.h"
#include "cc/resources/shared_bitmap_manager.h"
#include "mojo/public/cpp/bindings/thread_safe_interface_ptr.h"

namespace ui {

class ChildSharedBitmapManager : public cc::SharedBitmapManager {
 public:
  explicit ChildSharedBitmapManager(
      const scoped_refptr<
          cc::mojom::ThreadSafeSharedBitmapManagerAssociatedPtr>&
          shared_bitmap_manager_ptr);
  ~ChildSharedBitmapManager() override;

  // cc::SharedBitmapManager implementation.
  std::unique_ptr<cc::SharedBitmap> AllocateSharedBitmap(
      const gfx::Size& size) override;
  std::unique_ptr<cc::SharedBitmap> GetSharedBitmapFromId(
      const gfx::Size&,
      const cc::SharedBitmapId&) override;

  std::unique_ptr<cc::SharedBitmap> GetBitmapForSharedMemory(
      base::SharedMemory* mem);

 private:
  void NotifyAllocatedSharedBitmap(base::SharedMemory* memory,
                                   const cc::SharedBitmapId& id);

  scoped_refptr<cc::mojom::ThreadSafeSharedBitmapManagerAssociatedPtr>
      shared_bitmap_manager_ptr_;

  DISALLOW_COPY_AND_ASSIGN(ChildSharedBitmapManager);
};

}  // namespace ui

#endif  // SERVICES_UI_PUBLIC_CPP_BITMAP_CHILD_CHILD_SHARED_BITMAP_MANAGER_H_
