// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/pipeline_controller.h"

#include "base/bind.h"
#include "media/base/demuxer.h"

namespace media {

PipelineController::PipelineController(
    std::unique_ptr<Pipeline> pipeline,
    const RendererFactoryCB& renderer_factory_cb,
    const SeekedCB& seeked_cb,
    const SuspendedCB& suspended_cb,
    const BeforeResumeCB& before_resume_cb,
    const ResumedCB& resumed_cb,
    const PipelineStatusCB& error_cb)
    : pipeline_(std::move(pipeline)),
      renderer_factory_cb_(renderer_factory_cb),
      seeked_cb_(seeked_cb),
      suspended_cb_(suspended_cb),
      before_resume_cb_(before_resume_cb),
      resumed_cb_(resumed_cb),
      error_cb_(error_cb),
      weak_factory_(this) {
  DCHECK(pipeline_);
  DCHECK(!renderer_factory_cb_.is_null());
  DCHECK(!seeked_cb_.is_null());
  DCHECK(!suspended_cb_.is_null());
  DCHECK(!before_resume_cb_.is_null());
  DCHECK(!resumed_cb_.is_null());
  DCHECK(!error_cb_.is_null());
}

PipelineController::~PipelineController() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

// TODO(sandersd): If there is a pending suspend, don't call pipeline_->Start()
// until Resume().
void PipelineController::Start(Demuxer* demuxer,
                               Pipeline::Client* client,
                               bool is_streaming,
                               bool is_static) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(state_ == State::CREATED);
  DCHECK(demuxer);

  // Once the pipeline is started, we want to call the seeked callback but
  // without a time update.
  pending_seeked_cb_ = true;
  state_ = State::STARTING;

  demuxer_ = demuxer;
  is_streaming_ = is_streaming;
  is_static_ = is_static;
  pipeline_->Start(demuxer, renderer_factory_cb_.Run(), client,
                   base::Bind(&PipelineController::OnPipelineStatus,
                              weak_factory_.GetWeakPtr(), State::PLAYING));
}

void PipelineController::Seek(base::TimeDelta time, bool time_updated) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // It would be slightly more clear to set this in Dispatch(), but we want to
  // be sure it gets updated even if the seek is elided.
  if (time_updated)
    pending_time_updated_ = true;
  pending_seeked_cb_ = true;

  // If we are already seeking to |time|, and the media is static, elide the
  // seek.
  if ((state_ == State::SEEKING || state_ == State::RESUMING) &&
      seek_time_ == time && is_static_) {
    pending_seek_ = false;
    return;
  }

  pending_seek_time_ = time;
  pending_seek_ = true;
  Dispatch();
}

// TODO(sandersd): It may be easier to use this interface if |suspended_cb_| is
// executed when Suspend() is called while already suspended.
void PipelineController::Suspend() {
  DCHECK(thread_checker_.CalledOnValidThread());
  pending_resume_ = false;
  if (state_ != State::SUSPENDING && state_ != State::SUSPENDED) {
    pending_suspend_ = true;
    Dispatch();
  }
}

void PipelineController::Resume() {
  DCHECK(thread_checker_.CalledOnValidThread());
  pending_suspend_ = false;
  if (state_ == State::SUSPENDING || state_ == State::SUSPENDED) {
    pending_resume_ = true;
    Dispatch();
  }
}

bool PipelineController::IsStable() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return state_ == State::PLAYING;
}

bool PipelineController::IsSuspended() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return (pending_suspend_ || state_ == State::SUSPENDING ||
          state_ == State::SUSPENDED) &&
         !pending_resume_;
}

bool PipelineController::IsPipelineSuspended() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return state_ == State::SUSPENDED;
}

void PipelineController::OnPipelineStatus(State state,
                                          PipelineStatus pipeline_status) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (pipeline_status != PIPELINE_OK) {
    error_cb_.Run(pipeline_status);
    return;
  }

  State old_state = state_;
  state_ = state;

  if (state == State::PLAYING) {
    // Start(), Seek(), or Resume() completed; we can be sure that
    // |demuxer_| got the seek it was waiting for.
    waiting_for_seek_ = false;

    // TODO(avayvod): Remove resumed callback after https://crbug.com/678374 is
    // properly fixed.
    if (old_state == State::RESUMING)
      resumed_cb_.Run();
  } else if (state == State::SUSPENDED) {
    // Warning: possibly reentrant. The state may change inside this callback.
    // It must be safe to call Dispatch() twice in a row here.
    suspended_cb_.Run();
  }

  Dispatch();
}

// Note: Dispatch() may be called re-entrantly (by callbacks internally) or
// twice in a row (by OnPipelineStatus()).
void PipelineController::Dispatch() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Suspend/resume transitions take priority because seeks before a suspend
  // are wasted, and seeks after can be merged into the resume operation.
  if (pending_suspend_ && state_ == State::PLAYING) {
    pending_suspend_ = false;
    state_ = State::SUSPENDING;
    pipeline_->Suspend(base::Bind(&PipelineController::OnPipelineStatus,
                                  weak_factory_.GetWeakPtr(),
                                  State::SUSPENDED));
    return;
  }

  if (pending_resume_ && state_ == State::SUSPENDED) {
    // If there is a pending seek, resume to that time instead...
    if (pending_seek_) {
      seek_time_ = pending_seek_time_;
      pending_seek_ = false;
    } else {
      seek_time_ = pipeline_->GetMediaTime();
    }

    // ...unless the media is streaming, in which case we resume at the start
    // because seeking doesn't work well.
    if (is_streaming_ && !seek_time_.is_zero()) {
      seek_time_ = base::TimeDelta();

      // In this case we want to make sure that the controls get updated
      // immediately, so we don't try to hide the seek.
      pending_time_updated_ = true;
    }

    // Tell |demuxer_| to expect our resume.
    DCHECK(!waiting_for_seek_);
    waiting_for_seek_ = true;
    demuxer_->StartWaitingForSeek(seek_time_);

    pending_resume_ = false;
    state_ = State::RESUMING;
    before_resume_cb_.Run();
    pipeline_->Resume(renderer_factory_cb_.Run(), seek_time_,
                      base::Bind(&PipelineController::OnPipelineStatus,
                                 weak_factory_.GetWeakPtr(), State::PLAYING));
    return;
  }

  // If we have pending operations, and a seek is ongoing, abort it.
  if ((pending_seek_ || pending_suspend_) && waiting_for_seek_) {
    // If there is no pending seek, return the current seek to pending status.
    if (!pending_seek_) {
      pending_seek_time_ = seek_time_;
      pending_seek_ = true;
    }

    // CancelPendingSeek() may be reentrant, so update state first and return
    // immediately.
    waiting_for_seek_ = false;
    demuxer_->CancelPendingSeek(pending_seek_time_);
    return;
  }

  // Ordinary seeking.
  if (pending_seek_ && state_ == State::PLAYING) {
    seek_time_ = pending_seek_time_;

    // Tell |demuxer_| to expect our seek.
    DCHECK(!waiting_for_seek_);
    waiting_for_seek_ = true;
    demuxer_->StartWaitingForSeek(seek_time_);

    pending_seek_ = false;
    state_ = State::SEEKING;
    pipeline_->Seek(seek_time_,
                    base::Bind(&PipelineController::OnPipelineStatus,
                               weak_factory_.GetWeakPtr(), State::PLAYING));
    return;
  }

  // If |state_| is PLAYING and we didn't trigger an operation above then we
  // are in a stable state. If there is a seeked callback pending, emit it.
  if (state_ == State::PLAYING) {
    if (pending_seeked_cb_) {
      // |seeked_cb_| may be reentrant, so update state first and return
      // immediately.
      pending_seeked_cb_ = false;
      bool was_pending_time_updated = pending_time_updated_;
      pending_time_updated_ = false;
      seeked_cb_.Run(was_pending_time_updated);
      return;
    }
  }
}

void PipelineController::Stop() {
  // For the moment, Stop() is only called on WMPI destruction, and updating the
  // state of |this| is not relevant. Eventually, Start()/Stop() will be called
  // in order to swap between demuxer types, and this will need to be adressed.
  //
  // TODO(tguilbert): Clarify the appropriate state changes when Stop() is
  // called. See crbug.com/695734.
  pipeline_->Stop();
}

bool PipelineController::IsPipelineRunning() const {
  return pipeline_->IsRunning();
}

double PipelineController::GetPlaybackRate() const {
  return pipeline_->GetPlaybackRate();
}

void PipelineController::SetPlaybackRate(double playback_rate) {
  pipeline_->SetPlaybackRate(playback_rate);
}

float PipelineController::GetVolume() const {
  return pipeline_->GetVolume();
}

void PipelineController::SetVolume(float volume) {
  pipeline_->SetVolume(volume);
}

base::TimeDelta PipelineController::GetMediaTime() const {
  return pipeline_->GetMediaTime();
}

Ranges<base::TimeDelta> PipelineController::GetBufferedTimeRanges() const {
  return pipeline_->GetBufferedTimeRanges();
}

base::TimeDelta PipelineController::GetMediaDuration() const {
  return pipeline_->GetMediaDuration();
}

bool PipelineController::DidLoadingProgress() {
  return pipeline_->DidLoadingProgress();
}

PipelineStatistics PipelineController::GetStatistics() const {
  return pipeline_->GetStatistics();
}

void PipelineController::SetCdm(CdmContext* cdm_context,
                                const CdmAttachedCB& cdm_attached_cb) {
  pipeline_->SetCdm(cdm_context, cdm_attached_cb);
}

void PipelineController::OnEnabledAudioTracksChanged(
    const std::vector<MediaTrack::Id>& enabledTrackIds) {
  pipeline_->OnEnabledAudioTracksChanged(enabledTrackIds);
}

void PipelineController::OnSelectedVideoTrackChanged(
    base::Optional<MediaTrack::Id> selected_track_id) {
  pipeline_->OnSelectedVideoTrackChanged(selected_track_id);
}

}  // namespace media
