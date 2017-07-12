// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CanvasAsyncBlobCreator_h
#define CanvasAsyncBlobCreator_h

#include <memory>
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/CoreExport.h"
#include "core/dom/DOMTypedArray.h"
#include "core/fileapi/BlobCallback.h"
#include "core/workers/ParentFrameTaskRunners.h"
#include "platform/geometry/IntSize.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebTraceLocation.h"

namespace blink {

class Document;
class JPEGImageEncoderState;
class PNGImageEncoderState;

class CORE_EXPORT CanvasAsyncBlobCreator
    : public GarbageCollectedFinalized<CanvasAsyncBlobCreator> {
 public:
  static CanvasAsyncBlobCreator* Create(
      DOMUint8ClampedArray* unpremultiplied_rgba_image_data,
      const String& mime_type,
      const IntSize&,
      BlobCallback*,
      double start_time,
      Document*);
  static CanvasAsyncBlobCreator* Create(
      DOMUint8ClampedArray* unpremultiplied_rgba_image_data,
      const String& mime_type,
      const IntSize&,
      double start_time,
      Document*,
      ScriptPromiseResolver*);
  void ScheduleAsyncBlobCreation(const double& quality);
  virtual ~CanvasAsyncBlobCreator();
  enum MimeType {
    kMimeTypePng,
    kMimeTypeJpeg,
    kMimeTypeWebp,
    kNumberOfMimeTypeSupported
  };
  // This enum is used to back an UMA histogram, and should therefore be treated
  // as append-only.
  enum IdleTaskStatus {
    kIdleTaskNotStarted,
    kIdleTaskStarted,
    kIdleTaskCompleted,
    kIdleTaskFailed,
    kIdleTaskSwitchedToImmediateTask,
    kIdleTaskNotSupported,  // Idle tasks are not implemented for some image
                            // types
    kIdleTaskCount,         // Should not be seen in production
  };

  enum ToBlobFunctionType {
    kHTMLCanvasToBlobCallback,
    kOffscreenCanvasToBlobPromise,
    kNumberOfToBlobFunctionTypes
  };

  // Methods are virtual for mocking in unit tests
  virtual void SignalTaskSwitchInStartTimeoutEventForTesting() {}
  virtual void SignalTaskSwitchInCompleteTimeoutEventForTesting() {}

  DECLARE_VIRTUAL_TRACE();

 protected:
  CanvasAsyncBlobCreator(DOMUint8ClampedArray* data,
                         MimeType,
                         const IntSize&,
                         BlobCallback*,
                         double,
                         Document*,
                         ScriptPromiseResolver*);
  // Methods are virtual for unit testing
  virtual void ScheduleInitiatePngEncoding();
  virtual void ScheduleInitiateJpegEncoding(const double&);
  virtual void IdleEncodeRowsPng(double deadline_seconds);
  virtual void IdleEncodeRowsJpeg(double deadline_seconds);
  virtual void PostDelayedTaskToCurrentThread(const WebTraceLocation&,
                                              std::unique_ptr<WTF::Closure>,
                                              double delay_ms);
  virtual void SignalAlternativeCodePathFinishedForTesting() {}
  virtual void CreateBlobAndReturnResult();
  virtual void CreateNullAndReturnResult();

  void InitiatePngEncoding(double deadline_seconds);
  void InitiateJpegEncoding(const double& quality, double deadline_seconds);
  IdleTaskStatus idle_task_status_;

 private:
  friend class CanvasAsyncBlobCreatorTest;

  void Dispose();

  std::unique_ptr<PNGImageEncoderState> png_encoder_state_;
  std::unique_ptr<JPEGImageEncoderState> jpeg_encoder_state_;
  Member<DOMUint8ClampedArray> data_;
  std::unique_ptr<Vector<unsigned char>> encoded_image_;
  int num_rows_completed_;
  Member<Document> document_;

  const IntSize size_;
  size_t pixel_row_stride_;
  const MimeType mime_type_;
  double start_time_;
  double schedule_initiate_start_time_;
  double elapsed_time_;

  ToBlobFunctionType function_type_;

  // Used when CanvasAsyncBlobCreator runs on main thread only
  Member<ParentFrameTaskRunners> parent_frame_task_runner_;

  // Used for HTMLCanvasElement only
  Member<BlobCallback> callback_;

  // Used for OffscreenCanvas only
  Member<ScriptPromiseResolver> script_promise_resolver_;

  // PNG
  bool InitializePngStruct();
  void ForceEncodeRowsPngOnCurrentThread();  // Similar to idleEncodeRowsPng
                                             // without deadline

  // JPEG
  bool InitializeJpegStruct(double quality);
  void ForceEncodeRowsJpegOnCurrentThread();  // Similar to idleEncodeRowsJpeg
                                              // without
                                              // deadline

  // WEBP
  void EncodeImageOnEncoderThread(double quality);

  void IdleTaskStartTimeoutEvent(double quality);
  void IdleTaskCompleteTimeoutEvent();
};

}  // namespace blink

#endif  // CanvasAsyncBlobCreator_h
