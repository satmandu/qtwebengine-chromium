// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_API_BINDING_JS_UTIL_H_
#define EXTENSIONS_RENDERER_API_BINDING_JS_UTIL_H_

#include <string>

#include "base/macros.h"
#include "extensions/renderer/api_binding_types.h"
#include "gin/wrappable.h"
#include "v8/include/v8.h"

namespace gin {
class Arguments;
}

namespace extensions {
class APIEventHandler;
class APIRequestHandler;
class APITypeReferenceMap;

// An object that exposes utility methods to the existing JS bindings, such as
// sendRequest and registering event argument massagers. If/when we get rid of
// some of our JS bindings, we can reduce or remove this class.
class APIBindingJSUtil final : public gin::Wrappable<APIBindingJSUtil> {
 public:
  APIBindingJSUtil(const APITypeReferenceMap* type_refs,
                   APIRequestHandler* request_handler,
                   APIEventHandler* event_handler,
                   const binding::RunJSFunction& run_js);
  ~APIBindingJSUtil() override;

  static gin::WrapperInfo kWrapperInfo;

  // gin::Wrappable:
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) final;

 private:
  // A handler to initiate an API request through the APIRequestHandler. A
  // replacement for custom bindings that utilize require('sendRequest').
  void SendRequest(gin::Arguments* arguments,
                   const std::string& name,
                   const std::vector<v8::Local<v8::Value>>& request_args,
                   v8::Local<v8::Value> schemas_unused,
                   v8::Local<v8::Value> options);

  // A handler to register an argument massager for a specific event.
  // Replacement for event_bindings.registerArgumentMassager.
  void RegisterEventArgumentMassager(gin::Arguments* arguments,
                                     const std::string& event_name,
                                     v8::Local<v8::Function> massager);

  // A handler to allow custom bindings to create custom extension API event
  // objects (e.g. foo.onBar).
  // Note: The JS version allows for constructing declarative events; it's
  // unclear if we'll need to support this.
  // TODO(devlin): Currently, we ignore schema. We may want to take it into
  // account.
  void CreateCustomEvent(gin::Arguments* arguments,
                         v8::Local<v8::Value> v8_event_name,
                         v8::Local<v8::Value> unused_schema,
                         bool supports_filters);

  // Invalidates an event, removing its listeners and preventing any more from
  // being added.
  void InvalidateEvent(gin::Arguments* arguments, v8::Local<v8::Object> event);

  // Sets the last error in the context.
  void SetLastError(gin::Arguments* arguments, const std::string& error);

  // Clears the last error in the context.
  void ClearLastError(gin::Arguments* arguments);

  // Returns true if there is a set lastError in the given context.
  void HasLastError(gin::Arguments* arguments);

  // Sets the lastError in the given context, runs the provided callback, and
  // then clears the last error.
  void RunCallbackWithLastError(gin::Arguments* arguments,
                                const std::string& error,
                                v8::Local<v8::Function> callback);

  // Type references. Guaranteed to outlive this object.
  const APITypeReferenceMap* type_refs_;

  // The request handler. Guaranteed to outlive this object.
  APIRequestHandler* request_handler_;

  // The event handler. Guaranteed to outlive this object.
  APIEventHandler* event_handler_;

  binding::RunJSFunction run_js_;

  DISALLOW_COPY_AND_ASSIGN(APIBindingJSUtil);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_API_BINDING_JS_UTIL_H_
