// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/gpu/DrawingBuffer.h"

#include "cc/resources/single_release_callback.h"
#include "cc/resources/texture_mailbox.h"
#include "gpu/command_buffer/client/gles2_interface_stub.h"
#include "platform/graphics/gpu/DrawingBufferTestHelpers.h"
#include "testing/gtest/include/gtest/gtest.h"

// These unit tests are separate from DrawingBufferTests.cpp because they are
// built as a part of webkit_unittests instead blink_platform_unittests. This is
// because the software rendering mode has a dependency on the blink::Platform
// interface for buffer allocations.

namespace blink {
namespace {

using namespace testing;

class WebGraphicsContext3DProviderSoftwareRenderingForTests
    : public WebGraphicsContext3DProvider {
 public:
  WebGraphicsContext3DProviderSoftwareRenderingForTests(
      std::unique_ptr<gpu::gles2::GLES2Interface> gl)
      : gl_(std::move(gl)) {}

  gpu::gles2::GLES2Interface* ContextGL() override { return gl_.get(); }
  bool IsSoftwareRendering() const override { return true; }

  // Not used by WebGL code.
  GrContext* GetGrContext() override { return nullptr; }
  bool BindToCurrentThread() override { return false; }
  gpu::Capabilities GetCapabilities() override { return gpu::Capabilities(); }
  void SetLostContextCallback(const base::Closure&) {}
  void SetErrorMessageCallback(
      const base::Callback<void(const char*, int32_t id)>&) {}
  void SignalQuery(uint32_t, const base::Closure&) override {}

 private:
  std::unique_ptr<gpu::gles2::GLES2Interface> gl_;
};

class DrawingBufferSoftwareRenderingTest : public Test {
 protected:
  void SetUp() override {
    IntSize initial_size(kInitialWidth, kInitialHeight);
    std::unique_ptr<GLES2InterfaceForTests> gl =
        WTF::WrapUnique(new GLES2InterfaceForTests);
    std::unique_ptr<WebGraphicsContext3DProviderSoftwareRenderingForTests>
        provider = WTF::WrapUnique(
            new WebGraphicsContext3DProviderSoftwareRenderingForTests(
                std::move(gl)));
    drawing_buffer_ = DrawingBufferForTests::Create(
        std::move(provider), nullptr, initial_size, DrawingBuffer::kPreserve,
        kDisableMultisampling);
    CHECK(drawing_buffer_);
  }

  RefPtr<DrawingBufferForTests> drawing_buffer_;
  bool is_software_rendering_ = false;
};

TEST_F(DrawingBufferSoftwareRenderingTest, bitmapRecycling) {
  cc::TextureMailbox texture_mailbox;
  std::unique_ptr<cc::SingleReleaseCallback> release_callback1;
  std::unique_ptr<cc::SingleReleaseCallback> release_callback2;
  std::unique_ptr<cc::SingleReleaseCallback> release_callback3;
  IntSize initial_size(kInitialWidth, kInitialHeight);
  IntSize alternate_size(kInitialWidth, kAlternateHeight);

  drawing_buffer_->Resize(initial_size);
  drawing_buffer_->MarkContentsChanged();
  drawing_buffer_->PrepareTextureMailbox(
      &texture_mailbox, &release_callback1);  // create a bitmap.
  EXPECT_EQ(0, drawing_buffer_->RecycledBitmapCount());
  release_callback1->Run(
      gpu::SyncToken(),
      false /* lostResource */);  // release bitmap to the recycling queue
  EXPECT_EQ(1, drawing_buffer_->RecycledBitmapCount());
  drawing_buffer_->MarkContentsChanged();
  drawing_buffer_->PrepareTextureMailbox(
      &texture_mailbox, &release_callback2);  // recycle a bitmap.
  EXPECT_EQ(0, drawing_buffer_->RecycledBitmapCount());
  release_callback2->Run(
      gpu::SyncToken(),
      false /* lostResource */);  // release bitmap to the recycling queue
  EXPECT_EQ(1, drawing_buffer_->RecycledBitmapCount());
  drawing_buffer_->Resize(alternate_size);
  drawing_buffer_->MarkContentsChanged();
  // Regression test for crbug.com/647896 - Next line must not crash
  drawing_buffer_->PrepareTextureMailbox(
      &texture_mailbox,
      &release_callback3);  // cause recycling queue to be purged due to resize
  EXPECT_EQ(0, drawing_buffer_->RecycledBitmapCount());
  release_callback3->Run(gpu::SyncToken(), false /* lostResource */);
  EXPECT_EQ(1, drawing_buffer_->RecycledBitmapCount());

  drawing_buffer_->BeginDestruction();
}

}  // unnamed namespace
}  // blink
