/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef InputMethodController_h
#define InputMethodController_h

#include "core/CoreExport.h"
#include "core/dom/Range.h"
#include "core/dom/SynchronousMutationObserver.h"
#include "core/editing/CompositionUnderline.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/PlainTextRange.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Vector.h"
#include "public/platform/WebTextInputInfo.h"
#include "public/platform/WebTextInputType.h"

namespace blink {

class Editor;
class LocalFrame;
class Range;

class CORE_EXPORT InputMethodController final
    : public GarbageCollectedFinalized<InputMethodController>,
      public SynchronousMutationObserver {
  WTF_MAKE_NONCOPYABLE(InputMethodController);
  USING_GARBAGE_COLLECTED_MIXIN(InputMethodController);

 public:
  enum ConfirmCompositionBehavior {
    kDoNotKeepSelection,
    kKeepSelection,
  };

  static InputMethodController* Create(LocalFrame&);
  virtual ~InputMethodController();
  DECLARE_TRACE();

  // international text input composition
  bool HasComposition() const;
  void SetComposition(const String& text,
                      const Vector<CompositionUnderline>& underlines,
                      int selection_start,
                      int selection_end);
  void SetCompositionFromExistingText(const Vector<CompositionUnderline>& text,
                                      unsigned composition_start,
                                      unsigned composition_end);

  // Deletes ongoing composing text if any, inserts specified text, and
  // changes the selection according to relativeCaretPosition, which is
  // relative to the end of the inserting text.
  bool CommitText(const String& text,
                  const Vector<CompositionUnderline>& underlines,
                  int relative_caret_position);

  // Inserts ongoing composing text; changes the selection to the end of
  // the inserting text if DoNotKeepSelection, or holds the selection if
  // KeepSelection.
  bool FinishComposingText(ConfirmCompositionBehavior);

  // Deletes the existing composition text.
  void CancelComposition();

  EphemeralRange CompositionEphemeralRange() const;
  Range* CompositionRange() const;

  void Clear();
  void DocumentAttached(Document*);

  PlainTextRange GetSelectionOffsets() const;
  // Returns true if setting selection to specified offsets, otherwise false.
  bool SetEditableSelectionOffsets(
      const PlainTextRange&,
      FrameSelection::SetSelectionOptions = FrameSelection::kCloseTyping);
  void ExtendSelectionAndDelete(int before, int after);
  PlainTextRange CreateRangeForSelection(int start,
                                         int end,
                                         size_t text_length) const;
  void DeleteSurroundingText(int before, int after);
  void DeleteSurroundingTextInCodePoints(int before, int after);
  WebTextInputInfo TextInputInfo() const;
  WebTextInputType TextInputType() const;

  // Call this when we will change focus.
  void WillChangeFocus();

 private:
  Document& GetDocument() const;
  bool IsAvailable() const;

  Member<LocalFrame> frame_;
  Member<Range> composition_range_;
  bool has_composition_;

  explicit InputMethodController(LocalFrame&);

  Editor& GetEditor() const;
  LocalFrame& GetFrame() const {
    DCHECK(frame_);
    return *frame_;
  }

  String ComposingText() const;
  void SelectComposition() const;

  EphemeralRange EphemeralRangeForOffsets(const PlainTextRange&) const;

  // Returns true if selection offsets were successfully set.
  bool SetSelectionOffsets(
      const PlainTextRange&,
      FrameSelection::SetSelectionOptions = FrameSelection::kCloseTyping);

  void AddCompositionUnderlines(const Vector<CompositionUnderline>& underlines,
                                ContainerNode* base_element,
                                unsigned offset_in_plain_chars);

  bool InsertText(const String&);
  bool InsertTextAndMoveCaret(const String&,
                              int relative_caret_position,
                              const Vector<CompositionUnderline>& underlines);

  // Inserts the given text string in the place of the existing composition.
  // Returns true if did replace.
  bool ReplaceComposition(const String& text);
  // Inserts the given text string in the place of the existing composition
  // and moves caret. Returns true if did replace and moved caret successfully.
  bool ReplaceCompositionAndMoveCaret(
      const String&,
      int relative_caret_position,
      const Vector<CompositionUnderline>& underlines);

  // Returns true if moved caret successfully.
  bool MoveCaret(int new_caret_position);

  PlainTextRange CreateSelectionRangeForSetComposition(
      int selection_start,
      int selection_end,
      size_t text_length) const;
  int TextInputFlags() const;
  WebTextInputMode InputModeOfFocusedElement() const;

  // Implements |SynchronousMutationObserver|.
  void ContextDestroyed(Document*) final;
};

}  // namespace blink

#endif  // InputMethodController_h
