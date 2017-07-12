// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_RECORDER_VPX_ENCODER_H_
#define CONTENT_RENDERER_MEDIA_RECORDER_VPX_ENCODER_H_

#include <vector>

#include "content/renderer/media_recorder/video_track_recorder.h"

extern "C" {
// VPX_CODEC_DISABLE_COMPAT excludes parts of the libvpx API that provide
// backwards compatibility for legacy applications using the library.
#define VPX_CODEC_DISABLE_COMPAT 1
#include "third_party/libvpx/source/libvpx/vpx/vp8cx.h"
#include "third_party/libvpx/source/libvpx/vpx/vpx_encoder.h"
}

namespace content {

// Class encapsulating all libvpx interactions for VP8/VP9 encoding.
class VpxEncoder final : public VideoTrackRecorder::Encoder {
 public:
  // Originally from remoting/codec/scoped_vpx_codec.h.
  // TODO(mcasas): Refactor into a common location.
  struct VpxCodecDeleter {
    void operator()(vpx_codec_ctx_t* codec);
  };
  typedef std::unique_ptr<vpx_codec_ctx_t, VpxCodecDeleter>
      ScopedVpxCodecCtxPtr;

  static void ShutdownEncoder(std::unique_ptr<base::Thread> encoding_thread,
                              ScopedVpxCodecCtxPtr encoder);

  VpxEncoder(
      bool use_vp9,
      const VideoTrackRecorder::OnEncodedVideoCB& on_encoded_video_callback,
      int32_t bits_per_second);

 private:
  // VideoTrackRecorder::Encoder implementation.
  ~VpxEncoder() override;
  void EncodeOnEncodingTaskRunner(scoped_refptr<media::VideoFrame> frame,
                                  base::TimeTicks capture_timestamp) override;
  bool CanEncodeAlphaChannel() override;

  void ConfigureEncoderOnEncodingTaskRunner(const gfx::Size& size,
                                            vpx_codec_enc_cfg_t* codec_config,
                                            ScopedVpxCodecCtxPtr* encoder);
  void DoEncode(vpx_codec_ctx_t* const encoder,
                const gfx::Size& frame_size,
                uint8_t* const data,
                uint8_t* const y_plane,
                int y_stride,
                uint8_t* const u_plane,
                int u_stride,
                uint8_t* const v_plane,
                int v_stride,
                const base::TimeDelta& duration,
                bool force_keyframe,
                std::string* const output_data,
                bool* const keyframe);

  // Returns true if |codec_config| has been filled in at least once.
  bool IsInitialized(const vpx_codec_enc_cfg_t& codec_config) const;

  // Estimate the frame duration from |frame| and |last_frame_timestamp_|.
  base::TimeDelta EstimateFrameDuration(
      const scoped_refptr<media::VideoFrame>& frame);

  // Force usage of VP9 for encoding, instead of VP8 which is the default.
  const bool use_vp9_;

  // VPx internal objects: configuration and encoder. |encoder_| is a special
  // scoped pointer to guarantee proper destruction, particularly when
  // reconfiguring due to parameters change. Only used on
  // VideoTrackRecorder::Encoder::encoding_thread_.
  vpx_codec_enc_cfg_t codec_config_;
  ScopedVpxCodecCtxPtr encoder_;

  vpx_codec_enc_cfg_t alpha_codec_config_;
  ScopedVpxCodecCtxPtr alpha_encoder_;

  std::vector<uint8_t> alpha_dummy_planes_;
  size_t v_plane_offset_;
  size_t u_plane_stride_;
  size_t v_plane_stride_;
  bool last_frame_had_alpha_ = false;

  // The |media::VideoFrame::timestamp()| of the last encoded frame.  This is
  // used to predict the duration of the next frame. Only used on
  // VideoTrackRecorder::Encoder::encoding_thread_.
  base::TimeDelta last_frame_timestamp_;

  DISALLOW_COPY_AND_ASSIGN(VpxEncoder);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_RECORDER_VPX_ENCODER_H_
