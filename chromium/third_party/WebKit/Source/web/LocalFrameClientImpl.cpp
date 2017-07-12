/*
 * Copyright (C) 2009, 2012 Google Inc. All rights reserved.
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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

#include "web/LocalFrameClientImpl.h"

#include "bindings/core/v8/ScriptController.h"
#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/Fullscreen.h"
#include "core/events/MessageEvent.h"
#include "core/events/MouseEvent.h"
#include "core/events/UIEventWithKeyState.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/HitTestResult.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/HistoryItem.h"
#include "core/origin_trials/OriginTrials.h"
#include "core/page/Page.h"
#include "core/page/WindowFeatures.h"
#include "modules/audio_output_devices/HTMLMediaElementAudioOutputDevice.h"
#include "modules/device_light/DeviceLightController.h"
#include "modules/device_orientation/DeviceMotionController.h"
#include "modules/device_orientation/DeviceOrientationAbsoluteController.h"
#include "modules/device_orientation/DeviceOrientationController.h"
#include "modules/encryptedmedia/HTMLMediaElementEncryptedMedia.h"
#include "modules/gamepad/NavigatorGamepad.h"
#include "modules/presentation/PresentationReceiver.h"
#include "modules/remoteplayback/HTMLMediaElementRemotePlayback.h"
#include "modules/remoteplayback/RemotePlayback.h"
#include "modules/serviceworkers/NavigatorServiceWorker.h"
#include "modules/serviceworkers/ServiceWorkerLinkResource.h"
#include "modules/storage/DOMWindowStorageController.h"
#include "modules/vr/NavigatorVR.h"
#include "platform/Histogram.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/UserGestureIndicator.h"
#include "platform/exported/WrappedResourceRequest.h"
#include "platform/exported/WrappedResourceResponse.h"
#include "platform/network/HTTPParsers.h"
#include "platform/network/mime/MIMETypeRegistry.h"
#include "platform/plugins/PluginData.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StringExtras.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/Platform.h"
#include "public/platform/WebApplicationCacheHost.h"
#include "public/platform/WebMediaPlayerSource.h"
#include "public/platform/WebRTCPeerConnectionHandler.h"
#include "public/platform/WebSecurityOrigin.h"
#include "public/platform/WebURL.h"
#include "public/platform/WebURLError.h"
#include "public/platform/WebVector.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerProvider.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerProviderClient.h"
#include "public/web/WebAutofillClient.h"
#include "public/web/WebDOMEvent.h"
#include "public/web/WebDocument.h"
#include "public/web/WebFormElement.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebNode.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebPluginParams.h"
#include "public/web/WebViewClient.h"
#include "v8/include/v8.h"
#include "web/DevToolsEmulator.h"
#include "web/SharedWorkerRepositoryClientImpl.h"
#include "web/WebDataSourceImpl.h"
#include "web/WebDevToolsAgentImpl.h"
#include "web/WebDevToolsFrontendImpl.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebPluginContainerImpl.h"
#include "web/WebViewImpl.h"

#include <memory>

namespace blink {

namespace {

// Convenience helper for frame tree helpers in FrameClient to reduce the amount
// of null-checking boilerplate code. Since the frame tree is maintained in the
// web/ layer, the frame tree helpers often have to deal with null WebFrames:
// for example, a frame with no parent will return null for WebFrame::parent().
// TODO(dcheng): Remove duplication between LocalFrameClientImpl and
// RemoteFrameClientImpl somehow...
Frame* ToCoreFrame(WebFrame* frame) {
  return frame ? frame->ToImplBase()->GetFrame() : nullptr;
}

}  // namespace

LocalFrameClientImpl::LocalFrameClientImpl(WebLocalFrameImpl* frame)
    : web_frame_(frame) {}

LocalFrameClientImpl* LocalFrameClientImpl::Create(WebLocalFrameImpl* frame) {
  return new LocalFrameClientImpl(frame);
}

LocalFrameClientImpl::~LocalFrameClientImpl() {}

DEFINE_TRACE(LocalFrameClientImpl) {
  visitor->Trace(web_frame_);
  LocalFrameClient::Trace(visitor);
}

void LocalFrameClientImpl::DidCreateNewDocument() {
  if (web_frame_->Client())
    web_frame_->Client()->DidCreateNewDocument(web_frame_);
}

void LocalFrameClientImpl::DispatchDidClearWindowObjectInMainWorld() {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidClearWindowObject(web_frame_);
    Document* document = web_frame_->GetFrame()->GetDocument();
    if (document) {
      DeviceMotionController::From(*document);
      DeviceOrientationController::From(*document);
      DeviceOrientationAbsoluteController::From(*document);
      if (RuntimeEnabledFeatures::deviceLightEnabled())
        DeviceLightController::From(*document);
      NavigatorGamepad::From(*document);
      NavigatorServiceWorker::From(*document);
      DOMWindowStorageController::From(*document);
      if (RuntimeEnabledFeatures::webVREnabled() ||
          OriginTrials::webVREnabled(document->GetExecutionContext()))
        NavigatorVR::From(*document);
      if (RuntimeEnabledFeatures::presentationEnabled() &&
          web_frame_->GetFrame()->GetSettings()->GetPresentationReceiver()) {
        // Call this in order to ensure the object is created.
        PresentationReceiver::From(*document);
      }
    }
  }
  // FIXME: when extensions go out of process, this whole concept stops working.
  WebDevToolsFrontendImpl* dev_tools_frontend =
      web_frame_->Top()->IsWebLocalFrame()
          ? ToWebLocalFrameImpl(web_frame_->Top())->DevToolsFrontend()
          : nullptr;
  if (dev_tools_frontend)
    dev_tools_frontend->DidClearWindowObject(web_frame_);
}

void LocalFrameClientImpl::DocumentElementAvailable() {
  if (web_frame_->Client())
    web_frame_->Client()->DidCreateDocumentElement(web_frame_);
}

void LocalFrameClientImpl::RunScriptsAtDocumentElementAvailable() {
  if (web_frame_->Client())
    web_frame_->Client()->RunScriptsAtDocumentElementAvailable(web_frame_);
  // The callback might have deleted the frame, do not use |this|!
}

void LocalFrameClientImpl::RunScriptsAtDocumentReady(bool document_is_empty) {
  if (web_frame_->Client()) {
    web_frame_->Client()->RunScriptsAtDocumentReady(document_is_empty);
  }
  // The callback might have deleted the frame, do not use |this|!
}

void LocalFrameClientImpl::RunScriptsAtDocumentIdle() {
  if (web_frame_->Client())
    web_frame_->Client()->RunScriptsAtDocumentIdle();
  // The callback might have deleted the frame, do not use |this|!
}

void LocalFrameClientImpl::DidCreateScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  if (web_frame_->Client())
    web_frame_->Client()->DidCreateScriptContext(web_frame_, context, world_id);
}

void LocalFrameClientImpl::WillReleaseScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  if (web_frame_->Client()) {
    web_frame_->Client()->WillReleaseScriptContext(web_frame_, context,
                                                   world_id);
  }
}

bool LocalFrameClientImpl::AllowScriptExtensions() {
  return true;
}

void LocalFrameClientImpl::DidChangeScrollOffset() {
  if (web_frame_->Client())
    web_frame_->Client()->DidChangeScrollOffset(web_frame_);
}

void LocalFrameClientImpl::DidUpdateCurrentHistoryItem() {
  if (web_frame_->Client())
    web_frame_->Client()->DidUpdateCurrentHistoryItem();
}

bool LocalFrameClientImpl::HasWebView() const {
  return web_frame_->ViewImpl();
}

bool LocalFrameClientImpl::InShadowTree() const {
  return web_frame_->InShadowTree();
}

Frame* LocalFrameClientImpl::Opener() const {
  return ToCoreFrame(web_frame_->Opener());
}

void LocalFrameClientImpl::SetOpener(Frame* opener) {
  WebFrame* opener_frame = WebFrame::FromFrame(opener);
  if (web_frame_->Client() && web_frame_->Opener() != opener_frame)
    web_frame_->Client()->DidChangeOpener(opener_frame);
  web_frame_->SetOpener(opener_frame);
}

Frame* LocalFrameClientImpl::Parent() const {
  return ToCoreFrame(web_frame_->Parent());
}

Frame* LocalFrameClientImpl::Top() const {
  return ToCoreFrame(web_frame_->Top());
}

Frame* LocalFrameClientImpl::NextSibling() const {
  return ToCoreFrame(web_frame_->NextSibling());
}

Frame* LocalFrameClientImpl::FirstChild() const {
  return ToCoreFrame(web_frame_->FirstChild());
}

void LocalFrameClientImpl::WillBeDetached() {
  web_frame_->WillBeDetached();
}

void LocalFrameClientImpl::Detached(FrameDetachType type) {
  // Alert the client that the frame is being detached. This is the last
  // chance we have to communicate with the client.
  WebFrameClient* client = web_frame_->Client();
  if (!client)
    return;

  web_frame_->WillDetachParent();

  // Signal that no further communication with WebFrameClient should take
  // place at this point since we are no longer associated with the Page.
  web_frame_->SetClient(0);

  client->FrameDetached(web_frame_,
                        static_cast<WebFrameClient::DetachType>(type));
  // Clear our reference to LocalFrame at the very end, in case the client
  // refers to it.
  web_frame_->SetCoreFrame(nullptr);
}

void LocalFrameClientImpl::DispatchWillSendRequest(ResourceRequest& request) {
  // Give the WebFrameClient a crack at the request.
  if (web_frame_->Client()) {
    WrappedResourceRequest webreq(request);
    web_frame_->Client()->WillSendRequest(web_frame_, webreq);
  }
}

void LocalFrameClientImpl::DispatchDidReceiveResponse(
    const ResourceResponse& response) {
  if (web_frame_->Client()) {
    WrappedResourceResponse webresp(response);
    web_frame_->Client()->DidReceiveResponse(webresp);
  }
}

void LocalFrameClientImpl::DispatchDidFinishDocumentLoad() {
  // TODO(dglazkov): Sadly, workers are WebFrameClients, and they can totally
  // destroy themselves when didFinishDocumentLoad is invoked, and in turn
  // destroy the fake WebLocalFrame that they create, which means that you
  // should not put any code touching `this` after the two lines below.
  if (web_frame_->Client())
    web_frame_->Client()->DidFinishDocumentLoad(web_frame_);
}

void LocalFrameClientImpl::DispatchDidLoadResourceFromMemoryCache(
    const ResourceRequest& request,
    const ResourceResponse& response) {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidLoadResourceFromMemoryCache(
        WrappedResourceRequest(request), WrappedResourceResponse(response));
  }
}

void LocalFrameClientImpl::DispatchDidHandleOnloadEvents() {
  if (web_frame_->Client())
    web_frame_->Client()->DidHandleOnloadEvents();
}

void LocalFrameClientImpl::
    DispatchDidReceiveServerRedirectForProvisionalLoad() {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidReceiveServerRedirectForProvisionalLoad();
  }
}

void LocalFrameClientImpl::DispatchDidNavigateWithinPage(
    HistoryItem* item,
    HistoryCommitType commit_type,
    bool content_initiated) {
  bool should_create_history_entry = commit_type == kStandardCommit;
  // TODO(dglazkov): Does this need to be called for subframes?
  web_frame_->ViewImpl()->DidCommitLoad(should_create_history_entry, true);
  if (web_frame_->Client()) {
    web_frame_->Client()->DidNavigateWithinPage(
        web_frame_, WebHistoryItem(item),
        static_cast<WebHistoryCommitType>(commit_type), content_initiated);
  }
}

void LocalFrameClientImpl::DispatchWillCommitProvisionalLoad() {
  if (web_frame_->Client())
    web_frame_->Client()->WillCommitProvisionalLoad();
}

void LocalFrameClientImpl::DispatchDidStartProvisionalLoad(
    DocumentLoader* loader,
    ResourceRequest& request) {
  if (web_frame_->Client()) {
    WrappedResourceRequest wrapped_request(request);
    web_frame_->Client()->DidStartProvisionalLoad(
        WebDataSourceImpl::FromDocumentLoader(loader), wrapped_request);
  }
  if (WebDevToolsAgentImpl* dev_tools = DevToolsAgent())
    dev_tools->DidStartProvisionalLoad(web_frame_->GetFrame());
}

void LocalFrameClientImpl::DispatchDidReceiveTitle(const String& title) {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidReceiveTitle(web_frame_, title,
                                          kWebTextDirectionLeftToRight);
  }
}

void LocalFrameClientImpl::DispatchDidChangeIcons(IconType type) {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidChangeIcon(static_cast<WebIconURL::Type>(type));
  }
}

void LocalFrameClientImpl::DispatchDidCommitLoad(
    HistoryItem* item,
    HistoryCommitType commit_type) {
  if (!web_frame_->Parent()) {
    web_frame_->ViewImpl()->DidCommitLoad(commit_type == kStandardCommit,
                                          false);
  }

  if (web_frame_->Client()) {
    web_frame_->Client()->DidCommitProvisionalLoad(
        web_frame_, WebHistoryItem(item),
        static_cast<WebHistoryCommitType>(commit_type));
  }
  if (WebDevToolsAgentImpl* dev_tools = DevToolsAgent())
    dev_tools->DidCommitLoadForLocalFrame(web_frame_->GetFrame());
}

void LocalFrameClientImpl::DispatchDidFailProvisionalLoad(
    const ResourceError& error,
    HistoryCommitType commit_type) {
  web_frame_->DidFail(error, true, commit_type);
}

void LocalFrameClientImpl::DispatchDidFailLoad(const ResourceError& error,
                                               HistoryCommitType commit_type) {
  web_frame_->DidFail(error, false, commit_type);
}

void LocalFrameClientImpl::DispatchDidFinishLoad() {
  web_frame_->DidFinish();
}

void LocalFrameClientImpl::DispatchDidChangeThemeColor() {
  if (web_frame_->Client())
    web_frame_->Client()->DidChangeThemeColor();
}

static bool AllowCreatingBackgroundTabs() {
  const WebInputEvent* input_event = WebViewImpl::CurrentInputEvent();
  if (!input_event || (input_event->GetType() != WebInputEvent::kMouseUp &&
                       (input_event->GetType() != WebInputEvent::kRawKeyDown &&
                        input_event->GetType() != WebInputEvent::kKeyDown) &&
                       input_event->GetType() != WebInputEvent::kGestureTap))
    return false;

  unsigned short button_number;
  if (WebInputEvent::IsMouseEventType(input_event->GetType())) {
    const WebMouseEvent* mouse_event =
        static_cast<const WebMouseEvent*>(input_event);

    switch (mouse_event->button) {
      case WebMouseEvent::Button::kLeft:
        button_number = 0;
        break;
      case WebMouseEvent::Button::kMiddle:
        button_number = 1;
        break;
      case WebMouseEvent::Button::kRight:
        button_number = 2;
        break;
      default:
        return false;
    }
  } else {
    // The click is simulated when triggering the keypress event.
    button_number = 0;
  }
  bool ctrl = input_event->GetModifiers() & WebMouseEvent::kControlKey;
  bool shift = input_event->GetModifiers() & WebMouseEvent::kShiftKey;
  bool alt = input_event->GetModifiers() & WebMouseEvent::kAltKey;
  bool meta = input_event->GetModifiers() & WebMouseEvent::kMetaKey;

  NavigationPolicy user_policy;
  if (!NavigationPolicyFromMouseEvent(button_number, ctrl, shift, alt, meta,
                                      &user_policy))
    return false;
  return user_policy == kNavigationPolicyNewBackgroundTab;
}

NavigationPolicy LocalFrameClientImpl::DecidePolicyForNavigation(
    const ResourceRequest& request,
    DocumentLoader* loader,
    NavigationType type,
    NavigationPolicy policy,
    bool replaces_current_history_item,
    bool is_client_redirect,
    HTMLFormElement* form,
    ContentSecurityPolicyDisposition
        should_check_main_world_content_security_policy) {
  if (!web_frame_->Client())
    return kNavigationPolicyIgnore;

  if (policy == kNavigationPolicyNewBackgroundTab &&
      !AllowCreatingBackgroundTabs() &&
      !UIEventWithKeyState::NewTabModifierSetFromIsolatedWorld())
    policy = kNavigationPolicyNewForegroundTab;

  WebDataSourceImpl* ds = WebDataSourceImpl::FromDocumentLoader(loader);

  // Newly created child frames may need to be navigated to a history item
  // during a back/forward navigation. This will only happen when the parent
  // is a LocalFrame doing a back/forward navigation that has not completed.
  // (If the load has completed and the parent later adds a frame with script,
  // we do not want to use a history item for it.)
  bool is_history_navigation_in_new_child_frame =
      web_frame_->Parent() && web_frame_->Parent()->IsWebLocalFrame() &&
      IsBackForwardLoadType(ToWebLocalFrameImpl(web_frame_->Parent())
                                ->GetFrame()
                                ->Loader()
                                .GetDocumentLoader()
                                ->LoadType()) &&
      !ToWebLocalFrameImpl(web_frame_->Parent())
           ->GetFrame()
           ->GetDocument()
           ->LoadEventFinished();

  WrappedResourceRequest wrapped_resource_request(request);
  WebFrameClient::NavigationPolicyInfo navigation_info(
      wrapped_resource_request);
  navigation_info.navigation_type = static_cast<WebNavigationType>(type);
  navigation_info.default_policy = static_cast<WebNavigationPolicy>(policy);
  navigation_info.extra_data = ds ? ds->GetExtraData() : nullptr;
  navigation_info.replaces_current_history_item = replaces_current_history_item;
  navigation_info.is_history_navigation_in_new_child_frame =
      is_history_navigation_in_new_child_frame;
  navigation_info.is_client_redirect = is_client_redirect;
  navigation_info.should_check_main_world_content_security_policy =
      should_check_main_world_content_security_policy ==
              kCheckContentSecurityPolicy
          ? kWebContentSecurityPolicyDispositionCheck
          : kWebContentSecurityPolicyDispositionDoNotCheck;
  // Caching could be disabled for requests initiated by DevTools.
  // TODO(ananta)
  // We should extract the network cache state into a global component which
  // can be queried here and wherever necessary.
  navigation_info.is_cache_disabled =
      DevToolsAgent() ? DevToolsAgent()->CacheDisabled() : false;
  if (form)
    navigation_info.form = WebFormElement(form);

  std::unique_ptr<SourceLocation> source_location =
      SourceLocation::Capture(web_frame_->GetFrame()->GetDocument());
  if (source_location && !source_location->IsUnknown()) {
    navigation_info.source_location.url = source_location->Url();
    navigation_info.source_location.line_number = source_location->LineNumber();
    navigation_info.source_location.column_number =
        source_location->ColumnNumber();
  }

  WebNavigationPolicy web_policy =
      web_frame_->Client()->DecidePolicyForNavigation(navigation_info);
  return static_cast<NavigationPolicy>(web_policy);
}

void LocalFrameClientImpl::DispatchWillSendSubmitEvent(HTMLFormElement* form) {
  if (web_frame_->Client())
    web_frame_->Client()->WillSendSubmitEvent(WebFormElement(form));
}

void LocalFrameClientImpl::DispatchWillSubmitForm(HTMLFormElement* form) {
  if (web_frame_->Client())
    web_frame_->Client()->WillSubmitForm(WebFormElement(form));
}

void LocalFrameClientImpl::DidStartLoading(LoadStartType load_start_type) {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidStartLoading(load_start_type ==
                                          kNavigationToDifferentDocument);
  }
}

void LocalFrameClientImpl::ProgressEstimateChanged(double progress_estimate) {
  if (web_frame_->Client())
    web_frame_->Client()->DidChangeLoadProgress(progress_estimate);
}

void LocalFrameClientImpl::DidStopLoading() {
  if (web_frame_->Client())
    web_frame_->Client()->DidStopLoading();
}

void LocalFrameClientImpl::LoadURLExternally(
    const ResourceRequest& request,
    NavigationPolicy policy,
    const String& suggested_name,
    bool should_replace_current_entry) {
  if (!web_frame_->Client())
    return;
  DCHECK(web_frame_->GetFrame()->GetDocument());
  Fullscreen::FullyExitFullscreen(*web_frame_->GetFrame()->GetDocument());
  web_frame_->Client()->LoadURLExternally(
      WrappedResourceRequest(request), static_cast<WebNavigationPolicy>(policy),
      suggested_name, should_replace_current_entry);
}

void LocalFrameClientImpl::LoadErrorPage(int reason) {
  if (web_frame_->Client())
    web_frame_->Client()->LoadErrorPage(reason);
}

bool LocalFrameClientImpl::NavigateBackForward(int offset) const {
  WebViewImpl* webview = web_frame_->ViewImpl();
  if (!webview->Client())
    return false;

  DCHECK(offset);
  if (offset > webview->Client()->HistoryForwardListCount())
    return false;
  if (offset < -webview->Client()->HistoryBackListCount())
    return false;
  webview->Client()->NavigateBackForwardSoon(offset);
  return true;
}

void LocalFrameClientImpl::DidAccessInitialDocument() {
  if (web_frame_->Client())
    web_frame_->Client()->DidAccessInitialDocument();
}

void LocalFrameClientImpl::DidDisplayInsecureContent() {
  if (web_frame_->Client())
    web_frame_->Client()->DidDisplayInsecureContent();
}

void LocalFrameClientImpl::DidContainInsecureFormAction() {
  if (web_frame_->Client())
    web_frame_->Client()->DidContainInsecureFormAction();
}

void LocalFrameClientImpl::DidRunInsecureContent(SecurityOrigin* origin,
                                                 const KURL& insecure_url) {
  if (web_frame_->Client()) {
    web_frame_->Client()->DidRunInsecureContent(WebSecurityOrigin(origin),
                                                insecure_url);
  }
}

void LocalFrameClientImpl::DidDetectXSS(const KURL& insecure_url,
                                        bool did_block_entire_page) {
  if (web_frame_->Client())
    web_frame_->Client()->DidDetectXSS(insecure_url, did_block_entire_page);
}

void LocalFrameClientImpl::DidDispatchPingLoader(const KURL& url) {
  if (web_frame_->Client())
    web_frame_->Client()->DidDispatchPingLoader(url);
}

void LocalFrameClientImpl::DidDisplayContentWithCertificateErrors(
    const KURL& url) {
  if (web_frame_->Client())
    web_frame_->Client()->DidDisplayContentWithCertificateErrors(url);
}

void LocalFrameClientImpl::DidRunContentWithCertificateErrors(const KURL& url) {
  if (web_frame_->Client())
    web_frame_->Client()->DidRunContentWithCertificateErrors(url);
}

void LocalFrameClientImpl::DidChangePerformanceTiming() {
  if (web_frame_->Client())
    web_frame_->Client()->DidChangePerformanceTiming();
}

void LocalFrameClientImpl::DidObserveLoadingBehavior(
    WebLoadingBehaviorFlag behavior) {
  if (web_frame_->Client())
    web_frame_->Client()->DidObserveLoadingBehavior(behavior);
}

void LocalFrameClientImpl::SelectorMatchChanged(
    const Vector<String>& added_selectors,
    const Vector<String>& removed_selectors) {
  if (WebFrameClient* client = web_frame_->Client()) {
    client->DidMatchCSS(web_frame_, WebVector<WebString>(added_selectors),
                        WebVector<WebString>(removed_selectors));
  }
}

DocumentLoader* LocalFrameClientImpl::CreateDocumentLoader(
    LocalFrame* frame,
    const ResourceRequest& request,
    const SubstituteData& data,
    ClientRedirectPolicy client_redirect_policy) {
  DCHECK(frame);

  WebDataSourceImpl* ds =
      WebDataSourceImpl::Create(frame, request, data, client_redirect_policy);
  if (web_frame_->Client())
    web_frame_->Client()->DidCreateDataSource(web_frame_, ds);
  return ds;
}

String LocalFrameClientImpl::UserAgent() {
  WebString override =
      web_frame_->Client() ? web_frame_->Client()->UserAgentOverride() : "";
  if (!override.IsEmpty())
    return override;

  if (user_agent_.IsEmpty())
    user_agent_ = Platform::Current()->UserAgent();
  return user_agent_;
}

String LocalFrameClientImpl::DoNotTrackValue() {
  WebString do_not_track = web_frame_->Client()->DoNotTrackValue();
  if (!do_not_track.IsEmpty())
    return do_not_track;
  return String();
}

// Called when the FrameLoader goes into a state in which a new page load
// will occur.
void LocalFrameClientImpl::TransitionToCommittedForNewPage() {
  web_frame_->CreateFrameView();
}

LocalFrame* LocalFrameClientImpl::CreateFrame(
    const FrameLoadRequest& request,
    const AtomicString& name,
    HTMLFrameOwnerElement* owner_element) {
  return web_frame_->CreateChildFrame(request, name, owner_element);
}

bool LocalFrameClientImpl::CanCreatePluginWithoutRenderer(
    const String& mime_type) const {
  if (!web_frame_->Client())
    return false;

  return web_frame_->Client()->CanCreatePluginWithoutRenderer(mime_type);
}

PluginView* LocalFrameClientImpl::CreatePlugin(
    HTMLPlugInElement* element,
    const KURL& url,
    const Vector<String>& param_names,
    const Vector<String>& param_values,
    const String& mime_type,
    bool load_manually,
    DetachedPluginPolicy policy) {
  if (!web_frame_->Client())
    return nullptr;

  WebPluginParams params;
  params.url = url;
  params.mime_type = mime_type;
  params.attribute_names = param_names;
  params.attribute_values = param_values;
  params.load_manually = load_manually;

  WebPlugin* web_plugin =
      web_frame_->Client()->CreatePlugin(web_frame_, params);
  if (!web_plugin)
    return nullptr;

  // The container takes ownership of the WebPlugin.
  WebPluginContainerImpl* container =
      WebPluginContainerImpl::Create(element, web_plugin);

  if (!web_plugin->Initialize(container))
    return nullptr;

  if (policy != kAllowDetachedPlugin && !element->GetLayoutObject())
    return nullptr;

  return container;
}

std::unique_ptr<WebMediaPlayer> LocalFrameClientImpl::CreateWebMediaPlayer(
    HTMLMediaElement& html_media_element,
    const WebMediaPlayerSource& source,
    WebMediaPlayerClient* client) {
  WebLocalFrameImpl* web_frame =
      WebLocalFrameImpl::FromFrame(html_media_element.GetDocument().GetFrame());

  if (!web_frame || !web_frame->Client())
    return nullptr;

  HTMLMediaElementEncryptedMedia& encrypted_media =
      HTMLMediaElementEncryptedMedia::From(html_media_element);
  WebString sink_id(
      HTMLMediaElementAudioOutputDevice::sinkId(html_media_element));
  return WTF::WrapUnique(web_frame->Client()->CreateMediaPlayer(
      source, client, &encrypted_media,
      encrypted_media.ContentDecryptionModule(), sink_id));
}

WebRemotePlaybackClient* LocalFrameClientImpl::CreateWebRemotePlaybackClient(
    HTMLMediaElement& html_media_element) {
  return HTMLMediaElementRemotePlayback::remote(html_media_element);
}

ObjectContentType LocalFrameClientImpl::GetObjectContentType(
    const KURL& url,
    const String& explicit_mime_type,
    bool should_prefer_plug_ins_for_images) {
  // This code is based on Apple's implementation from
  // WebCoreSupport/WebFrameBridge.mm.

  String mime_type = explicit_mime_type;
  if (mime_type.IsEmpty()) {
    // Try to guess the MIME type based off the extension.
    String filename = url.LastPathComponent();
    int extension_pos = filename.ReverseFind('.');
    if (extension_pos >= 0) {
      String extension = filename.Substring(extension_pos + 1);
      mime_type = MIMETypeRegistry::GetWellKnownMIMETypeForExtension(extension);
    }

    if (mime_type.IsEmpty())
      return kObjectContentFrame;
  }

  // If Chrome is started with the --disable-plugins switch, pluginData is 0.
  PluginData* plugin_data = web_frame_->GetFrame()->GetPluginData();
  bool plug_in_supports_mime_type =
      plugin_data && plugin_data->SupportsMimeType(mime_type);

  if (MIMETypeRegistry::IsSupportedImageMIMEType(mime_type)) {
    return should_prefer_plug_ins_for_images && plug_in_supports_mime_type
               ? kObjectContentNetscapePlugin
               : kObjectContentImage;
  }

  if (plug_in_supports_mime_type)
    return kObjectContentNetscapePlugin;

  if (MIMETypeRegistry::IsSupportedNonImageMIMEType(mime_type))
    return kObjectContentFrame;

  return kObjectContentNone;
}

WebCookieJar* LocalFrameClientImpl::CookieJar() const {
  if (!web_frame_->Client())
    return 0;
  return web_frame_->Client()->CookieJar();
}

void LocalFrameClientImpl::FrameFocused() const {
  if (web_frame_->Client())
    web_frame_->Client()->FrameFocused();
}

void LocalFrameClientImpl::DidChangeName(const String& name) {
  if (!web_frame_->Client())
    return;
  web_frame_->Client()->DidChangeName(name);
}

void LocalFrameClientImpl::DidEnforceInsecureRequestPolicy(
    WebInsecureRequestPolicy policy) {
  if (!web_frame_->Client())
    return;
  web_frame_->Client()->DidEnforceInsecureRequestPolicy(policy);
}

void LocalFrameClientImpl::DidUpdateToUniqueOrigin() {
  if (!web_frame_->Client())
    return;
  DCHECK(web_frame_->GetSecurityOrigin().IsUnique());
  web_frame_->Client()->DidUpdateToUniqueOrigin(
      web_frame_->GetSecurityOrigin().IsPotentiallyTrustworthy());
}

void LocalFrameClientImpl::DidChangeSandboxFlags(Frame* child_frame,
                                                 SandboxFlags flags) {
  if (!web_frame_->Client())
    return;
  web_frame_->Client()->DidChangeSandboxFlags(
      WebFrame::FromFrame(child_frame), static_cast<WebSandboxFlags>(flags));
}

void LocalFrameClientImpl::DidSetFeaturePolicyHeader(
    const WebParsedFeaturePolicy& parsed_header) {
  if (web_frame_->Client())
    web_frame_->Client()->DidSetFeaturePolicyHeader(parsed_header);
}

void LocalFrameClientImpl::DidAddContentSecurityPolicies(
    const blink::WebVector<WebContentSecurityPolicy>& policies) {
  if (web_frame_->Client())
    web_frame_->Client()->DidAddContentSecurityPolicies(policies);
}

void LocalFrameClientImpl::DidChangeFrameOwnerProperties(
    HTMLFrameOwnerElement* frame_element) {
  if (!web_frame_->Client())
    return;

  web_frame_->Client()->DidChangeFrameOwnerProperties(
      WebFrame::FromFrame(frame_element->ContentFrame()),
      WebFrameOwnerProperties(
          frame_element->BrowsingContextContainerName(),
          frame_element->ScrollingMode(), frame_element->MarginWidth(),
          frame_element->MarginHeight(), frame_element->AllowFullscreen(),
          frame_element->AllowPaymentRequest(), frame_element->IsDisplayNone(),
          frame_element->Csp(), frame_element->AllowedFeatures()));
}

void LocalFrameClientImpl::DispatchWillStartUsingPeerConnectionHandler(
    WebRTCPeerConnectionHandler* handler) {
  web_frame_->Client()->WillStartUsingPeerConnectionHandler(handler);
}

bool LocalFrameClientImpl::AllowWebGL(bool enabled_per_settings) {
  if (web_frame_->Client())
    return web_frame_->Client()->AllowWebGL(enabled_per_settings);

  return enabled_per_settings;
}

void LocalFrameClientImpl::DispatchWillInsertBody() {
  if (web_frame_->Client())
    web_frame_->Client()->WillInsertBody(web_frame_);
}

std::unique_ptr<WebServiceWorkerProvider>
LocalFrameClientImpl::CreateServiceWorkerProvider() {
  if (!web_frame_->Client())
    return nullptr;
  return WTF::WrapUnique(web_frame_->Client()->CreateServiceWorkerProvider());
}

ContentSettingsClient& LocalFrameClientImpl::GetContentSettingsClient() {
  return web_frame_->GetContentSettingsClient();
}

SharedWorkerRepositoryClient*
LocalFrameClientImpl::GetSharedWorkerRepositoryClient() {
  return web_frame_->SharedWorkerRepositoryClient();
}

std::unique_ptr<WebApplicationCacheHost>
LocalFrameClientImpl::CreateApplicationCacheHost(
    WebApplicationCacheHostClient* client) {
  if (!web_frame_->Client())
    return nullptr;
  return WTF::WrapUnique(
      web_frame_->Client()->CreateApplicationCacheHost(client));
}

void LocalFrameClientImpl::DispatchDidChangeManifest() {
  if (web_frame_->Client())
    web_frame_->Client()->DidChangeManifest();
}

unsigned LocalFrameClientImpl::BackForwardLength() {
  WebViewImpl* webview = web_frame_->ViewImpl();
  if (!webview || !webview->Client())
    return 0;
  return webview->Client()->HistoryBackListCount() + 1 +
         webview->Client()->HistoryForwardListCount();
}

void LocalFrameClientImpl::SuddenTerminationDisablerChanged(
    bool present,
    SuddenTerminationDisablerType type) {
  if (web_frame_->Client()) {
    web_frame_->Client()->SuddenTerminationDisablerChanged(
        present,
        static_cast<WebFrameClient::SuddenTerminationDisablerType>(type));
  }
}

BlameContext* LocalFrameClientImpl::GetFrameBlameContext() {
  if (!web_frame_->Client())
    return nullptr;
  return web_frame_->Client()->GetFrameBlameContext();
}

LinkResource* LocalFrameClientImpl::CreateServiceWorkerLinkResource(
    HTMLLinkElement* owner) {
  return ServiceWorkerLinkResource::Create(owner);
}

WebEffectiveConnectionType LocalFrameClientImpl::GetEffectiveConnectionType() {
  if (web_frame_->Client())
    return web_frame_->Client()->GetEffectiveConnectionType();
  return WebEffectiveConnectionType::kTypeUnknown;
}

WebDevToolsAgentImpl* LocalFrameClientImpl::DevToolsAgent() {
  return WebLocalFrameImpl::FromFrame(web_frame_->GetFrame()->LocalFrameRoot())
      ->DevToolsAgentImpl();
}

KURL LocalFrameClientImpl::OverrideFlashEmbedWithHTML(const KURL& url) {
  return web_frame_->Client()->OverrideFlashEmbedWithHTML(WebURL(url));
}

void LocalFrameClientImpl::SetHasReceivedUserGesture(bool received_previously) {
  // The client potentially needs to dispatch the event to other processes only
  // for the first time.
  if (!received_previously && web_frame_->Client())
    web_frame_->Client()->SetHasReceivedUserGesture();
  // WebAutofillClient reacts only to the user gestures for this particular
  // frame. |received_previously| is ignored because it may be true due to an
  // event in a child frame.
  if (WebAutofillClient* autofill_client = web_frame_->AutofillClient())
    autofill_client->UserGestureObserved();
}

void LocalFrameClientImpl::AbortClientNavigation() {
  if (web_frame_->Client())
    web_frame_->Client()->AbortClientNavigation();
}

TextCheckerClient& LocalFrameClientImpl::GetTextCheckerClient() const {
  return web_frame_->GetTextCheckerClient();
}

}  // namespace blink
