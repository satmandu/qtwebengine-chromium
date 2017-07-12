// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_ANDROID_PAYMENT_DETAILS_VALIDATION_ANDROID_H_
#define COMPONENTS_PAYMENTS_CONTENT_ANDROID_PAYMENT_DETAILS_VALIDATION_ANDROID_H_

#include <jni.h>

namespace payments {

bool RegisterPaymentValidator(JNIEnv* env);

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_ANDROID_PAYMENT_DETAILS_VALIDATION_ANDROID_H_
