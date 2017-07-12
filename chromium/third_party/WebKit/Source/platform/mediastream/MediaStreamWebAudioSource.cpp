/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Google Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
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

#include "platform/audio/AudioBus.h"
#include "platform/mediastream/MediaStreamWebAudioSource.h"
#include "public/platform/WebAudioSourceProvider.h"
#include <memory>

namespace blink {

MediaStreamWebAudioSource::MediaStreamWebAudioSource(
    std::unique_ptr<WebAudioSourceProvider> provider)
    : web_audio_source_provider_(std::move(provider)) {}

MediaStreamWebAudioSource::~MediaStreamWebAudioSource() {}

void MediaStreamWebAudioSource::ProvideInput(AudioBus* bus,
                                             size_t frames_to_process) {
  ASSERT(bus);
  if (!bus)
    return;

  if (!web_audio_source_provider_) {
    bus->Zero();
    return;
  }

  // Wrap the AudioBus channel data using WebVector.
  size_t n = bus->NumberOfChannels();
  WebVector<float*> web_audio_data(n);
  for (size_t i = 0; i < n; ++i)
    web_audio_data[i] = bus->Channel(i)->MutableData();

  web_audio_source_provider_->ProvideInput(web_audio_data, frames_to_process);
}

}  // namespace blink
