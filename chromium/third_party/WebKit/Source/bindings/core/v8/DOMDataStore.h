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

#ifndef DOMDataStore_h
#define DOMDataStore_h

#include <memory>

#include "bindings/core/v8/DOMWrapperMap.h"
#include "bindings/core/v8/DOMWrapperWorld.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "bindings/core/v8/WrapperTypeInfo.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/Optional.h"
#include "platform/wtf/StackUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "v8/include/v8.h"

namespace blink {

class DOMDataStore {
  WTF_MAKE_NONCOPYABLE(DOMDataStore);
  USING_FAST_MALLOC(DOMDataStore);

 public:
  DOMDataStore(v8::Isolate* isolate, bool is_main_world)
      : is_main_world_(is_main_world) {
    // We never use |m_wrapperMap| when it's the main world.
    if (!is_main_world)
      wrapper_map_.emplace(isolate);
  }

  static DOMDataStore& Current(v8::Isolate* isolate) {
    return DOMWrapperWorld::Current(isolate).DomDataStore();
  }

  static bool SetReturnValue(v8::ReturnValue<v8::Value> return_value,
                             ScriptWrappable* object) {
    if (CanUseMainWorldWrapper())
      return object->SetReturnValue(return_value);
    return Current(return_value.GetIsolate())
        .SetReturnValueFrom(return_value, object);
  }

  static bool SetReturnValueForMainWorld(
      v8::ReturnValue<v8::Value> return_value,
      ScriptWrappable* object) {
    return object->SetReturnValue(return_value);
  }

  static bool SetReturnValueFast(v8::ReturnValue<v8::Value> return_value,
                                 ScriptWrappable* object,
                                 v8::Local<v8::Object> holder,
                                 const ScriptWrappable* wrappable) {
    if (CanUseMainWorldWrapper()
        // The second fastest way to check if we're in the main world is to
        // check if the wrappable's wrapper is the same as the holder.
        || HolderContainsWrapper(holder, wrappable))
      return object->SetReturnValue(return_value);
    return Current(return_value.GetIsolate())
        .SetReturnValueFrom(return_value, object);
  }

  static v8::Local<v8::Object> GetWrapper(ScriptWrappable* object,
                                          v8::Isolate* isolate) {
    if (CanUseMainWorldWrapper())
      return object->MainWorldWrapper(isolate);
    return Current(isolate).Get(object, isolate);
  }

  // Associates the given |object| with the given |wrapper| if the object is
  // not yet associated with any wrapper.  Returns true if the given wrapper
  // is associated with the object, or false if the object is already
  // associated with a wrapper.  In the latter case, |wrapper| will be updated
  // to the existing wrapper.
  WARN_UNUSED_RESULT static bool SetWrapper(
      v8::Isolate* isolate,
      ScriptWrappable* object,
      const WrapperTypeInfo* wrapper_type_info,
      v8::Local<v8::Object>& wrapper) {
    if (CanUseMainWorldWrapper())
      return object->SetWrapper(isolate, wrapper_type_info, wrapper);
    return Current(isolate).Set(isolate, object, wrapper_type_info, wrapper);
  }

  static bool ContainsWrapper(ScriptWrappable* object, v8::Isolate* isolate) {
    return Current(isolate).ContainsWrapper(object);
  }

  v8::Local<v8::Object> Get(ScriptWrappable* object, v8::Isolate* isolate) {
    if (is_main_world_)
      return object->MainWorldWrapper(isolate);
    return wrapper_map_->NewLocal(isolate, object);
  }

  WARN_UNUSED_RESULT bool Set(v8::Isolate* isolate,
                              ScriptWrappable* object,
                              const WrapperTypeInfo* wrapper_type_info,
                              v8::Local<v8::Object>& wrapper) {
    DCHECK(object);
    DCHECK(!wrapper.IsEmpty());
    if (is_main_world_)
      return object->SetWrapper(isolate, wrapper_type_info, wrapper);
    return wrapper_map_->Set(object, wrapper_type_info, wrapper);
  }

  void MarkWrapper(ScriptWrappable* script_wrappable) {
    wrapper_map_->MarkWrapper(script_wrappable);
  }

  bool SetReturnValueFrom(v8::ReturnValue<v8::Value> return_value,
                          ScriptWrappable* object) {
    if (is_main_world_)
      return object->SetReturnValue(return_value);
    return wrapper_map_->SetReturnValueFrom(return_value, object);
  }

  bool ContainsWrapper(ScriptWrappable* object) {
    if (is_main_world_)
      return object->ContainsWrapper();
    return wrapper_map_->ContainsKey(object);
  }

 private:
  // We can use a wrapper stored in a ScriptWrappable when we're in the main
  // world.  This method does the fast check if we're in the main world. If this
  // method returns true, it is guaranteed that we're in the main world. On the
  // other hand, if this method returns false, nothing is guaranteed (we might
  // be in the main world).
  static bool CanUseMainWorldWrapper() {
    return !WTF::MayNotBeMainThread() &&
           !DOMWrapperWorld::NonMainWorldsExistInMainThread();
  }

  static bool HolderContainsWrapper(v8::Local<v8::Object> holder,
                                    const ScriptWrappable* wrappable) {
    // Verify our assumptions about the main world.
    ASSERT(wrappable);
    ASSERT(!wrappable->ContainsWrapper() || !wrappable->IsEqualTo(holder) ||
           Current(v8::Isolate::GetCurrent()).is_main_world_);
    return wrappable->IsEqualTo(holder);
  }

  bool is_main_world_;
  WTF::Optional<DOMWrapperMap<ScriptWrappable>> wrapper_map_;
};

template <>
inline void DOMWrapperMap<ScriptWrappable>::PersistentValueMapTraits::Dispose(
    v8::Isolate*,
    v8::Global<v8::Object> value,
    ScriptWrappable*) {
  ToWrapperTypeInfo(value)->WrapperDestroyed();
}

template <>
inline void
DOMWrapperMap<ScriptWrappable>::PersistentValueMapTraits::DisposeWeak(
    const v8::WeakCallbackInfo<WeakCallbackDataType>& data) {
  auto wrapper_type_info = reinterpret_cast<WrapperTypeInfo*>(
      data.GetInternalField(kV8DOMWrapperTypeIndex));
  wrapper_type_info->WrapperDestroyed();
}

}  // namespace blink

#endif  // DOMDataStore_h
