/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#ifndef WebInputElement_h
#define WebInputElement_h

#include "WebFormControlElement.h"

namespace blink {

class HTMLInputElement;
class WebOptionElement;

// Provides readonly access to some properties of a DOM input element node.
class WebInputElement final : public WebFormControlElement {
 public:
  WebInputElement() : WebFormControlElement() {}
  WebInputElement(const WebInputElement& element)
      : WebFormControlElement(element) {}

  WebInputElement& operator=(const WebInputElement& element) {
    WebFormControlElement::Assign(element);
    return *this;
  }
  void Assign(const WebInputElement& element) {
    WebFormControlElement::Assign(element);
  }

  // This returns true for all of textfield-looking types such as text,
  // password, search, email, url, and number.
  BLINK_EXPORT bool IsTextField() const;
  // This returns true only for type=text.
  BLINK_EXPORT bool IsText() const;
  BLINK_EXPORT bool IsEmailField() const;
  BLINK_EXPORT bool IsPasswordField() const;
  BLINK_EXPORT bool IsImageButton() const;
  BLINK_EXPORT bool IsRadioButton() const;
  BLINK_EXPORT bool IsCheckbox() const;
  // This has different behavior from 'maxLength' IDL attribute, it returns
  // defaultMaxLength() when no valid has been set, whereas 'maxLength' IDL
  // attribute returns -1.
  BLINK_EXPORT int MaxLength() const;
  BLINK_EXPORT void SetActivatedSubmit(bool);
  BLINK_EXPORT int size() const;
  BLINK_EXPORT void SetChecked(bool, bool send_events = false);
  // Sets the value inside the text field without being sanitized. Can't be
  // used if a renderer doesn't exist or on a non text field type. Caret will
  // be moved to the end.
  BLINK_EXPORT void SetEditingValue(const WebString&);
  BLINK_EXPORT bool IsValidValue(const WebString&) const;
  BLINK_EXPORT bool IsChecked() const;
  BLINK_EXPORT bool IsMultiple() const;

  // Associated <datalist> options which match to the current INPUT value.
  BLINK_EXPORT WebVector<WebOptionElement> FilteredDataListOptions() const;

  // Return the localized value for this input type.
  BLINK_EXPORT WebString LocalizeValue(const WebString&) const;

  // Exposes the default value of the maxLength attribute.
  BLINK_EXPORT static int DefaultMaxLength();

  // If true, forces the text of the element to be visible.
  BLINK_EXPORT void SetShouldRevealPassword(bool value);

  // Returns true if the text of the element should be visible.
  BLINK_EXPORT bool ShouldRevealPassword() const;

#if BLINK_IMPLEMENTATION
  WebInputElement(HTMLInputElement*);
  WebInputElement& operator=(HTMLInputElement*);
  operator HTMLInputElement*() const;
#endif
};

DECLARE_WEB_NODE_TYPE_CASTS(WebInputElement);

// This returns 0 if the specified WebElement is not a WebInputElement.
BLINK_EXPORT WebInputElement* ToWebInputElement(WebElement*);
// This returns 0 if the specified WebElement is not a WebInputElement.
inline const WebInputElement* ToWebInputElement(const WebElement* element) {
  return ToWebInputElement(const_cast<WebElement*>(element));
}

}  // namespace blink

#endif
