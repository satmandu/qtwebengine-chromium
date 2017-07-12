/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/donottrack/NavigatorDoNotTrack.h"

#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/Navigator.h"

namespace blink {

NavigatorDoNotTrack::NavigatorDoNotTrack(Navigator& navigator)
    : Supplement<Navigator>(navigator) {}

DEFINE_TRACE(NavigatorDoNotTrack) {
  Supplement<Navigator>::Trace(visitor);
}

const char* NavigatorDoNotTrack::SupplementName() {
  return "NavigatorDoNotTrack";
}

NavigatorDoNotTrack& NavigatorDoNotTrack::From(Navigator& navigator) {
  NavigatorDoNotTrack* supplement = static_cast<NavigatorDoNotTrack*>(
      Supplement<Navigator>::From(navigator, SupplementName()));
  if (!supplement) {
    supplement = new NavigatorDoNotTrack(navigator);
    ProvideTo(navigator, SupplementName(), supplement);
  }
  return *supplement;
}

String NavigatorDoNotTrack::doNotTrack(Navigator& navigator) {
  return NavigatorDoNotTrack::From(navigator).doNotTrack();
}

String NavigatorDoNotTrack::doNotTrack() {
  LocalFrame* frame = GetSupplementable()->GetFrame();
  if (!frame || !frame->Loader().Client())
    return String();
  return frame->Loader().Client()->DoNotTrackValue();
}

}  // namespace blink
