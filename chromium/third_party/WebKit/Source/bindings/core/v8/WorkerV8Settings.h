// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WorkerV8Settings_h
#define WorkerV8Settings_h

#include "bindings/core/v8/V8CacheOptions.h"
#include "core/CoreExport.h"

namespace blink {

// The V8 settings that are passed from the main isolate to the worker isolate.
struct CORE_EXPORT WorkerV8Settings {
  enum class HeapLimitMode { kDefault, kIncreasedForDebugging };
  WorkerV8Settings(HeapLimitMode heap_limit_mode,
                   V8CacheOptions v8_cache_options)
      : heap_limit_mode_(heap_limit_mode),
        v8_cache_options_(v8_cache_options) {}
  static WorkerV8Settings Default() {
    return WorkerV8Settings(HeapLimitMode::kDefault, kV8CacheOptionsDefault);
  }
  HeapLimitMode heap_limit_mode_;
  V8CacheOptions v8_cache_options_;
};

}  // namespace blink
#endif  // WorkerV8Setttings
