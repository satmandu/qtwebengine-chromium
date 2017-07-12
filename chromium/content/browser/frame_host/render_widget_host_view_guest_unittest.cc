// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/render_widget_host_view_guest.h"

#include <stdint.h>
#include <utility>

#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/scoped_task_scheduler.h"
#include "build/build_config.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/surfaces/surface_sequence.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/compositor/test/no_transport_image_transport_factory.h"
#include "content/browser/gpu/compositor_util.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_plugin_guest_delegate.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_context.h"
#include "content/test/fake_renderer_compositor_frame_sink.h"
#include "content/test/test_render_view_host.h"
#include "content/test/test_web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/compositor.h"

namespace content {
namespace {
class MockRenderWidgetHostDelegate : public RenderWidgetHostDelegate {
 public:
  MockRenderWidgetHostDelegate() {}
  ~MockRenderWidgetHostDelegate() override {}

 private:
  // RenderWidgetHostDelegate:
  void Cut() override {}
  void Copy() override {}
  void Paste() override {}
  void SelectAll() override {}
};

class RenderWidgetHostViewGuestTest : public testing::Test {
 public:
  RenderWidgetHostViewGuestTest() : task_scheduler_(&message_loop_) {}

  void SetUp() override {
#if !defined(OS_ANDROID)
    ImageTransportFactory::InitializeForUnitTests(
        std::unique_ptr<ImageTransportFactory>(
            new NoTransportImageTransportFactory));
#endif
    browser_context_.reset(new TestBrowserContext);
    MockRenderProcessHost* process_host =
        new MockRenderProcessHost(browser_context_.get());
    int32_t routing_id = process_host->GetNextRoutingID();
    widget_host_ =
        new RenderWidgetHostImpl(&delegate_, process_host, routing_id, false);
    view_ = RenderWidgetHostViewGuest::Create(
        widget_host_, NULL,
        (new TestRenderWidgetHostView(widget_host_))->GetWeakPtr());
  }

  void TearDown() override {
    if (view_)
      view_->Destroy();
    delete widget_host_;

    browser_context_.reset();

    message_loop_.task_runner()->DeleteSoon(FROM_HERE,
                                            browser_context_.release());
    base::RunLoop().RunUntilIdle();
#if !defined(OS_ANDROID)
    ImageTransportFactory::Terminate();
#endif
  }

 protected:
  base::MessageLoopForUI message_loop_;

  // Needed by base::PostTaskWithTraits in RenderWidgetHostImpl constructor.
  base::test::ScopedTaskScheduler task_scheduler_;

  std::unique_ptr<BrowserContext> browser_context_;
  MockRenderWidgetHostDelegate delegate_;

  // Tests should set these to NULL if they've already triggered their
  // destruction.
  RenderWidgetHostImpl* widget_host_;
  RenderWidgetHostViewGuest* view_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewGuestTest);
};

}  // namespace

TEST_F(RenderWidgetHostViewGuestTest, VisibilityTest) {
  view_->Show();
  ASSERT_TRUE(view_->IsShowing());

  view_->Hide();
  ASSERT_FALSE(view_->IsShowing());
}

class TestBrowserPluginGuest : public BrowserPluginGuest {
 public:
  TestBrowserPluginGuest(WebContentsImpl* web_contents,
                         BrowserPluginGuestDelegate* delegate)
      : BrowserPluginGuest(web_contents->HasOpener(), web_contents, delegate) {}

  ~TestBrowserPluginGuest() override {}

  void ResetTestData() { last_surface_info_ = cc::SurfaceInfo(); }

  void set_has_attached_since_surface_set(bool has_attached_since_surface_set) {
    BrowserPluginGuest::set_has_attached_since_surface_set_for_test(
        has_attached_since_surface_set);
  }

  void set_attached(bool attached) {
    BrowserPluginGuest::set_attached_for_test(attached);
  }

  void SetChildFrameSurface(const cc::SurfaceInfo& surface_info,
                            const cc::SurfaceSequence& sequence) override {
    last_surface_info_ = surface_info;
  }

  cc::SurfaceInfo last_surface_info_;
};

// TODO(wjmaclean): we should restructure RenderWidgetHostViewChildFrameTest to
// look more like this one, and then this one could be derived from it. Also,
// include CreateDelegatedFrame as part of the test class so we don't have to
// repeat it here.
class RenderWidgetHostViewGuestSurfaceTest
    : public testing::Test {
 public:
  RenderWidgetHostViewGuestSurfaceTest()
      : widget_host_(nullptr), view_(nullptr) {}

  void SetUp() override {
#if !defined(OS_ANDROID)
    ImageTransportFactory::InitializeForUnitTests(
        std::unique_ptr<ImageTransportFactory>(
            new NoTransportImageTransportFactory));
#endif
    browser_context_.reset(new TestBrowserContext);
    MockRenderProcessHost* process_host =
        new MockRenderProcessHost(browser_context_.get());
    web_contents_.reset(
        TestWebContents::Create(browser_context_.get(), nullptr));
    // We don't own the BPG, the WebContents does.
    browser_plugin_guest_ = new TestBrowserPluginGuest(
        web_contents_.get(), &browser_plugin_guest_delegate_);

    int32_t routing_id = process_host->GetNextRoutingID();
    widget_host_ =
        new RenderWidgetHostImpl(&delegate_, process_host, routing_id, false);
    view_ = RenderWidgetHostViewGuest::Create(
        widget_host_, browser_plugin_guest_,
        (new TestRenderWidgetHostView(widget_host_))->GetWeakPtr());
    cc::mojom::MojoCompositorFrameSinkPtr sink;
    cc::mojom::MojoCompositorFrameSinkRequest sink_request =
        mojo::MakeRequest(&sink);
    cc::mojom::MojoCompositorFrameSinkClientRequest client_request =
        mojo::MakeRequest(&renderer_compositor_frame_sink_ptr_);
    renderer_compositor_frame_sink_ =
        base::MakeUnique<FakeRendererCompositorFrameSink>(
            std::move(sink), std::move(client_request));
    view_->DidCreateNewRendererCompositorFrameSink(
        renderer_compositor_frame_sink_ptr_.get());
  }

  void TearDown() override {
    if (view_)
      view_->Destroy();
    delete widget_host_;

    // It's important to make sure that the view finishes destructing before
    // we hit the destructor for the TestBrowserThreadBundle, so run the message
    // loop here.
    base::RunLoop().RunUntilIdle();
#if !defined(OS_ANDROID)
    ImageTransportFactory::Terminate();
#endif
  }

  cc::SurfaceId GetSurfaceId() const {
    DCHECK(view_);
    RenderWidgetHostViewChildFrame* rwhvcf =
        static_cast<RenderWidgetHostViewChildFrame*>(view_);
    if (!rwhvcf->local_surface_id_.is_valid())
      return cc::SurfaceId();
    return cc::SurfaceId(rwhvcf->frame_sink_id_, rwhvcf->local_surface_id_);
  }

 protected:
  TestBrowserThreadBundle thread_bundle_;
  std::unique_ptr<BrowserContext> browser_context_;
  MockRenderWidgetHostDelegate delegate_;
  BrowserPluginGuestDelegate browser_plugin_guest_delegate_;
  std::unique_ptr<TestWebContents> web_contents_;
  TestBrowserPluginGuest* browser_plugin_guest_;

  // Tests should set these to NULL if they've already triggered their
  // destruction.
  RenderWidgetHostImpl* widget_host_;
  RenderWidgetHostViewGuest* view_;
  std::unique_ptr<FakeRendererCompositorFrameSink>
      renderer_compositor_frame_sink_;

 private:
  cc::mojom::MojoCompositorFrameSinkClientPtr
      renderer_compositor_frame_sink_ptr_;
  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewGuestSurfaceTest);
};

namespace {
cc::CompositorFrame CreateDelegatedFrame(float scale_factor,
                                         gfx::Size size,
                                         const gfx::Rect& damage) {
  cc::CompositorFrame frame;
  frame.metadata.device_scale_factor = scale_factor;
  frame.metadata.begin_frame_ack = cc::BeginFrameAck(0, 1, 1, true);

  std::unique_ptr<cc::RenderPass> pass = cc::RenderPass::Create();
  pass->SetNew(1, gfx::Rect(size), damage, gfx::Transform());
  frame.render_pass_list.push_back(std::move(pass));
  return frame;
}
}  // anonymous namespace

TEST_F(RenderWidgetHostViewGuestSurfaceTest, TestGuestSurface) {
  gfx::Size view_size(100, 100);
  gfx::Rect view_rect(view_size);
  float scale_factor = 1.f;
  cc::LocalSurfaceId local_surface_id(1, base::UnguessableToken::Create());

  ASSERT_TRUE(browser_plugin_guest_);

  view_->SetSize(view_size);
  view_->Show();

  browser_plugin_guest_->set_attached(true);
  view_->SubmitCompositorFrame(
      local_surface_id,
      CreateDelegatedFrame(scale_factor, view_size, view_rect));

  cc::SurfaceId id = GetSurfaceId();

  EXPECT_TRUE(id.is_valid());

#if !defined(OS_ANDROID)
  cc::SurfaceManager* manager = ImageTransportFactory::GetInstance()
                                    ->GetContextFactoryPrivate()
                                    ->GetSurfaceManager();
  cc::Surface* surface = manager->GetSurfaceForId(id);
  EXPECT_TRUE(surface);
  // There should be a SurfaceSequence created by the RWHVGuest.
  EXPECT_EQ(1u, surface->GetDestructionDependencyCount());
#endif
  // Surface ID should have been passed to BrowserPluginGuest to
  // be sent to the embedding renderer.
  EXPECT_EQ(cc::SurfaceInfo(id, scale_factor, view_size),
            browser_plugin_guest_->last_surface_info_);

  browser_plugin_guest_->ResetTestData();
  browser_plugin_guest_->set_has_attached_since_surface_set(true);

  view_->SubmitCompositorFrame(
      local_surface_id,
      CreateDelegatedFrame(scale_factor, view_size, view_rect));

  // Since we have not changed the frame size and scale factor, the same surface
  // id must be used.
  DCHECK_EQ(id, GetSurfaceId());

#if !defined(OS_ANDROID)
  surface = manager->GetSurfaceForId(id);
  EXPECT_TRUE(surface);
  // Another SurfaceSequence should be created by the RWHVGuest when sending
  // SurfaceInfo to the embedder.
  EXPECT_EQ(2u, surface->GetDestructionDependencyCount());
#endif
  // Surface ID should have been passed to BrowserPluginGuest to
  // be sent to the embedding renderer.
  EXPECT_EQ(cc::SurfaceInfo(id, scale_factor, view_size),
            browser_plugin_guest_->last_surface_info_);

  browser_plugin_guest_->set_attached(false);
  browser_plugin_guest_->ResetTestData();

  view_->SubmitCompositorFrame(
      local_surface_id,
      CreateDelegatedFrame(scale_factor, view_size, view_rect));
  // Since guest is not attached, the CompositorFrame must be processed but the
  // frame must be evicted to return the resources immediately.
  EXPECT_FALSE(view_->has_frame());
}

}  // namespace content
