/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
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

#ifndef ParsedContentType_h
#define ParsedContentType_h

#include "platform/PlatformExport.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/text/StringHash.h"

namespace blink {

// ParsedContentType parses the constructor argument as specified in RFC2045
// and stores the result.
// FIXME: add support for comments.
class PLATFORM_EXPORT ParsedContentType final {
  STACK_ALLOCATED();

 public:
  // When |Relaxed| is specified, the parser parses parameter values in a sloppy
  // manner, i.e., only ';' and '"' are treated as special characters.
  // See https://chromiumcodereview.appspot.com/23043002.
  // When |Strict| is specified, the parser does not allow multiple values
  // for the same parameter. Some RFCs based on RFC2045 (e.g. RFC6838) note that
  // "It is an error for a specific parameter to be specified more than once."
  enum class Mode {
    kNormal,
    kRelaxed,
    kStrict,
  };
  explicit ParsedContentType(const String&, Mode = Mode::kNormal);

  String MimeType() const { return mime_type_; }
  String Charset() const;

  // Note that in the case of multiple values for the same name, the last value
  // is returned.
  String ParameterValueForName(const String&) const;
  size_t ParameterCount() const;

  bool IsValid() const { return is_valid_; }

 private:
  bool Parse(const String&);

  const Mode mode_;
  bool is_valid_;

  typedef HashMap<String, String> KeyValuePairs;
  KeyValuePairs parameters_;
  String mime_type_;
};

}  // namespace blink

#endif
