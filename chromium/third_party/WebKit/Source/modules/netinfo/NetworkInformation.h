// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NetworkInformation_h
#define NetworkInformation_h

#include "bindings/core/v8/ActiveScriptWrappable.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/events/EventTarget.h"
#include "platform/network/NetworkStateNotifier.h"
#include "public/platform/WebConnectionType.h"

namespace blink {

class ExecutionContext;

class NetworkInformation final
    : public EventTargetWithInlineData,
      public ActiveScriptWrappable<NetworkInformation>,
      public ContextLifecycleObserver,
      public NetworkStateNotifier::NetworkStateObserver {
  USING_GARBAGE_COLLECTED_MIXIN(NetworkInformation);
  DEFINE_WRAPPERTYPEINFO();

 public:
  static NetworkInformation* Create(ExecutionContext*);
  ~NetworkInformation() override;

  String type() const;
  double downlinkMax() const;

  // NetworkStateObserver overrides.
  void ConnectionChange(WebConnectionType, double downlink_max_mbps) override;

  // EventTarget overrides.
  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;
  void RemoveAllEventListeners() override;

  // ScriptWrappable
  bool HasPendingActivity() const final;

  // ContextLifecycleObserver overrides.
  void ContextDestroyed(ExecutionContext*) override;

  DECLARE_VIRTUAL_TRACE();

  DEFINE_ATTRIBUTE_EVENT_LISTENER(change);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(typechange);  // Deprecated

 protected:
  // EventTarget overrides.
  void AddedEventListener(const AtomicString& event_type,
                          RegisteredEventListener&) final;
  void RemovedEventListener(const AtomicString& event_type,
                            const RegisteredEventListener&) final;

 private:
  explicit NetworkInformation(ExecutionContext*);
  void StartObserving();
  void StopObserving();

  // Touched only on context thread.
  WebConnectionType type_;

  // Touched only on context thread.
  double downlink_max_mbps_;

  // Whether this object is listening for events from NetworkStateNotifier.
  bool observing_;

  // Whether ContextLifecycleObserver::contextDestroyed has been called.
  bool context_stopped_;
};

}  // namespace blink

#endif  // NetworkInformation_h
