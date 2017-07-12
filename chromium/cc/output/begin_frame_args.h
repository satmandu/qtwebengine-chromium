// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_OUTPUT_BEGIN_FRAME_ARGS_H_
#define CC_OUTPUT_BEGIN_FRAME_ARGS_H_

#include <stdint.h>
#include <memory>

#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "base/values.h"
#include "cc/cc_export.h"

namespace base {
namespace trace_event {
class ConvertableToTraceFormat;
class TracedValue;
}
}

/**
 * In debug builds we trace the creation origin of BeginFrameArgs objects. We
 * reuse the tracked_objects::Location system to do that.
 *
 * However, in release builds we don't want this as it doubles the size of the
 * BeginFrameArgs object. As well it adds a number of largish strings to the
 * binary. Despite the argument being unused, most compilers are unable to
 * optimise it away even when unused. Instead we use the BEGINFRAME_FROM_HERE
 * macro to prevent the data even getting referenced.
 */
#ifdef NDEBUG
#define BEGINFRAME_FROM_HERE nullptr
#else
#define BEGINFRAME_FROM_HERE FROM_HERE
#endif

namespace cc {

struct CC_EXPORT BeginFrameArgs {
  enum BeginFrameArgsType {
    INVALID,
    NORMAL,
    MISSED,
    // Not a real type, but used by the IPC system. Should always remain the
    // *last* value in this enum.
    BEGIN_FRAME_ARGS_TYPE_MAX,
  };
  static const char* TypeToString(BeginFrameArgsType type);

  static constexpr uint32_t kStartingSourceId = 0;
  // |source_id| for BeginFrameArgs not created by a BeginFrameSource. Used to
  // avoid sequence number conflicts of BeginFrameArgs manually fed to an
  // observer with those fed to the observer by the its BeginFrameSource.
  static constexpr uint32_t kManualSourceId = UINT32_MAX;

  static constexpr uint64_t kInvalidFrameNumber = 0;
  static constexpr uint64_t kStartingFrameNumber = 1;

  // Creates an invalid set of values.
  BeginFrameArgs();

#ifdef NDEBUG
  typedef const void* CreationLocation;
#else
  typedef const tracked_objects::Location& CreationLocation;
  tracked_objects::Location created_from;
#endif

  // You should be able to find all instances where a BeginFrame has been
  // created by searching for "BeginFrameArgs::Create".
  // The location argument should **always** be BEGINFRAME_FROM_HERE macro.
  static BeginFrameArgs Create(CreationLocation location,
                               uint32_t source_id,
                               uint64_t sequence_number,
                               base::TimeTicks frame_time,
                               base::TimeTicks deadline,
                               base::TimeDelta interval,
                               BeginFrameArgsType type);

  // This is the default delta that will be used to adjust the deadline when
  // proper draw-time estimations are not yet available.
  static base::TimeDelta DefaultEstimatedParentDrawTime();

  // This is the default interval to use to avoid sprinkling the code with
  // magic numbers.
  static base::TimeDelta DefaultInterval();

  bool IsValid() const { return interval >= base::TimeDelta(); }

  std::unique_ptr<base::trace_event::ConvertableToTraceFormat> AsValue() const;
  void AsValueInto(base::trace_event::TracedValue* dict) const;

  base::TimeTicks frame_time;
  base::TimeTicks deadline;
  base::TimeDelta interval;

  // |source_id| and |sequence_number| identify a BeginFrame within a single
  // process and are set by the original BeginFrameSource that created the
  // BeginFrameArgs. When |source_id| of consecutive BeginFrameArgs changes,
  // observers should expect the continuity of |sequence_number| to break.
  uint64_t sequence_number;
  uint32_t source_id;  // |source_id| after |sequence_number| for packing.

  BeginFrameArgsType type;
  bool on_critical_path;

 private:
  BeginFrameArgs(uint32_t source_id,
                 uint64_t sequence_number,
                 base::TimeTicks frame_time,
                 base::TimeTicks deadline,
                 base::TimeDelta interval,
                 BeginFrameArgsType type);
};

// Sent by a BeginFrameObserver as acknowledgment of completing a BeginFrame.
struct CC_EXPORT BeginFrameAck {
  BeginFrameAck();
  BeginFrameAck(uint32_t source_id,
                uint64_t sequence_number,
                uint64_t latest_confirmed_sequence_number,
                bool has_damage);

  // Creates a BeginFrameAck for a manual BeginFrame. Used when clients produce
  // a CompositorFrame without prior BeginFrame, e.g. for synchronous drawing.
  static BeginFrameAck CreateManualAckWithDamage();

  // Sequence number of the BeginFrame that is acknowledged.
  uint64_t sequence_number;

  // Sequence number of the latest BeginFrame that was positively acknowledged
  // (confirmed) by the observer.
  //
  // There are two scenarios for a positive acknowledgment:
  //  a) All of the observer's pending updates led to successful damage (e.g. a
  //     CompositorFrame or a damaged surface).
  //  b) The observer did not have any updates and thus did not need to
  //     produce damage.
  // A negative acknowledgment, in contrast, describes a situation in which the
  // observer had pending updates, but was unable to successfully produce
  // corresponding damage for all its updates in time.
  //
  // As a result, |latest_confirmed_sequence_number| describes the "staleness"
  // of the last damage that was produced by the observer. Note that even if
  // |has_damage == true|, the damage produced as a result of the acknowledged
  // BeginFrame may be stale
  // (|latest_confirmed_sequence_number < sequence_number|). In such a case, the
  // damage that was produced may contain updates from previous BeginFrames or
  // only part of this BeginFrame's updates.
  //
  // Observers aggregate the |latest_confirmed_sequence_number| of their
  // children: The compositor Scheduler indicates the latest BeginFrame that
  // both impl and main thread confirmed. Likewise, the DisplayScheduler
  // indicates the minimum |latest_confirmed_sequence_number| that all its
  // BeginFrameObservers acknowledged.
  // TODO(eseckler): Set this in Scheduler / DisplayScheduler and other
  // observers according to above comment.
  uint64_t latest_confirmed_sequence_number;

  // Source identifier of the BeginFrame that is acknowledged. The
  // BeginFrameSource that receives the acknowledgment uses this to discard
  // BeginFrameAcks for BeginFrames sent by a different source. Such a situation
  // may occur when the BeginFrameSource of the observer changes while a
  // BeginFrame from the old source is still in flight.
  uint32_t source_id;  // |source_id| after above fields for packing.

  // |true| if the observer has produced damage (e.g. sent a CompositorFrame or
  // damaged a surface) as part of responding to the BeginFrame.
  bool has_damage;
};

}  // namespace cc

#endif  // CC_OUTPUT_BEGIN_FRAME_ARGS_H_
