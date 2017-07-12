// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_SURFACES_SURFACE_DEPENDENCY_TRACKER_H_
#define CC_SURFACES_SURFACE_DEPENDENCY_TRACKER_H_

#include "cc/scheduler/begin_frame_source.h"
#include "cc/surfaces/pending_frame_observer.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_observer.h"
#include "cc/surfaces/surfaces_export.h"

namespace cc {

class SurfaceManager;

// SurfaceDependencyTracker tracks unresolved dependencies blocking
// CompositorFrames from activating. This class maintains a map from
// a dependent surface ID to a set of Surfaces that have CompositorFrames
// blocked on that surface ID. SurfaceDependencyTracker observes when
// dependent frames activate, and informs blocked surfaces.
//
// When a blocking CompositorFrame is first submitted, SurfaceDependencyTracker
// will begin listening for BeginFrames, setting a deadline some number of
// BeginFrames in the future. If there are unresolved dependencies when the
// deadline hits, then SurfaceDependencyTracker will clear then and activate
// all pending CompositorFrames. Once there are no more remaining pending
// frames, then SurfaceDependencyTracker will stop observing BeginFrames.
// TODO(fsamuel): Deadlines should not be global. They should be scoped to a
// surface subtree. However, that will not be possible until SurfaceReference
// work is complete.
class CC_SURFACES_EXPORT SurfaceDependencyTracker : public BeginFrameObserver,
                                                    public PendingFrameObserver,
                                                    public SurfaceObserver {
 public:
  SurfaceDependencyTracker(SurfaceManager* surface_manager,
                           BeginFrameSource* begin_frame_source);
  ~SurfaceDependencyTracker() override;

  // Called when |surface| has a pending CompositorFrame and it wishes to be
  // informed when that surface's dependencies are resolved.
  void RequestSurfaceResolution(Surface* surface);

  bool has_deadline() const { return frames_since_deadline_set_.has_value(); }

  // BeginFrameObserver implementation.
  void OnBeginFrame(const BeginFrameArgs& args) override;
  const BeginFrameArgs& LastUsedBeginFrameArgs() const override;
  void OnBeginFrameSourcePausedChanged(bool paused) override;

  // PendingFrameObserver implementation:
  void OnSurfaceActivated(Surface* surface) override;
  void OnSurfaceDependenciesChanged(
      Surface* surface,
      const SurfaceDependencies& added_dependencies,
      const SurfaceDependencies& removed_dependencies) override;
  void OnSurfaceDiscarded(Surface* surface) override;

  // SurfaceObserver implementation:
  void OnSurfaceCreated(const SurfaceInfo& surface_info) override;
  void OnSurfaceDamaged(const SurfaceId& surface_id, bool* changed) override;

 private:
  // Informs all Surfaces with pending frames blocked on the provided
  // |surface_id| that there is now an active frame available in Surface
  // corresponding to |surface_id|.
  void NotifySurfaceIdAvailable(const SurfaceId& surface_id);

  SurfaceManager* const surface_manager_;

  // The last begin frame args generated by the begin frame source.
  BeginFrameArgs last_begin_frame_args_;

  // The BeginFrameSource used to set deadlines.
  BeginFrameSource* const begin_frame_source_;

  // The number of BeginFrames observed since a deadline was set. If
  // base::nullopt_t then a deadline is not set.
  base::Optional<uint32_t> frames_since_deadline_set_;

  // A map from a SurfaceId to the set of Surfaces blocked on that SurfaceId.
  std::unordered_map<SurfaceId, base::flat_set<SurfaceId>, SurfaceIdHash>
      blocked_surfaces_from_dependency_;

  // The set of SurfaceIds corresponding to observed Surfaces that have
  // blockers.
  base::flat_set<SurfaceId> observed_surfaces_by_id_;

  // The set of SurfaceIds to which corresponding CompositorFrames have not
  // arrived by the time their deadline fired.
  base::flat_set<SurfaceId> late_surfaces_by_id_;

  DISALLOW_COPY_AND_ASSIGN(SurfaceDependencyTracker);
};

}  // namespace cc

#endif  // CC_SURFACES_SURFACE_DEPENDENCY_TRACKER_H_
