/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bindings/core/v8/V8PerIsolateData.h"

#include <memory>

#include "bindings/core/v8/DOMDataStore.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/V8Binding.h"
#include "bindings/core/v8/V8ObjectConstructor.h"
#include "bindings/core/v8/V8PrivateProperty.h"
#include "bindings/core/v8/V8ScriptRunner.h"
#include "bindings/core/v8/V8ValueCache.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/wtf/LeakAnnotations.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/Platform.h"
#include "v8/include/v8-debug.h"

namespace blink {

static V8PerIsolateData* g_main_thread_per_isolate_data = 0;

static void BeforeCallEnteredCallback(v8::Isolate* isolate) {
  RELEASE_ASSERT(!ScriptForbiddenScope::IsScriptForbidden());
}

static void MicrotasksCompletedCallback(v8::Isolate* isolate) {
  V8PerIsolateData::From(isolate)->RunEndOfScopeTasks();
}

V8PerIsolateData::V8PerIsolateData(WebTaskRunner* task_runner)
    : isolate_holder_(
          task_runner ? task_runner->ToSingleThreadTaskRunner() : nullptr,
          gin::IsolateHolder::kSingleThread,
          IsMainThread() ? gin::IsolateHolder::kDisallowAtomicsWait
                         : gin::IsolateHolder::kAllowAtomicsWait),
      string_cache_(WTF::WrapUnique(new StringCache(GetIsolate()))),
      private_property_(V8PrivateProperty::Create()),
      constructor_mode_(ConstructorMode::kCreateNewObject),
      use_counter_disabled_(false),
      is_handling_recursion_level_error_(false),
      is_reporting_exception_(false) {
  // FIXME: Remove once all v8::Isolate::GetCurrent() calls are gone.
  GetIsolate()->Enter();
  GetIsolate()->AddBeforeCallEnteredCallback(&BeforeCallEnteredCallback);
  GetIsolate()->AddMicrotasksCompletedCallback(&MicrotasksCompletedCallback);
  if (IsMainThread())
    g_main_thread_per_isolate_data = this;
}

V8PerIsolateData::~V8PerIsolateData() {}

v8::Isolate* V8PerIsolateData::MainThreadIsolate() {
  ASSERT(g_main_thread_per_isolate_data);
  return g_main_thread_per_isolate_data->GetIsolate();
}

v8::Isolate* V8PerIsolateData::Initialize(WebTaskRunner* task_runner) {
  V8PerIsolateData* data = new V8PerIsolateData(task_runner);
  v8::Isolate* isolate = data->GetIsolate();
  isolate->SetData(gin::kEmbedderBlink, data);
  return isolate;
}

void V8PerIsolateData::EnableIdleTasks(
    v8::Isolate* isolate,
    std::unique_ptr<gin::V8IdleTaskRunner> task_runner) {
  From(isolate)->isolate_holder_.EnableIdleTasks(std::move(task_runner));
}

v8::Persistent<v8::Value>& V8PerIsolateData::EnsureLiveRoot() {
  if (live_root_.IsEmpty())
    live_root_.Set(GetIsolate(), v8::Null(GetIsolate()));
  return live_root_.Get();
}

// willBeDestroyed() clear things that should be cleared before
// ThreadState::detach() gets called.
void V8PerIsolateData::WillBeDestroyed(v8::Isolate* isolate) {
  V8PerIsolateData* data = From(isolate);

  data->thread_debugger_.reset();
  // Clear any data that may have handles into the heap,
  // prior to calling ThreadState::detach().
  data->ClearEndOfScopeTasks();

  data->active_script_wrappables_.Clear();
}

// destroy() clear things that should be cleared after ThreadState::detach()
// gets called but before the Isolate exits.
void V8PerIsolateData::Destroy(v8::Isolate* isolate) {
  isolate->RemoveBeforeCallEnteredCallback(&BeforeCallEnteredCallback);
  isolate->RemoveMicrotasksCompletedCallback(&MicrotasksCompletedCallback);
  V8PerIsolateData* data = From(isolate);

  // Clear everything before exiting the Isolate.
  if (data->script_regexp_script_state_)
    data->script_regexp_script_state_->DisposePerContextData();
  data->live_root_.Clear();
  data->private_property_.reset();
  data->string_cache_->Dispose();
  data->string_cache_.reset();
  data->interface_template_map_for_non_main_world_.Clear();
  data->interface_template_map_for_main_world_.Clear();
  data->operation_template_map_for_non_main_world_.Clear();
  data->operation_template_map_for_main_world_.Clear();
  if (IsMainThread())
    g_main_thread_per_isolate_data = 0;

  // FIXME: Remove once all v8::Isolate::GetCurrent() calls are gone.
  isolate->Exit();
  delete data;
}

V8PerIsolateData::V8FunctionTemplateMap&
V8PerIsolateData::SelectInterfaceTemplateMap(const DOMWrapperWorld& world) {
  return world.IsMainWorld() ? interface_template_map_for_main_world_
                             : interface_template_map_for_non_main_world_;
}

V8PerIsolateData::V8FunctionTemplateMap&
V8PerIsolateData::SelectOperationTemplateMap(const DOMWrapperWorld& world) {
  return world.IsMainWorld() ? operation_template_map_for_main_world_
                             : operation_template_map_for_non_main_world_;
}

v8::Local<v8::FunctionTemplate> V8PerIsolateData::FindOrCreateOperationTemplate(
    const DOMWrapperWorld& world,
    const void* key,
    v8::FunctionCallback callback,
    v8::Local<v8::Value> data,
    v8::Local<v8::Signature> signature,
    int length) {
  auto& map = SelectOperationTemplateMap(world);
  auto result = map.Find(key);
  if (result != map.end())
    return result->value.Get(GetIsolate());

  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(
      GetIsolate(), callback, data, signature, length);
  templ->RemovePrototype();
  map.insert(key, v8::Eternal<v8::FunctionTemplate>(GetIsolate(), templ));
  return templ;
}

v8::Local<v8::FunctionTemplate> V8PerIsolateData::FindInterfaceTemplate(
    const DOMWrapperWorld& world,
    const void* key) {
  auto& map = SelectInterfaceTemplateMap(world);
  auto result = map.Find(key);
  if (result != map.end())
    return result->value.Get(GetIsolate());
  return v8::Local<v8::FunctionTemplate>();
}

void V8PerIsolateData::SetInterfaceTemplate(
    const DOMWrapperWorld& world,
    const void* key,
    v8::Local<v8::FunctionTemplate> value) {
  auto& map = SelectInterfaceTemplateMap(world);
  map.insert(key, v8::Eternal<v8::FunctionTemplate>(GetIsolate(), value));
}

const v8::Eternal<v8::Name>* V8PerIsolateData::FindOrCreateEternalNameCache(
    const void* lookup_key,
    const char* const names[],
    size_t count) {
  auto it = eternal_name_cache_.Find(lookup_key);
  const Vector<v8::Eternal<v8::Name>>* vector = nullptr;
  if (UNLIKELY(it == eternal_name_cache_.end())) {
    v8::Isolate* isolate = this->GetIsolate();
    Vector<v8::Eternal<v8::Name>> new_vector(count);
    std::transform(
        names, names + count, new_vector.begin(), [isolate](const char* name) {
          return v8::Eternal<v8::Name>(isolate, V8AtomicString(isolate, name));
        });
    vector = &eternal_name_cache_.Set(lookup_key, std::move(new_vector))
                  .stored_value->value;
  } else {
    vector = &it->value;
  }
  DCHECK_EQ(vector->size(), count);
  return vector->Data();
}

v8::Local<v8::Context> V8PerIsolateData::EnsureScriptRegexpContext() {
  if (!script_regexp_script_state_) {
    LEAK_SANITIZER_DISABLED_SCOPE;
    v8::Local<v8::Context> context(v8::Context::New(GetIsolate()));
    script_regexp_script_state_ = ScriptState::Create(
        context, DOMWrapperWorld::Create(GetIsolate(),
                                         DOMWrapperWorld::WorldType::kRegExp));
  }
  return script_regexp_script_state_->GetContext();
}

void V8PerIsolateData::ClearScriptRegexpContext() {
  if (script_regexp_script_state_)
    script_regexp_script_state_->DisposePerContextData();
  script_regexp_script_state_.Clear();
}

bool V8PerIsolateData::HasInstance(
    const WrapperTypeInfo* untrusted_wrapper_type_info,
    v8::Local<v8::Value> value) {
  return HasInstance(untrusted_wrapper_type_info, value,
                     interface_template_map_for_main_world_) ||
         HasInstance(untrusted_wrapper_type_info, value,
                     interface_template_map_for_non_main_world_);
}

bool V8PerIsolateData::HasInstance(
    const WrapperTypeInfo* untrusted_wrapper_type_info,
    v8::Local<v8::Value> value,
    V8FunctionTemplateMap& map) {
  auto result = map.Find(untrusted_wrapper_type_info);
  if (result == map.end())
    return false;
  v8::Local<v8::FunctionTemplate> templ = result->value.Get(GetIsolate());
  return templ->HasInstance(value);
}

v8::Local<v8::Object> V8PerIsolateData::FindInstanceInPrototypeChain(
    const WrapperTypeInfo* info,
    v8::Local<v8::Value> value) {
  v8::Local<v8::Object> wrapper = FindInstanceInPrototypeChain(
      info, value, interface_template_map_for_main_world_);
  if (!wrapper.IsEmpty())
    return wrapper;
  return FindInstanceInPrototypeChain(
      info, value, interface_template_map_for_non_main_world_);
}

v8::Local<v8::Object> V8PerIsolateData::FindInstanceInPrototypeChain(
    const WrapperTypeInfo* info,
    v8::Local<v8::Value> value,
    V8FunctionTemplateMap& map) {
  if (value.IsEmpty() || !value->IsObject())
    return v8::Local<v8::Object>();
  auto result = map.Find(info);
  if (result == map.end())
    return v8::Local<v8::Object>();
  v8::Local<v8::FunctionTemplate> templ = result->value.Get(GetIsolate());
  return v8::Local<v8::Object>::Cast(value)->FindInstanceInPrototypeChain(
      templ);
}

void V8PerIsolateData::AddEndOfScopeTask(std::unique_ptr<EndOfScopeTask> task) {
  end_of_scope_tasks_.push_back(std::move(task));
}

void V8PerIsolateData::RunEndOfScopeTasks() {
  Vector<std::unique_ptr<EndOfScopeTask>> tasks;
  tasks.Swap(end_of_scope_tasks_);
  for (const auto& task : tasks)
    task->Run();
  ASSERT(end_of_scope_tasks_.IsEmpty());
}

void V8PerIsolateData::ClearEndOfScopeTasks() {
  end_of_scope_tasks_.Clear();
}

void V8PerIsolateData::SetThreadDebugger(
    std::unique_ptr<V8PerIsolateData::Data> thread_debugger) {
  ASSERT(!thread_debugger_);
  thread_debugger_ = std::move(thread_debugger);
}

V8PerIsolateData::Data* V8PerIsolateData::ThreadDebugger() {
  return thread_debugger_.get();
}

void V8PerIsolateData::AddActiveScriptWrappable(
    ActiveScriptWrappableBase* wrappable) {
  if (!active_script_wrappables_)
    active_script_wrappables_ = new ActiveScriptWrappableSet();

  active_script_wrappables_->insert(wrappable);
}

void V8PerIsolateData::TemporaryScriptWrappableVisitorScope::
    SwapWithV8PerIsolateDataVisitor(
        std::unique_ptr<ScriptWrappableVisitor>& visitor) {
  ScriptWrappableVisitor* current = CurrentVisitor();
  if (current)
    current->PerformCleanup();

  V8PerIsolateData::From(isolate_)->script_wrappable_visitor_.swap(
      saved_visitor_);
  isolate_->SetEmbedderHeapTracer(CurrentVisitor());
}

}  // namespace blink
