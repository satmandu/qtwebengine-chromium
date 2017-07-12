// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/sensor/Gyroscope.h"


using device::mojom::blink::SensorType;

namespace blink {

Gyroscope* Gyroscope::Create(ExecutionContext* execution_context,
                             const SensorOptions& options,
                             ExceptionState& exception_state) {
  return new Gyroscope(execution_context, options, exception_state);
}

// static
Gyroscope* Gyroscope::Create(ExecutionContext* execution_context,
                             ExceptionState& exception_state) {
  return Create(execution_context, SensorOptions(), exception_state);
}

Gyroscope::Gyroscope(ExecutionContext* execution_context,
                     const SensorOptions& options,
                     ExceptionState& exception_state)
    : Sensor(execution_context,
             options,
             exception_state,
             SensorType::GYROSCOPE) {}

double Gyroscope::x(bool& is_null) const {
  return ReadingValue(0, is_null);
}

double Gyroscope::y(bool& is_null) const {
  return ReadingValue(1, is_null);
}

double Gyroscope::z(bool& is_null) const {
  return ReadingValue(2, is_null);
}

DEFINE_TRACE(Gyroscope) {
  Sensor::Trace(visitor);
}

}  // namespace blink
