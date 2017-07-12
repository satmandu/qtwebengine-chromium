// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/renderer/media_capture_from_element/html_video_element_capturer_source.h"
#include "media/base/limits.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/platform/WebMediaPlayer.h"
#include "third_party/WebKit/public/platform/WebString.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::SaveArg;

namespace content {

ACTION_P(RunClosure, closure) {
  closure.Run();
}

// An almost empty WebMediaPlayer to override paint() method.
class MockWebMediaPlayer : public blink::WebMediaPlayer,
                           public base::SupportsWeakPtr<MockWebMediaPlayer> {
 public:
  MockWebMediaPlayer()  = default;
  ~MockWebMediaPlayer() override = default;

  void Load(LoadType, const blink::WebMediaPlayerSource&, CORSMode) override {}
  void Play() override {}
  void Pause() override {}
  bool SupportsSave() const override { return true; }
  void Seek(double seconds) override {}
  void SetRate(double) override {}
  void SetVolume(double) override {}
  blink::WebTimeRanges Buffered() const override {
    return blink::WebTimeRanges();
  }
  blink::WebTimeRanges Seekable() const override {
    return blink::WebTimeRanges();
  }
  void SetSinkId(const blink::WebString& sinkId,
                 const blink::WebSecurityOrigin&,
                 blink::WebSetSinkIdCallbacks*) override {}
  bool HasVideo() const override { return true; }
  bool HasAudio() const override { return false; }
  blink::WebSize NaturalSize() const override { return blink::WebSize(16, 10); }
  bool Paused() const override { return false; }
  bool Seeking() const override { return false; }
  double Duration() const override { return 0.0; }
  double CurrentTime() const override { return 0.0; }
  NetworkState GetNetworkState() const override { return kNetworkStateEmpty; }
  ReadyState GetReadyState() const override { return kReadyStateHaveNothing; }
  blink::WebString GetErrorMessage() const override {
    return blink::WebString();
  }

  bool DidLoadingProgress() override { return true; }
  bool HasSingleSecurityOrigin() const override { return true; }
  bool DidPassCORSAccessCheck() const override { return true; }
  double MediaTimeForTimeValue(double timeValue) const override { return 0.0; }
  unsigned DecodedFrameCount() const override { return 0; }
  unsigned DroppedFrameCount() const override { return 0; }
  unsigned CorruptedFrameCount() const override { return 0; }
  size_t AudioDecodedByteCount() const override { return 0; }
  size_t VideoDecodedByteCount() const override { return 0; }

  void Paint(blink::WebCanvas* canvas,
             const blink::WebRect& paint_rectangle,
             cc::PaintFlags&) override {
    // We could fill in |canvas| with a meaningful pattern in ARGB and verify
    // that is correctly captured (as I420) by HTMLVideoElementCapturerSource
    // but I don't think that'll be easy/useful/robust, so just let go here.
    return;
  }
};

class HTMLVideoElementCapturerSourceTest : public testing::Test {
 public:
  HTMLVideoElementCapturerSourceTest()
      : web_media_player_(new MockWebMediaPlayer()),
        html_video_capturer_(new HtmlVideoElementCapturerSource(
            web_media_player_->AsWeakPtr(),
            base::ThreadTaskRunnerHandle::Get())) {}

  // Necessary callbacks and MOCK_METHODS for them.
  MOCK_METHOD2(DoOnDeliverFrame,
               void(const scoped_refptr<media::VideoFrame>&, base::TimeTicks));
  void OnDeliverFrame(const scoped_refptr<media::VideoFrame>& video_frame,
                    base::TimeTicks estimated_capture_time) {
    DoOnDeliverFrame(video_frame, estimated_capture_time);
  }

  MOCK_METHOD1(DoOnVideoCaptureDeviceFormats,
               void(const media::VideoCaptureFormats&));
  void OnVideoCaptureDeviceFormats(const media::VideoCaptureFormats& formats) {
    DoOnVideoCaptureDeviceFormats(formats);
  }

  MOCK_METHOD1(DoOnRunning, void(bool));
  void OnRunning(bool state) { DoOnRunning(state); }

 protected:
  // We need some kind of message loop to allow |html_video_capturer_| to
  // schedule capture events.
  const base::MessageLoopForUI message_loop_;

  std::unique_ptr<MockWebMediaPlayer> web_media_player_;
  std::unique_ptr<HtmlVideoElementCapturerSource> html_video_capturer_;
};

// Constructs and destructs all objects, in particular |html_video_capturer_|
// and its inner object(s). This is a non trivial sequence.
TEST_F(HTMLVideoElementCapturerSourceTest, ConstructAndDestruct) {}

// Checks that the usual sequence of GetCurrentSupportedFormats() ->
// StartCapture() -> StopCapture() works as expected and let it capture two
// frames.
TEST_F(HTMLVideoElementCapturerSourceTest, GetFormatsAndStartAndStop) {
  InSequence s;
  media::VideoCaptureFormats formats;
  EXPECT_CALL(*this, DoOnVideoCaptureDeviceFormats(_))
      .Times(1)
      .WillOnce(SaveArg<0>(&formats));

  html_video_capturer_->GetCurrentSupportedFormats(
      media::limits::kMaxCanvas /* max_requesteed_width */,
      media::limits::kMaxCanvas /* max_requesteed_height */,
      media::limits::kMaxFramesPerSecond /* max_requested_frame_rate */,
      base::Bind(
          &HTMLVideoElementCapturerSourceTest::OnVideoCaptureDeviceFormats,
          base::Unretained(this)));
  ASSERT_EQ(1u, formats.size());
  EXPECT_EQ(web_media_player_->NaturalSize().width,
            formats[0].frame_size.width());
  EXPECT_EQ(web_media_player_->NaturalSize().height,
            formats[0].frame_size.height());

  media::VideoCaptureParams params;
  params.requested_format = formats[0];

  EXPECT_CALL(*this, DoOnRunning(true)).Times(1);

  base::RunLoop run_loop;
  base::Closure quit_closure = run_loop.QuitClosure();
  EXPECT_CALL(*this, DoOnDeliverFrame(_, _)).Times(1);
  EXPECT_CALL(*this, DoOnDeliverFrame(_, _))
      .Times(1)
      .WillOnce(RunClosure(quit_closure));

  html_video_capturer_->StartCapture(
      params, base::Bind(&HTMLVideoElementCapturerSourceTest::OnDeliverFrame,
                         base::Unretained(this)),
      base::Bind(&HTMLVideoElementCapturerSourceTest::OnRunning,
                 base::Unretained(this)));

  run_loop.Run();

  html_video_capturer_->StopCapture();
  Mock::VerifyAndClearExpectations(this);
}

}  // namespace content
