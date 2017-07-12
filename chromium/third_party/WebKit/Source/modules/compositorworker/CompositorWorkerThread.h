// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CompositorWorkerThread_h
#define CompositorWorkerThread_h

#include "modules/ModulesExport.h"
#include "modules/compositorworker/AbstractAnimationWorkletThread.h"
#include <memory>

namespace blink {

class InProcessWorkerObjectProxy;

class MODULES_EXPORT CompositorWorkerThread final
    : public AbstractAnimationWorkletThread {
 public:
  static std::unique_ptr<CompositorWorkerThread> Create(
      PassRefPtr<WorkerLoaderProxy>,
      InProcessWorkerObjectProxy&,
      double time_origin);
  ~CompositorWorkerThread() override;

  InProcessWorkerObjectProxy& WorkerObjectProxy() const {
    return worker_object_proxy_;
  }

 protected:
  WorkerOrWorkletGlobalScope* CreateWorkerGlobalScope(
      std::unique_ptr<WorkerThreadStartupData>) override;

 private:
  CompositorWorkerThread(PassRefPtr<WorkerLoaderProxy>,
                         InProcessWorkerObjectProxy&,
                         double time_origin);

  InProcessWorkerObjectProxy& worker_object_proxy_;
  double time_origin_;
};

}  // namespace blink

#endif  // CompositorWorkerThread_h
