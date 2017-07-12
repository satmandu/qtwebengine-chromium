// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/AudioOutputDeviceClientImpl.h"

#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "public/web/WebFrameClient.h"
#include "web/WebLocalFrameImpl.h"
#include <memory>

namespace blink {

AudioOutputDeviceClientImpl::AudioOutputDeviceClientImpl(LocalFrame& frame)
    : AudioOutputDeviceClient(frame) {}

AudioOutputDeviceClientImpl::~AudioOutputDeviceClientImpl() {}

void AudioOutputDeviceClientImpl::CheckIfAudioSinkExistsAndIsAuthorized(
    ExecutionContext* context,
    const WebString& sink_id,
    std::unique_ptr<WebSetSinkIdCallbacks> callbacks) {
  DCHECK(context);
  DCHECK(context->IsDocument());
  Document* document = ToDocument(context);
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(document->GetFrame());
  web_frame->Client()->CheckIfAudioSinkExistsAndIsAuthorized(
      sink_id, WebSecurityOrigin(context->GetSecurityOrigin()),
      callbacks.release());
}

}  // namespace blink
