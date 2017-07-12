/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
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

#ifndef MutationRecord_h
#define MutationRecord_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

class Node;
class QualifiedName;
template <typename NodeType>
class StaticNodeTypeList;
using StaticNodeList = StaticNodeTypeList<Node>;

class MutationRecord : public GarbageCollectedFinalized<MutationRecord>,
                       public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static MutationRecord* CreateChildList(Node* target,
                                         StaticNodeList* added,
                                         StaticNodeList* removed,
                                         Node* previous_sibling,
                                         Node* next_sibling);
  static MutationRecord* CreateAttributes(Node* target,
                                          const QualifiedName&,
                                          const AtomicString& old_value);
  static MutationRecord* CreateCharacterData(Node* target,
                                             const String& old_value);
  static MutationRecord* CreateWithNullOldValue(MutationRecord*);

  MutationRecord() {}

  virtual ~MutationRecord();

  virtual const AtomicString& type() = 0;
  virtual Node* target() = 0;

  virtual StaticNodeList* addedNodes() = 0;
  virtual StaticNodeList* removedNodes() = 0;
  virtual Node* previousSibling() { return 0; }
  virtual Node* nextSibling() { return 0; }

  virtual const AtomicString& attributeName() { return g_null_atom; }
  virtual const AtomicString& attributeNamespace() { return g_null_atom; }

  virtual String oldValue() { return String(); }

  DEFINE_INLINE_VIRTUAL_TRACE() {}
};

}  // namespace blink

#endif  // MutationRecord_h
