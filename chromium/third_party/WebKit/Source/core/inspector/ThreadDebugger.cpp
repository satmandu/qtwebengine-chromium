// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/inspector/ThreadDebugger.h"

#include "bindings/core/v8/SourceLocation.h"
#include "bindings/core/v8/V8Binding.h"
#include "bindings/core/v8/V8DOMException.h"
#include "bindings/core/v8/V8DOMTokenList.h"
#include "bindings/core/v8/V8Event.h"
#include "bindings/core/v8/V8EventListener.h"
#include "bindings/core/v8/V8EventListenerHelper.h"
#include "bindings/core/v8/V8EventListenerInfo.h"
#include "bindings/core/v8/V8HTMLAllCollection.h"
#include "bindings/core/v8/V8HTMLCollection.h"
#include "bindings/core/v8/V8Node.h"
#include "bindings/core/v8/V8NodeList.h"
#include "bindings/core/v8/V8ScriptRunner.h"
#include "core/dom/DocumentUserGestureToken.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/InspectorDOMDebuggerAgent.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/inspector/V8InspectorString.h"
#include "platform/ScriptForbiddenScope.h"
#include "wtf/CurrentTime.h"
#include "wtf/PtrUtil.h"
#include <memory>

namespace blink {

ThreadDebugger::ThreadDebugger(v8::Isolate* isolate)
    : isolate_(isolate),
      v8_inspector_(v8_inspector::V8Inspector::create(isolate, this)),
      v8_tracing_cpu_profiler_(v8::TracingCpuProfiler::Create(isolate)) {}

ThreadDebugger::~ThreadDebugger() {}

// static
ThreadDebugger* ThreadDebugger::From(v8::Isolate* isolate) {
  if (!isolate)
    return nullptr;
  V8PerIsolateData* data = V8PerIsolateData::From(isolate);
  return data ? static_cast<ThreadDebugger*>(data->ThreadDebugger()) : nullptr;
}

// static
MessageLevel ThreadDebugger::V8MessageLevelToMessageLevel(
    v8::Isolate::MessageErrorLevel level) {
  MessageLevel result = kInfoMessageLevel;
  switch (level) {
    case v8::Isolate::kMessageDebug:
      result = kVerboseMessageLevel;
      break;
    case v8::Isolate::kMessageWarning:
      result = kWarningMessageLevel;
      break;
    case v8::Isolate::kMessageError:
      result = kErrorMessageLevel;
      break;
    case v8::Isolate::kMessageLog:
    case v8::Isolate::kMessageInfo:
    default:
      result = kInfoMessageLevel;
      break;
  }
  return result;
}

void ThreadDebugger::IdleStarted(v8::Isolate* isolate) {
  if (ThreadDebugger* debugger = ThreadDebugger::From(isolate))
    debugger->GetV8Inspector()->idleStarted();
}

void ThreadDebugger::IdleFinished(v8::Isolate* isolate) {
  if (ThreadDebugger* debugger = ThreadDebugger::From(isolate))
    debugger->GetV8Inspector()->idleFinished();
}

void ThreadDebugger::AsyncTaskScheduled(const String& operation_name,
                                        void* task,
                                        bool recurring) {
  v8_inspector_->asyncTaskScheduled(ToV8InspectorStringView(operation_name),
                                    task, recurring);
}

void ThreadDebugger::AsyncTaskCanceled(void* task) {
  v8_inspector_->asyncTaskCanceled(task);
}

void ThreadDebugger::AllAsyncTasksCanceled() {
  v8_inspector_->allAsyncTasksCanceled();
}

void ThreadDebugger::AsyncTaskStarted(void* task) {
  v8_inspector_->asyncTaskStarted(task);
}

void ThreadDebugger::AsyncTaskFinished(void* task) {
  v8_inspector_->asyncTaskFinished(task);
}

unsigned ThreadDebugger::PromiseRejected(
    v8::Local<v8::Context> context,
    const String& error_message,
    v8::Local<v8::Value> exception,
    std::unique_ptr<SourceLocation> location) {
  const String default_message = "Uncaught (in promise)";
  String message = error_message;
  if (message.IsEmpty())
    message = default_message;
  else if (message.StartsWith("Uncaught "))
    message = message.Substring(0, 8) + " (in promise)" + message.Substring(8);

  ReportConsoleMessage(ToExecutionContext(context), kJSMessageSource,
                       kErrorMessageLevel, message, location.get());
  String url = location->Url();
  return GetV8Inspector()->exceptionThrown(
      context, ToV8InspectorStringView(default_message), exception,
      ToV8InspectorStringView(message), ToV8InspectorStringView(url),
      location->LineNumber(), location->ColumnNumber(),
      location->TakeStackTrace(), location->ScriptId());
}

void ThreadDebugger::PromiseRejectionRevoked(v8::Local<v8::Context> context,
                                             unsigned promise_rejection_id) {
  const String message = "Handler added to rejected promise";
  GetV8Inspector()->exceptionRevoked(context, promise_rejection_id,
                                     ToV8InspectorStringView(message));
}

void ThreadDebugger::beginUserGesture() {
  user_gesture_indicator_ = WTF::WrapUnique(
      new UserGestureIndicator(DocumentUserGestureToken::Create(nullptr)));
}

void ThreadDebugger::endUserGesture() {
  user_gesture_indicator_.reset();
}

std::unique_ptr<v8_inspector::StringBuffer> ThreadDebugger::valueSubtype(
    v8::Local<v8::Value> value) {
  static const char kNode[] = "node";
  static const char kArray[] = "array";
  static const char kError[] = "error";
  if (V8Node::hasInstance(value, isolate_))
    return ToV8InspectorStringBuffer(kNode);
  if (V8NodeList::hasInstance(value, isolate_) ||
      V8DOMTokenList::hasInstance(value, isolate_) ||
      V8HTMLCollection::hasInstance(value, isolate_) ||
      V8HTMLAllCollection::hasInstance(value, isolate_)) {
    return ToV8InspectorStringBuffer(kArray);
  }
  if (V8DOMException::hasInstance(value, isolate_))
    return ToV8InspectorStringBuffer(kError);
  return nullptr;
}

bool ThreadDebugger::formatAccessorsAsProperties(v8::Local<v8::Value> value) {
  return V8DOMWrapper::IsWrapper(isolate_, value);
}

double ThreadDebugger::currentTimeMS() {
  return WTF::CurrentTimeMS();
}

bool ThreadDebugger::isInspectableHeapObject(v8::Local<v8::Object> object) {
  if (object->InternalFieldCount() < kV8DefaultWrapperInternalFieldCount)
    return true;
  v8::Local<v8::Value> wrapper =
      object->GetInternalField(kV8DOMWrapperObjectIndex);
  // Skip wrapper boilerplates which are like regular wrappers but don't have
  // native object.
  if (!wrapper.IsEmpty() && wrapper->IsUndefined())
    return false;
  return true;
}

static void ReturnDataCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(info.Data());
}

static v8::Maybe<bool> CreateDataProperty(v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> object,
                                          v8::Local<v8::Name> key,
                                          v8::Local<v8::Value> value) {
  v8::TryCatch try_catch(context->GetIsolate());
  v8::Isolate::DisallowJavascriptExecutionScope throw_js(
      context->GetIsolate(),
      v8::Isolate::DisallowJavascriptExecutionScope::THROW_ON_FAILURE);
  return object->CreateDataProperty(context, key, value);
}

static void CreateFunctionPropertyWithData(v8::Local<v8::Context> context,
                                           v8::Local<v8::Object> object,
                                           const char* name,
                                           v8::FunctionCallback callback,
                                           v8::Local<v8::Value> data,
                                           const char* description) {
  v8::Local<v8::String> func_name = V8String(context->GetIsolate(), name);
  v8::Local<v8::Function> func;
  if (!v8::Function::New(context, callback, data, 0,
                         v8::ConstructorBehavior::kThrow)
           .ToLocal(&func))
    return;
  func->SetName(func_name);
  v8::Local<v8::String> return_value =
      V8String(context->GetIsolate(), description);
  v8::Local<v8::Function> to_string_function;
  if (v8::Function::New(context, ReturnDataCallback, return_value, 0,
                        v8::ConstructorBehavior::kThrow)
          .ToLocal(&to_string_function))
    CreateDataProperty(context, func,
                       V8String(context->GetIsolate(), "toString"),
                       to_string_function);
  CreateDataProperty(context, object, func_name, func);
}

v8::Maybe<bool> ThreadDebugger::CreateDataPropertyInArray(
    v8::Local<v8::Context> context,
    v8::Local<v8::Array> array,
    int index,
    v8::Local<v8::Value> value) {
  v8::TryCatch try_catch(context->GetIsolate());
  v8::Isolate::DisallowJavascriptExecutionScope throw_js(
      context->GetIsolate(),
      v8::Isolate::DisallowJavascriptExecutionScope::THROW_ON_FAILURE);
  return array->CreateDataProperty(context, index, value);
}

void ThreadDebugger::CreateFunctionProperty(v8::Local<v8::Context> context,
                                            v8::Local<v8::Object> object,
                                            const char* name,
                                            v8::FunctionCallback callback,
                                            const char* description) {
  CreateFunctionPropertyWithData(context, object, name, callback,
                                 v8::External::New(context->GetIsolate(), this),
                                 description);
}

void ThreadDebugger::installAdditionalCommandLineAPI(
    v8::Local<v8::Context> context,
    v8::Local<v8::Object> object) {
  CreateFunctionProperty(
      context, object, "getEventListeners",
      ThreadDebugger::GetEventListenersCallback,
      "function getEventListeners(node) { [Command Line API] }");

  v8::Local<v8::Value> function_value;
  bool success =
      V8ScriptRunner::CompileAndRunInternalScript(
          V8String(isolate_, "(function(e) { console.log(e.type, e); })"),
          isolate_)
          .ToLocal(&function_value) &&
      function_value->IsFunction();
  DCHECK(success);
  CreateFunctionPropertyWithData(
      context, object, "monitorEvents", ThreadDebugger::MonitorEventsCallback,
      function_value,
      "function monitorEvents(object, [types]) { [Command Line API] }");
  CreateFunctionPropertyWithData(
      context, object, "unmonitorEvents",
      ThreadDebugger::UnmonitorEventsCallback, function_value,
      "function unmonitorEvents(object, [types]) { [Command Line API] }");
}

static Vector<String> NormalizeEventTypes(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  Vector<String> types;
  if (info.Length() > 1 && info[1]->IsString())
    types.push_back(ToCoreString(info[1].As<v8::String>()));
  if (info.Length() > 1 && info[1]->IsArray()) {
    v8::Local<v8::Array> types_array = v8::Local<v8::Array>::Cast(info[1]);
    for (size_t i = 0; i < types_array->Length(); ++i) {
      v8::Local<v8::Value> type_value;
      if (!types_array->Get(info.GetIsolate()->GetCurrentContext(), i)
               .ToLocal(&type_value) ||
          !type_value->IsString())
        continue;
      types.push_back(ToCoreString(v8::Local<v8::String>::Cast(type_value)));
    }
  }
  if (info.Length() == 1)
    types.AppendVector(
        Vector<String>({"mouse",   "key",          "touch",
                        "pointer", "control",      "load",
                        "unload",  "abort",        "error",
                        "select",  "input",        "change",
                        "submit",  "reset",        "focus",
                        "blur",    "resize",       "scroll",
                        "search",  "devicemotion", "deviceorientation"}));

  Vector<String> output_types;
  for (size_t i = 0; i < types.size(); ++i) {
    if (types[i] == "mouse")
      output_types.AppendVector(
          Vector<String>({"auxclick", "click", "dblclick", "mousedown",
                          "mouseeenter", "mouseleave", "mousemove", "mouseout",
                          "mouseover", "mouseup", "mouseleave", "mousewheel"}));
    else if (types[i] == "key")
      output_types.AppendVector(
          Vector<String>({"keydown", "keyup", "keypress", "textInput"}));
    else if (types[i] == "touch")
      output_types.AppendVector(Vector<String>(
          {"touchstart", "touchmove", "touchend", "touchcancel"}));
    else if (types[i] == "pointer")
      output_types.AppendVector(Vector<String>(
          {"pointerover", "pointerout", "pointerenter", "pointerleave",
           "pointerdown", "pointerup", "pointermove", "pointercancel",
           "gotpointercapture", "lostpointercapture"}));
    else if (types[i] == "control")
      output_types.AppendVector(
          Vector<String>({"resize", "scroll", "zoom", "focus", "blur", "select",
                          "input", "change", "submit", "reset"}));
    else
      output_types.push_back(types[i]);
  }
  return output_types;
}

static EventTarget* FirstArgumentAsEventTarget(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  if (info.Length() < 1)
    return nullptr;
  if (EventTarget* target =
          V8EventTarget::toImplWithTypeCheck(info.GetIsolate(), info[0]))
    return target;
  return ToDOMWindow(info.GetIsolate(), info[0]);
}

void ThreadDebugger::SetMonitorEventsCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    bool enabled) {
  EventTarget* event_target = FirstArgumentAsEventTarget(info);
  if (!event_target)
    return;
  Vector<String> types = NormalizeEventTypes(info);
  EventListener* event_listener = V8EventListenerHelper::GetEventListener(
      ScriptState::Current(info.GetIsolate()),
      v8::Local<v8::Function>::Cast(info.Data()), false,
      enabled ? kListenerFindOrCreate : kListenerFindOnly);
  if (!event_listener)
    return;
  for (size_t i = 0; i < types.size(); ++i) {
    if (enabled)
      event_target->addEventListener(AtomicString(types[i]), event_listener,
                                     false);
    else
      event_target->removeEventListener(AtomicString(types[i]), event_listener,
                                        false);
  }
}

// static
void ThreadDebugger::MonitorEventsCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SetMonitorEventsCallback(info, true);
}

// static
void ThreadDebugger::UnmonitorEventsCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SetMonitorEventsCallback(info, false);
}

// static
void ThreadDebugger::GetEventListenersCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  if (info.Length() < 1)
    return;

  ThreadDebugger* debugger = static_cast<ThreadDebugger*>(
      v8::Local<v8::External>::Cast(info.Data())->Value());
  DCHECK(debugger);
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  int group_id = debugger->ContextGroupId(ToExecutionContext(context));

  V8EventListenerInfoList listener_info;
  // eventListeners call can produce message on ErrorEvent during lazy event
  // listener compilation.
  if (group_id)
    debugger->muteMetrics(group_id);
  InspectorDOMDebuggerAgent::EventListenersInfoForTarget(isolate, info[0],
                                                         &listener_info);
  if (group_id)
    debugger->unmuteMetrics(group_id);

  v8::Local<v8::Object> result = v8::Object::New(isolate);
  AtomicString current_event_type;
  v8::Local<v8::Array> listeners;
  size_t output_index = 0;
  for (auto& info : listener_info) {
    if (current_event_type != info.event_type) {
      current_event_type = info.event_type;
      listeners = v8::Array::New(isolate);
      output_index = 0;
      CreateDataProperty(context, result, V8String(isolate, current_event_type),
                         listeners);
    }

    v8::Local<v8::Object> listener_object = v8::Object::New(isolate);
    CreateDataProperty(context, listener_object, V8String(isolate, "listener"),
                       info.handler);
    CreateDataProperty(context, listener_object,
                       V8String(isolate, "useCapture"),
                       v8::Boolean::New(isolate, info.use_capture));
    CreateDataProperty(context, listener_object, V8String(isolate, "passive"),
                       v8::Boolean::New(isolate, info.passive));
    CreateDataProperty(context, listener_object, V8String(isolate, "once"),
                       v8::Boolean::New(isolate, info.once));
    CreateDataProperty(context, listener_object, V8String(isolate, "type"),
                       V8String(isolate, current_event_type));
    CreateDataPropertyInArray(context, listeners, output_index++,
                              listener_object);
  }
  info.GetReturnValue().Set(result);
}

void ThreadDebugger::consoleTime(const v8_inspector::StringView& title) {
  // TODO(dgozman): we can save on a copy here if trace macro would take a
  // pointer with length.
  TRACE_EVENT_COPY_ASYNC_BEGIN0("blink.console",
                                ToCoreString(title).Utf8().Data(), this);
}

void ThreadDebugger::consoleTimeEnd(const v8_inspector::StringView& title) {
  // TODO(dgozman): we can save on a copy here if trace macro would take a
  // pointer with length.
  TRACE_EVENT_COPY_ASYNC_END0("blink.console",
                              ToCoreString(title).Utf8().Data(), this);
}

void ThreadDebugger::consoleTimeStamp(const v8_inspector::StringView& title) {
  v8::Isolate* isolate = isolate_;
  // TODO(dgozman): we can save on a copy here if TracedValue would take a
  // StringView.
  TRACE_EVENT_INSTANT1(
      "devtools.timeline", "TimeStamp", TRACE_EVENT_SCOPE_THREAD, "data",
      InspectorTimeStampEvent::Data(CurrentExecutionContext(isolate),
                                    ToCoreString(title)));
}

void ThreadDebugger::startRepeatingTimer(
    double interval,
    V8InspectorClient::TimerCallback callback,
    void* data) {
  timer_data_.push_back(data);
  timer_callbacks_.push_back(callback);

  std::unique_ptr<Timer<ThreadDebugger>> timer = WTF::WrapUnique(
      new Timer<ThreadDebugger>(this, &ThreadDebugger::OnTimer));
  Timer<ThreadDebugger>* timer_ptr = timer.get();
  timers_.push_back(std::move(timer));
  timer_ptr->StartRepeating(interval, BLINK_FROM_HERE);
}

void ThreadDebugger::cancelTimer(void* data) {
  for (size_t index = 0; index < timer_data_.size(); ++index) {
    if (timer_data_[index] == data) {
      timers_[index]->Stop();
      timer_callbacks_.erase(index);
      timers_.erase(index);
      timer_data_.erase(index);
      return;
    }
  }
}

void ThreadDebugger::OnTimer(TimerBase* timer) {
  for (size_t index = 0; index < timers_.size(); ++index) {
    if (timers_[index].get() == timer) {
      timer_callbacks_[index](timer_data_[index]);
      return;
    }
  }
}

}  // namespace blink
