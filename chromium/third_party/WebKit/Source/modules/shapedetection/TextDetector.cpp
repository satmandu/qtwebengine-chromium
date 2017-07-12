// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/shapedetection/TextDetector.h"

#include "core/dom/DOMException.h"
#include "core/geometry/DOMRect.h"
#include "core/html/canvas/CanvasImageSource.h"
#include "modules/shapedetection/DetectedText.h"
#include "public/platform/InterfaceProvider.h"
#include "public/platform/Platform.h"

namespace blink {

TextDetector* TextDetector::Create() {
  return new TextDetector();
}

TextDetector::TextDetector() : ShapeDetector() {
  Platform::Current()->GetInterfaceProvider()->GetInterface(
      mojo::MakeRequest(&text_service_));
  text_service_.set_connection_error_handler(ConvertToBaseCallback(WTF::Bind(
      &TextDetector::OnTextServiceConnectionError, WrapWeakPersistent(this))));
}

ScriptPromise TextDetector::DoDetect(
    ScriptPromiseResolver* resolver,
    mojo::ScopedSharedBufferHandle shared_buffer_handle,
    int image_width,
    int image_height) {
  ScriptPromise promise = resolver->Promise();
  if (!text_service_) {
    resolver->Reject(DOMException::Create(
        kNotSupportedError, "Text detection service unavailable."));
    return promise;
  }
  text_service_requests_.insert(resolver);
  text_service_->Detect(std::move(shared_buffer_handle), image_width,
                        image_height,
                        ConvertToBaseCallback(WTF::Bind(
                            &TextDetector::OnDetectText, WrapPersistent(this),
                            WrapPersistent(resolver))));
  return promise;
}

void TextDetector::OnDetectText(
    ScriptPromiseResolver* resolver,
    Vector<shape_detection::mojom::blink::TextDetectionResultPtr>
        text_detection_results) {
  DCHECK(text_service_requests_.Contains(resolver));
  text_service_requests_.erase(resolver);

  HeapVector<Member<DetectedText>> detected_text;
  for (const auto& text : text_detection_results) {
    detected_text.push_back(DetectedText::Create(
        text->raw_value,
        DOMRect::Create(text->bounding_box->x, text->bounding_box->y,
                        text->bounding_box->width,
                        text->bounding_box->height)));
  }

  resolver->Resolve(detected_text);
}

void TextDetector::OnTextServiceConnectionError() {
  for (const auto& request : text_service_requests_) {
    request->Reject(DOMException::Create(kNotSupportedError,
                                         "Text Detection not implemented."));
  }
  text_service_requests_.Clear();
  text_service_.reset();
}

DEFINE_TRACE(TextDetector) {
  ShapeDetector::Trace(visitor);
  visitor->Trace(text_service_requests_);
}

}  // namespace blink
