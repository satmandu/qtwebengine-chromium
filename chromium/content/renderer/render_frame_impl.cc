// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/render_frame_impl.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/debug/alias.h"
#include "base/debug/asan_invalid_access.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/files/file.h"
#include "base/i18n/char_iterator.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/shared_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/process/process.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "cc/base/switches.h"
#include "content/child/appcache/appcache_dispatcher.h"
#include "content/child/feature_policy/feature_policy_platform.h"
#include "content/child/quota_dispatcher.h"
#include "content/child/request_extra_data.h"
#include "content/child/service_worker/service_worker_handle_reference.h"
#include "content/child/service_worker/service_worker_network_provider.h"
#include "content/child/service_worker/service_worker_provider_context.h"
#include "content/child/service_worker/web_service_worker_provider_impl.h"
#include "content/child/v8_value_converter_impl.h"
#include "content/child/web_url_loader_impl.h"
#include "content/child/web_url_request_util.h"
#include "content/child/webmessageportchannel_impl.h"
#include "content/child/weburlresponse_extradata_impl.h"
#include "content/common/accessibility_messages.h"
#include "content/common/associated_interface_provider_impl.h"
#include "content/common/associated_interfaces.mojom.h"
#include "content/common/clipboard_messages.h"
#include "content/common/content_constants_internal.h"
#include "content/common/content_security_policy/csp_context.h"
#include "content/common/content_security_policy_header.h"
#include "content/common/download/mhtml_save_status.h"
#include "content/common/edit_command.h"
#include "content/common/frame_messages.h"
#include "content/common/frame_owner_properties.h"
#include "content/common/frame_replication_state.h"
#include "content/common/input_messages.h"
#include "content/common/navigation_params.h"
#include "content/common/page_messages.h"
#include "content/common/savable_subframe.h"
#include "content/common/service_worker/service_worker_types.h"
#include "content/common/site_isolation_policy.h"
#include "content/common/swapped_out_messages.h"
#include "content/common/view_messages.h"
#include "content/public/common/appcache_info.h"
#include "content/public/common/associated_interface_provider.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/context_menu_params.h"
#include "content/public/common/file_chooser_file_info.h"
#include "content/public/common/file_chooser_params.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/page_state.h"
#include "content/public/common/resource_response.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "content/public/renderer/browser_plugin_delegate.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/renderer/context_menu_client.h"
#include "content/public/renderer/document_state.h"
#include "content/public/renderer/navigation_state.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_visitor.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "content/renderer/accessibility/render_accessibility_impl.h"
#include "content/renderer/browser_plugin/browser_plugin.h"
#include "content/renderer/browser_plugin/browser_plugin_manager.h"
#include "content/renderer/child_frame_compositing_helper.h"
#include "content/renderer/content_security_policy_util.h"
#include "content/renderer/context_menu_params_builder.h"
#include "content/renderer/devtools/devtools_agent.h"
#include "content/renderer/dom_automation_controller.h"
#include "content/renderer/effective_connection_type_helper.h"
#include "content/renderer/external_popup_menu.h"
#include "content/renderer/frame_owner_properties.h"
#include "content/renderer/gpu/gpu_benchmarking_extension.h"
#include "content/renderer/history_entry.h"
#include "content/renderer/history_serialization.h"
#include "content/renderer/image_downloader/image_downloader_impl.h"
#include "content/renderer/ime_event_guard.h"
#include "content/renderer/input/input_handler_manager.h"
#include "content/renderer/installedapp/related_apps_fetcher.h"
#include "content/renderer/internal_document_state_data.h"
#include "content/renderer/manifest/manifest_manager.h"
#include "content/renderer/media/audio_device_factory.h"
#include "content/renderer/media/media_devices_listener_impl.h"
#include "content/renderer/media/media_permission_dispatcher.h"
#include "content/renderer/media/media_stream_dispatcher.h"
#include "content/renderer/media/media_stream_renderer_factory_impl.h"
#include "content/renderer/media/render_media_log.h"
#include "content/renderer/media/renderer_webmediaplayer_delegate.h"
#include "content/renderer/media/user_media_client_impl.h"
#include "content/renderer/media/web_media_element_source_utils.h"
#include "content/renderer/media/webmediaplayer_ms.h"
#include "content/renderer/mojo/blink_connector_js_wrapper.h"
#include "content/renderer/mojo/blink_interface_registry_impl.h"
#include "content/renderer/mojo/interface_provider_js_wrapper.h"
#include "content/renderer/mojo_bindings_controller.h"
#include "content/renderer/navigation_state_impl.h"
#include "content/renderer/pepper/pepper_audio_controller.h"
#include "content/renderer/pepper/plugin_instance_throttler_impl.h"
#include "content/renderer/presentation/presentation_dispatcher.h"
#include "content/renderer/push_messaging/push_messaging_client.h"
#include "content/renderer/render_frame_proxy.h"
#include "content/renderer/render_process.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/render_view_impl.h"
#include "content/renderer/render_widget_fullscreen_pepper.h"
#include "content/renderer/renderer_blink_platform_impl.h"
#include "content/renderer/renderer_webapplicationcachehost_impl.h"
#include "content/renderer/renderer_webcolorchooser_impl.h"
#include "content/renderer/savable_resources.h"
#include "content/renderer/screen_orientation/screen_orientation_dispatcher.h"
#include "content/renderer/shared_worker/shared_worker_repository.h"
#include "content/renderer/shared_worker/websharedworker_proxy.h"
#include "content/renderer/skia_benchmarking_extension.h"
#include "content/renderer/stats_collection_controller.h"
#include "content/renderer/web_frame_utils.h"
#include "content/renderer/web_ui_extension.h"
#include "content/renderer/web_ui_extension_data.h"
#include "crypto/sha2.h"
#include "gin/modules/console.h"
#include "gin/modules/module_registry.h"
#include "gin/modules/timer.h"
#include "media/audio/audio_output_device.h"
#include "media/base/audio_renderer_mixer_input.h"
#include "media/base/cdm_factory.h"
#include "media/base/decoder_factory.h"
#include "media/base/media.h"
#include "media/base/media_log.h"
#include "media/base/media_switches.h"
#include "media/blink/url_index.h"
#include "media/blink/webencryptedmediaclient_impl.h"
#include "media/blink/webmediaplayer_impl.h"
#include "media/media_features.h"
#include "media/renderers/gpu_video_accelerator_factories.h"
#include "mojo/edk/js/core.h"
#include "mojo/edk/js/support.h"
#include "net/base/data_url.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/http/http_util.h"
#include "ppapi/features/features.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "services/service_manager/public/cpp/interface_registry.h"
#include "services/ui/public/cpp/gpu/context_provider_command_buffer.h"
#include "storage/common/data_element.h"
#include "third_party/WebKit/public/platform/FilePathConversion.h"
#include "third_party/WebKit/public/platform/URLConversion.h"
#include "third_party/WebKit/public/platform/WebCachePolicy.h"
#include "third_party/WebKit/public/platform/WebData.h"
#include "third_party/WebKit/public/platform/WebKeyboardEvent.h"
#include "third_party/WebKit/public/platform/WebMediaPlayer.h"
#include "third_party/WebKit/public/platform/WebMediaPlayerSource.h"
#include "third_party/WebKit/public/platform/WebPoint.h"
#include "third_party/WebKit/public/platform/WebSecurityOrigin.h"
#include "third_party/WebKit/public/platform/WebStorageQuotaCallbacks.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
#include "third_party/WebKit/public/platform/WebVector.h"
#include "third_party/WebKit/public/platform/modules/serviceworker/WebServiceWorkerNetworkProvider.h"
#include "third_party/WebKit/public/platform/scheduler/renderer/renderer_scheduler.h"
#include "third_party/WebKit/public/web/WebColorSuggestion.h"
#include "third_party/WebKit/public/web/WebConsoleMessage.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFindOptions.h"
#include "third_party/WebKit/public/web/WebFrameOwnerProperties.h"
#include "third_party/WebKit/public/web/WebFrameSerializer.h"
#include "third_party/WebKit/public/web/WebFrameSerializerCacheControlPolicy.h"
#include "third_party/WebKit/public/web/WebFrameWidget.h"
#include "third_party/WebKit/public/web/WebInputMethodController.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebMediaStreamRegistry.h"
#include "third_party/WebKit/public/web/WebNavigationPolicy.h"
#include "third_party/WebKit/public/web/WebPlugin.h"
#include "third_party/WebKit/public/web/WebPluginContainer.h"
#include "third_party/WebKit/public/web/WebPluginDocument.h"
#include "third_party/WebKit/public/web/WebPluginParams.h"
#include "third_party/WebKit/public/web/WebRange.h"
#include "third_party/WebKit/public/web/WebScopedUserGesture.h"
#include "third_party/WebKit/public/web/WebScriptSource.h"
#include "third_party/WebKit/public/web/WebSearchableFormData.h"
#include "third_party/WebKit/public/web/WebSecurityPolicy.h"
#include "third_party/WebKit/public/web/WebSerializedScriptValue.h"
#include "third_party/WebKit/public/web/WebSettings.h"
#include "third_party/WebKit/public/web/WebSurroundingText.h"
#include "third_party/WebKit/public/web/WebUserGestureIndicator.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "third_party/WebKit/public/web/WebWidget.h"
#include "ui/events/base_event_utils.h"
#include "url/origin.h"
#include "url/url_constants.h"
#include "url/url_util.h"

#if BUILDFLAG(ENABLE_PLUGINS)
#include "content/renderer/pepper/pepper_browser_connection.h"
#include "content/renderer/pepper/pepper_plugin_instance_impl.h"
#include "content/renderer/pepper/pepper_plugin_registry.h"
#include "content/renderer/pepper/pepper_webplugin_impl.h"
#include "content/renderer/pepper/plugin_module.h"
#endif

#if BUILDFLAG(ENABLE_WEBRTC)
#include "content/renderer/media/rtc_peer_connection_handler.h"
#endif

#if defined(OS_ANDROID)
#include <cpu-features.h>

#include "content/renderer/java/gin_java_bridge_dispatcher.h"
#include "content/renderer/media/android/media_player_renderer_client_factory.h"
#include "content/renderer/media/android/renderer_media_player_manager.h"
#include "content/renderer/media/android/renderer_surface_view_manager.h"
#include "content/renderer/media/android/stream_texture_factory.h"
#include "content/renderer/media/android/stream_texture_wrapper_impl.h"
#include "media/base/android/media_codec_util.h"
#include "third_party/WebKit/public/platform/WebFloatPoint.h"
#endif

#if BUILDFLAG(ENABLE_PEPPER_CDMS)
#include "content/renderer/media/cdm/pepper_cdm_wrapper_impl.h"
#include "content/renderer/media/cdm/render_cdm_factory.h"
#endif

#if defined(ENABLE_MOJO_MEDIA)
#include "content/renderer/media/media_interface_provider.h"
#endif

#if defined(ENABLE_MOJO_CDM)
#include "media/mojo/clients/mojo_cdm_factory.h"  // nogncheck
#endif

#if defined(ENABLE_MOJO_RENDERER)
#include "media/mojo/clients/mojo_renderer_factory.h"  // nogncheck
#endif

#if !defined(ENABLE_MOJO_RENDERER) || \
    BUILDFLAG(ENABLE_RUNTIME_MEDIA_RENDERER_SELECTION)
#include "media/renderers/default_renderer_factory.h"  // nogncheck
#endif

#if defined(ENABLE_MOJO_AUDIO_DECODER) || defined(ENABLE_MOJO_VIDEO_DECODER)
#include "media/mojo/clients/mojo_decoder_factory.h"  // nogncheck
#endif

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
#include "media/remoting/adaptive_renderer_factory.h"     // nogncheck
#include "media/remoting/remoting_cdm_controller.h"       // nogncheck
#include "media/remoting/remoting_cdm_factory.h"          // nogncheck
#include "media/remoting/renderer_controller.h"           // nogncheck
#include "media/remoting/shared_session.h"                // nogncheck
#include "media/remoting/sink_availability_observer.h"    // nogncheck
#endif

using base::Time;
using base::TimeDelta;
using blink::WebCachePolicy;
using blink::WebContentDecryptionModule;
using blink::WebContextMenuData;
using blink::WebCString;
using blink::WebData;
using blink::WebDataSource;
using blink::WebDocument;
using blink::WebDOMEvent;
using blink::WebDOMMessageEvent;
using blink::WebElement;
using blink::WebExternalPopupMenu;
using blink::WebExternalPopupMenuClient;
using blink::WebFindOptions;
using blink::WebFrame;
using blink::WebFrameLoadType;
using blink::WebFrameSerializer;
using blink::WebFrameSerializerClient;
using blink::WebHistoryItem;
using blink::WebHTTPBody;
using blink::WebLocalFrame;
using blink::WebMediaPlayer;
using blink::WebMediaPlayerClient;
using blink::WebMediaPlayerEncryptedMediaClient;
using blink::WebNavigationPolicy;
using blink::WebNavigationType;
using blink::WebNode;
using blink::WebPluginDocument;
using blink::WebPluginParams;
using blink::WebPoint;
using blink::WebPopupMenuInfo;
using blink::WebRange;
using blink::WebRect;
using blink::WebReferrerPolicy;
using blink::WebScriptSource;
using blink::WebSearchableFormData;
using blink::WebSecurityOrigin;
using blink::WebSecurityPolicy;
using blink::WebSerializedScriptValue;
using blink::WebServiceWorkerProvider;
using blink::WebSettings;
using blink::WebStorageQuotaCallbacks;
using blink::WebString;
using blink::WebThreadSafeData;
using blink::WebURL;
using blink::WebURLError;
using blink::WebURLRequest;
using blink::WebURLResponse;
using blink::WebUserGestureIndicator;
using blink::WebVector;
using blink::WebView;

#if defined(OS_ANDROID)
using blink::WebFloatPoint;
using blink::WebFloatRect;
#endif

#define STATIC_ASSERT_ENUM(a, b)                            \
  static_assert(static_cast<int>(a) == static_cast<int>(b), \
                "mismatching enums: " #a)

namespace content {

namespace {

const int kExtraCharsBeforeAndAfterSelection = 100;

typedef std::map<int, RenderFrameImpl*> RoutingIDFrameMap;
static base::LazyInstance<RoutingIDFrameMap>::DestructorAtExit
    g_routing_id_frame_map = LAZY_INSTANCE_INITIALIZER;

typedef std::map<blink::WebFrame*, RenderFrameImpl*> FrameMap;
base::LazyInstance<FrameMap>::DestructorAtExit g_frame_map =
    LAZY_INSTANCE_INITIALIZER;

int64_t ExtractPostId(const WebHistoryItem& item) {
  if (item.IsNull() || item.HttpBody().IsNull())
    return -1;

  return item.HttpBody().Identifier();
}

WebURLResponseExtraDataImpl* GetExtraDataFromResponse(
    const WebURLResponse& response) {
  return static_cast<WebURLResponseExtraDataImpl*>(response.GetExtraData());
}

void GetRedirectChain(WebDataSource* ds, std::vector<GURL>* result) {
  WebVector<WebURL> urls;
  ds->RedirectChain(urls);
  result->reserve(urls.size());
  for (size_t i = 0; i < urls.size(); ++i) {
    result->push_back(urls[i]);
  }
}

// Gets URL that should override the default getter for this data source
// (if any), storing it in |output|. Returns true if there is an override URL.
bool MaybeGetOverriddenURL(WebDataSource* ds, GURL* output) {
  DocumentState* document_state = DocumentState::FromDataSource(ds);

  // If load was from a data URL, then the saved data URL, not the history
  // URL, should be the URL of the data source.
  if (document_state->was_load_data_with_base_url_request()) {
    *output = document_state->data_url();
    return true;
  }

  // WebDataSource has unreachable URL means that the frame is loaded through
  // blink::WebFrame::loadData(), and the base URL will be in the redirect
  // chain. However, we never visited the baseURL. So in this case, we should
  // use the unreachable URL as the original URL.
  if (ds->HasUnreachableURL()) {
    *output = ds->UnreachableURL();
    return true;
  }

  return false;
}

// Returns the original request url. If there is no redirect, the original
// url is the same as ds->getRequest()->url(). If the WebDataSource belongs to a
// frame was loaded by loadData, the original url will be ds->unreachableURL()
GURL GetOriginalRequestURL(WebDataSource* ds) {
  GURL overriden_url;
  if (MaybeGetOverriddenURL(ds, &overriden_url))
    return overriden_url;

  std::vector<GURL> redirects;
  GetRedirectChain(ds, &redirects);
  if (!redirects.empty())
    return redirects.at(0);

  return ds->OriginalRequest().Url();
}

bool IsBrowserInitiated(NavigationParams* pending) {
  // A navigation resulting from loading a javascript URL should not be treated
  // as a browser initiated event.  Instead, we want it to look as if the page
  // initiated any load resulting from JS execution.
  return pending &&
         !pending->common_params.url.SchemeIs(url::kJavaScriptScheme);
}

// Returns false unless this is a top-level navigation.
bool IsTopLevelNavigation(WebFrame* frame) {
  return frame->Parent() == NULL;
}

WebURLRequest CreateURLRequestForNavigation(
    const CommonNavigationParams& common_params,
    const RequestNavigationParams& request_params,
    std::unique_ptr<StreamOverrideParameters> stream_override,
    bool is_view_source_mode_enabled,
    bool is_same_document_navigation) {
  // PlzNavigate: use the original navigation url to construct the
  // WebURLRequest. The WebURLloaderImpl will replay the redirects afterwards
  // and will eventually commit the final url.
  const GURL navigation_url = IsBrowserSideNavigationEnabled() &&
                                      !request_params.original_url.is_empty()
                                  ? request_params.original_url
                                  : common_params.url;
  const std::string navigation_method =
      IsBrowserSideNavigationEnabled() &&
              !request_params.original_method.empty()
          ? request_params.original_method
          : common_params.method;
  WebURLRequest request(navigation_url);
  request.SetHTTPMethod(WebString::FromUTF8(navigation_method));

  if (is_view_source_mode_enabled)
    request.SetCachePolicy(WebCachePolicy::kReturnCacheDataElseLoad);

  if (common_params.referrer.url.is_valid()) {
    WebString web_referrer = WebSecurityPolicy::GenerateReferrerHeader(
        common_params.referrer.policy, common_params.url,
        WebString::FromUTF8(common_params.referrer.url.spec()));
    if (!web_referrer.IsEmpty()) {
      request.SetHTTPReferrer(web_referrer, common_params.referrer.policy);
      request.AddHTTPOriginIfNeeded(
          WebSecurityOrigin(url::Origin(common_params.referrer.url)));
    }
  }

  request.SetIsSameDocumentNavigation(is_same_document_navigation);
  request.SetPreviewsState(
      static_cast<WebURLRequest::PreviewsState>(common_params.previews_state));

  RequestExtraData* extra_data = new RequestExtraData();
  extra_data->set_stream_override(std::move(stream_override));
  extra_data->set_navigation_initiated_by_renderer(
      request_params.nav_entry_id == 0);
  request.SetExtraData(extra_data);

  // Set the ui timestamp for this navigation. Currently the timestamp here is
  // only non empty when the navigation was triggered by an Android intent. The
  // timestamp is converted to a double version supported by blink. It will be
  // passed back to the browser in the DidCommitProvisionalLoad and the
  // DocumentLoadComplete IPCs.
  base::TimeDelta ui_timestamp = common_params.ui_timestamp - base::TimeTicks();
  request.SetUiStartTime(ui_timestamp.InSecondsF());
  request.SetInputPerfMetricReportPolicy(
      static_cast<WebURLRequest::InputToLoadPerfMetricReportPolicy>(
          common_params.report_type));
  return request;
}

// Sanitizes the navigation_start timestamp for browser-initiated navigations,
// where the browser possibly has a better notion of start time than the
// renderer. In the case of cross-process navigations, this carries over the
// time of finishing the onbeforeunload handler of the previous page.
// TimeTicks is sometimes not monotonic across processes, and because
// |browser_navigation_start| is likely before this process existed,
// InterProcessTimeTicksConverter won't help. The timestamp is sanitized by
// clamping it to renderer_navigation_start, initialized earlier in the call
// stack.
base::TimeTicks SanitizeNavigationTiming(
    const base::TimeTicks& browser_navigation_start,
    const base::TimeTicks& renderer_navigation_start) {
  DCHECK(!browser_navigation_start.is_null());
  return std::min(browser_navigation_start, renderer_navigation_start);
}

// PlzNavigate
CommonNavigationParams MakeCommonNavigationParams(
    const blink::WebFrameClient::NavigationPolicyInfo& info,
    int load_flags) {
  Referrer referrer(
      GURL(info.url_request.HttpHeaderField(WebString::FromUTF8("Referer"))
               .Latin1()),
      info.url_request.GetReferrerPolicy());

  // Set the ui timestamp for this navigation. Currently the timestamp here is
  // only non empty when the navigation was triggered by an Android intent, or
  // by the user clicking on a link. The timestamp is converted from a double
  // version supported by blink. It will be passed back to the renderer in the
  // CommitNavigation IPC, and then back to the browser again in the
  // DidCommitProvisionalLoad and the DocumentLoadComplete IPCs.
  base::TimeTicks ui_timestamp =
      base::TimeTicks() +
      base::TimeDelta::FromSecondsD(info.url_request.UiStartTime());
  FrameMsg_UILoadMetricsReportType::Value report_type =
      static_cast<FrameMsg_UILoadMetricsReportType::Value>(
          info.url_request.InputPerfMetricReportPolicy());

  // No history-navigation is expected to happen.
  DCHECK(info.navigation_type != blink::kWebNavigationTypeBackForward);

  // Determine the navigation type. No same-document navigation is expected
  // because it is loaded immediately by the FrameLoader.
  FrameMsg_Navigate_Type::Value navigation_type =
      FrameMsg_Navigate_Type::DIFFERENT_DOCUMENT;
  if (info.navigation_type == blink::kWebNavigationTypeReload) {
    if (load_flags & net::LOAD_BYPASS_CACHE)
      navigation_type = FrameMsg_Navigate_Type::RELOAD_BYPASSING_CACHE;
    else
      navigation_type = FrameMsg_Navigate_Type::RELOAD;
  }

  base::Optional<SourceLocation> source_location;
  if (!info.source_location.url.IsNull()) {
    source_location = SourceLocation(info.source_location.url.Latin1(),
                                     info.source_location.line_number,
                                     info.source_location.column_number);
  }

  CSPDisposition should_check_main_world_csp =
      info.should_check_main_world_content_security_policy ==
              blink::kWebContentSecurityPolicyDispositionCheck
          ? CSPDisposition::CHECK
          : CSPDisposition::DO_NOT_CHECK;

  const RequestExtraData* extra_data =
      static_cast<RequestExtraData*>(info.url_request.GetExtraData());
  DCHECK(extra_data);
  return CommonNavigationParams(
      info.url_request.Url(), referrer, extra_data->transition_type(),
      navigation_type, true, info.replaces_current_history_item, ui_timestamp,
      report_type, GURL(), GURL(),
      static_cast<PreviewsState>(info.url_request.GetPreviewsState()),
      base::TimeTicks::Now(), info.url_request.HttpMethod().Latin1(),
      GetRequestBodyForWebURLRequest(info.url_request), source_location,
      should_check_main_world_csp);
}

media::Context3D GetSharedMainThreadContext3D(
    scoped_refptr<ui::ContextProviderCommandBuffer> provider) {
  if (!provider)
    return media::Context3D();
  return media::Context3D(provider->ContextGL(), provider->GrContext());
}

WebFrameLoadType ReloadFrameLoadTypeFor(
    FrameMsg_Navigate_Type::Value navigation_type) {
  switch (navigation_type) {
    case FrameMsg_Navigate_Type::RELOAD:
    case FrameMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL:
      return WebFrameLoadType::kReload;

    case FrameMsg_Navigate_Type::RELOAD_BYPASSING_CACHE:
      return WebFrameLoadType::kReloadBypassingCache;

    default:
      NOTREACHED();
      return WebFrameLoadType::kStandard;
  }
}

RenderFrameImpl::CreateRenderFrameImplFunction g_create_render_frame_impl =
    nullptr;

WebString ConvertRelativePathToHtmlAttribute(const base::FilePath& path) {
  DCHECK(!path.IsAbsolute());
  return WebString::FromUTF8(
      std::string("./") +
      path.NormalizePathSeparatorsTo(FILE_PATH_LITERAL('/')).AsUTF8Unsafe());
}

// Implementation of WebFrameSerializer::LinkRewritingDelegate that responds
// based on the payload of FrameMsg_GetSerializedHtmlWithLocalLinks.
class LinkRewritingDelegate : public WebFrameSerializer::LinkRewritingDelegate {
 public:
  LinkRewritingDelegate(
      const std::map<GURL, base::FilePath>& url_to_local_path,
      const std::map<int, base::FilePath>& frame_routing_id_to_local_path)
      : url_to_local_path_(url_to_local_path),
        frame_routing_id_to_local_path_(frame_routing_id_to_local_path) {}

  bool RewriteFrameSource(WebFrame* frame, WebString* rewritten_link) override {
    int routing_id = GetRoutingIdForFrameOrProxy(frame);
    auto it = frame_routing_id_to_local_path_.find(routing_id);
    if (it == frame_routing_id_to_local_path_.end())
      return false;  // This can happen because of https://crbug.com/541354.

    const base::FilePath& local_path = it->second;
    *rewritten_link = ConvertRelativePathToHtmlAttribute(local_path);
    return true;
  }

  bool RewriteLink(const WebURL& url, WebString* rewritten_link) override {
    auto it = url_to_local_path_.find(url);
    if (it == url_to_local_path_.end())
      return false;

    const base::FilePath& local_path = it->second;
    *rewritten_link = ConvertRelativePathToHtmlAttribute(local_path);
    return true;
  }

 private:
  const std::map<GURL, base::FilePath>& url_to_local_path_;
  const std::map<int, base::FilePath>& frame_routing_id_to_local_path_;
};

// Implementation of WebFrameSerializer::MHTMLPartsGenerationDelegate that
// 1. Bases shouldSkipResource and getContentID responses on contents of
//    FrameMsg_SerializeAsMHTML_Params.
// 2. Stores digests of urls of serialized resources (i.e. urls reported via
//    shouldSkipResource) into |serialized_resources_uri_digests| passed
//    to the constructor.
class MHTMLPartsGenerationDelegate
    : public WebFrameSerializer::MHTMLPartsGenerationDelegate {
 public:
  MHTMLPartsGenerationDelegate(
      const FrameMsg_SerializeAsMHTML_Params& params,
      std::set<std::string>* serialized_resources_uri_digests)
      : params_(params),
        serialized_resources_uri_digests_(serialized_resources_uri_digests) {
    DCHECK(serialized_resources_uri_digests_);
  }

  bool ShouldSkipResource(const WebURL& url) override {
    std::string digest =
        crypto::SHA256HashString(params_.salt + GURL(url).spec());

    // Skip if the |url| already covered by serialization of an *earlier* frame.
    if (base::ContainsKey(params_.digests_of_uris_to_skip, digest))
      return true;

    // Let's record |url| as being serialized for the *current* frame.
    auto pair = serialized_resources_uri_digests_->insert(digest);
    bool insertion_took_place = pair.second;
    DCHECK(insertion_took_place);  // Blink should dedupe within a frame.

    return false;
  }

  WebString GetContentID(WebFrame* frame) override {
    int routing_id = GetRoutingIdForFrameOrProxy(frame);

    auto it = params_.frame_routing_id_to_content_id.find(routing_id);
    if (it == params_.frame_routing_id_to_content_id.end())
      return WebString();

    const std::string& content_id = it->second;
    return WebString::FromUTF8(content_id);
  }

  blink::WebFrameSerializerCacheControlPolicy CacheControlPolicy() override {
    return params_.mhtml_cache_control_policy;
  }

  bool UseBinaryEncoding() override { return params_.mhtml_binary_encoding; }

  bool RemovePopupOverlay() override {
    return params_.mhtml_popup_overlay_removal;
  }

 private:
  const FrameMsg_SerializeAsMHTML_Params& params_;
  std::set<std::string>* serialized_resources_uri_digests_;

  DISALLOW_COPY_AND_ASSIGN(MHTMLPartsGenerationDelegate);
};

bool IsHttpPost(const blink::WebURLRequest& request) {
  return request.HttpMethod().Utf8() == "POST";
}

// Writes to file the serialized and encoded MHTML data from WebThreadSafeData
// instances.
MhtmlSaveStatus WriteMHTMLToDisk(std::vector<WebThreadSafeData> mhtml_contents,
                                 base::File file) {
  TRACE_EVENT0("page-serialization", "WriteMHTMLToDisk (RenderFrameImpl)");
  SCOPED_UMA_HISTOGRAM_TIMER(
      "PageSerialization.MhtmlGeneration.WriteToDiskTime.SingleFrame");
  DCHECK(!RenderThread::Get()) << "Should not run in the main renderer thread";
  MhtmlSaveStatus save_status = MhtmlSaveStatus::SUCCESS;
  for (const WebThreadSafeData& data : mhtml_contents) {
    if (!data.IsEmpty() &&
        file.WriteAtCurrentPos(data.Data(), data.size()) < 0) {
      save_status = MhtmlSaveStatus::FILE_WRITTING_ERROR;
      break;
    }
  }
  // Explicitly close |file| here to make sure to include any flush operations
  // in the UMA metric.
  file.Close();
  return save_status;
}

#if defined(OS_ANDROID)
// Returns true if the MediaPlayerRenderer should be used for playback, false
// if the default renderer should be used instead.
//
// Note that HLS and MP4 detection are pre-redirect and path-based. It is
// possible to load such a URL and find different content.
bool UseMediaPlayerRenderer(const GURL& url) {
  // Always use the default renderer for playing blob URLs.
  if (url.SchemeIsBlob())
    return false;

  // The default renderer does not support HLS.
  if (media::MediaCodecUtil::IsHLSURL(url))
    return true;

  // Don't use the default renderer if the container likely contains a codec we
  // can't decode in software and platform decoders are not available.
  if (!media::HasPlatformDecoderSupport()) {
    // Assume that "mp4" means H264. Without platform decoder support we cannot
    // play it with the default renderer so use MediaPlayerRenderer.
    // http://crbug.com/642988.
    if (base::ToLowerASCII(url.spec()).find("mp4") != std::string::npos)
      return true;
  }

  // Indicates if the Android MediaPlayer should be used instead of WMPI.
  if (GetContentClient()->renderer()->ShouldUseMediaPlayerForURL(url))
    return true;

  // Otherwise, use the default renderer.
  return false;
}
#endif  // defined(OS_ANDROID)

double ConvertToBlinkTime(const base::TimeTicks& time_ticks) {
  return (time_ticks - base::TimeTicks()).InSecondsF();
}

}  // namespace

// The following methods are outside of the anonymous namespace to ensure that
// the corresponding symbols get emmitted even on symbol_level 1.
NOINLINE void ExhaustMemory() {
  volatile void* ptr = nullptr;
  do {
    ptr = malloc(0x10000000);
    base::debug::Alias(&ptr);
  } while (ptr);
}

NOINLINE void CrashIntentionally() {
  // NOTE(shess): Crash directly rather than using NOTREACHED() so
  // that the signature is easier to triage in crash reports.
  //
  // Linker's ICF feature may merge this function with other functions with the
  // same definition and it may confuse the crash report processing system.
  static int static_variable_to_make_this_function_unique = 0;
  base::debug::Alias(&static_variable_to_make_this_function_unique);

  volatile int* zero = nullptr;
  *zero = 0;
}

NOINLINE void BadCastCrashIntentionally() {
  class A {
    virtual void f() {}
  };

  class B {
    virtual void f() {}
  };

  A a;
  (void)(B*) & a;
}

#if defined(ADDRESS_SANITIZER) || defined(SYZYASAN)
NOINLINE void MaybeTriggerAsanError(const GURL& url) {
  // NOTE(rogerm): We intentionally perform an invalid heap access here in
  //     order to trigger an Address Sanitizer (ASAN) error report.
  const char kCrashDomain[] = "crash";
  const char kHeapOverflow[] = "/heap-overflow";
  const char kHeapUnderflow[] = "/heap-underflow";
  const char kUseAfterFree[] = "/use-after-free";
#if defined(SYZYASAN)
  const char kCorruptHeapBlock[] = "/corrupt-heap-block";
  const char kCorruptHeap[] = "/corrupt-heap";
#endif

  if (!url.DomainIs(kCrashDomain))
    return;

  if (!url.has_path())
    return;

  std::string crash_type(url.path());
  if (crash_type == kHeapOverflow) {
    LOG(ERROR) << "Intentionally causing ASAN heap overflow"
               << " because user navigated to " << url.spec();
    base::debug::AsanHeapOverflow();
  } else if (crash_type == kHeapUnderflow) {
    LOG(ERROR) << "Intentionally causing ASAN heap underflow"
               << " because user navigated to " << url.spec();
    base::debug::AsanHeapUnderflow();
  } else if (crash_type == kUseAfterFree) {
    LOG(ERROR) << "Intentionally causing ASAN heap use-after-free"
               << " because user navigated to " << url.spec();
    base::debug::AsanHeapUseAfterFree();
#if defined(SYZYASAN)
  } else if (crash_type == kCorruptHeapBlock) {
    LOG(ERROR) << "Intentionally causing ASAN corrupt heap block"
               << " because user navigated to " << url.spec();
    base::debug::AsanCorruptHeapBlock();
  } else if (crash_type == kCorruptHeap) {
    LOG(ERROR) << "Intentionally causing ASAN corrupt heap"
               << " because user navigated to " << url.spec();
    base::debug::AsanCorruptHeap();
#endif
  }
}
#endif  // ADDRESS_SANITIZER || SYZYASAN

void MaybeHandleDebugURL(const GURL& url) {
  if (!url.SchemeIs(kChromeUIScheme))
    return;
  if (url == kChromeUIBadCastCrashURL) {
    LOG(ERROR) << "Intentionally crashing (with bad cast)"
               << " because user navigated to " << url.spec();
    BadCastCrashIntentionally();
  } else if (url == kChromeUICrashURL) {
    LOG(ERROR) << "Intentionally crashing (with null pointer dereference)"
               << " because user navigated to " << url.spec();
    CrashIntentionally();
  } else if (url == kChromeUIDumpURL) {
    // This URL will only correctly create a crash dump file if content is
    // hosted in a process that has correctly called
    // base::debug::SetDumpWithoutCrashingFunction.  Refer to the documentation
    // of base::debug::DumpWithoutCrashing for more details.
    base::debug::DumpWithoutCrashing();
  } else if (url == kChromeUIKillURL) {
    LOG(ERROR) << "Intentionally issuing kill signal to current process"
               << " because user navigated to " << url.spec();
    base::Process::Current().Terminate(1, false);
  } else if (url == kChromeUIHangURL) {
    LOG(ERROR) << "Intentionally hanging ourselves with sleep infinite loop"
               << " because user navigated to " << url.spec();
    for (;;) {
      base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
    }
  } else if (url == kChromeUIShorthangURL) {
    LOG(ERROR) << "Intentionally sleeping renderer for 20 seconds"
               << " because user navigated to " << url.spec();
    base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(20));
  } else if (url == kChromeUIMemoryExhaustURL) {
    LOG(ERROR)
        << "Intentionally exhausting renderer memory because user navigated to "
        << url.spec();
    ExhaustMemory();
  } else if (url == kChromeUICheckCrashURL) {
    LOG(ERROR) << "Intentionally causing CHECK because user navigated to "
               << url.spec();
    CHECK(false);
  }

#if defined(ADDRESS_SANITIZER) || defined(SYZYASAN)
  MaybeTriggerAsanError(url);
#endif  // ADDRESS_SANITIZER || SYZYASAN
}

struct RenderFrameImpl::PendingFileChooser {
  PendingFileChooser(const FileChooserParams& p,
                     blink::WebFileChooserCompletion* c)
      : params(p), completion(c) {}
  FileChooserParams params;
  blink::WebFileChooserCompletion* completion;  // MAY BE NULL to skip callback.
};

// static
RenderFrameImpl* RenderFrameImpl::Create(RenderViewImpl* render_view,
                                         int32_t routing_id) {
  DCHECK(routing_id != MSG_ROUTING_NONE);
  CreateParams params(render_view, routing_id);

  if (g_create_render_frame_impl)
    return g_create_render_frame_impl(params);
  else
    return new RenderFrameImpl(params);
}

// static
RenderFrame* RenderFrame::FromRoutingID(int routing_id) {
  return RenderFrameImpl::FromRoutingID(routing_id);
}

// static
RenderFrameImpl* RenderFrameImpl::FromRoutingID(int routing_id) {
  RoutingIDFrameMap::iterator iter =
      g_routing_id_frame_map.Get().find(routing_id);
  if (iter != g_routing_id_frame_map.Get().end())
    return iter->second;
  return NULL;
}

// static
RenderFrameImpl* RenderFrameImpl::CreateMainFrame(
    RenderViewImpl* render_view,
    int32_t routing_id,
    int32_t widget_routing_id,
    bool hidden,
    const ScreenInfo& screen_info,
    CompositorDependencies* compositor_deps,
    blink::WebFrame* opener) {
  // A main frame RenderFrame must have a RenderWidget.
  DCHECK_NE(MSG_ROUTING_NONE, widget_routing_id);

  RenderFrameImpl* render_frame =
      RenderFrameImpl::Create(render_view, routing_id);
  render_frame->InitializeBlameContext(nullptr);
  WebLocalFrame* web_frame = WebLocalFrame::Create(
      blink::WebTreeScopeType::kDocument, render_frame,
      render_frame->blink_interface_provider_.get(),
      render_frame->blink_interface_registry_.get(), opener);
  render_frame->BindToWebFrame(web_frame);
  render_view->webview()->SetMainFrame(web_frame);
  render_frame->render_widget_ = RenderWidget::CreateForFrame(
      widget_routing_id, hidden, screen_info, compositor_deps, web_frame);
  // TODO(avi): This DCHECK is to track cleanup for https://crbug.com/545684
  DCHECK_EQ(render_view->GetWidget(), render_frame->render_widget_)
      << "Main frame is no longer reusing the RenderView as its widget! "
      << "Does the RenderFrame need to register itself with the RenderWidget?";
  return render_frame;
}

// static
void RenderFrameImpl::CreateFrame(
    int routing_id,
    int proxy_routing_id,
    int opener_routing_id,
    int parent_routing_id,
    int previous_sibling_routing_id,
    const FrameReplicationState& replicated_state,
    CompositorDependencies* compositor_deps,
    const mojom::CreateFrameWidgetParams& widget_params,
    const FrameOwnerProperties& frame_owner_properties) {
  blink::WebLocalFrame* web_frame;
  RenderFrameImpl* render_frame;
  if (proxy_routing_id == MSG_ROUTING_NONE) {
    RenderFrameProxy* parent_proxy =
        RenderFrameProxy::FromRoutingID(parent_routing_id);
    // If the browser is sending a valid parent routing id, it should already
    // be created and registered.
    CHECK(parent_proxy);
    blink::WebRemoteFrame* parent_web_frame = parent_proxy->web_frame();

    blink::WebFrame* previous_sibling_web_frame = nullptr;
    RenderFrameProxy* previous_sibling_proxy =
        RenderFrameProxy::FromRoutingID(previous_sibling_routing_id);
    if (previous_sibling_proxy)
      previous_sibling_web_frame = previous_sibling_proxy->web_frame();

    // Create the RenderFrame and WebLocalFrame, linking the two.
    render_frame =
        RenderFrameImpl::Create(parent_proxy->render_view(), routing_id);
    render_frame->InitializeBlameContext(FromRoutingID(parent_routing_id));
    render_frame->unique_name_helper_.set_propagated_name(
        replicated_state.unique_name);
    web_frame = parent_web_frame->CreateLocalChild(
        replicated_state.scope, WebString::FromUTF8(replicated_state.name),
        replicated_state.sandbox_flags, render_frame,
        render_frame->blink_interface_provider_.get(),
        render_frame->blink_interface_registry_.get(),
        previous_sibling_web_frame,
        ConvertFrameOwnerPropertiesToWebFrameOwnerProperties(
            frame_owner_properties),
        ResolveOpener(opener_routing_id));

    // The RenderFrame is created and inserted into the frame tree in the above
    // call to createLocalChild.
    render_frame->in_frame_tree_ = true;
  } else {
    RenderFrameProxy* proxy =
        RenderFrameProxy::FromRoutingID(proxy_routing_id);
    // The remote frame could've been detached while the remote-to-local
    // navigation was being initiated in the browser process. Drop the
    // navigation and don't create the frame in that case.  See
    // https://crbug.com/526304.
    if (!proxy)
      return;

    render_frame = RenderFrameImpl::Create(proxy->render_view(), routing_id);
    render_frame->InitializeBlameContext(nullptr);
    render_frame->proxy_routing_id_ = proxy_routing_id;
    proxy->set_provisional_frame_routing_id(routing_id);
    web_frame = blink::WebLocalFrame::CreateProvisional(
        render_frame, render_frame->blink_interface_provider_.get(),
        render_frame->blink_interface_registry_.get(), proxy->web_frame(),
        replicated_state.sandbox_flags);
  }
  render_frame->BindToWebFrame(web_frame);
  CHECK(parent_routing_id != MSG_ROUTING_NONE || !web_frame->Parent());

  if (widget_params.routing_id != MSG_ROUTING_NONE) {
    CHECK(!web_frame->Parent() ||
          SiteIsolationPolicy::AreCrossProcessFramesPossible());
    render_frame->render_widget_ = RenderWidget::CreateForFrame(
        widget_params.routing_id, widget_params.hidden,
        render_frame->render_view_->screen_info(), compositor_deps, web_frame);
    // TODO(avi): The main frame re-uses the RenderViewImpl as its widget, so
    // avoid double-registering the frame as an observer.
    // https://crbug.com/545684
    if (web_frame->Parent())
      render_frame->render_widget_->RegisterRenderFrame(render_frame);
  }

  render_frame->Initialize();
}

// static
RenderFrame* RenderFrame::FromWebFrame(blink::WebFrame* web_frame) {
  return RenderFrameImpl::FromWebFrame(web_frame);
}

// static
void RenderFrame::ForEach(RenderFrameVisitor* visitor) {
  FrameMap* frames = g_frame_map.Pointer();
  for (FrameMap::iterator it = frames->begin(); it != frames->end(); ++it) {
    if (!visitor->Visit(it->second))
      return;
  }
}

// static
RenderFrameImpl* RenderFrameImpl::FromWebFrame(blink::WebFrame* web_frame) {
  FrameMap::iterator iter = g_frame_map.Get().find(web_frame);
  if (iter != g_frame_map.Get().end())
    return iter->second;
  return NULL;
}

// static
void RenderFrameImpl::InstallCreateHook(
    CreateRenderFrameImplFunction create_render_frame_impl) {
  CHECK(!g_create_render_frame_impl);
  g_create_render_frame_impl = create_render_frame_impl;
}

// static
blink::WebFrame* RenderFrameImpl::ResolveOpener(int opener_frame_routing_id) {
  if (opener_frame_routing_id == MSG_ROUTING_NONE)
    return nullptr;

  // Opener routing ID could refer to either a RenderFrameProxy or a
  // RenderFrame, so need to check both.
  RenderFrameProxy* opener_proxy =
      RenderFrameProxy::FromRoutingID(opener_frame_routing_id);
  if (opener_proxy)
    return opener_proxy->web_frame();

  RenderFrameImpl* opener_frame =
      RenderFrameImpl::FromRoutingID(opener_frame_routing_id);
  if (opener_frame)
    return opener_frame->GetWebFrame();

  return nullptr;
}

blink::WebURL RenderFrameImpl::OverrideFlashEmbedWithHTML(
    const blink::WebURL& url) {
  return GetContentClient()->renderer()->OverrideFlashEmbedWithHTML(url);
}

// RenderFrameImpl ----------------------------------------------------------
RenderFrameImpl::RenderFrameImpl(const CreateParams& params)
    : frame_(NULL),
      is_main_frame_(true),
      unique_name_helper_(this),
      in_browser_initiated_detach_(false),
      in_frame_tree_(false),
      render_view_(params.render_view),
      routing_id_(params.routing_id),
      proxy_routing_id_(MSG_ROUTING_NONE),
#if BUILDFLAG(ENABLE_PLUGINS)
      plugin_power_saver_helper_(nullptr),
      plugin_find_handler_(nullptr),
#endif
      cookie_jar_(this),
      selection_text_offset_(0),
      selection_range_(gfx::Range::InvalidRange()),
      handling_select_range_(false),
      web_user_media_client_(NULL),
#if defined(OS_ANDROID)
      media_player_manager_(NULL),
#endif
      media_surface_manager_(nullptr),
      devtools_agent_(nullptr),
      presentation_dispatcher_(NULL),
      push_messaging_client_(NULL),
      screen_orientation_dispatcher_(NULL),
      manifest_manager_(NULL),
      render_accessibility_(NULL),
      media_player_delegate_(NULL),
      previews_state_(PREVIEWS_UNSPECIFIED),
      effective_connection_type_(
          blink::WebEffectiveConnectionType::kTypeUnknown),
      is_pasting_(false),
      suppress_further_dialogs_(false),
      blame_context_(nullptr),
#if BUILDFLAG(ENABLE_PLUGINS)
      focused_pepper_plugin_(nullptr),
      pepper_last_mouse_event_target_(nullptr),
#endif
      engagement_binding_(this),
      frame_binding_(this),
      host_zoom_binding_(this),
      frame_bindings_control_binding_(this),
      has_accessed_initial_document_(false),
      weak_factory_(this) {
  // We don't have a service_manager::Connection at this point, so use empty
  // identity/specs.
  // TODO(beng): We should fix this, so we can apply policy about which
  //             interfaces get exposed.
  interface_registry_ = base::MakeUnique<service_manager::InterfaceRegistry>(
      mojom::kNavigation_FrameSpec);
  service_manager::mojom::InterfaceProviderPtr remote_interfaces;
  pending_remote_interface_provider_request_ = MakeRequest(&remote_interfaces);
  remote_interfaces_.reset(new service_manager::InterfaceProvider);
  remote_interfaces_->Bind(std::move(remote_interfaces));
  blink_interface_provider_.reset(new BlinkInterfaceProviderImpl(
      remote_interfaces_->GetWeakPtr()));
  blink_interface_registry_.reset(
      new BlinkInterfaceRegistryImpl(interface_registry_->GetWeakPtr()));

  std::pair<RoutingIDFrameMap::iterator, bool> result =
      g_routing_id_frame_map.Get().insert(std::make_pair(routing_id_, this));
  CHECK(result.second) << "Inserting a duplicate item.";

  RenderThread::Get()->AddRoute(routing_id_, this);

  render_view_->RegisterRenderFrame(this);

  // Everything below subclasses RenderFrameObserver and is automatically
  // deleted when the RenderFrame gets deleted.
#if defined(OS_ANDROID)
  new GinJavaBridgeDispatcher(this);
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
  // Manages its own lifetime.
  plugin_power_saver_helper_ = new PluginPowerSaverHelper(this);
#endif

  manifest_manager_ = new ManifestManager(this);

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  // Create the SinkAvailabilityObserver to monitor the remoting sink
  // availablity.
  media::mojom::RemotingSourcePtr remoting_source;
  media::mojom::RemotingSourceRequest remoting_source_request(&remoting_source);
  media::mojom::RemoterPtr remoter;
  GetRemoterFactory()->Create(std::move(remoting_source),
                              mojo::MakeRequest(&remoter));
  remoting_sink_observer_ =
      base::MakeUnique<media::remoting::SinkAvailabilityObserver>(
          std::move(remoting_source_request), std::move(remoter));
#endif  // BUILDFLAG(ENABLE_MEDIA_REMOTING)
}

RenderFrameImpl::~RenderFrameImpl() {
  // If file chooser is still waiting for answer, dispatch empty answer.
  while (!file_chooser_completions_.empty()) {
    if (file_chooser_completions_.front()->completion) {
      file_chooser_completions_.front()->completion->DidChooseFile(
          WebVector<WebString>());
    }
    file_chooser_completions_.pop_front();
  }

  for (auto& observer : observers_)
    observer.RenderFrameGone();
  for (auto& observer : observers_)
    observer.OnDestruct();

  base::trace_event::TraceLog::GetInstance()->RemoveProcessLabel(routing_id_);

  // Unregister from InputHandlerManager. render_thread may be NULL in tests.
  RenderThreadImpl* render_thread = RenderThreadImpl::current();
  InputHandlerManager* input_handler_manager =
      render_thread ? render_thread->input_handler_manager() : nullptr;
  if (input_handler_manager)
    input_handler_manager->UnregisterRoutingID(GetRoutingID());

  if (is_main_frame_) {
    // Ensure the RenderView doesn't point to this object, once it is destroyed.
    // TODO(nasko): Add a check that the |main_render_frame_| of |render_view_|
    // is |this|, once the object is no longer leaked.
    // See https://crbug.com/464764.
    render_view_->main_render_frame_ = nullptr;
  }

  render_view_->UnregisterRenderFrame(this);
  g_routing_id_frame_map.Get().erase(routing_id_);
  RenderThread::Get()->RemoveRoute(routing_id_);
}

void RenderFrameImpl::BindToWebFrame(blink::WebLocalFrame* web_frame) {
  DCHECK(!frame_);

  std::pair<FrameMap::iterator, bool> result = g_frame_map.Get().insert(
      std::make_pair(web_frame, this));
  CHECK(result.second) << "Inserting a duplicate item.";

  frame_ = web_frame;
}

void RenderFrameImpl::Initialize() {
  is_main_frame_ = !frame_->Parent();

  RenderFrameImpl* parent_frame =
      RenderFrameImpl::FromWebFrame(frame_->Parent());
  if (parent_frame) {
    previews_state_ = parent_frame->GetPreviewsState();
    effective_connection_type_ = parent_frame->GetEffectiveConnectionType();
  }

  bool is_tracing_rail = false;
  bool is_tracing_navigation = false;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("navigation", &is_tracing_navigation);
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("rail", &is_tracing_rail);
  if (is_tracing_rail || is_tracing_navigation) {
    int parent_id = GetRoutingIdForFrameOrProxy(frame_->Parent());
    TRACE_EVENT2("navigation,rail", "RenderFrameImpl::Initialize",
                 "id", routing_id_,
                 "parent", parent_id);
  }

#if BUILDFLAG(ENABLE_PLUGINS)
  new PepperBrowserConnection(this);
#endif
  shared_worker_repository_ = base::MakeUnique<SharedWorkerRepository>(this);
  GetWebFrame()->SetSharedWorkerRepositoryClient(
      shared_worker_repository_.get());

  if (IsLocalRoot()) {
    // DevToolsAgent is a RenderFrameObserver, and will destruct itself
    // when |this| is deleted.
    devtools_agent_ = new DevToolsAgent(this);
  }

  RegisterMojoInterfaces();

  // We delay calling this until we have the WebFrame so that any observer or
  // embedder can call GetWebFrame on any RenderFrame.
  GetContentClient()->renderer()->RenderFrameCreated(this);

  RenderThreadImpl* render_thread = RenderThreadImpl::current();
  // render_thread may be NULL in tests.
  InputHandlerManager* input_handler_manager =
      render_thread ? render_thread->input_handler_manager() : nullptr;
  if (input_handler_manager) {
    DCHECK(render_view_->HasAddedInputHandler());
    input_handler_manager->RegisterAssociatedRenderFrameRoutingID(
        GetRoutingID(), render_view_->GetRoutingID());
  }

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kDomAutomationController))
    enabled_bindings_ |= BINDINGS_POLICY_DOM_AUTOMATION;
  if (command_line.HasSwitch(switches::kStatsCollectionController))
    enabled_bindings_ |= BINDINGS_POLICY_STATS_COLLECTION;
}

void RenderFrameImpl::InitializeBlameContext(RenderFrameImpl* parent_frame) {
  DCHECK(!blame_context_);
  blame_context_ = base::MakeUnique<FrameBlameContext>(this, parent_frame);
  blame_context_->Initialize();
}

RenderWidget* RenderFrameImpl::GetRenderWidget() {
  return GetLocalRoot()->render_widget_.get();
}

#if BUILDFLAG(ENABLE_PLUGINS)
void RenderFrameImpl::PepperPluginCreated(RendererPpapiHost* host) {
  for (auto& observer : observers_)
    observer.DidCreatePepperPlugin(host);
}

void RenderFrameImpl::PepperDidChangeCursor(
    PepperPluginInstanceImpl* instance,
    const blink::WebCursorInfo& cursor) {
  // Update the cursor appearance immediately if the requesting plugin is the
  // one which receives the last mouse event. Otherwise, the new cursor won't be
  // picked up until the plugin gets the next input event. That is bad if, e.g.,
  // the plugin would like to set an invisible cursor when there isn't any user
  // input for a while.
  if (instance == pepper_last_mouse_event_target_)
    GetRenderWidget()->DidChangeCursor(cursor);
}

void RenderFrameImpl::PepperDidReceiveMouseEvent(
    PepperPluginInstanceImpl* instance) {
  set_pepper_last_mouse_event_target(instance);
}

void RenderFrameImpl::PepperTextInputTypeChanged(
    PepperPluginInstanceImpl* instance) {
  if (instance != focused_pepper_plugin_)
    return;

  GetRenderWidget()->UpdateTextInputState();

  FocusedNodeChangedForAccessibility(WebNode());
}

void RenderFrameImpl::PepperCaretPositionChanged(
    PepperPluginInstanceImpl* instance) {
  if (instance != focused_pepper_plugin_)
    return;
  GetRenderWidget()->UpdateSelectionBounds();
}

void RenderFrameImpl::PepperCancelComposition(
    PepperPluginInstanceImpl* instance) {
  if (instance != focused_pepper_plugin_)
    return;
  Send(new InputHostMsg_ImeCancelComposition(render_view_->GetRoutingID()));
#if defined(OS_MACOSX) || defined(USE_AURA)
  GetRenderWidget()->UpdateCompositionInfo(
      false /* not an immediate request */);
#endif
}

void RenderFrameImpl::PepperSelectionChanged(
    PepperPluginInstanceImpl* instance) {
  if (instance != focused_pepper_plugin_)
    return;
  SyncSelectionIfRequired();
}

RenderWidgetFullscreenPepper* RenderFrameImpl::CreatePepperFullscreenContainer(
    PepperPluginInstanceImpl* plugin) {
  GURL active_url;
  if (render_view()->webview())
    active_url = render_view()->GetURLForGraphicsContext3D();

  // Synchronous IPC to obtain a routing id for the fullscreen widget.
  int32_t fullscreen_widget_routing_id = MSG_ROUTING_NONE;
  if (!RenderThreadImpl::current_render_message_filter()
           ->CreateFullscreenWidget(render_view()->routing_id(),
                                    &fullscreen_widget_routing_id)) {
    return nullptr;
  }
  RenderWidget::ShowCallback show_callback =
      base::Bind(&RenderViewImpl::ShowCreatedFullscreenWidget,
                 render_view()->GetWeakPtr());

  RenderWidgetFullscreenPepper* widget = RenderWidgetFullscreenPepper::Create(
      fullscreen_widget_routing_id, show_callback,
      GetRenderWidget()->compositor_deps(), plugin, active_url,
      GetRenderWidget()->screen_info());
  // TODO(nick): The show() handshake seems like unnecessary complexity here,
  // since there's no real delay between CreateFullscreenWidget and
  // ShowCreatedFullscreenWidget. Would it be simpler to have the
  // CreateFullscreenWidget mojo method implicitly show the window, and skip the
  // subsequent step?
  widget->Show(blink::kWebNavigationPolicyIgnore);
  return widget;
}

bool RenderFrameImpl::IsPepperAcceptingCompositionEvents() const {
  if (!focused_pepper_plugin_)
    return false;
  return focused_pepper_plugin_->IsPluginAcceptingCompositionEvents();
}

void RenderFrameImpl::PluginCrashed(const base::FilePath& plugin_path,
                                   base::ProcessId plugin_pid) {
  // TODO(jam): dispatch this IPC in RenderFrameHost and switch to use
  // routing_id_ as a result.
  Send(new FrameHostMsg_PluginCrashed(routing_id_, plugin_path, plugin_pid));
}

void RenderFrameImpl::SimulateImeSetComposition(
    const base::string16& text,
    const std::vector<blink::WebCompositionUnderline>& underlines,
    int selection_start,
    int selection_end) {
  render_view_->OnImeSetComposition(
      text, underlines, gfx::Range::InvalidRange(),
      selection_start, selection_end);
}

void RenderFrameImpl::SimulateImeCommitText(
    const base::string16& text,
    const std::vector<blink::WebCompositionUnderline>& underlines,
    const gfx::Range& replacement_range) {
  render_view_->OnImeCommitText(text, underlines, replacement_range, 0);
}

void RenderFrameImpl::SimulateImeFinishComposingText(bool keep_selection) {
  render_view_->OnImeFinishComposingText(keep_selection);
}

void RenderFrameImpl::OnImeSetComposition(
    const base::string16& text,
    const std::vector<blink::WebCompositionUnderline>& underlines,
    int selection_start,
    int selection_end) {
  // When a PPAPI plugin has focus, we bypass WebKit.
  if (!IsPepperAcceptingCompositionEvents()) {
    pepper_composition_text_ = text;
  } else {
    // TODO(kinaba) currently all composition events are sent directly to
    // plugins. Use DOM event mechanism after WebKit is made aware about
    // plugins that support composition.
    // The code below mimics the behavior of WebCore::Editor::setComposition.

    // Empty -> nonempty: composition started.
    if (pepper_composition_text_.empty() && !text.empty()) {
      focused_pepper_plugin_->HandleCompositionStart(base::string16());
    }
    // Nonempty -> empty: composition canceled.
    if (!pepper_composition_text_.empty() && text.empty()) {
      focused_pepper_plugin_->HandleCompositionEnd(base::string16());
    }
    pepper_composition_text_ = text;
    // Nonempty: composition is ongoing.
    if (!pepper_composition_text_.empty()) {
      focused_pepper_plugin_->HandleCompositionUpdate(
          pepper_composition_text_, underlines, selection_start, selection_end);
    }
  }
}

void RenderFrameImpl::OnImeCommitText(const base::string16& text,
                                      const gfx::Range& replacement_range,
                                      int relative_cursor_pos) {
  HandlePepperImeCommit(text);
}

void RenderFrameImpl::OnImeFinishComposingText(bool keep_selection) {
  const base::string16& text = pepper_composition_text_;
  HandlePepperImeCommit(text);
}
#endif  // BUILDFLAG(ENABLE_PLUGINS)

MediaStreamDispatcher* RenderFrameImpl::GetMediaStreamDispatcher() {
  if (!web_user_media_client_)
    InitializeUserMediaClient();
  return web_user_media_client_
             ? web_user_media_client_->media_stream_dispatcher()
             : nullptr;
}

void RenderFrameImpl::ScriptedPrint(bool user_initiated) {
  for (auto& observer : observers_)
    observer.ScriptedPrint(user_initiated);
}

bool RenderFrameImpl::Send(IPC::Message* message) {
  return RenderThread::Get()->Send(message);
}

#if BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
void RenderFrameImpl::DidHideExternalPopupMenu() {
  // We need to clear external_popup_menu_ as soon as ExternalPopupMenu::close
  // is called. Otherwise, createExternalPopupMenu() for new popup will fail.
  external_popup_menu_.reset();
}
#endif

bool RenderFrameImpl::OnMessageReceived(const IPC::Message& msg) {
  // Forward Page IPCs to the RenderView.
  if ((IPC_MESSAGE_CLASS(msg) == PageMsgStart)) {
    if (render_view())
      return render_view()->OnMessageReceived(msg);

    return false;
  }

  // We may get here while detaching, when the WebFrame has been deleted.  Do
  // not process any messages in this state.
  if (!frame_)
    return false;

  DCHECK(!frame_->GetDocument().IsNull());

  GetContentClient()->SetActiveURL(frame_->GetDocument().Url());

  for (auto& observer : observers_) {
    if (observer.OnMessageReceived(msg))
      return true;
  }

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(RenderFrameImpl, msg)
    IPC_MESSAGE_HANDLER(FrameMsg_Navigate, OnNavigate)
    IPC_MESSAGE_HANDLER(FrameMsg_BeforeUnload, OnBeforeUnload)
    IPC_MESSAGE_HANDLER(FrameMsg_SwapOut, OnSwapOut)
    IPC_MESSAGE_HANDLER(FrameMsg_SwapIn, OnSwapIn)
    IPC_MESSAGE_HANDLER(FrameMsg_Delete, OnDeleteFrame)
    IPC_MESSAGE_HANDLER(FrameMsg_Stop, OnStop)
    IPC_MESSAGE_HANDLER(FrameMsg_ContextMenuClosed, OnContextMenuClosed)
    IPC_MESSAGE_HANDLER(FrameMsg_CustomContextMenuAction,
                        OnCustomContextMenuAction)
#if BUILDFLAG(ENABLE_PLUGINS)
    IPC_MESSAGE_HANDLER(FrameMsg_SetPepperVolume, OnSetPepperVolume)
#endif
    IPC_MESSAGE_HANDLER(InputMsg_Undo, OnUndo)
    IPC_MESSAGE_HANDLER(InputMsg_Redo, OnRedo)
    IPC_MESSAGE_HANDLER(InputMsg_Cut, OnCut)
    IPC_MESSAGE_HANDLER(InputMsg_Copy, OnCopy)
    IPC_MESSAGE_HANDLER(InputMsg_Paste, OnPaste)
    IPC_MESSAGE_HANDLER(InputMsg_PasteAndMatchStyle, OnPasteAndMatchStyle)
    IPC_MESSAGE_HANDLER(InputMsg_Delete, OnDelete)
    IPC_MESSAGE_HANDLER(InputMsg_SelectAll, OnSelectAll)
    IPC_MESSAGE_HANDLER(InputMsg_SelectRange, OnSelectRange)
    IPC_MESSAGE_HANDLER(InputMsg_AdjustSelectionByCharacterOffset,
                        OnAdjustSelectionByCharacterOffset)
    IPC_MESSAGE_HANDLER(InputMsg_CollapseSelection, OnCollapseSelection)
    IPC_MESSAGE_HANDLER(InputMsg_MoveRangeSelectionExtent,
                        OnMoveRangeSelectionExtent)
    IPC_MESSAGE_HANDLER(InputMsg_Replace, OnReplace)
    IPC_MESSAGE_HANDLER(InputMsg_ReplaceMisspelling, OnReplaceMisspelling)
    IPC_MESSAGE_HANDLER(FrameMsg_CopyImageAt, OnCopyImageAt)
    IPC_MESSAGE_HANDLER(FrameMsg_SaveImageAt, OnSaveImageAt)
    IPC_MESSAGE_HANDLER(InputMsg_ExtendSelectionAndDelete,
                        OnExtendSelectionAndDelete)
    IPC_MESSAGE_HANDLER(InputMsg_DeleteSurroundingText, OnDeleteSurroundingText)
    IPC_MESSAGE_HANDLER(InputMsg_DeleteSurroundingTextInCodePoints,
                        OnDeleteSurroundingTextInCodePoints)
    IPC_MESSAGE_HANDLER(InputMsg_SetCompositionFromExistingText,
                        OnSetCompositionFromExistingText)
    IPC_MESSAGE_HANDLER(InputMsg_SetEditableSelectionOffsets,
                        OnSetEditableSelectionOffsets)
    IPC_MESSAGE_HANDLER(InputMsg_ExecuteNoValueEditCommand,
                        OnExecuteNoValueEditCommand)
    IPC_MESSAGE_HANDLER(FrameMsg_AddMessageToConsole, OnAddMessageToConsole)
    IPC_MESSAGE_HANDLER(FrameMsg_JavaScriptExecuteRequest,
                        OnJavaScriptExecuteRequest)
    IPC_MESSAGE_HANDLER(FrameMsg_JavaScriptExecuteRequestForTests,
                        OnJavaScriptExecuteRequestForTests)
    IPC_MESSAGE_HANDLER(FrameMsg_JavaScriptExecuteRequestInIsolatedWorld,
                        OnJavaScriptExecuteRequestInIsolatedWorld)
    IPC_MESSAGE_HANDLER(FrameMsg_VisualStateRequest,
                        OnVisualStateRequest)
    IPC_MESSAGE_HANDLER(FrameMsg_Reload, OnReload)
    IPC_MESSAGE_HANDLER(FrameMsg_ReloadLoFiImages, OnReloadLoFiImages)
    IPC_MESSAGE_HANDLER(FrameMsg_TextSurroundingSelectionRequest,
                        OnTextSurroundingSelectionRequest)
    IPC_MESSAGE_HANDLER(FrameMsg_SetAccessibilityMode,
                        OnSetAccessibilityMode)
    IPC_MESSAGE_HANDLER(AccessibilityMsg_SnapshotTree,
                        OnSnapshotAccessibilityTree)
    IPC_MESSAGE_HANDLER(FrameMsg_ExtractSmartClipData, OnExtractSmartClipData)
    IPC_MESSAGE_HANDLER(FrameMsg_UpdateOpener, OnUpdateOpener)
    IPC_MESSAGE_HANDLER(FrameMsg_CommitNavigation, OnCommitNavigation)
    IPC_MESSAGE_HANDLER(FrameMsg_DidUpdateSandboxFlags, OnDidUpdateSandboxFlags)
    IPC_MESSAGE_HANDLER(FrameMsg_SetFrameOwnerProperties,
                        OnSetFrameOwnerProperties)
    IPC_MESSAGE_HANDLER(FrameMsg_AdvanceFocus, OnAdvanceFocus)
    IPC_MESSAGE_HANDLER(FrameMsg_SetFocusedFrame, OnSetFocusedFrame)
    IPC_MESSAGE_HANDLER(FrameMsg_SetTextTrackSettings,
                        OnTextTrackSettingsChanged)
    IPC_MESSAGE_HANDLER(FrameMsg_PostMessageEvent, OnPostMessageEvent)
    IPC_MESSAGE_HANDLER(FrameMsg_FailedNavigation, OnFailedNavigation)
    IPC_MESSAGE_HANDLER(FrameMsg_ReportContentSecurityPolicyViolation,
                        OnReportContentSecurityPolicyViolation)
    IPC_MESSAGE_HANDLER(FrameMsg_GetSavableResourceLinks,
                        OnGetSavableResourceLinks)
    IPC_MESSAGE_HANDLER(FrameMsg_GetSerializedHtmlWithLocalLinks,
                        OnGetSerializedHtmlWithLocalLinks)
    IPC_MESSAGE_HANDLER(FrameMsg_SerializeAsMHTML, OnSerializeAsMHTML)
    IPC_MESSAGE_HANDLER(FrameMsg_Find, OnFind)
    IPC_MESSAGE_HANDLER(FrameMsg_ClearActiveFindMatch, OnClearActiveFindMatch)
    IPC_MESSAGE_HANDLER(FrameMsg_StopFinding, OnStopFinding)
    IPC_MESSAGE_HANDLER(FrameMsg_EnableViewSourceMode, OnEnableViewSourceMode)
    IPC_MESSAGE_HANDLER(FrameMsg_SuppressFurtherDialogs,
                        OnSuppressFurtherDialogs)
    IPC_MESSAGE_HANDLER(FrameMsg_RunFileChooserResponse, OnFileChooserResponse)
    IPC_MESSAGE_HANDLER(FrameMsg_ClearFocusedElement, OnClearFocusedElement)
    IPC_MESSAGE_HANDLER(FrameMsg_BlinkFeatureUsageReport,
                        OnBlinkFeatureUsageReport)
    IPC_MESSAGE_HANDLER(FrameMsg_MixedContentFound, OnMixedContentFound)
#if defined(OS_ANDROID)
    IPC_MESSAGE_HANDLER(FrameMsg_ActivateNearestFindResult,
                        OnActivateNearestFindResult)
    IPC_MESSAGE_HANDLER(FrameMsg_GetNearestFindResult,
                        OnGetNearestFindResult)
    IPC_MESSAGE_HANDLER(FrameMsg_FindMatchRects, OnFindMatchRects)
#endif

#if BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(FrameMsg_SelectPopupMenuItem, OnSelectPopupMenuItem)
#else
    IPC_MESSAGE_HANDLER(FrameMsg_SelectPopupMenuItems, OnSelectPopupMenuItems)
#endif
#endif

#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(InputMsg_CopyToFindPboard, OnCopyToFindPboard)
#endif
  IPC_END_MESSAGE_MAP()

  return handled;
}

void RenderFrameImpl::OnAssociatedInterfaceRequest(
    const std::string& interface_name,
    mojo::ScopedInterfaceEndpointHandle handle) {
  associated_interfaces_.BindRequest(interface_name, std::move(handle));
}

void RenderFrameImpl::OnNavigate(
    const CommonNavigationParams& common_params,
    const StartNavigationParams& start_params,
    const RequestNavigationParams& request_params) {
  RenderThreadImpl* render_thread_impl = RenderThreadImpl::current();
  // Can be NULL in tests.
  if (render_thread_impl)
    render_thread_impl->GetRendererScheduler()->OnNavigationStarted();
  DCHECK(!IsBrowserSideNavigationEnabled());
  TRACE_EVENT2("navigation,rail", "RenderFrameImpl::OnNavigate", "id",
               routing_id_, "url", common_params.url.possibly_invalid_spec());
  NavigateInternal(common_params, start_params, request_params,
                   std::unique_ptr<StreamOverrideParameters>());
}

void RenderFrameImpl::BindEngagement(
    blink::mojom::EngagementClientAssociatedRequest request) {
  engagement_binding_.Bind(std::move(request));
}

void RenderFrameImpl::BindFrame(mojom::FrameRequest request,
                                mojom::FrameHostPtr host) {
  frame_binding_.Bind(std::move(request));
  frame_host_ = std::move(host);
  frame_host_->GetInterfaceProvider(
      std::move(pending_remote_interface_provider_request_));
}

void RenderFrameImpl::BindFrameBindingsControl(
    mojom::FrameBindingsControlAssociatedRequest request) {
  frame_bindings_control_binding_.Bind(std::move(request));
}

ManifestManager* RenderFrameImpl::manifest_manager() {
  return manifest_manager_;
}

void RenderFrameImpl::SetPendingNavigationParams(
    std::unique_ptr<NavigationParams> navigation_params) {
  pending_navigation_params_ = std::move(navigation_params);
}

void RenderFrameImpl::OnBeforeUnload(bool is_reload) {
  TRACE_EVENT1("navigation,rail", "RenderFrameImpl::OnBeforeUnload",
               "id", routing_id_);
  // Save the routing_id, as the RenderFrameImpl can be deleted in
  // dispatchBeforeUnloadEvent. See https://crbug.com/666714 for details.
  int routing_id = routing_id_;

  base::TimeTicks before_unload_start_time = base::TimeTicks::Now();

  // TODO(clamy): Ensure BeforeUnload is dispatched to all subframes, even when
  // --site-per-process is enabled. |dispatchBeforeUnloadEvent| will only
  // execute the BeforeUnload event in this frame and local child frames. It
  // should also be dispatched to out-of-process child frames.
  bool proceed = frame_->DispatchBeforeUnloadEvent(is_reload);

  base::TimeTicks before_unload_end_time = base::TimeTicks::Now();
  RenderThread::Get()->Send(new FrameHostMsg_BeforeUnload_ACK(
      routing_id, proceed, before_unload_start_time, before_unload_end_time));
}

void RenderFrameImpl::OnSwapOut(
    int proxy_routing_id,
    bool is_loading,
    const FrameReplicationState& replicated_frame_state) {
  TRACE_EVENT1("navigation,rail", "RenderFrameImpl::OnSwapOut",
               "id", routing_id_);
  RenderFrameProxy* proxy = NULL;

  // This codepath should only be hit for subframes when in --site-per-process.
  CHECK(is_main_frame_ || SiteIsolationPolicy::AreCrossProcessFramesPossible());

  // Swap this RenderFrame out so the frame can navigate to a page rendered by
  // a different process.  This involves running the unload handler and
  // clearing the page.  We also allow this process to exit if there are no
  // other active RenderFrames in it.

  // Send an UpdateState message before we get deleted.
  SendUpdateState();

  // There should always be a proxy to replace this RenderFrame.  Create it now
  // so its routing id is registered for receiving IPC messages.
  CHECK_NE(proxy_routing_id, MSG_ROUTING_NONE);
  proxy = RenderFrameProxy::CreateProxyToReplaceFrame(
      this, proxy_routing_id, replicated_frame_state.scope);

  // Synchronously run the unload handler before sending the ACK.
  // TODO(creis): Call dispatchUnloadEvent unconditionally here to support
  // unload on subframes as well.
  if (is_main_frame_)
    frame_->DispatchUnloadEvent();

  // Swap out and stop sending any IPC messages that are not ACKs.
  if (is_main_frame_)
    render_view_->SetSwappedOut(true);

  RenderViewImpl* render_view = render_view_;
  bool is_main_frame = is_main_frame_;
  int routing_id = GetRoutingID();

  // Now that all of the cleanup is complete and the browser side is notified,
  // start using the RenderFrameProxy.
  //
  // The swap call deletes this RenderFrame via frameDetached.  Do not access
  // any members after this call.
  //
  // TODO(creis): WebFrame::swap() can return false.  Most of those cases
  // should be due to the frame being detached during unload (in which case
  // the necessary cleanup has happened anyway), but it might be possible for
  // it to return false without detaching.  Catch any cases that the
  // RenderView's main_render_frame_ isn't cleared below (whether swap returns
  // false or not).
  bool success = frame_->Swap(proxy->web_frame());

  // For main frames, the swap should have cleared the RenderView's pointer to
  // this frame.
  if (is_main_frame)
    CHECK(!render_view->main_render_frame_);

  if (!success) {
    // The swap can fail when the frame is detached during swap (this can
    // happen while running the unload handlers). When that happens, delete
    // the proxy.
    proxy->FrameDetached(blink::WebRemoteFrameClient::DetachType::kSwap);
    return;
  }

  if (is_loading)
    proxy->OnDidStartLoading();

  // Initialize the WebRemoteFrame with the replication state passed by the
  // process that is now rendering the frame.
  proxy->SetReplicatedState(replicated_frame_state);

  // Safe to exit if no one else is using the process.
  // TODO(nasko): Remove the dependency on RenderViewImpl here and ref count
  // the process based on the lifetime of this RenderFrameImpl object.
  if (is_main_frame)
    render_view->WasSwappedOut();

  // Notify the browser that this frame was swapped. Use the RenderThread
  // directly because |this| is deleted.
  RenderThread::Get()->Send(new FrameHostMsg_SwapOut_ACK(routing_id));
}

void RenderFrameImpl::OnSwapIn() {
  SwapIn();
}

void RenderFrameImpl::OnDeleteFrame() {
  // TODO(nasko): If this message is received right after a commit has
  // swapped a RenderFrameProxy with this RenderFrame, the proxy needs to be
  // recreated in addition to the RenderFrame being deleted.
  // See https://crbug.com/569683 for details.
  in_browser_initiated_detach_ = true;

  // This will result in a call to RendeFrameImpl::frameDetached, which
  // deletes the object. Do not access |this| after detach.
  frame_->Detach();
}

void RenderFrameImpl::OnContextMenuClosed(
    const CustomContextMenuContext& custom_context) {
  if (custom_context.request_id) {
    // External request, should be in our map.
    ContextMenuClient* client =
        pending_context_menus_.Lookup(custom_context.request_id);
    if (client) {
      client->OnMenuClosed(custom_context.request_id);
      pending_context_menus_.Remove(custom_context.request_id);
    }
  } else {
    if (custom_context.link_followed.is_valid())
      frame_->SendPings(custom_context.link_followed);
  }

  render_view()->webview()->DidCloseContextMenu();
}

void RenderFrameImpl::OnCustomContextMenuAction(
    const CustomContextMenuContext& custom_context,
    unsigned action) {
  if (custom_context.request_id) {
    // External context menu request, look in our map.
    ContextMenuClient* client =
        pending_context_menus_.Lookup(custom_context.request_id);
    if (client)
      client->OnMenuAction(custom_context.request_id, action);
  } else {
    // Internal request, forward to WebKit.
    render_view_->webview()->PerformCustomContextMenuAction(action);
  }
}

void RenderFrameImpl::OnUndo() {
  frame_->ExecuteCommand(WebString::FromUTF8("Undo"));
}

void RenderFrameImpl::OnRedo() {
  frame_->ExecuteCommand(WebString::FromUTF8("Redo"));
}

void RenderFrameImpl::OnCut() {
  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->ExecuteCommand(WebString::FromUTF8("Cut"));
}

void RenderFrameImpl::OnCopy() {
  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->ExecuteCommand(WebString::FromUTF8("Copy"));
}

void RenderFrameImpl::OnPaste() {
  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  base::AutoReset<bool> handling_paste(&is_pasting_, true);
  frame_->ExecuteCommand(WebString::FromUTF8("Paste"));
}

void RenderFrameImpl::OnPasteAndMatchStyle() {
  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->ExecuteCommand(WebString::FromUTF8("PasteAndMatchStyle"));
}

#if defined(OS_MACOSX)
void RenderFrameImpl::OnCopyToFindPboard() {
  // Since the find pasteboard supports only plain text, this can be simpler
  // than the |OnCopy()| case.
  if (frame_->HasSelection()) {
    base::string16 selection = frame_->SelectionAsText().Utf16();
    RenderThread::Get()->Send(
        new ClipboardHostMsg_FindPboardWriteStringAsync(selection));
  }
}
#endif

void RenderFrameImpl::OnDelete() {
  frame_->ExecuteCommand(WebString::FromUTF8("Delete"));
}

void RenderFrameImpl::OnSelectAll() {
  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->ExecuteCommand(WebString::FromUTF8("SelectAll"));
}

void RenderFrameImpl::OnSelectRange(const gfx::Point& base,
                                    const gfx::Point& extent) {
  // This IPC is dispatched by RenderWidgetHost, so use its routing id.
  Send(new InputHostMsg_SelectRange_ACK(GetRenderWidget()->routing_id()));

  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->SelectRange(render_view_->ConvertWindowPointToViewport(base),
                      render_view_->ConvertWindowPointToViewport(extent));
}

void RenderFrameImpl::OnAdjustSelectionByCharacterOffset(int start_adjust,
                                                         int end_adjust) {
  WebRange range = GetRenderWidget()->GetWebWidget()->CaretOrSelectionRange();
  if (range.IsNull())
    return;

  // Sanity checks to disallow empty and out of range selections.
  if (start_adjust - end_adjust > range.length() ||
      range.StartOffset() + start_adjust < 0)
    return;

  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);

  // A negative adjust amount moves the selection towards the beginning of
  // the document, a positive amount moves the selection towards the end of
  // the document.
  frame_->SelectRange(WebRange(range.StartOffset() + start_adjust,
                               range.length() + end_adjust - start_adjust),
                      WebLocalFrame::kPreserveHandleVisibility);
}

void RenderFrameImpl::OnCollapseSelection() {
  const WebRange& range =
      GetRenderWidget()->GetWebWidget()->CaretOrSelectionRange();
  if (range.IsNull())
    return;

  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->SelectRange(WebRange(range.EndOffset(), 0));
}

void RenderFrameImpl::OnMoveRangeSelectionExtent(const gfx::Point& point) {
  // This IPC is dispatched by RenderWidgetHost, so use its routing id.
  Send(new InputHostMsg_MoveRangeSelectionExtent_ACK(
      GetRenderWidget()->routing_id()));

  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  frame_->MoveRangeSelectionExtent(
      render_view_->ConvertWindowPointToViewport(point));
}

void RenderFrameImpl::OnReplace(const base::string16& text) {
  if (!frame_->HasSelection())
    frame_->SelectWordAroundCaret();

  frame_->ReplaceSelection(WebString::FromUTF16(text));
  SyncSelectionIfRequired();
}

void RenderFrameImpl::OnReplaceMisspelling(const base::string16& text) {
  if (!frame_->HasSelection())
    return;

  frame_->ReplaceMisspelledRange(WebString::FromUTF16(text));
}

void RenderFrameImpl::OnCopyImageAt(int x, int y) {
  blink::WebFloatRect viewport_position(x, y, 0, 0);
  GetRenderWidget()->ConvertWindowToViewport(&viewport_position);
  frame_->CopyImageAt(WebPoint(viewport_position.x, viewport_position.y));
}

void RenderFrameImpl::OnSaveImageAt(int x, int y) {
  blink::WebFloatRect viewport_position(x, y, 0, 0);
  GetRenderWidget()->ConvertWindowToViewport(&viewport_position);
  frame_->SaveImageAt(WebPoint(viewport_position.x, viewport_position.y));
}

void RenderFrameImpl::OnAddMessageToConsole(ConsoleMessageLevel level,
                                            const std::string& message) {
  AddMessageToConsole(level, message);
}

void RenderFrameImpl::OnJavaScriptExecuteRequest(
    const base::string16& jscript,
    int id,
    bool notify_result) {
  TRACE_EVENT_INSTANT0("test_tracing", "OnJavaScriptExecuteRequest",
                       TRACE_EVENT_SCOPE_THREAD);

  v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
  v8::Local<v8::Value> result = frame_->ExecuteScriptAndReturnValue(
      WebScriptSource(WebString::FromUTF16(jscript)));

  HandleJavascriptExecutionResult(jscript, id, notify_result, result);
}

void RenderFrameImpl::OnJavaScriptExecuteRequestForTests(
    const base::string16& jscript,
    int id,
    bool notify_result,
    bool has_user_gesture) {
  TRACE_EVENT_INSTANT0("test_tracing", "OnJavaScriptExecuteRequestForTests",
                       TRACE_EVENT_SCOPE_THREAD);

  // A bunch of tests expect to run code in the context of a user gesture, which
  // can grant additional privileges (e.g. the ability to create popups).
  std::unique_ptr<blink::WebScopedUserGesture> gesture(
      has_user_gesture ? new blink::WebScopedUserGesture(frame_) : nullptr);
  v8::HandleScope handle_scope(blink::MainThreadIsolate());
  v8::Local<v8::Value> result = frame_->ExecuteScriptAndReturnValue(
      WebScriptSource(WebString::FromUTF16(jscript)));

  HandleJavascriptExecutionResult(jscript, id, notify_result, result);
}

void RenderFrameImpl::OnJavaScriptExecuteRequestInIsolatedWorld(
    const base::string16& jscript,
    int id,
    bool notify_result,
    int world_id) {
  TRACE_EVENT_INSTANT0("test_tracing",
                       "OnJavaScriptExecuteRequestInIsolatedWorld",
                       TRACE_EVENT_SCOPE_THREAD);

  if (world_id <= ISOLATED_WORLD_ID_GLOBAL ||
      world_id > ISOLATED_WORLD_ID_MAX) {
    // Return if the world_id is not valid. world_id is passed as a plain int
    // over IPC and needs to be verified here, in the IPC endpoint.
    NOTREACHED();
    return;
  }

  v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
  WebScriptSource script = WebScriptSource(WebString::FromUTF16(jscript));
  JavaScriptIsolatedWorldRequest* request = new JavaScriptIsolatedWorldRequest(
      id, notify_result, routing_id_, weak_factory_.GetWeakPtr());
  frame_->RequestExecuteScriptInIsolatedWorld(
      world_id, &script, 1, false, WebLocalFrame::kSynchronous, request);
}

RenderFrameImpl::JavaScriptIsolatedWorldRequest::JavaScriptIsolatedWorldRequest(
    int id,
    bool notify_result,
    int routing_id,
    base::WeakPtr<RenderFrameImpl> render_frame_impl)
    : id_(id),
      notify_result_(notify_result),
      routing_id_(routing_id),
      render_frame_impl_(render_frame_impl) {
}

RenderFrameImpl::JavaScriptIsolatedWorldRequest::
    ~JavaScriptIsolatedWorldRequest() {
}

void RenderFrameImpl::JavaScriptIsolatedWorldRequest::Completed(
    const blink::WebVector<v8::Local<v8::Value>>& result) {
  if (!render_frame_impl_.get()) {
    return;
  }

  if (notify_result_) {
    base::ListValue list;
    if (!result.IsEmpty()) {
      // It's safe to always use the main world context when converting
      // here. V8ValueConverterImpl shouldn't actually care about the
      // context scope, and it switches to v8::Object's creation context
      // when encountered. (from extensions/renderer/script_injection.cc)
      v8::Local<v8::Context> context =
          render_frame_impl_.get()->frame_->MainWorldScriptContext();
      v8::Context::Scope context_scope(context);
      V8ValueConverterImpl converter;
      converter.SetDateAllowed(true);
      converter.SetRegExpAllowed(true);
      for (const auto& value : result) {
        std::unique_ptr<base::Value> result_value(
            converter.FromV8Value(value, context));
        list.Append(result_value ? std::move(result_value)
                                 : base::MakeUnique<base::Value>());
      }
    } else {
      list.Set(0, base::MakeUnique<base::Value>());
    }
    render_frame_impl_.get()->Send(
        new FrameHostMsg_JavaScriptExecuteResponse(routing_id_, id_, list));
  }

  delete this;
}

void RenderFrameImpl::HandleJavascriptExecutionResult(
    const base::string16& jscript,
    int id,
    bool notify_result,
    v8::Local<v8::Value> result) {
  if (notify_result) {
    base::ListValue list;
    if (!result.IsEmpty()) {
      v8::Local<v8::Context> context = frame_->MainWorldScriptContext();
      v8::Context::Scope context_scope(context);
      V8ValueConverterImpl converter;
      converter.SetDateAllowed(true);
      converter.SetRegExpAllowed(true);
      std::unique_ptr<base::Value> result_value(
          converter.FromV8Value(result, context));
      list.Set(0, result_value ? std::move(result_value)
                               : base::MakeUnique<base::Value>());
    } else {
      list.Set(0, base::MakeUnique<base::Value>());
    }
    Send(new FrameHostMsg_JavaScriptExecuteResponse(routing_id_, id, list));
  }
}

void RenderFrameImpl::OnVisualStateRequest(uint64_t id) {
  GetRenderWidget()->QueueMessage(
      new FrameHostMsg_VisualStateResponse(routing_id_, id),
      MESSAGE_DELIVERY_POLICY_WITH_VISUAL_STATE);
}

void RenderFrameImpl::OnSetEditableSelectionOffsets(int start, int end) {
  base::AutoReset<bool> handling_select_range(&handling_select_range_, true);
  ImeEventGuard guard(GetRenderWidget());
  frame_->SetEditableSelectionOffsets(start, end);
}

void RenderFrameImpl::OnSetCompositionFromExistingText(
    int start, int end,
    const std::vector<blink::WebCompositionUnderline>& underlines) {
  ImeEventGuard guard(GetRenderWidget());
  frame_->SetCompositionFromExistingText(start, end, underlines);
}

void RenderFrameImpl::OnExecuteNoValueEditCommand(const std::string& name) {
  frame_->ExecuteCommand(WebString::FromUTF8(name));
}

void RenderFrameImpl::OnExtendSelectionAndDelete(int before, int after) {
  ImeEventGuard guard(GetRenderWidget());
  frame_->ExtendSelectionAndDelete(before, after);
}

void RenderFrameImpl::OnDeleteSurroundingText(int before, int after) {
  ImeEventGuard guard(GetRenderWidget());
  frame_->DeleteSurroundingText(before, after);
}

void RenderFrameImpl::OnDeleteSurroundingTextInCodePoints(int before,
                                                          int after) {
  ImeEventGuard guard(GetRenderWidget());
  frame_->DeleteSurroundingTextInCodePoints(before, after);
}

void RenderFrameImpl::OnSetAccessibilityMode(AccessibilityMode new_mode) {
  if (accessibility_mode_ == new_mode)
    return;
  AccessibilityMode old_mode = accessibility_mode_;
  accessibility_mode_ = new_mode;

  if (new_mode.has_mode(AccessibilityMode::kWebContents) &&
      !old_mode.has_mode(AccessibilityMode::kWebContents)) {
    render_accessibility_ = new RenderAccessibilityImpl(this, new_mode);
  } else if (!new_mode.has_mode(AccessibilityMode::kWebContents) &&
             old_mode.has_mode(AccessibilityMode::kWebContents)) {
    // Note: this isn't called automatically by the destructor because
    // there'd be no point in calling it in frame teardown, only if there's
    // an accessibility mode change but the frame is persisting.
    render_accessibility_->DisableAccessibility();
    delete render_accessibility_;
    render_accessibility_ = nullptr;
  }

  for (auto& observer : observers_)
    observer.AccessibilityModeChanged();
}

void RenderFrameImpl::OnSnapshotAccessibilityTree(int callback_id) {
  AXContentTreeUpdate response;
  RenderAccessibilityImpl::SnapshotAccessibilityTree(this, &response);
  Send(new AccessibilityHostMsg_SnapshotResponse(
      routing_id_, callback_id, response));
}

void RenderFrameImpl::OnExtractSmartClipData(uint32_t id,
                                             const gfx::Rect& rect) {
  blink::WebString clip_text;
  blink::WebString clip_html;
  GetWebFrame()->ExtractSmartClipData(rect, clip_text, clip_html);
  Send(new FrameHostMsg_SmartClipDataExtracted(
      routing_id_, id, clip_text.Utf16(), clip_html.Utf16()));
}

void RenderFrameImpl::OnUpdateOpener(int opener_routing_id) {
  WebFrame* opener = ResolveOpener(opener_routing_id);
  frame_->SetOpener(opener);
}

void RenderFrameImpl::OnDidUpdateSandboxFlags(blink::WebSandboxFlags flags) {
  frame_->SetFrameOwnerSandboxFlags(flags);
}

void RenderFrameImpl::OnSetFrameOwnerProperties(
    const FrameOwnerProperties& frame_owner_properties) {
  DCHECK(frame_);
  frame_->SetFrameOwnerProperties(
      ConvertFrameOwnerPropertiesToWebFrameOwnerProperties(
          frame_owner_properties));
}

void RenderFrameImpl::OnAdvanceFocus(blink::WebFocusType type,
                                     int32_t source_routing_id) {
  RenderFrameProxy* source_frame =
      RenderFrameProxy::FromRoutingID(source_routing_id);
  if (!source_frame)
    return;

  render_view_->webview()->AdvanceFocusAcrossFrames(
      type, source_frame->web_frame(), frame_);
}

void RenderFrameImpl::OnSetFocusedFrame() {
  // This uses focusDocumentView rather than setFocusedFrame so that focus/blur
  // events are properly dispatched on any currently focused elements.
  render_view_->webview()->FocusDocumentView(frame_);
}

void RenderFrameImpl::OnTextTrackSettingsChanged(
    const FrameMsg_TextTrackSettings_Params& params) {
  DCHECK(!frame_->Parent());
  if (!render_view_->webview())
    return;

  if (params.text_tracks_enabled) {
    render_view_->webview()->GetSettings()->SetTextTrackKindUserPreference(
        WebSettings::TextTrackKindUserPreference::kCaptions);
  } else {
    render_view_->webview()->GetSettings()->SetTextTrackKindUserPreference(
        WebSettings::TextTrackKindUserPreference::kDefault);
  }
  render_view_->webview()->GetSettings()->SetTextTrackBackgroundColor(
      WebString::FromUTF8(params.text_track_background_color));
  render_view_->webview()->GetSettings()->SetTextTrackFontFamily(
      WebString::FromUTF8(params.text_track_font_family));
  render_view_->webview()->GetSettings()->SetTextTrackFontStyle(
      WebString::FromUTF8(params.text_track_font_style));
  render_view_->webview()->GetSettings()->SetTextTrackFontVariant(
      WebString::FromUTF8(params.text_track_font_variant));
  render_view_->webview()->GetSettings()->SetTextTrackTextColor(
      WebString::FromUTF8(params.text_track_text_color));
  render_view_->webview()->GetSettings()->SetTextTrackTextShadow(
      WebString::FromUTF8(params.text_track_text_shadow));
  render_view_->webview()->GetSettings()->SetTextTrackTextSize(
      WebString::FromUTF8(params.text_track_text_size));
}

void RenderFrameImpl::OnPostMessageEvent(
    const FrameMsg_PostMessage_Params& params) {
  // Find the source frame if it exists.
  WebFrame* source_frame = NULL;
  if (params.source_routing_id != MSG_ROUTING_NONE) {
    RenderFrameProxy* source_proxy =
        RenderFrameProxy::FromRoutingID(params.source_routing_id);
    if (source_proxy)
      source_frame = source_proxy->web_frame();
  }

  // If the message contained MessagePorts, create the corresponding endpoints.
  blink::WebMessagePortChannelArray channels =
      WebMessagePortChannelImpl::CreateFromMessagePorts(params.message_ports);

  WebSerializedScriptValue serialized_script_value;
  if (params.is_data_raw_string) {
    v8::Isolate* isolate = blink::MainThreadIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = frame_->MainWorldScriptContext();
    v8::Context::Scope context_scope(context);
    V8ValueConverterImpl converter;
    converter.SetDateAllowed(true);
    converter.SetRegExpAllowed(true);
    std::unique_ptr<base::Value> value(new base::Value(params.data));
    v8::Local<v8::Value> result_value = converter.ToV8Value(value.get(),
                                                             context);
    serialized_script_value =
        WebSerializedScriptValue::Serialize(isolate, result_value);
  } else {
    serialized_script_value =
        WebSerializedScriptValue::FromString(WebString::FromUTF16(params.data));
  }

  // We must pass in the target_origin to do the security check on this side,
  // since it may have changed since the original postMessage call was made.
  WebSecurityOrigin target_origin;
  if (!params.target_origin.empty()) {
    target_origin = WebSecurityOrigin::CreateFromString(
        WebString::FromUTF16(params.target_origin));
  }

  WebDOMMessageEvent msg_event(
      serialized_script_value, WebString::FromUTF16(params.source_origin),
      source_frame, frame_->GetDocument(), std::move(channels));
  frame_->DispatchMessageEventWithOriginCheck(target_origin, msg_event);
}

void RenderFrameImpl::OnReload(bool bypass_cache) {
  frame_->Reload(bypass_cache ? WebFrameLoadType::kReloadBypassingCache
                              : WebFrameLoadType::kReload);
}

void RenderFrameImpl::OnReloadLoFiImages() {
  previews_state_ = PREVIEWS_NO_TRANSFORM;
  GetWebFrame()->ReloadLoFiImages();
}

void RenderFrameImpl::OnTextSurroundingSelectionRequest(uint32_t max_length) {
  blink::WebSurroundingText surroundingText;
  surroundingText.InitializeFromCurrentSelection(frame_, max_length);

  if (surroundingText.IsNull()) {
    // |surroundingText| might not be correctly initialized, for example if
    // |frame_->selectionRange().isNull()|, in other words, if there was no
    // selection.
    Send(new FrameHostMsg_TextSurroundingSelectionResponse(
        routing_id_, base::string16(), 0, 0));
    return;
  }

  Send(new FrameHostMsg_TextSurroundingSelectionResponse(
      routing_id_, surroundingText.TextContent().Utf16(),
      surroundingText.StartOffsetInTextContent(),
      surroundingText.EndOffsetInTextContent()));
}

bool RenderFrameImpl::RunJavaScriptDialog(JavaScriptDialogType type,
                                          const base::string16& message,
                                          const base::string16& default_value,
                                          const GURL& frame_url,
                                          base::string16* result) {
  // Don't allow further dialogs if we are waiting to swap out, since the
  // ScopedPageLoadDeferrer in our stack prevents it.
  if (suppress_further_dialogs_)
    return false;

  int32_t message_length = static_cast<int32_t>(message.length());
  if (WebUserGestureIndicator::ProcessedUserGestureSinceLoad(frame_)) {
    UMA_HISTOGRAM_COUNTS("JSDialogs.CharacterCount.UserGestureSinceLoad",
                         message_length);
  } else {
    UMA_HISTOGRAM_COUNTS("JSDialogs.CharacterCount.NoUserGestureSinceLoad",
                         message_length);
  }

  bool success = false;
  base::string16 result_temp;
  if (!result)
    result = &result_temp;

  Send(new FrameHostMsg_RunJavaScriptDialog(routing_id_, message, default_value,
                                            frame_url, type, &success, result));
  return success;
}

bool RenderFrameImpl::ScheduleFileChooser(
    const FileChooserParams& params,
    blink::WebFileChooserCompletion* completion) {
  static const size_t kMaximumPendingFileChooseRequests = 4;

  // Do not open the file dialog in a hidden RenderFrame.
  if (IsHidden())
    return false;

  if (file_chooser_completions_.size() > kMaximumPendingFileChooseRequests) {
    // This sanity check prevents too many file choose requests from getting
    // queued which could DoS the user. Getting these is most likely a
    // programming error (there are many ways to DoS the user so it's not
    // considered a "real" security check), either in JS requesting many file
    // choosers to pop up, or in a plugin.
    //
    // TODO(brettw): We might possibly want to require a user gesture to open
    // a file picker, which will address this issue in a better way.
    return false;
  }

  file_chooser_completions_.push_back(
      base::MakeUnique<PendingFileChooser>(params, completion));
  if (file_chooser_completions_.size() == 1) {
    // Actually show the browse dialog when this is the first request.
    Send(new FrameHostMsg_RunFileChooser(routing_id_, params));
  }
  return true;
}

void RenderFrameImpl::LoadNavigationErrorPage(
    const WebURLRequest& failed_request,
    const WebURLError& error,
    bool replace,
    HistoryEntry* entry) {
  std::string error_html;
  GetContentClient()->renderer()->GetNavigationErrorStrings(
      this, failed_request, error, &error_html, nullptr);

  blink::WebFrameLoadType frame_load_type =
      entry ? blink::WebFrameLoadType::kBackForward
            : blink::WebFrameLoadType::kStandard;
  const blink::WebHistoryItem& history_item =
      entry ? entry->root() : blink::WebHistoryItem();

  // Requests blocked by the X-Frame-Options HTTP response header don't display
  // error pages but a blank page instead.
  // TODO(alexmos, mkwst, arthursonzogni): This block can be removed once error
  // pages are refactored. See crbug.com/588314 and crbug.com/622385.
  if (error.reason == net::ERR_BLOCKED_BY_RESPONSE) {
    frame_->LoadData("", WebString::FromUTF8("text/html"),
                     WebString::FromUTF8("UTF-8"), GURL("data:,"), WebURL(),
                     replace, frame_load_type, history_item,
                     blink::kWebHistoryDifferentDocumentLoad, false);
    return;
  }

  frame_->LoadData(error_html, WebString::FromUTF8("text/html"),
                   WebString::FromUTF8("UTF-8"), GURL(kUnreachableWebDataURL),
                   error.unreachable_url, replace, frame_load_type,
                   history_item, blink::kWebHistoryDifferentDocumentLoad,
                   false);
}

void RenderFrameImpl::DidMeaningfulLayout(
    blink::WebMeaningfulLayout layout_type) {
  for (auto& observer : observers_)
    observer.DidMeaningfulLayout(layout_type);
}

void RenderFrameImpl::DidCommitCompositorFrame() {
  if (BrowserPluginManager::Get())
    BrowserPluginManager::Get()->DidCommitCompositorFrame(GetRoutingID());
  for (auto& observer : observers_)
    observer.DidCommitCompositorFrame();
}

void RenderFrameImpl::DidCommitAndDrawCompositorFrame() {
#if BUILDFLAG(ENABLE_PLUGINS)
  // Notify all instances that we painted.  The same caveats apply as for
  // ViewFlushedPaint regarding instances closing themselves, so we take
  // similar precautions.
  PepperPluginSet plugins = active_pepper_instances_;
  for (auto* plugin : plugins) {
    if (active_pepper_instances_.find(plugin) != active_pepper_instances_.end())
      plugin->ViewInitiatedPaint();
  }
#endif
}

RenderView* RenderFrameImpl::GetRenderView() {
  return render_view_;
}

RenderAccessibility* RenderFrameImpl::GetRenderAccessibility() {
  return render_accessibility_;
}

int RenderFrameImpl::GetRoutingID() {
  return routing_id_;
}

blink::WebLocalFrame* RenderFrameImpl::GetWebFrame() {
  DCHECK(frame_);
  return frame_;
}

WebPreferences& RenderFrameImpl::GetWebkitPreferences() {
  return render_view_->GetWebkitPreferences();
}

int RenderFrameImpl::ShowContextMenu(ContextMenuClient* client,
                                     const ContextMenuParams& params) {
  DCHECK(client);  // A null client means "internal" when we issue callbacks.
  ContextMenuParams our_params(params);

  blink::WebRect position_in_window(params.x, params.y, 0, 0);
  GetRenderWidget()->ConvertViewportToWindow(&position_in_window);
  our_params.x = position_in_window.x;
  our_params.y = position_in_window.y;

  our_params.custom_context.request_id = pending_context_menus_.Add(client);
  Send(new FrameHostMsg_ContextMenu(routing_id_, our_params));
  return our_params.custom_context.request_id;
}

void RenderFrameImpl::CancelContextMenu(int request_id) {
  DCHECK(pending_context_menus_.Lookup(request_id));
  pending_context_menus_.Remove(request_id);
}

blink::WebPlugin* RenderFrameImpl::CreatePlugin(
    blink::WebFrame* frame,
    const WebPluginInfo& info,
    const blink::WebPluginParams& params,
    std::unique_ptr<content::PluginInstanceThrottler> throttler) {
  DCHECK_EQ(frame_, frame);
#if BUILDFLAG(ENABLE_PLUGINS)
  if (info.type == WebPluginInfo::PLUGIN_TYPE_BROWSER_PLUGIN) {
    return BrowserPluginManager::Get()->CreateBrowserPlugin(
        this, GetContentClient()
                  ->renderer()
                  ->CreateBrowserPluginDelegate(this, params.mime_type.Utf8(),
                                                GURL(params.url))
                  ->GetWeakPtr());
  }

  bool pepper_plugin_was_registered = false;
  scoped_refptr<PluginModule> pepper_module(PluginModule::Create(
      this, info, &pepper_plugin_was_registered));
  if (pepper_plugin_was_registered) {
    if (pepper_module.get()) {
      return new PepperWebPluginImpl(
          pepper_module.get(), params, this,
          base::WrapUnique(
              static_cast<PluginInstanceThrottlerImpl*>(throttler.release())));
    }
  }
#if defined(OS_CHROMEOS)
  LOG(WARNING) << "Pepper module/plugin creation failed.";
#endif
#endif
  return NULL;
}

void RenderFrameImpl::LoadURLExternally(const blink::WebURLRequest& request,
                                        blink::WebNavigationPolicy policy) {
  LoadURLExternally(request, policy, WebString(), false);
}

void RenderFrameImpl::LoadErrorPage(int reason) {
  blink::WebURLError error;
  error.unreachable_url = frame_->GetDocument().Url();
  error.domain = WebString::FromUTF8(net::kErrorDomain);
  error.reason = reason;

  std::string error_html;
  GetContentClient()->renderer()->GetNavigationErrorStrings(
      this, frame_->DataSource()->GetRequest(), error, &error_html, nullptr);

  frame_->LoadData(error_html, WebString::FromUTF8("text/html"),
                   WebString::FromUTF8("UTF-8"), GURL(kUnreachableWebDataURL),
                   error.unreachable_url, true,
                   blink::WebFrameLoadType::kStandard, blink::WebHistoryItem(),
                   blink::kWebHistoryDifferentDocumentLoad, true);
}

void RenderFrameImpl::ExecuteJavaScript(const base::string16& javascript) {
  OnJavaScriptExecuteRequest(javascript, 0, false);
}

service_manager::InterfaceRegistry* RenderFrameImpl::GetInterfaceRegistry() {
  return interface_registry_.get();
}

service_manager::InterfaceProvider* RenderFrameImpl::GetRemoteInterfaces() {
  return remote_interfaces_.get();
}

AssociatedInterfaceRegistry*
RenderFrameImpl::GetAssociatedInterfaceRegistry() {
  return &associated_interfaces_;
}

AssociatedInterfaceProvider*
RenderFrameImpl::GetRemoteAssociatedInterfaces() {
  if (!remote_associated_interfaces_) {
    ChildThreadImpl* thread = ChildThreadImpl::current();
    if (thread) {
      mojom::AssociatedInterfaceProviderAssociatedPtr remote_interfaces;
      thread->GetRemoteRouteProvider()->GetRoute(
          routing_id_, mojo::MakeRequest(&remote_interfaces));
      remote_associated_interfaces_.reset(
          new AssociatedInterfaceProviderImpl(std::move(remote_interfaces)));
    } else {
      // In some tests the thread may be null,
      // so set up a self-contained interface provider instead.
      remote_associated_interfaces_.reset(
          new AssociatedInterfaceProviderImpl());
    }
  }
  return remote_associated_interfaces_.get();
}

#if BUILDFLAG(ENABLE_PLUGINS)
void RenderFrameImpl::RegisterPeripheralPlugin(
    const url::Origin& content_origin,
    const base::Closure& unthrottle_callback) {
  return plugin_power_saver_helper_->RegisterPeripheralPlugin(
      content_origin, unthrottle_callback);
}

RenderFrame::PeripheralContentStatus
RenderFrameImpl::GetPeripheralContentStatus(
    const url::Origin& main_frame_origin,
    const url::Origin& content_origin,
    const gfx::Size& unobscured_size,
    RecordPeripheralDecision record_decision) const {
  return plugin_power_saver_helper_->GetPeripheralContentStatus(
      main_frame_origin, content_origin, unobscured_size, record_decision);
}

void RenderFrameImpl::WhitelistContentOrigin(
    const url::Origin& content_origin) {
  return plugin_power_saver_helper_->WhitelistContentOrigin(content_origin);
}

void RenderFrameImpl::PluginDidStartLoading() {
  DidStartLoading(true);
}

void RenderFrameImpl::PluginDidStopLoading() {
  DidStopLoading();
}
#endif  // BUILDFLAG(ENABLE_PLUGINS)

bool RenderFrameImpl::IsFTPDirectoryListing() {
  WebURLResponseExtraDataImpl* extra_data =
      GetExtraDataFromResponse(frame_->DataSource()->GetResponse());
  return extra_data ? extra_data->is_ftp_directory_listing() : false;
}

void RenderFrameImpl::AttachGuest(int element_instance_id) {
  BrowserPluginManager::Get()->Attach(element_instance_id);
}

void RenderFrameImpl::DetachGuest(int element_instance_id) {
  BrowserPluginManager::Get()->Detach(element_instance_id);
}

void RenderFrameImpl::SetSelectedText(const base::string16& selection_text,
                                      size_t offset,
                                      const gfx::Range& range) {
  Send(new FrameHostMsg_SelectionChanged(routing_id_, selection_text,
                                         static_cast<uint32_t>(offset), range));
}

void RenderFrameImpl::EnsureMojoBuiltinsAreAvailable(
    v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
  gin::ModuleRegistry* registry = gin::ModuleRegistry::From(context);
  if (registry->available_modules().count(mojo::edk::js::Core::kModuleName))
    return;

  v8::HandleScope handle_scope(isolate);

  registry->AddBuiltinModule(isolate, gin::Console::kModuleName,
                             gin::Console::GetModule(isolate));
  registry->AddBuiltinModule(isolate, gin::TimerModule::kName,
                             gin::TimerModule::GetModule(isolate));
  registry->AddBuiltinModule(isolate, mojo::edk::js::Core::kModuleName,
                             mojo::edk::js::Core::GetModule(isolate));
  registry->AddBuiltinModule(isolate, mojo::edk::js::Support::kModuleName,
                             mojo::edk::js::Support::GetModule(isolate));
  registry->AddBuiltinModule(
      isolate, InterfaceProviderJsWrapper::kPerFrameModuleName,
      InterfaceProviderJsWrapper::Create(
          isolate, context, remote_interfaces_.get())
          .ToV8());
  registry->AddBuiltinModule(
      isolate, InterfaceProviderJsWrapper::kPerProcessModuleName,
      InterfaceProviderJsWrapper::Create(isolate, context,
                                         RenderThread::Get()->GetConnector())
          .ToV8());
  registry->AddBuiltinModule(
      isolate, BlinkConnectorJsWrapper::kModuleName,
      BlinkConnectorJsWrapper::Create(
          isolate, context,
          RenderThreadImpl::current()->blink_platform_impl()->GetConnector())
          .ToV8());
}

void RenderFrameImpl::AddMessageToConsole(ConsoleMessageLevel level,
                                          const std::string& message) {
  blink::WebConsoleMessage::Level target_level =
      blink::WebConsoleMessage::kLevelInfo;
  switch (level) {
    case CONSOLE_MESSAGE_LEVEL_VERBOSE:
      target_level = blink::WebConsoleMessage::kLevelVerbose;
      break;
    case CONSOLE_MESSAGE_LEVEL_INFO:
      target_level = blink::WebConsoleMessage::kLevelInfo;
      break;
    case CONSOLE_MESSAGE_LEVEL_WARNING:
      target_level = blink::WebConsoleMessage::kLevelWarning;
      break;
    case CONSOLE_MESSAGE_LEVEL_ERROR:
      target_level = blink::WebConsoleMessage::kLevelError;
      break;
  }

  blink::WebConsoleMessage wcm(target_level, WebString::FromUTF8(message));
  frame_->AddMessageToConsole(wcm);
}

PreviewsState RenderFrameImpl::GetPreviewsState() const {
  return previews_state_;
}

bool RenderFrameImpl::IsPasting() const {
  return is_pasting_;
}

// blink::mojom::EngagementClient implementation -------------------------------

void RenderFrameImpl::SetEngagementLevel(const url::Origin& origin,
                                         blink::mojom::EngagementLevel level) {
  // Set the engagement level on |frame_| if its origin matches the one we have
  // been provided with.
  if (frame_ && url::Origin(frame_->GetSecurityOrigin()) == origin) {
    frame_->SetEngagementLevel(level);
    return;
  }

  engagement_level_ = std::make_pair(origin, level);
}

// mojom::Frame implementation -------------------------------------------------

void RenderFrameImpl::GetInterfaceProvider(
    service_manager::mojom::InterfaceProviderRequest request) {
  service_manager::ServiceInfo child_info =
      ChildThreadImpl::current()->GetChildServiceInfo();
  service_manager::ServiceInfo browser_info =
      ChildThreadImpl::current()->GetBrowserServiceInfo();

  service_manager::InterfaceProviderSpec child_spec, browser_spec;
  // TODO(beng): CHECK these return true.
  service_manager::GetInterfaceProviderSpec(
      mojom::kNavigation_FrameSpec, child_info.interface_provider_specs,
      &child_spec);
  service_manager::GetInterfaceProviderSpec(
      mojom::kNavigation_FrameSpec, browser_info.interface_provider_specs,
      &browser_spec);
  interface_registry_->Bind(std::move(request), child_info.identity, child_spec,
                            browser_info.identity, browser_spec);
}

void RenderFrameImpl::AllowBindings(int32_t enabled_bindings_flags) {
  if (IsMainFrame() && (enabled_bindings_flags & BINDINGS_POLICY_WEB_UI) &&
      !(enabled_bindings_ & BINDINGS_POLICY_WEB_UI)) {
    // TODO(sammc): Move WebUIExtensionData to be a RenderFrameObserver.
    // WebUIExtensionData deletes itself when |render_view_| is destroyed.
    new WebUIExtensionData(render_view_);
  }

  enabled_bindings_ |= enabled_bindings_flags;

  // Keep track of the total bindings accumulated in this process.
  RenderProcess::current()->AddBindings(enabled_bindings_flags);

  MaybeEnableMojoBindings();
}

// mojom::HostZoom implementation ----------------------------------------------

void RenderFrameImpl::SetHostZoomLevel(const GURL& url, double zoom_level) {
  // TODO(wjmaclean): We should see if this restriction is really necessary,
  // since it isn't enforced in other parts of the page zoom system (e.g.
  // when a users changes the zoom of a currently displayed page). Android
  // has no UI for this, so in theory the following code would normally just use
  // the default zoom anyways.
#if !defined(OS_ANDROID)
  // On Android, page zoom isn't used, and in case of WebView, text zoom is used
  // for legacy WebView text scaling emulation. Thus, the code that resets
  // the zoom level from this map will be effectively resetting text zoom level.
  host_zoom_levels_[url] = zoom_level;
#endif
}

// blink::WebFrameClient implementation ----------------------------------------

blink::WebPlugin* RenderFrameImpl::CreatePlugin(
    blink::WebLocalFrame* frame,
    const blink::WebPluginParams& params) {
  DCHECK_EQ(frame_, frame);
  blink::WebPlugin* plugin = NULL;
  if (GetContentClient()->renderer()->OverrideCreatePlugin(
          this, frame, params, &plugin)) {
    return plugin;
  }

  if (params.mime_type.ContainsOnlyASCII() &&
      params.mime_type.Ascii() == kBrowserPluginMimeType) {
    return BrowserPluginManager::Get()->CreateBrowserPlugin(
        this, GetContentClient()
                  ->renderer()
                  ->CreateBrowserPluginDelegate(this, kBrowserPluginMimeType,
                                                GURL(params.url))
                  ->GetWeakPtr());
  }

#if BUILDFLAG(ENABLE_PLUGINS)
  WebPluginInfo info;
  std::string mime_type;
  bool found = false;
  Send(new FrameHostMsg_GetPluginInfo(
      routing_id_, params.url, frame->Top()->GetSecurityOrigin(),
      params.mime_type.Utf8(), &found, &info, &mime_type));
  if (!found)
    return NULL;

  WebPluginParams params_to_use = params;
  params_to_use.mime_type = WebString::FromUTF8(mime_type);
  return CreatePlugin(frame, info, params_to_use, nullptr /* throttler */);
#else
  return NULL;
#endif  // BUILDFLAG(ENABLE_PLUGINS)
}

blink::WebMediaPlayer* RenderFrameImpl::CreateMediaPlayer(
    const blink::WebMediaPlayerSource& source,
    WebMediaPlayerClient* client,
    WebMediaPlayerEncryptedMediaClient* encrypted_client,
    WebContentDecryptionModule* initial_cdm,
    const blink::WebString& sink_id) {
  blink::WebSecurityOrigin security_origin = frame_->GetSecurityOrigin();
  blink::WebMediaStream web_stream =
      GetWebMediaStreamFromWebMediaPlayerSource(source);
  if (!web_stream.IsNull())
    return CreateWebMediaPlayerForMediaStream(client, sink_id, security_origin);

  // If |source| was not a MediaStream, it must be a URL.
  // TODO(guidou): Fix this when support for other srcObject types is added.
  DCHECK(source.IsURL());
  blink::WebURL url = source.GetAsURL();

  RenderThreadImpl* render_thread = RenderThreadImpl::current();
  // Render thread may not exist in tests, returning nullptr if it does not.
  if (!render_thread)
    return nullptr;

  scoped_refptr<media::SwitchableAudioRendererSink> audio_renderer_sink =
      AudioDeviceFactory::NewSwitchableAudioRendererSink(
          AudioDeviceFactory::kSourceMediaElement, routing_id_, 0,
          sink_id.Utf8(), security_origin);
  // We need to keep a reference to the context provider (see crbug.com/610527)
  // but media/ can't depend on cc/, so for now, just keep a reference in the
  // callback.
  // TODO(piman): replace media::Context3D to scoped_refptr<ContextProvider> in
  // media/ once ContextProvider is in gpu/.
  media::WebMediaPlayerParams::Context3DCB context_3d_cb = base::Bind(
      &GetSharedMainThreadContext3D,
      RenderThreadImpl::current()->SharedMainThreadContextProvider());

  // This must be created for every new WebMediaPlayer, each instance generates
  // a new player id which is used to collate logs on the browser side.
  // TODO(chcunningham, dalecurtis): This should be switched to a unique_ptr
  // owned by WebMediaPlayer to avoid this confusion. http://crbug.com/711818.
  scoped_refptr<media::MediaLog> media_log(
      new RenderMediaLog(url::Origin(security_origin).GetURL()));

  bool embedded_media_experience_enabled = false;
#if defined(OS_ANDROID)
  if (!UseMediaPlayerRenderer(url) && !media_surface_manager_)
    media_surface_manager_ = new RendererSurfaceViewManager(this);
  embedded_media_experience_enabled =
      GetWebkitPreferences().embedded_media_experience_enabled;
#endif  // defined(OS_ANDROID)

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  media::mojom::RemotingSourcePtr remoting_source;
  media::mojom::RemotingSourceRequest remoting_source_request(&remoting_source);
  media::mojom::RemoterPtr remoter;
  GetRemoterFactory()->Create(std::move(remoting_source),
                              mojo::MakeRequest(&remoter));
  using RemotingController = media::remoting::RendererController;
  std::unique_ptr<RemotingController> remoting_controller(
      new RemotingController(new media::remoting::SharedSession(
          std::move(remoting_source_request), std::move(remoter))));
  base::WeakPtr<media::MediaObserver> media_observer =
      remoting_controller->GetWeakPtr();
#else
  base::WeakPtr<media::MediaObserver> media_observer = nullptr;
#endif

  base::TimeDelta max_keyframe_distance_to_disable_background_video =
      base::TimeDelta::FromMilliseconds(base::GetFieldTrialParamByFeatureAsInt(
          media::kBackgroundVideoTrackOptimization, "max_keyframe_distance_ms",
          base::TimeDelta::FromSeconds(10).InMilliseconds()));
  base::TimeDelta max_keyframe_distance_to_disable_background_video_mse =
      base::TimeDelta::FromMilliseconds(base::GetFieldTrialParamByFeatureAsInt(
          media::kBackgroundVideoTrackOptimization,
          "max_keyframe_distance_media_source_ms",
          base::TimeDelta::FromSeconds(10).InMilliseconds()));

  media::WebMediaPlayerParams params(
      base::Bind(&ContentRendererClient::DeferMediaLoad,
                 base::Unretained(GetContentClient()->renderer()),
                 static_cast<RenderFrame*>(this),
                 GetWebMediaPlayerDelegate()->has_played_media()),
      audio_renderer_sink, media_log, render_thread->GetMediaThreadTaskRunner(),
      render_thread->GetWorkerTaskRunner(),
      render_thread->compositor_task_runner(), context_3d_cb,
      base::Bind(&v8::Isolate::AdjustAmountOfExternalAllocatedMemory,
                 base::Unretained(blink::MainThreadIsolate())),
      initial_cdm, media_surface_manager_, media_observer,
      max_keyframe_distance_to_disable_background_video,
      max_keyframe_distance_to_disable_background_video_mse,
      GetWebkitPreferences().enable_instant_source_buffer_gc,
      GetContentClient()->renderer()->AllowMediaSuspend(),
      embedded_media_experience_enabled);

  bool use_fallback_path = false;
#if defined(OS_ANDROID)
  use_fallback_path = UseMediaPlayerRenderer(url);
#endif  // defined(OS_ANDROID)

  std::unique_ptr<media::RendererFactory> media_renderer_factory;
  if (use_fallback_path) {
#if defined(OS_ANDROID)
    auto mojo_renderer_factory = base::MakeUnique<media::MojoRendererFactory>(
        media::MojoRendererFactory::GetGpuFactoriesCB(),
        GetRemoteInterfaces()->get());

    media_renderer_factory = base::MakeUnique<MediaPlayerRendererClientFactory>(
        render_thread->compositor_task_runner(),
        std::move(mojo_renderer_factory),
        base::Bind(&StreamTextureWrapperImpl::Create,
                   render_thread->EnableStreamTextureCopy(),
                   render_thread->GetStreamTexureFactory(),
                   base::ThreadTaskRunnerHandle::Get()));
#endif  // defined(OS_ANDROID)
  } else {
#if defined(ENABLE_MOJO_RENDERER)
#if BUILDFLAG(ENABLE_RUNTIME_MEDIA_RENDERER_SELECTION)
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableMojoRenderer)) {
      media_renderer_factory = base::MakeUnique<media::DefaultRendererFactory>(
          media_log, GetDecoderFactory(),
          base::Bind(&RenderThreadImpl::GetGpuFactories,
                     base::Unretained(render_thread)));
    }
#endif  // BUILDFLAG(ENABLE_RUNTIME_MEDIA_RENDERER_SELECTION)
    if (!media_renderer_factory) {
      media_renderer_factory = base::MakeUnique<media::MojoRendererFactory>(
          base::Bind(&RenderThreadImpl::GetGpuFactories,
                     base::Unretained(render_thread)),
          GetMediaInterfaceProvider());
    }
#else
    media_renderer_factory = base::MakeUnique<media::DefaultRendererFactory>(
        media_log, GetDecoderFactory(),
        base::Bind(&RenderThreadImpl::GetGpuFactories,
                   base::Unretained(render_thread)));
#endif  // defined(ENABLE_MOJO_RENDERER)
  }

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  media_renderer_factory =
      base::MakeUnique<media::remoting::AdaptiveRendererFactory>(
          std::move(media_renderer_factory), std::move(remoting_controller));
#endif

  if (!url_index_.get() || url_index_->frame() != frame_)
    url_index_.reset(new media::UrlIndex(frame_));

  media::WebMediaPlayerImpl* media_player = new media::WebMediaPlayerImpl(
      frame_, client, encrypted_client, GetWebMediaPlayerDelegate(),
      std::move(media_renderer_factory), url_index_, params);

#if defined(OS_ANDROID)  // WMPI_CAST
  media_player->SetMediaPlayerManager(GetMediaPlayerManager());
  media_player->SetDeviceScaleFactor(render_view_->GetDeviceScaleFactor());
  media_player->SetUseFallbackPath(use_fallback_path);
#endif  // defined(OS_ANDROID)

  return media_player;
}

blink::WebApplicationCacheHost* RenderFrameImpl::CreateApplicationCacheHost(
    blink::WebApplicationCacheHostClient* client) {
  if (!frame_ || !frame_->View())
    return NULL;

  DocumentState* document_state =
      frame_->ProvisionalDataSource()
          ? DocumentState::FromDataSource(frame_->ProvisionalDataSource())
          : DocumentState::FromDataSource(frame_->DataSource());

  NavigationStateImpl* navigation_state =
      static_cast<NavigationStateImpl*>(document_state->navigation_state());

  return new RendererWebApplicationCacheHostImpl(
      RenderViewImpl::FromWebView(frame_->View()), client,
      RenderThreadImpl::current()->appcache_dispatcher()->backend_proxy(),
      navigation_state->request_params().appcache_host_id);
}

blink::WebWorkerContentSettingsClientProxy*
RenderFrameImpl::CreateWorkerContentSettingsClientProxy() {
  if (!frame_ || !frame_->View())
    return NULL;
  return GetContentClient()->renderer()->CreateWorkerContentSettingsClientProxy(
      this, frame_);
}

WebExternalPopupMenu* RenderFrameImpl::CreateExternalPopupMenu(
    const WebPopupMenuInfo& popup_menu_info,
    WebExternalPopupMenuClient* popup_menu_client) {
#if BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
  // An IPC message is sent to the browser to build and display the actual
  // popup. The user could have time to click a different select by the time
  // the popup is shown. In that case external_popup_menu_ is non NULL.
  // By returning NULL in that case, we instruct Blink to cancel that new
  // popup. So from the user perspective, only the first one will show, and
  // will have to close the first one before another one can be shown.
  if (external_popup_menu_)
    return NULL;
  external_popup_menu_.reset(
      new ExternalPopupMenu(this, popup_menu_info, popup_menu_client));
  if (render_view_->screen_metrics_emulator_) {
    render_view_->SetExternalPopupOriginAdjustmentsForEmulation(
        external_popup_menu_.get(),
        render_view_->screen_metrics_emulator_.get());
  }
  return external_popup_menu_.get();
#else
  return NULL;
#endif
}

blink::WebCookieJar* RenderFrameImpl::CookieJar() {
  return &cookie_jar_;
}

blink::BlameContext* RenderFrameImpl::GetFrameBlameContext() {
  DCHECK(blame_context_);
  return blame_context_.get();
}

blink::WebServiceWorkerProvider*
RenderFrameImpl::CreateServiceWorkerProvider() {
  // At this point we should have non-null data source.
  DCHECK(frame_->DataSource());
  if (!ChildThreadImpl::current())
    return nullptr;  // May be null in some tests.
  ServiceWorkerNetworkProvider* provider =
      ServiceWorkerNetworkProvider::FromWebServiceWorkerNetworkProvider(
          frame_->DataSource()->GetServiceWorkerNetworkProvider());
  if (!provider->context()) {
    // The context can be null when the frame is sandboxed.
    return nullptr;
  }
  return new WebServiceWorkerProviderImpl(
      ChildThreadImpl::current()->thread_safe_sender(), provider->context());
}

void RenderFrameImpl::DidAccessInitialDocument() {
  DCHECK(!frame_->Parent());
  // NOTE: Do not call back into JavaScript here, since this call is made from a
  // V8 security check.

  // If the request hasn't yet committed, notify the browser process that it is
  // no longer safe to show the pending URL of the main frame, since a URL spoof
  // is now possible. (If the request has committed, the browser already knows.)
  if (!has_accessed_initial_document_) {
    DocumentState* document_state =
        DocumentState::FromDataSource(frame_->DataSource());
    NavigationStateImpl* navigation_state =
        static_cast<NavigationStateImpl*>(document_state->navigation_state());

    if (!navigation_state->request_committed()) {
      Send(new FrameHostMsg_DidAccessInitialDocument(routing_id_));
    }
  }

  has_accessed_initial_document_ = true;
}

blink::WebLocalFrame* RenderFrameImpl::CreateChildFrame(
    blink::WebLocalFrame* parent,
    blink::WebTreeScopeType scope,
    const blink::WebString& name,
    const blink::WebString& fallback_name,
    blink::WebSandboxFlags sandbox_flags,
    const blink::WebFrameOwnerProperties& frame_owner_properties) {
  // Synchronously notify the browser of a child frame creation to get the
  // routing_id for the RenderFrame.
  int child_routing_id = MSG_ROUTING_NONE;
  FrameHostMsg_CreateChildFrame_Params params;
  params.parent_routing_id = routing_id_;
  params.scope = scope;
  params.frame_name = name.Utf8();
  // The unique name generation logic was moved out of Blink, so for historical
  // reasons, unique name generation needs to take something called the
  // |fallback_name| into account. Normally, unique names are generated based on
  // the browing context name. For new frames, the initial browsing context name
  // comes from the name attribute of the browsing context container element.
  //
  // However, when the browsing context name is null, Blink instead uses the
  // "fallback name" to derive the unique name. The exact contents of the
  // "fallback name" are unspecified, but may contain the value of the
  // 'subresource attribute' of the browsing context container element.
  //
  // Note that Blink can't be changed to just pass |fallback_name| as |name| in
  // the case |name| is empty: |fallback_name| should never affect the actual
  // browsing context name, only unique name generation.
  params.frame_unique_name = UniqueNameHelper::GenerateNameForNewChildFrame(
      parent,
      params.frame_name.empty() ? fallback_name.Utf8() : params.frame_name);
  params.sandbox_flags = sandbox_flags;
  params.frame_owner_properties =
      ConvertWebFrameOwnerPropertiesToFrameOwnerProperties(
          frame_owner_properties);
  Send(new FrameHostMsg_CreateChildFrame(params, &child_routing_id));

  // Allocation of routing id failed, so we can't create a child frame. This can
  // happen if the synchronous IPC message above has failed.  This can
  // legitimately happen when the browser process has already destroyed
  // RenderProcessHost, but the renderer process hasn't quit yet.
  if (child_routing_id == MSG_ROUTING_NONE)
    return nullptr;

  // This method is always called by local frames, never remote frames.

  // Tracing analysis uses this to find main frames when this value is
  // MSG_ROUTING_NONE, and build the frame tree otherwise.
  TRACE_EVENT2("navigation,rail", "RenderFrameImpl::createChildFrame",
               "id", routing_id_,
               "child", child_routing_id);

  // Create the RenderFrame and WebLocalFrame, linking the two.
  RenderFrameImpl* child_render_frame =
      RenderFrameImpl::Create(render_view_, child_routing_id);
  child_render_frame->unique_name_helper_.set_propagated_name(
      params.frame_unique_name);
  child_render_frame->InitializeBlameContext(this);
  blink::WebLocalFrame* web_frame = WebLocalFrame::Create(
      scope, child_render_frame,
      child_render_frame->blink_interface_provider_.get(),
      child_render_frame->blink_interface_registry_.get());
  child_render_frame->BindToWebFrame(web_frame);

  // Add the frame to the frame tree and initialize it.
  parent->AppendChild(web_frame);
  child_render_frame->in_frame_tree_ = true;
  child_render_frame->Initialize();

  return web_frame;
}

void RenderFrameImpl::DidChangeOpener(blink::WebFrame* opener) {
  // Only a local frame should be able to update another frame's opener.
  DCHECK(!opener || opener->IsWebLocalFrame());

  int opener_routing_id =
      opener ? RenderFrameImpl::FromWebFrame(opener->ToWebLocalFrame())
                   ->GetRoutingID()
             : MSG_ROUTING_NONE;
  Send(new FrameHostMsg_DidChangeOpener(routing_id_, opener_routing_id));
}

void RenderFrameImpl::FrameDetached(blink::WebLocalFrame* frame,
                                    DetachType type) {
  // NOTE: This function is called on the frame that is being detached and not
  // the parent frame.  This is different from createChildFrame() which is
  // called on the parent frame.
  DCHECK_EQ(frame_, frame);

#if BUILDFLAG(ENABLE_PLUGINS)
  if (focused_pepper_plugin_)
    GetRenderWidget()->set_focused_pepper_plugin(nullptr);
#endif

  for (auto& observer : observers_)
    observer.FrameDetached();
  for (auto& observer : render_view_->observers())
    observer.FrameDetached(frame);

  // Send a state update before the frame is detached.
  SendUpdateState();

  // We only notify the browser process when the frame is being detached for
  // removal and it was initiated from the renderer process.
  if (!in_browser_initiated_detach_ && type == DetachType::kRemove)
    Send(new FrameHostMsg_Detach(routing_id_));

  // Clean up the associated RenderWidget for the frame, if there is one.
  if (render_widget_) {
    render_widget_->UnregisterRenderFrame(this);
    render_widget_->CloseForFrame();
  }

  // We need to clean up subframes by removing them from the map and deleting
  // the RenderFrameImpl.  In contrast, the main frame is owned by its
  // containing RenderViewHost (so that they have the same lifetime), so only
  // removal from the map is needed and no deletion.
  FrameMap::iterator it = g_frame_map.Get().find(frame);
  CHECK(it != g_frame_map.Get().end());
  CHECK_EQ(it->second, this);
  g_frame_map.Get().erase(it);

  // Only remove the frame from the renderer's frame tree if the frame is
  // being detached for removal and is already inserted in the frame tree.
  // In the case of a swap, the frame needs to remain in the tree so
  // WebFrame::swap() can replace it with the new frame.
  if (!is_main_frame_ && in_frame_tree_ && type == DetachType::kRemove) {
    frame->Parent()->RemoveChild(frame);
  }

  // |frame| is invalid after here.  Be sure to clear frame_ as well, since this
  // object may not be deleted immediately and other methods may try to access
  // it.
  frame->Close();
  frame_ = nullptr;

  // If this was a provisional frame with an associated proxy, tell the proxy
  // that it's no longer associated with this frame.
  if (proxy_routing_id_ != MSG_ROUTING_NONE) {
    RenderFrameProxy* proxy =
        RenderFrameProxy::FromRoutingID(proxy_routing_id_);

    // |proxy| should always exist.  Detaching the proxy would've also detached
    // this provisional frame.  The proxy should also not be associated with
    // another provisional frame at this point.
    CHECK(proxy);
    CHECK_EQ(routing_id_, proxy->provisional_frame_routing_id());

    proxy->set_provisional_frame_routing_id(MSG_ROUTING_NONE);
  }

  delete this;
  // Object is invalid after this point.
}

void RenderFrameImpl::FrameFocused() {
  Send(new FrameHostMsg_FrameFocused(routing_id_));
}

void RenderFrameImpl::WillCommitProvisionalLoad() {
  for (auto& observer : observers_)
    observer.WillCommitProvisionalLoad();
}

void RenderFrameImpl::DidChangeName(const blink::WebString& name) {
  if (current_history_item_.IsNull()) {
    // Once a navigation has committed, the unique name must no longer change to
    // avoid breaking back/forward navigations: https://crbug.com/607205
    unique_name_helper_.UpdateName(name.Utf8());
  }
  Send(new FrameHostMsg_DidChangeName(routing_id_, name.Utf8(),
                                      unique_name_helper_.value()));

  if (!committed_first_load_)
    name_changed_before_first_commit_ = true;
}

void RenderFrameImpl::DidEnforceInsecureRequestPolicy(
    blink::WebInsecureRequestPolicy policy) {
  Send(new FrameHostMsg_EnforceInsecureRequestPolicy(routing_id_, policy));
}

void RenderFrameImpl::DidUpdateToUniqueOrigin(
    bool is_potentially_trustworthy_unique_origin) {
  Send(new FrameHostMsg_UpdateToUniqueOrigin(
      routing_id_, is_potentially_trustworthy_unique_origin));
}

void RenderFrameImpl::DidChangeSandboxFlags(blink::WebFrame* child_frame,
                                            blink::WebSandboxFlags flags) {
  Send(new FrameHostMsg_DidChangeSandboxFlags(
      routing_id_, GetRoutingIdForFrameOrProxy(child_frame), flags));
}

void RenderFrameImpl::DidSetFeaturePolicyHeader(
    const blink::WebParsedFeaturePolicy& parsed_header) {
  Send(new FrameHostMsg_DidSetFeaturePolicyHeader(
      routing_id_, FeaturePolicyHeaderFromWeb(parsed_header)));
}

void RenderFrameImpl::DidAddContentSecurityPolicies(
    const blink::WebVector<blink::WebContentSecurityPolicy>& policies) {
  std::vector<ContentSecurityPolicy> content_policies;
  for (const auto& policy : policies)
    content_policies.push_back(BuildContentSecurityPolicy(policy));

  Send(new FrameHostMsg_DidAddContentSecurityPolicies(routing_id_,
                                                      content_policies));
}

void RenderFrameImpl::DidChangeFrameOwnerProperties(
    blink::WebFrame* child_frame,
    const blink::WebFrameOwnerProperties& frame_owner_properties) {
  Send(new FrameHostMsg_DidChangeFrameOwnerProperties(
      routing_id_, GetRoutingIdForFrameOrProxy(child_frame),
      ConvertWebFrameOwnerPropertiesToFrameOwnerProperties(
          frame_owner_properties)));
}

void RenderFrameImpl::DidMatchCSS(
    blink::WebLocalFrame* frame,
    const blink::WebVector<blink::WebString>& newly_matching_selectors,
    const blink::WebVector<blink::WebString>& stopped_matching_selectors) {
  DCHECK_EQ(frame_, frame);

  for (auto& observer : observers_)
    observer.DidMatchCSS(newly_matching_selectors, stopped_matching_selectors);
}

void RenderFrameImpl::SetHasReceivedUserGesture() {
  Send(new FrameHostMsg_SetHasReceivedUserGesture(routing_id_));
}

bool RenderFrameImpl::ShouldReportDetailedMessageForSource(
    const blink::WebString& source) {
  return GetContentClient()->renderer()->ShouldReportDetailedMessageForSource(
      source.Utf16());
}

void RenderFrameImpl::DidAddMessageToConsole(
    const blink::WebConsoleMessage& message,
    const blink::WebString& source_name,
    unsigned source_line,
    const blink::WebString& stack_trace) {
  logging::LogSeverity log_severity = logging::LOG_VERBOSE;
  switch (message.level) {
    case blink::WebConsoleMessage::kLevelVerbose:
      log_severity = logging::LOG_VERBOSE;
      break;
    case blink::WebConsoleMessage::kLevelInfo:
      log_severity = logging::LOG_INFO;
      break;
    case blink::WebConsoleMessage::kLevelWarning:
      log_severity = logging::LOG_WARNING;
      break;
    case blink::WebConsoleMessage::kLevelError:
      log_severity = logging::LOG_ERROR;
      break;
    default:
      log_severity = logging::LOG_VERBOSE;
  }

  if (ShouldReportDetailedMessageForSource(source_name)) {
    for (auto& observer : observers_) {
      observer.DetailedConsoleMessageAdded(
          message.text.Utf16(), source_name.Utf16(), stack_trace.Utf16(),
          source_line, static_cast<uint32_t>(log_severity));
    }
  }

  Send(new FrameHostMsg_DidAddMessageToConsole(
      routing_id_, static_cast<int32_t>(log_severity), message.text.Utf16(),
      static_cast<int32_t>(source_line), source_name.Utf16()));
}

void RenderFrameImpl::LoadURLExternally(const blink::WebURLRequest& request,
                                        blink::WebNavigationPolicy policy,
                                        const blink::WebString& suggested_name,
                                        bool should_replace_current_entry) {
  Referrer referrer(RenderViewImpl::GetReferrerFromRequest(frame_, request));
  if (policy == blink::kWebNavigationPolicyDownload) {
    FrameHostMsg_DownloadUrl_Params params;
    params.render_view_id = render_view_->GetRoutingID();
    params.render_frame_id = GetRoutingID();
    params.url = request.Url();
    params.referrer = referrer;
    params.initiator_origin = request.RequestorOrigin();
    params.suggested_name = suggested_name.Utf16();

    Send(new FrameHostMsg_DownloadUrl(params));
  } else {
    OpenURL(request.Url(), IsHttpPost(request),
            GetRequestBodyForWebURLRequest(request),
            GetWebURLRequestHeaders(request), referrer, policy,
            should_replace_current_entry, false);
  }
}

blink::WebHistoryItem RenderFrameImpl::HistoryItemForNewChildFrame() {
  // We will punt this navigation to the browser in decidePolicyForNavigation.
  // TODO(creis): Look into cleaning this up.
  return WebHistoryItem();
}

void RenderFrameImpl::WillSendSubmitEvent(const blink::WebFormElement& form) {
  for (auto& observer : observers_)
    observer.WillSendSubmitEvent(form);
}

void RenderFrameImpl::WillSubmitForm(const blink::WebFormElement& form) {
  DocumentState* document_state =
      DocumentState::FromDataSource(frame_->ProvisionalDataSource());
  NavigationStateImpl* navigation_state =
      static_cast<NavigationStateImpl*>(document_state->navigation_state());
  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDocumentState(document_state);

  if (ui::PageTransitionCoreTypeIs(navigation_state->GetTransitionType(),
                                   ui::PAGE_TRANSITION_LINK)) {
    navigation_state->set_transition_type(ui::PAGE_TRANSITION_FORM_SUBMIT);
  }

  // Save these to be processed when the ensuing navigation is committed.
  WebSearchableFormData web_searchable_form_data(form);
  internal_data->set_searchable_form_url(web_searchable_form_data.Url());
  internal_data->set_searchable_form_encoding(
      web_searchable_form_data.Encoding().Utf8());

  for (auto& observer : observers_)
    observer.WillSubmitForm(form);
}

void RenderFrameImpl::DidCreateDataSource(blink::WebLocalFrame* frame,
                                          blink::WebDataSource* datasource) {
  DCHECK(!frame_ || frame_ == frame);

  bool content_initiated = !pending_navigation_params_.get();

  // Make sure any previous redirect URLs end up in our new data source.
  if (pending_navigation_params_.get() && !IsBrowserSideNavigationEnabled()) {
    for (const auto& i :
         pending_navigation_params_->request_params.redirects) {
      datasource->AppendRedirect(i);
    }
  }

  DocumentState* document_state = DocumentState::FromDataSource(datasource);
  if (!document_state) {
    document_state = new DocumentState;
    datasource->SetExtraData(document_state);
    if (!content_initiated)
      PopulateDocumentStateFromPending(document_state);
  }

  // Carry over the user agent override flag, if it exists.
  blink::WebView* webview = render_view_->webview();
  if (content_initiated && webview && webview->MainFrame() &&
      webview->MainFrame()->IsWebLocalFrame() &&
      webview->MainFrame()->DataSource()) {
    DocumentState* old_document_state =
        DocumentState::FromDataSource(webview->MainFrame()->DataSource());
    if (old_document_state) {
      InternalDocumentStateData* internal_data =
          InternalDocumentStateData::FromDocumentState(document_state);
      InternalDocumentStateData* old_internal_data =
          InternalDocumentStateData::FromDocumentState(old_document_state);
      internal_data->set_is_overriding_user_agent(
          old_internal_data->is_overriding_user_agent());
    }
  }

  // The rest of RenderView assumes that a WebDataSource will always have a
  // non-null NavigationState.
  UpdateNavigationState(document_state, false /* was_within_same_page */,
                        content_initiated);

  NavigationStateImpl* navigation_state = static_cast<NavigationStateImpl*>(
      document_state->navigation_state());

  // Set the navigation start time in blink.
  datasource->SetNavigationStartTime(
      ConvertToBlinkTime(navigation_state->common_params().navigation_start));

  // PlzNavigate: if an actual navigation took place, inform the datasource of
  // what happened in the browser.
  if (IsBrowserSideNavigationEnabled() &&
      !navigation_state->request_params()
           .navigation_timing.fetch_start.is_null()) {
    // Set timing of several events that happened during navigation.
    // They will be used in blink for the Navigation Timing API.
    double redirect_start = ConvertToBlinkTime(
        navigation_state->request_params().navigation_timing.redirect_start);
    double redirect_end = ConvertToBlinkTime(
        navigation_state->request_params().navigation_timing.redirect_end);
    double fetch_start = ConvertToBlinkTime(
        navigation_state->request_params().navigation_timing.fetch_start);

    datasource->UpdateNavigation(
        redirect_start, redirect_end, fetch_start,
        !navigation_state->request_params().redirects.empty());
    // TODO(clamy) We need to provide additional timing values for the
    // Navigation Timing API to work with browser-side navigations.
    // UnloadEventStart and UnloadEventEnd are still missing.
  }

  // PlzNavigate: update the source location before processing the navigation
  // commit.
  if (IsBrowserSideNavigationEnabled() &&
      navigation_state->common_params().source_location.has_value()) {
    blink::WebSourceLocation source_location;
    source_location.url = WebString::FromLatin1(
        navigation_state->common_params().source_location->url);
    source_location.line_number =
        navigation_state->common_params().source_location->line_number;
    source_location.column_number =
        navigation_state->common_params().source_location->column_number;
    datasource->SetSourceLocation(source_location);
  }

  // Create the serviceworker's per-document network observing object if it
  // does not exist (When navigation happens within a page, the provider already
  // exists).
  if (datasource->GetServiceWorkerNetworkProvider())
    return;

  datasource->SetServiceWorkerNetworkProvider(
      ServiceWorkerNetworkProvider::CreateForNavigation(
          routing_id_, navigation_state->request_params(), frame,
          content_initiated));
}

void RenderFrameImpl::DidStartProvisionalLoad(blink::WebDataSource* data_source,
                                              blink::WebURLRequest& request) {
  // In fast/loader/stop-provisional-loads.html, we abort the load before this
  // callback is invoked.
  if (!data_source)
    return;

  TRACE_EVENT2("navigation,benchmark,rail",
               "RenderFrameImpl::didStartProvisionalLoad", "id", routing_id_,
               "url", data_source->GetRequest().Url().GetString().Utf8());

  // PlzNavigate:
  // If we have a pending navigation to be sent to the browser send it here.
  if (pending_navigation_info_.get()) {
    DCHECK(IsBrowserSideNavigationEnabled());
    NavigationPolicyInfo info(request);
    info.navigation_type = pending_navigation_info_->navigation_type;
    info.default_policy = pending_navigation_info_->policy;
    info.replaces_current_history_item =
        pending_navigation_info_->replaces_current_history_item;
    info.is_history_navigation_in_new_child_frame =
        pending_navigation_info_->history_navigation_in_new_child_frame;
    info.is_client_redirect = pending_navigation_info_->client_redirect;
    info.is_cache_disabled = pending_navigation_info_->cache_disabled;
    info.form = pending_navigation_info_->form;
    info.source_location = pending_navigation_info_->source_location;

    pending_navigation_info_.reset(nullptr);

    BeginNavigation(info);
  }

  DocumentState* document_state = DocumentState::FromDataSource(data_source);
  NavigationStateImpl* navigation_state = static_cast<NavigationStateImpl*>(
      document_state->navigation_state());
  bool is_top_most = !frame_->Parent();
  if (is_top_most) {
    render_view_->set_navigation_gesture(
        WebUserGestureIndicator::IsProcessingUserGesture()
            ? NavigationGestureUser
            : NavigationGestureAuto);
  } else if (data_source->ReplacesCurrentHistoryItem()) {
    // Subframe navigations that don't add session history items must be
    // marked with AUTO_SUBFRAME. See also didFailProvisionalLoad for how we
    // handle loading of error pages.
    navigation_state->set_transition_type(ui::PAGE_TRANSITION_AUTO_SUBFRAME);
  }

  base::TimeTicks navigation_start =
      navigation_state->common_params().navigation_start;
  DCHECK(!navigation_start.is_null());

  for (auto& observer : render_view_->observers())
    observer.DidStartProvisionalLoad(frame_);
  for (auto& observer : observers_)
    observer.DidStartProvisionalLoad(data_source);

  std::vector<GURL> redirect_chain;
  GetRedirectChain(data_source, &redirect_chain);

  Send(new FrameHostMsg_DidStartProvisionalLoad(
      routing_id_, data_source->GetRequest().Url(), redirect_chain,
      navigation_start));
}

void RenderFrameImpl::DidReceiveServerRedirectForProvisionalLoad() {
  // TODO(creis): Determine if this can be removed or if we need to clear any
  // local state here to fix https://crbug.com/671276.
}

void RenderFrameImpl::DidFailProvisionalLoad(
    blink::WebLocalFrame* frame,
    const blink::WebURLError& error,
    blink::WebHistoryCommitType commit_type) {
  TRACE_EVENT1("navigation,benchmark,rail",
               "RenderFrameImpl::didFailProvisionalLoad", "id", routing_id_);
  DCHECK_EQ(frame_, frame);
  // Note: It is important this notification occur before DidStopLoading so the
  //       SSL manager can react to the provisional load failure before being
  //       notified the load stopped.
  //
  for (auto& observer : render_view_->observers())
    observer.DidFailProvisionalLoad(frame, error);
  for (auto& observer : observers_)
    observer.DidFailProvisionalLoad(error);

  WebDataSource* ds = frame->ProvisionalDataSource();
  if (!ds)
    return;

  const WebURLRequest& failed_request = ds->GetRequest();

  // Notify the browser that we failed a provisional load with an error.
  SendFailedProvisionalLoad(failed_request, error, frame);

  if (!ShouldDisplayErrorPageForFailedLoad(error.reason, error.unreachable_url))
    return;

  // Make sure we never show errors in view source mode.
  frame->EnableViewSourceMode(false);

  DocumentState* document_state = DocumentState::FromDataSource(ds);
  NavigationStateImpl* navigation_state =
      static_cast<NavigationStateImpl*>(document_state->navigation_state());

  // If this is a failed back/forward/reload navigation, then we need to do a
  // 'replace' load.  This is necessary to avoid messing up session history.
  // Otherwise, we do a normal load, which simulates a 'go' navigation as far
  // as session history is concerned.
  bool replace = commit_type != blink::kWebStandardCommit;

  // If we failed on a browser initiated request, then make sure that our error
  // page load is regarded as the same browser initiated request.
  if (!navigation_state->IsContentInitiated()) {
    pending_navigation_params_.reset(new NavigationParams(
        navigation_state->common_params(), navigation_state->start_params(),
        navigation_state->request_params()));
  }

  // Load an error page.
  LoadNavigationErrorPage(failed_request, error, replace, nullptr);
}

void RenderFrameImpl::DidCommitProvisionalLoad(
    blink::WebLocalFrame* frame,
    const blink::WebHistoryItem& item,
    blink::WebHistoryCommitType commit_type) {
  TRACE_EVENT2("navigation,rail", "RenderFrameImpl::didCommitProvisionalLoad",
               "id", routing_id_,
               "url", GetLoadingUrl().possibly_invalid_spec());
  DCHECK_EQ(frame_, frame);

  // TODO(dcheng): Remove this UMA once we have enough measurements.
  // Record the number of subframes where window.name changes between the
  // creation of the frame and the first commit that records a history entry
  // with a persisted unique name. We'd like to make unique name immutable to
  // simplify code, but it's unclear if there are site that depend on the
  // following pattern:
  //   1. Create a new subframe.
  //   2. Assign it a window.name.
  //   3. Navigate it.
  //
  // If unique name are immutable, then it's possible that session history would
  // become less reliable for subframes:
  //   * A subframe with no initial name will receive a generated name that
  //     depends on DOM insertion order instead of using a name baed on the
  //     window.name assigned in step 2.
  //   * A subframe may intentionally try to choose a non-conflicting
  //     window.name if it detects a conflict. Immutability would prevent this
  //     from having the desired effect.
  //
  // The logic for when to record the UMA is a bit subtle:
  //   * if |committed_first_load_| is false and |current_history_item_| is
  //     null, then this is being called to commit the initial empty document.
  //     Don't record the UMA yet. |current_history_item_| will be non-null in
  //     subsequent invocations of this callback.
  //   * if |committed_first_load_| is false and |current_history_item_| is
  //     *not* null, then the initial empty document has already committed.
  //     Record if window.name has changed.
  if (!committed_first_load_ && !current_history_item_.IsNull()) {
    if (!IsMainFrame()) {
      UMA_HISTOGRAM_BOOLEAN(
          "SessionRestore.SubFrameUniqueNameChangedBeforeFirstCommit",
          name_changed_before_first_commit_);
    }
    // TODO(dcheng): This signal is likely calculated incorrectly, and will be
    // removed in a followup CL (as we've decided to try to preserve backwards
    // compatibility as much as possible for the time being).
    committed_first_load_ = true;
  }

  DocumentState* document_state =
      DocumentState::FromDataSource(frame->DataSource());
  NavigationStateImpl* navigation_state =
      static_cast<NavigationStateImpl*>(document_state->navigation_state());
  const WebURLResponse& web_url_response = frame->DataSource()->GetResponse();
  WebURLResponseExtraDataImpl* extra_data =
      GetExtraDataFromResponse(web_url_response);
  // Only update the PreviewsState and effective connection type states for new
  // main frame documents. Subframes inherit from the main frame and should not
  // change at commit time.
  if (is_main_frame_ && !navigation_state->WasWithinSameDocument()) {
    previews_state_ =
        extra_data ? extra_data->previews_state() : PREVIEWS_OFF;

    // Set lite pages off if a lite page was not loaded for the main frame.
    if (web_url_response
            .HttpHeaderField(
                WebString::FromUTF8(kChromeProxyContentTransformHeader))
            .Utf8() != kChromeProxyLitePageDirective) {
      previews_state_ &= ~(SERVER_LITE_PAGE_ON);
    }

    if (extra_data) {
      effective_connection_type_ =
          EffectiveConnectionTypeToWebEffectiveConnectionType(
              extra_data->effective_connection_type());
    }
  }

  if (proxy_routing_id_ != MSG_ROUTING_NONE) {
    // If this is a provisional frame associated with a proxy (i.e., a frame
    // created for a remote-to-local navigation), swap it into the frame tree
    // now.
    if (!SwapIn())
      return;
  }

  // Navigations that change the document represent a new content source.  Keep
  // track of that on the widget to help the browser process detect when stale
  // compositor frames are being shown after a commit.
  if (is_main_frame_ && !navigation_state->WasWithinSameDocument())
    GetRenderWidget()->IncrementContentSourceId();

  // When we perform a new navigation, we need to update the last committed
  // session history entry with state for the page we are leaving. Do this
  // before updating the current history item.
  SendUpdateState();

  // Update the current history item for this frame.
  current_history_item_ = item;
  // Note: don't reference |item| after this point, as its value may not match
  // |current_history_item_|.
  current_history_item_.SetTarget(
      blink::WebString::FromUTF8(unique_name_helper_.value()));

  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDocumentState(document_state);

  if (internal_data->must_reset_scroll_and_scale_state()) {
    render_view_->webview()->ResetScrollAndScaleState();
    internal_data->set_must_reset_scroll_and_scale_state(false);
  }

  const RequestNavigationParams& request_params =
      navigation_state->request_params();
  bool is_new_navigation = commit_type == blink::kWebStandardCommit;
  if (is_new_navigation) {
    DCHECK(!navigation_state->common_params().should_replace_current_entry ||
           render_view_->history_list_length_ > 0);
    if (!navigation_state->common_params().should_replace_current_entry) {
      // Advance our offset in session history, applying the length limit.
      // There is now no forward history.
      render_view_->history_list_offset_++;
      if (render_view_->history_list_offset_ >= kMaxSessionHistoryEntries)
        render_view_->history_list_offset_ = kMaxSessionHistoryEntries - 1;
      render_view_->history_list_length_ =
          render_view_->history_list_offset_ + 1;
    }
  } else {
    if (request_params.nav_entry_id != 0 &&
        !request_params.intended_as_new_entry) {
      // This is a successful session history navigation!
      render_view_->history_list_offset_ =
          request_params.pending_history_list_offset;
    }
  }

  for (auto& observer : render_view_->observers_)
    observer.DidCommitProvisionalLoad(frame, is_new_navigation);
  for (auto& observer : observers_) {
    observer.DidCommitProvisionalLoad(
        is_new_navigation, navigation_state->WasWithinSameDocument());
  }

  if (!frame->Parent()) {  // Only for top frames.
    RenderThreadImpl* render_thread_impl = RenderThreadImpl::current();
    if (render_thread_impl) {  // Can be NULL in tests.
      render_thread_impl->histogram_customizer()->
          RenderViewNavigatedToHost(GURL(GetLoadingUrl()).host(),
                                    RenderView::GetRenderViewCount());
      // The scheduler isn't interested in history inert commits unless they
      // are reloads.
      if (commit_type != blink::kWebHistoryInertCommit ||
          PageTransitionCoreTypeIs(navigation_state->GetTransitionType(),
                                   ui::PAGE_TRANSITION_RELOAD)) {
        render_thread_impl->GetRendererScheduler()->OnNavigationStarted();
      }
    }
  }

  // Remember that we've already processed this request, so we don't update
  // the session history again.  We do this regardless of whether this is
  // a session history navigation, because if we attempted a session history
  // navigation without valid HistoryItem state, WebCore will think it is a
  // new navigation.
  navigation_state->set_request_committed(true);

  SendDidCommitProvisionalLoad(frame, commit_type);

  // Check whether we have new encoding name.
  UpdateEncoding(frame, frame->View()->PageEncoding().Utf8());
}

void RenderFrameImpl::DidCreateNewDocument(blink::WebLocalFrame* frame) {
  DCHECK(!frame_ || frame_ == frame);

  for (auto& observer : observers_)
    observer.DidCreateNewDocument();
  for (auto& observer : render_view_->observers())
    observer.DidCreateNewDocument(frame);
}

void RenderFrameImpl::DidClearWindowObject(blink::WebLocalFrame* frame) {
  DCHECK_EQ(frame_, frame);

  if (enabled_bindings_ & BINDINGS_POLICY_WEB_UI)
    WebUIExtension::Install(frame);

  if (enabled_bindings_ & BINDINGS_POLICY_DOM_AUTOMATION)
    DomAutomationController::Install(this, frame);

  if (enabled_bindings_ & BINDINGS_POLICY_STATS_COLLECTION)
    StatsCollectionController::Install(frame);

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  if (command_line.HasSwitch(cc::switches::kEnableGpuBenchmarking))
    GpuBenchmarking::Install(frame);

  if (command_line.HasSwitch(switches::kEnableSkiaBenchmarking))
    SkiaBenchmarking::Install(frame);

  for (auto& observer : render_view_->observers())
    observer.DidClearWindowObject(frame);
  for (auto& observer : observers_)
    observer.DidClearWindowObject();
}

void RenderFrameImpl::DidCreateDocumentElement(blink::WebLocalFrame* frame) {
  DCHECK(!frame_ || frame_ == frame);

  // Notify the browser about non-blank documents loading in the top frame.
  GURL url = frame->GetDocument().Url();
  if (url.is_valid() && url.spec() != url::kAboutBlankURL) {
    // TODO(nasko): Check if webview()->mainFrame() is the same as the
    // frame->tree()->top().
    blink::WebFrame* main_frame = render_view_->webview()->MainFrame();
    if (frame == main_frame) {
      // For now, don't remember plugin zoom values.  We don't want to mix them
      // with normal web content (i.e. a fixed layout plugin would usually want
      // them different).
      render_view_->Send(new ViewHostMsg_DocumentAvailableInMainFrame(
          render_view_->GetRoutingID(),
          main_frame->GetDocument().IsPluginDocument()));
    }
  }

  for (auto& observer : observers_)
    observer.DidCreateDocumentElement();
  for (auto& observer : render_view_->observers())
    observer.DidCreateDocumentElement(frame);
}

void RenderFrameImpl::RunScriptsAtDocumentElementAvailable(
    blink::WebLocalFrame* frame) {
  DCHECK(!frame_ || frame_ == frame);
  base::WeakPtr<RenderFrameImpl> weak_self = weak_factory_.GetWeakPtr();

  MojoBindingsController* mojo_bindings_controller =
      MojoBindingsController::Get(this);
  if (mojo_bindings_controller)
    mojo_bindings_controller->RunScriptsAtDocumentStart();

  if (!weak_self.get())
    return;

  GetContentClient()->renderer()->RunScriptsAtDocumentStart(this);
  // Do not use |this| or |frame|! ContentClient might have deleted them by now!
}

void RenderFrameImpl::DidReceiveTitle(blink::WebLocalFrame* frame,
                                      const blink::WebString& title,
                                      blink::WebTextDirection direction) {
  DCHECK_EQ(frame_, frame);
  // Ignore all but top level navigations.
  if (!frame->Parent()) {
    base::trace_event::TraceLog::GetInstance()->UpdateProcessLabel(
        routing_id_, title.Utf8());

    base::string16 title16 = title.Utf16();
    base::string16 shortened_title = title16.substr(0, kMaxTitleChars);
    Send(new FrameHostMsg_UpdateTitle(routing_id_,
                                      shortened_title, direction));
  }

  // Also check whether we have new encoding name.
  UpdateEncoding(frame, frame->View()->PageEncoding().Utf8());
}

void RenderFrameImpl::DidChangeIcon(blink::WebIconURL::Type icon_type) {
  // TODO(nasko): Investigate wheather implementation should move here.
  render_view_->didChangeIcon(frame_, icon_type);
}

void RenderFrameImpl::DidFinishDocumentLoad(blink::WebLocalFrame* frame) {
  TRACE_EVENT1("navigation,benchmark,rail",
               "RenderFrameImpl::didFinishDocumentLoad", "id", routing_id_);
  DCHECK_EQ(frame_, frame);

  Send(new FrameHostMsg_DidFinishDocumentLoad(routing_id_));

  for (auto& observer : render_view_->observers())
    observer.DidFinishDocumentLoad(frame);
  for (auto& observer : observers_)
    observer.DidFinishDocumentLoad();

  // Check whether we have new encoding name.
  UpdateEncoding(frame, frame->View()->PageEncoding().Utf8());
}

void RenderFrameImpl::RunScriptsAtDocumentReady(bool document_is_empty) {
  base::WeakPtr<RenderFrameImpl> weak_self = weak_factory_.GetWeakPtr();

  MojoBindingsController* mojo_bindings_controller =
      MojoBindingsController::Get(this);
  if (mojo_bindings_controller)
    mojo_bindings_controller->RunScriptsAtDocumentReady();

  if (!weak_self.get())
    return;

  GetContentClient()->renderer()->RunScriptsAtDocumentEnd(this);

  // ContentClient might have deleted |frame_| and |this| by now!
  if (!weak_self.get())
    return;

  // If this is an empty document with an http status code indicating an error,
  // we may want to display our own error page, so the user doesn't end up
  // with an unexplained blank page.
  if (!document_is_empty)
    return;

  // Do not show error page when DevTools is attached.
  const RenderFrameImpl* localRoot = GetLocalRoot();
  if (localRoot->devtools_agent_ && localRoot->devtools_agent_->IsAttached())
    return;

  // Display error page instead of a blank page, if appropriate.
  std::string error_domain = "http";
  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDataSource(frame_->DataSource());
  int http_status_code = internal_data->http_status_code();
  if (GetContentClient()->renderer()->HasErrorPage(http_status_code,
                                                   &error_domain)) {
    WebURLError error;
    error.unreachable_url = frame_->GetDocument().Url();
    error.domain = WebString::FromUTF8(error_domain);
    error.reason = http_status_code;
    // This call may run scripts, e.g. via the beforeunload event.
    LoadNavigationErrorPage(frame_->DataSource()->GetRequest(), error, true,
                            nullptr);
  }
  // Do not use |this| or |frame_| here without checking |weak_self|.
}

void RenderFrameImpl::RunScriptsAtDocumentIdle() {
  GetContentClient()->renderer()->RunScriptsAtDocumentIdle(this);
  // ContentClient might have deleted |this| by now!
}

void RenderFrameImpl::DidHandleOnloadEvents() {
  if (!frame_->Parent()) {
    FrameMsg_UILoadMetricsReportType::Value report_type =
        static_cast<FrameMsg_UILoadMetricsReportType::Value>(
            frame_->DataSource()->GetRequest().InputPerfMetricReportPolicy());
    base::TimeTicks ui_timestamp =
        base::TimeTicks() +
        base::TimeDelta::FromSecondsD(
            frame_->DataSource()->GetRequest().UiStartTime());

    Send(new FrameHostMsg_DocumentOnLoadCompleted(
        routing_id_, report_type, ui_timestamp));
  }
}

void RenderFrameImpl::DidFailLoad(const blink::WebURLError& error,
                                  blink::WebHistoryCommitType commit_type) {
  TRACE_EVENT1("navigation,rail", "RenderFrameImpl::didFailLoad",
               "id", routing_id_);
  // TODO(nasko): Move implementation here. No state needed.
  WebDataSource* ds = frame_->DataSource();
  DCHECK(ds);

  for (auto& observer : render_view_->observers())
    observer.DidFailLoad(frame_, error);

  const WebURLRequest& failed_request = ds->GetRequest();
  base::string16 error_description;
  GetContentClient()->renderer()->GetNavigationErrorStrings(
      this,
      failed_request,
      error,
      nullptr,
      &error_description);
  Send(new FrameHostMsg_DidFailLoadWithError(routing_id_, failed_request.Url(),
                                             error.reason, error_description,
                                             error.was_ignored_by_handler));
}

void RenderFrameImpl::DidFinishLoad(blink::WebLocalFrame* frame) {
  TRACE_EVENT1("navigation,benchmark,rail",
               "RenderFrameImpl::didFinishLoad", "id", routing_id_);
  DCHECK_EQ(frame_, frame);
  if (!frame->Parent()) {
    TRACE_EVENT_INSTANT0("WebCore,benchmark,rail", "LoadFinished",
                         TRACE_EVENT_SCOPE_PROCESS);
  }

  for (auto& observer : render_view_->observers())
    observer.DidFinishLoad(frame);
  for (auto& observer : observers_)
    observer.DidFinishLoad();

  WebDataSource* ds = frame->DataSource();
  Send(new FrameHostMsg_DidFinishLoad(routing_id_, ds->GetRequest().Url()));

  if (RenderThreadImpl::current()) {
    RenderThreadImpl::RendererMemoryMetrics memory_metrics;
    if (!RenderThreadImpl::current()->GetRendererMemoryMetrics(&memory_metrics))
      return;
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.PartitionAlloc.DidFinishLoad",
        memory_metrics.partition_alloc_kb / 1024);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.BlinkGC.DidFinishLoad",
        memory_metrics.blink_gc_kb / 1024);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.Malloc.DidFinishLoad",
        memory_metrics.malloc_mb);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.Discardable.DidFinishLoad",
        memory_metrics.discardable_kb / 1024);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.V8MainThreadIsolate.DidFinishLoad",
        memory_metrics.v8_main_thread_isolate_mb);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.TotalAllocated.DidFinishLoad",
        memory_metrics.total_allocated_mb);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.NonDiscardableTotalAllocated."
        "DidFinishLoad",
        memory_metrics.non_discardable_total_allocated_mb);
    UMA_HISTOGRAM_MEMORY_MB(
        "Memory.Experimental.Renderer.TotalAllocatedPerRenderView."
        "DidFinishLoad",
        memory_metrics.total_allocated_per_render_view_mb);
    if (IsMainFrame()) {
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.PartitionAlloc."
          "MainFrameDidFinishLoad",
          memory_metrics.partition_alloc_kb / 1024);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.BlinkGC.MainFrameDidFinishLoad",
          memory_metrics.blink_gc_kb / 1024);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.Malloc.MainFrameDidFinishLoad",
          memory_metrics.malloc_mb);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.Discardable.MainFrameDidFinishLoad",
          memory_metrics.discardable_kb / 1024);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.V8MainThreadIsolate."
          "MainFrameDidFinishLoad",
          memory_metrics.v8_main_thread_isolate_mb);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.TotalAllocated."
          "MainFrameDidFinishLoad",
          memory_metrics.total_allocated_mb);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.NonDiscardableTotalAllocated."
          "MainFrameDidFinishLoad",
          memory_metrics.non_discardable_total_allocated_mb);
      UMA_HISTOGRAM_MEMORY_MB(
          "Memory.Experimental.Renderer.TotalAllocatedPerRenderView."
          "MainFrameDidFinishLoad",
          memory_metrics.total_allocated_per_render_view_mb);
    }
  }
}

void RenderFrameImpl::DidNavigateWithinPage(
    blink::WebLocalFrame* frame,
    const blink::WebHistoryItem& item,
    blink::WebHistoryCommitType commit_type,
    bool content_initiated) {
  TRACE_EVENT1("navigation,rail", "RenderFrameImpl::didNavigateWithinPage",
               "id", routing_id_);
  DCHECK_EQ(frame_, frame);
  DocumentState* document_state =
      DocumentState::FromDataSource(frame->DataSource());
  UpdateNavigationState(document_state, true /* was_within_same_page */,
                        content_initiated);
  static_cast<NavigationStateImpl*>(document_state->navigation_state())
      ->set_was_within_same_document(true);

  DidCommitProvisionalLoad(frame, item, commit_type);
}

void RenderFrameImpl::DidUpdateCurrentHistoryItem() {
  render_view_->StartNavStateSyncTimerIfNecessary(this);
}

void RenderFrameImpl::DidChangeThemeColor() {
  if (frame_->Parent())
    return;

  Send(new FrameHostMsg_DidChangeThemeColor(
      routing_id_, frame_->GetDocument().ThemeColor()));
}

void RenderFrameImpl::DispatchLoad() {
  Send(new FrameHostMsg_DispatchLoad(routing_id_));
}

blink::WebEffectiveConnectionType
RenderFrameImpl::GetEffectiveConnectionType() {
  return effective_connection_type_;
}

void RenderFrameImpl::AbortClientNavigation() {
  Send(new FrameHostMsg_AbortNavigation(routing_id_));
}

void RenderFrameImpl::DidChangeSelection(bool is_empty_selection) {
  if (!GetRenderWidget()->input_handler().handling_input_event() &&
      !handling_select_range_)
    return;

  if (is_empty_selection)
    selection_text_.clear();

  // UpdateTextInputState should be called before SyncSelectionIfRequired.
  // UpdateTextInputState may send TextInputStateChanged to notify the focus
  // was changed, and SyncSelectionIfRequired may send SelectionChanged
  // to notify the selection was changed.  Focus change should be notified
  // before selection change.
  GetRenderWidget()->UpdateTextInputState();
  SyncSelectionIfRequired();
}

bool RenderFrameImpl::HandleCurrentKeyboardEvent() {
  bool did_execute_command = false;
  for (auto command : GetRenderWidget()->edit_commands()) {
    // In gtk and cocoa, it's possible to bind multiple edit commands to one
    // key (but it's the exception). Once one edit command is not executed, it
    // seems safest to not execute the rest.
    if (!frame_->ExecuteCommand(blink::WebString::FromUTF8(command.name),
                                blink::WebString::FromUTF8(command.value)))
      break;
    did_execute_command = true;
  }

  return did_execute_command;
}

blink::WebColorChooser* RenderFrameImpl::CreateColorChooser(
    blink::WebColorChooserClient* client,
    const blink::WebColor& initial_color,
    const blink::WebVector<blink::WebColorSuggestion>& suggestions) {
  RendererWebColorChooserImpl* color_chooser =
      new RendererWebColorChooserImpl(this, client);
  std::vector<ColorSuggestion> color_suggestions;
  for (size_t i = 0; i < suggestions.size(); i++) {
    color_suggestions.push_back(
        ColorSuggestion(suggestions[i].color, suggestions[i].label.Utf16()));
  }
  color_chooser->Open(static_cast<SkColor>(initial_color), color_suggestions);
  return color_chooser;
}

void RenderFrameImpl::RunModalAlertDialog(const blink::WebString& message) {
  RunJavaScriptDialog(JAVASCRIPT_DIALOG_TYPE_ALERT, message.Utf16(),
                      base::string16(), frame_->GetDocument().Url(), NULL);
}

bool RenderFrameImpl::RunModalConfirmDialog(const blink::WebString& message) {
  return RunJavaScriptDialog(JAVASCRIPT_DIALOG_TYPE_CONFIRM, message.Utf16(),
                             base::string16(), frame_->GetDocument().Url(),
                             NULL);
}

bool RenderFrameImpl::RunModalPromptDialog(
    const blink::WebString& message,
    const blink::WebString& default_value,
    blink::WebString* actual_value) {
  base::string16 result;
  bool ok = RunJavaScriptDialog(JAVASCRIPT_DIALOG_TYPE_PROMPT, message.Utf16(),
                                default_value.Utf16(),
                                frame_->GetDocument().Url(), &result);
  if (ok)
    actual_value->Assign(WebString::FromUTF16(result));
  return ok;
}

bool RenderFrameImpl::RunModalBeforeUnloadDialog(bool is_reload) {
  // Don't allow further dialogs if we are waiting to swap out, since the
  // ScopedPageLoadDeferrer in our stack prevents it.
  if (suppress_further_dialogs_)
    return false;

  bool success = false;
  // This is an ignored return value, but is included so we can accept the same
  // response as RunJavaScriptDialog.
  base::string16 ignored_result;
  Send(new FrameHostMsg_RunBeforeUnloadConfirm(
      routing_id_, frame_->GetDocument().Url(), is_reload, &success,
      &ignored_result));
  return success;
}

bool RenderFrameImpl::RunFileChooser(
    const blink::WebFileChooserParams& params,
    blink::WebFileChooserCompletion* chooser_completion) {
  FileChooserParams ipc_params;
  if (params.directory)
    ipc_params.mode = FileChooserParams::UploadFolder;
  else if (params.multi_select)
    ipc_params.mode = FileChooserParams::OpenMultiple;
  else if (params.save_as)
    ipc_params.mode = FileChooserParams::Save;
  else
    ipc_params.mode = FileChooserParams::Open;
  ipc_params.title = params.title.Utf16();
  ipc_params.accept_types.reserve(params.accept_types.size());
  for (const auto& type : params.accept_types)
    ipc_params.accept_types.push_back(type.Utf16());
  ipc_params.need_local_path = params.need_local_path;
#if defined(OS_ANDROID)
  ipc_params.capture = params.use_media_capture;
#endif
  ipc_params.requestor = params.requestor;

  return ScheduleFileChooser(ipc_params, chooser_completion);
}

void RenderFrameImpl::ShowContextMenu(const blink::WebContextMenuData& data) {
  ContextMenuParams params = ContextMenuParamsBuilder::Build(data);
  blink::WebRect position_in_window(params.x, params.y, 0, 0);
  GetRenderWidget()->ConvertViewportToWindow(&position_in_window);
  params.x = position_in_window.x;
  params.y = position_in_window.y;
  params.source_type =
      GetRenderWidget()->input_handler().context_menu_source_type();
  GetRenderWidget()->OnShowHostContextMenu(&params);
  if (GetRenderWidget()->has_host_context_menu_location()) {
    params.x = GetRenderWidget()->host_context_menu_location().x();
    params.y = GetRenderWidget()->host_context_menu_location().y();
  }

  // Serializing a GURL longer than kMaxURLChars will fail, so don't do
  // it.  We replace it with an empty GURL so the appropriate items are disabled
  // in the context menu.
  // TODO(jcivelli): http://crbug.com/45160 This prevents us from saving large
  //                 data encoded images.  We should have a way to save them.
  if (params.src_url.spec().size() > url::kMaxURLChars)
    params.src_url = GURL();

#if defined(OS_ANDROID)
  gfx::Rect start_rect;
  gfx::Rect end_rect;
  GetRenderWidget()->GetSelectionBounds(&start_rect, &end_rect);
  params.selection_start = gfx::Point(start_rect.x(), start_rect.bottom());
  params.selection_end = gfx::Point(end_rect.right(), end_rect.bottom());
#endif

  Send(new FrameHostMsg_ContextMenu(routing_id_, params));
}

void RenderFrameImpl::SaveImageFromDataURL(const blink::WebString& data_url) {
  // Note: We should basically send GURL but we use size-limited string instead
  // in order to send a larger data url to save a image for <canvas> or <img>.
  if (data_url.length() < kMaxLengthOfDataURLString) {
    Send(new FrameHostMsg_SaveImageFromDataURL(render_view_->GetRoutingID(),
                                               routing_id_, data_url.Utf8()));
  }
}

void RenderFrameImpl::WillSendRequest(blink::WebLocalFrame* frame,
                                      blink::WebURLRequest& request) {
  DCHECK_EQ(frame_, frame);

  // Set the first party for cookies url if it has not been set yet (new
  // requests). This value will be updated during redirects, consistent with
  // https://tools.ietf.org/html/draft-west-first-party-cookies-04#section-2.1.1
  if (request.FirstPartyForCookies().IsEmpty()) {
    if (request.GetFrameType() == blink::WebURLRequest::kFrameTypeTopLevel)
      request.SetFirstPartyForCookies(request.Url());
    else
      request.SetFirstPartyForCookies(
          frame->GetDocument().FirstPartyForCookies());
  }

  // Set the requestor origin to the same origin as the frame's document if it
  // hasn't yet been set.
  //
  // TODO(mkwst): It would be cleaner to adjust blink::ResourceRequest to
  // initialize itself with a `nullptr` initiator so that this can be a simple
  // `isNull()` check. https://crbug.com/625969
  WebDocument frame_document = frame->GetDocument();
  if (request.RequestorOrigin().IsUnique() &&
      !frame_document.GetSecurityOrigin().IsUnique()) {
    request.SetRequestorOrigin(frame_document.GetSecurityOrigin());
  }

  WebDataSource* provisional_data_source = frame->ProvisionalDataSource();
  WebDataSource* data_source =
      provisional_data_source ? provisional_data_source : frame->DataSource();

  DocumentState* document_state = DocumentState::FromDataSource(data_source);
  DCHECK(document_state);
  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDocumentState(document_state);
  NavigationStateImpl* navigation_state =
      static_cast<NavigationStateImpl*>(document_state->navigation_state());
  ui::PageTransition transition_type = navigation_state->GetTransitionType();
  if (provisional_data_source && provisional_data_source->IsClientRedirect()) {
    transition_type = ui::PageTransitionFromInt(
        transition_type | ui::PAGE_TRANSITION_CLIENT_REDIRECT);
  }

  GURL new_url;
  if (GetContentClient()->renderer()->WillSendRequest(
          frame, transition_type, request.Url(), &new_url)) {
    request.SetURL(WebURL(new_url));
  }

  if (internal_data->is_cache_policy_override_set())
    request.SetCachePolicy(internal_data->cache_policy_override());

  // The request's extra data may indicate that we should set a custom user
  // agent. This needs to be done here, after WebKit is through with setting the
  // user agent on its own. Similarly, it may indicate that we should set an
  // X-Requested-With header. This must be done here to avoid breaking CORS
  // checks.
  // PlzNavigate: there may also be a stream url associated with the request.
  WebString custom_user_agent;
  WebString requested_with;
  std::unique_ptr<StreamOverrideParameters> stream_override;
  if (request.GetExtraData()) {
    RequestExtraData* old_extra_data =
        static_cast<RequestExtraData*>(request.GetExtraData());

    custom_user_agent = old_extra_data->custom_user_agent();
    if (!custom_user_agent.IsNull()) {
      if (custom_user_agent.IsEmpty())
        request.ClearHTTPHeaderField("User-Agent");
      else
        request.SetHTTPHeaderField("User-Agent", custom_user_agent);
    }

    requested_with = old_extra_data->requested_with();
    if (!requested_with.IsNull()) {
      if (requested_with.IsEmpty())
        request.ClearHTTPHeaderField("X-Requested-With");
      else
        request.SetHTTPHeaderField("X-Requested-With", requested_with);
    }
    stream_override = old_extra_data->TakeStreamOverrideOwnership();
  }

  // Add an empty HTTP origin header for non GET methods if none is currently
  // present.
  request.AddHTTPOriginIfNeeded(WebSecurityOrigin::CreateUnique());

  // Attach |should_replace_current_entry| state to requests so that, should
  // this navigation later require a request transfer, all state is preserved
  // when it is re-created in the new process.
  bool should_replace_current_entry = data_source->ReplacesCurrentHistoryItem();

  WebFrame* parent = frame->Parent();
  int parent_routing_id = parent ? GetRoutingIdForFrameOrProxy(parent) : -1;

  RequestExtraData* extra_data =
      static_cast<RequestExtraData*>(request.GetExtraData());
  if (!extra_data)
    extra_data = new RequestExtraData();
  extra_data->set_visibility_state(VisibilityState());
  extra_data->set_custom_user_agent(custom_user_agent);
  extra_data->set_requested_with(requested_with);
  extra_data->set_render_frame_id(routing_id_);
  extra_data->set_is_main_frame(!parent);
  extra_data->set_frame_origin(url::Origin(frame_document.GetSecurityOrigin()));
  extra_data->set_parent_is_main_frame(parent && !parent->Parent());
  extra_data->set_parent_render_frame_id(parent_routing_id);
  extra_data->set_allow_download(
      navigation_state->common_params().allow_download);
  extra_data->set_transition_type(transition_type);
  extra_data->set_should_replace_current_entry(should_replace_current_entry);
  extra_data->set_stream_override(std::move(stream_override));
  bool is_prefetch =
      GetContentClient()->renderer()->IsPrefetchOnly(this, request);
  extra_data->set_is_prefetch(is_prefetch);
  extra_data->set_download_to_network_cache_only(
      is_prefetch &&
      WebURLRequestToResourceType(request) != RESOURCE_TYPE_MAIN_FRAME);
  extra_data->set_initiated_in_secure_context(frame_document.IsSecureContext());

  // Renderer process transfers apply only to navigational requests.
  bool is_navigational_request =
      request.GetFrameType() != WebURLRequest::kFrameTypeNone;
  if (is_navigational_request) {
    extra_data->set_transferred_request_child_id(
        navigation_state->start_params().transferred_request_child_id);
    extra_data->set_transferred_request_request_id(
        navigation_state->start_params().transferred_request_request_id);

    // For navigation requests, we should copy the flag which indicates if this
    // was a navigation initiated by the renderer to the new RequestExtraData
    // instance.
    RequestExtraData* current_request_data =
        static_cast<RequestExtraData*>(request.GetExtraData());
    if (current_request_data) {
      extra_data->set_navigation_initiated_by_renderer(
          current_request_data->navigation_initiated_by_renderer());
    }
  }

  request.SetExtraData(extra_data);

  if (request.GetPreviewsState() == WebURLRequest::kPreviewsUnspecified) {
    if (is_main_frame_ && !navigation_state->request_committed()) {
      request.SetPreviewsState(static_cast<WebURLRequest::PreviewsState>(
          navigation_state->common_params().previews_state));
    } else {
      request.SetPreviewsState(
          previews_state_ == PREVIEWS_UNSPECIFIED
              ? WebURLRequest::kPreviewsOff
              : static_cast<WebURLRequest::PreviewsState>(previews_state_));
    }
  }

  // This is an instance where we embed a copy of the routing id
  // into the data portion of the message. This can cause problems if we
  // don't register this id on the browser side, since the download manager
  // expects to find a RenderViewHost based off the id.
  request.SetRequestorID(render_view_->GetRoutingID());
  request.SetHasUserGesture(WebUserGestureIndicator::IsProcessingUserGesture());

  // StartNavigationParams should only apply to navigational requests (and not
  // to subresource requests).  For example - Content-Type header provided via
  // OpenURLParams::extra_headers should only be applied to the original POST
  // navigation request (and not to subresource requests).
  if (is_navigational_request &&
      !navigation_state->start_params().extra_headers.empty()) {
    for (net::HttpUtil::HeadersIterator i(
             navigation_state->start_params().extra_headers.begin(),
             navigation_state->start_params().extra_headers.end(), "\n");
         i.GetNext();) {
      if (base::LowerCaseEqualsASCII(i.name(), "referer")) {
        WebString referrer = WebSecurityPolicy::GenerateReferrerHeader(
            blink::kWebReferrerPolicyDefault, request.Url(),
            WebString::FromUTF8(i.values()));
        request.SetHTTPReferrer(referrer, blink::kWebReferrerPolicyDefault);
      } else {
        request.SetHTTPHeaderField(WebString::FromUTF8(i.name()),
                                   WebString::FromUTF8(i.values()));
      }
    }
  }

  if (!render_view_->renderer_preferences_.enable_referrers)
    request.SetHTTPReferrer(WebString(), blink::kWebReferrerPolicyDefault);
}

void RenderFrameImpl::DidReceiveResponse(
    const blink::WebURLResponse& response) {
  // Only do this for responses that correspond to a provisional data source
  // of the top-most frame.  If we have a provisional data source, then we
  // can't have any sub-resources yet, so we know that this response must
  // correspond to a frame load.
  if (!frame_->ProvisionalDataSource() || frame_->Parent())
    return;

  // If we are in view source mode, then just let the user see the source of
  // the server's error page.
  if (frame_->IsViewSourceModeEnabled())
    return;

  DocumentState* document_state =
      DocumentState::FromDataSource(frame_->ProvisionalDataSource());
  int http_status_code = response.HttpStatusCode();

  // Record page load flags.
  WebURLResponseExtraDataImpl* extra_data = GetExtraDataFromResponse(response);
  if (extra_data) {
    document_state->set_was_fetched_via_spdy(
        extra_data->was_fetched_via_spdy());
    document_state->set_was_alpn_negotiated(extra_data->was_alpn_negotiated());
    document_state->set_alpn_negotiated_protocol(
        extra_data->alpn_negotiated_protocol());
    document_state->set_was_alternate_protocol_available(
        extra_data->was_alternate_protocol_available());
    document_state->set_connection_info(
        extra_data->connection_info());
  }
  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDocumentState(document_state);
  internal_data->set_http_status_code(http_status_code);
}

void RenderFrameImpl::DidLoadResourceFromMemoryCache(
    const blink::WebURLRequest& request,
    const blink::WebURLResponse& response) {
  // The recipients of this message have no use for data: URLs: they don't
  // affect the page's insecure content list and are not in the disk cache. To
  // prevent large (1M+) data: URLs from crashing in the IPC system, we simply
  // filter them out here.
  if (request.Url().ProtocolIs(url::kDataScheme))
    return;

  // Let the browser know we loaded a resource from the memory cache.  This
  // message is needed to display the correct SSL indicators.
  Send(new FrameHostMsg_DidLoadResourceFromMemoryCache(
      routing_id_, request.Url(), request.HttpMethod().Utf8(),
      response.MimeType().Utf8(), WebURLRequestToResourceType(request)));
}

void RenderFrameImpl::DidDisplayInsecureContent() {
  Send(new FrameHostMsg_DidDisplayInsecureContent(routing_id_));
}

void RenderFrameImpl::DidContainInsecureFormAction() {
  Send(new FrameHostMsg_DidContainInsecureFormAction(routing_id_));
}

void RenderFrameImpl::DidRunInsecureContent(
    const blink::WebSecurityOrigin& origin,
    const blink::WebURL& target) {
  Send(new FrameHostMsg_DidRunInsecureContent(
      routing_id_, GURL(origin.ToString().Utf8()), target));
  GetContentClient()->renderer()->RecordRapporURL(
      "ContentSettings.MixedScript.RanMixedScript",
      GURL(origin.ToString().Utf8()));
}

void RenderFrameImpl::DidDisplayContentWithCertificateErrors(
    const blink::WebURL& url) {
  Send(new FrameHostMsg_DidDisplayContentWithCertificateErrors(
      routing_id_, url));
}

void RenderFrameImpl::DidRunContentWithCertificateErrors(
    const blink::WebURL& url) {
  Send(new FrameHostMsg_DidRunContentWithCertificateErrors(routing_id_, url));
}

void RenderFrameImpl::DidChangePerformanceTiming() {
  for (auto& observer : observers_)
    observer.DidChangePerformanceTiming();
}

void RenderFrameImpl::DidObserveLoadingBehavior(
    blink::WebLoadingBehaviorFlag behavior) {
  for (auto& observer : observers_)
    observer.DidObserveLoadingBehavior(behavior);
}

void RenderFrameImpl::DidCreateScriptContext(blink::WebLocalFrame* frame,
                                             v8::Local<v8::Context> context,
                                             int world_id) {
  DCHECK_EQ(frame_, frame);

  for (auto& observer : observers_)
    observer.DidCreateScriptContext(context, world_id);
}

void RenderFrameImpl::WillReleaseScriptContext(blink::WebLocalFrame* frame,
                                               v8::Local<v8::Context> context,
                                               int world_id) {
  DCHECK_EQ(frame_, frame);

  for (auto& observer : observers_)
    observer.WillReleaseScriptContext(context, world_id);
}

void RenderFrameImpl::DidChangeScrollOffset(blink::WebLocalFrame* frame) {
  DCHECK_EQ(frame_, frame);
  render_view_->StartNavStateSyncTimerIfNecessary(this);

  for (auto& observer : observers_)
    observer.DidChangeScrollOffset();
}

void RenderFrameImpl::WillInsertBody(blink::WebLocalFrame* frame) {
  DCHECK(!frame_ || frame_ == frame);
  Send(new FrameHostMsg_WillInsertBody(routing_id_,
                                       render_view_->GetRoutingID()));
}

void RenderFrameImpl::ReportFindInPageMatchCount(int request_id,
                                                 int count,
                                                 bool final_update) {
  // -1 here means don't update the active match ordinal.
  int active_match_ordinal = count ? -1 : 0;

  SendFindReply(request_id, count, active_match_ordinal, gfx::Rect(),
                final_update);
}

void RenderFrameImpl::ReportFindInPageSelection(
    int request_id,
    int active_match_ordinal,
    const blink::WebRect& selection_rect) {
  SendFindReply(request_id, -1 /* match_count */, active_match_ordinal,
                selection_rect, false /* final_status_update */);
}

void RenderFrameImpl::RequestStorageQuota(
    blink::WebStorageQuotaType type,
    unsigned long long requested_size,
    blink::WebStorageQuotaCallbacks callbacks) {
  WebSecurityOrigin origin = frame_->GetDocument().GetSecurityOrigin();
  if (origin.IsUnique()) {
    // Unique origins cannot store persistent state.
    callbacks.DidFail(blink::kWebStorageQuotaErrorAbort);
    return;
  }
  ChildThreadImpl::current()->quota_dispatcher()->RequestStorageQuota(
      routing_id_, url::Origin(origin).GetURL(),
      static_cast<storage::StorageType>(type), requested_size,
      QuotaDispatcher::CreateWebStorageQuotaCallbacksWrapper(callbacks));
}

blink::WebPresentationClient* RenderFrameImpl::PresentationClient() {
  if (!presentation_dispatcher_)
    presentation_dispatcher_ = new PresentationDispatcher(this);
  return presentation_dispatcher_;
}

blink::WebPushClient* RenderFrameImpl::PushClient() {
  if (!push_messaging_client_)
    push_messaging_client_ = new PushMessagingClient(this);
  return push_messaging_client_;
}

blink::WebRelatedAppsFetcher* RenderFrameImpl::GetRelatedAppsFetcher() {
  if (!related_apps_fetcher_)
    related_apps_fetcher_.reset(new RelatedAppsFetcher(manifest_manager_));

  return related_apps_fetcher_.get();
}

void RenderFrameImpl::WillStartUsingPeerConnectionHandler(
    blink::WebRTCPeerConnectionHandler* handler) {
#if BUILDFLAG(ENABLE_WEBRTC)
  static_cast<RTCPeerConnectionHandler*>(handler)->associateWithFrame(frame_);
#endif
}

blink::WebUserMediaClient* RenderFrameImpl::UserMediaClient() {
  if (!web_user_media_client_)
    InitializeUserMediaClient();
  return web_user_media_client_;
}

blink::WebEncryptedMediaClient* RenderFrameImpl::EncryptedMediaClient() {
  if (!web_encrypted_media_client_) {
    web_encrypted_media_client_.reset(new media::WebEncryptedMediaClientImpl(
        // base::Unretained(this) is safe because WebEncryptedMediaClientImpl
        // is destructed before |this|, and does not give away ownership of the
        // callback.
        base::Bind(&RenderFrameImpl::AreSecureCodecsSupported,
                   base::Unretained(this)),
        GetCdmFactory(), GetMediaPermission(),
        new RenderMediaLog(url::Origin(frame_->GetSecurityOrigin()).GetURL())));
  }
  return web_encrypted_media_client_.get();
}

blink::WebString RenderFrameImpl::UserAgentOverride() {
  if (!render_view_->webview() || !render_view_->webview()->MainFrame() ||
      render_view_->renderer_preferences_.user_agent_override.empty()) {
    return blink::WebString();
  }

  // TODO(nasko): When the top-level frame is remote, there is no WebDataSource
  // associated with it, so the checks below are not valid. Temporarily
  // return early and fix properly as part of https://crbug.com/426555.
  if (render_view_->webview()->MainFrame()->IsWebRemoteFrame())
    return blink::WebString();

  // If we're in the middle of committing a load, the data source we need
  // will still be provisional.
  WebFrame* main_frame = render_view_->webview()->MainFrame();
  WebDataSource* data_source = NULL;
  if (main_frame->ProvisionalDataSource())
    data_source = main_frame->ProvisionalDataSource();
  else
    data_source = main_frame->DataSource();

  InternalDocumentStateData* internal_data = data_source ?
      InternalDocumentStateData::FromDataSource(data_source) : NULL;
  if (internal_data && internal_data->is_overriding_user_agent())
    return WebString::FromUTF8(
        render_view_->renderer_preferences_.user_agent_override);
  return blink::WebString();
}

blink::WebString RenderFrameImpl::DoNotTrackValue() {
  if (render_view_->renderer_preferences_.enable_do_not_track)
    return WebString::FromUTF8("1");
  return WebString();
}

bool RenderFrameImpl::AllowWebGL(bool default_value) {
  if (!default_value)
    return false;

  bool blocked = true;
  Send(new FrameHostMsg_Are3DAPIsBlocked(
      routing_id_, url::Origin(frame_->Top()->GetSecurityOrigin()).GetURL(),
      THREE_D_API_TYPE_WEBGL, &blocked));
  return !blocked;
}

blink::WebScreenOrientationClient*
RenderFrameImpl::GetWebScreenOrientationClient() {
  if (!screen_orientation_dispatcher_)
    screen_orientation_dispatcher_ = new ScreenOrientationDispatcher(this);
  return screen_orientation_dispatcher_;
}

void RenderFrameImpl::PostAccessibilityEvent(const blink::WebAXObject& obj,
                                             blink::WebAXEvent event) {
  HandleWebAccessibilityEvent(obj, event);
}

void RenderFrameImpl::HandleAccessibilityFindInPageResult(
    int identifier,
    int match_index,
    const blink::WebAXObject& start_object,
    int start_offset,
    const blink::WebAXObject& end_object,
    int end_offset) {
  if (render_accessibility_) {
    render_accessibility_->HandleAccessibilityFindInPageResult(
        identifier, match_index, start_object, start_offset,
        end_object, end_offset);
  }
}

void RenderFrameImpl::DidChangeManifest() {
  for (auto& observer : observers_)
    observer.DidChangeManifest();
}

void RenderFrameImpl::EnterFullscreen() {
  Send(new FrameHostMsg_ToggleFullscreen(routing_id_, true));
}

void RenderFrameImpl::ExitFullscreen() {
  Send(new FrameHostMsg_ToggleFullscreen(routing_id_, false));
}

void RenderFrameImpl::RegisterProtocolHandler(const WebString& scheme,
                                              const WebURL& url,
                                              const WebString& title) {
  bool user_gesture = WebUserGestureIndicator::IsProcessingUserGesture();
  Send(new FrameHostMsg_RegisterProtocolHandler(routing_id_, scheme.Utf8(), url,
                                                title.Utf16(), user_gesture));
}

void RenderFrameImpl::UnregisterProtocolHandler(const WebString& scheme,
                                                const WebURL& url) {
  bool user_gesture = WebUserGestureIndicator::IsProcessingUserGesture();
  Send(new FrameHostMsg_UnregisterProtocolHandler(routing_id_, scheme.Utf8(),
                                                  url, user_gesture));
}

void RenderFrameImpl::DidSerializeDataForFrame(
    const WebCString& data,
    WebFrameSerializerClient::FrameSerializationStatus status) {
  bool end_of_data =
      status == WebFrameSerializerClient::kCurrentFrameIsFinished;
  Send(new FrameHostMsg_SerializedHtmlWithLocalLinksResponse(
      routing_id_, data, end_of_data));
}

void RenderFrameImpl::AddObserver(RenderFrameObserver* observer) {
  observers_.AddObserver(observer);
}

void RenderFrameImpl::RemoveObserver(RenderFrameObserver* observer) {
  observer->RenderFrameGone();
  observers_.RemoveObserver(observer);
}

void RenderFrameImpl::OnStop() {
  DCHECK(frame_);

  // The stopLoading call may run script, which may cause this frame to be
  // detached/deleted.  If that happens, return immediately.
  base::WeakPtr<RenderFrameImpl> weak_this = weak_factory_.GetWeakPtr();
  frame_->StopLoading();
  if (!weak_this)
    return;

  if (frame_ && !frame_->Parent()) {
    for (auto& observer : render_view_->observers_)
      observer.OnStop();
  }

  for (auto& observer : observers_)
    observer.OnStop();
}

void RenderFrameImpl::WasHidden() {
  for (auto& observer : observers_)
    observer.WasHidden();

#if BUILDFLAG(ENABLE_PLUGINS)
  for (auto* plugin : active_pepper_instances_)
    plugin->PageVisibilityChanged(false);
#endif  // ENABLE_PLUGINS

  if (GetWebFrame()->FrameWidget()) {
    GetWebFrame()->FrameWidget()->SetVisibilityState(VisibilityState());
  }
}

void RenderFrameImpl::WasShown() {
  for (auto& observer : observers_)
    observer.WasShown();

#if BUILDFLAG(ENABLE_PLUGINS)
  for (auto* plugin : active_pepper_instances_)
    plugin->PageVisibilityChanged(true);
#endif  // ENABLE_PLUGINS

  if (GetWebFrame()->FrameWidget()) {
    GetWebFrame()->FrameWidget()->SetVisibilityState(VisibilityState());
  }
}

void RenderFrameImpl::WidgetWillClose() {
  for (auto& observer : observers_)
    observer.WidgetWillClose();
}

bool RenderFrameImpl::IsMainFrame() {
  return is_main_frame_;
}

bool RenderFrameImpl::IsHidden() {
  return GetRenderWidget()->is_hidden();
}

bool RenderFrameImpl::IsLocalRoot() const {
  bool is_local_root = static_cast<bool>(render_widget_);
  DCHECK_EQ(is_local_root,
            !(frame_->Parent() && frame_->Parent()->IsWebLocalFrame()));
  return is_local_root;
}

const RenderFrameImpl* RenderFrameImpl::GetLocalRoot() const {
  return IsLocalRoot() ? this
                       : RenderFrameImpl::FromWebFrame(frame_->LocalRoot());
}

// Tell the embedding application that the URL of the active page has changed.
void RenderFrameImpl::SendDidCommitProvisionalLoad(
    blink::WebFrame* frame,
    blink::WebHistoryCommitType commit_type) {
  DCHECK_EQ(frame_, frame);
  WebDataSource* ds = frame->DataSource();
  DCHECK(ds);

  const WebURLRequest& request = ds->GetRequest();
  const WebURLResponse& response = ds->GetResponse();

  DocumentState* document_state = DocumentState::FromDataSource(ds);
  NavigationStateImpl* navigation_state =
      static_cast<NavigationStateImpl*>(document_state->navigation_state());
  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDocumentState(document_state);

  // Set the correct engagement level on the frame, and wipe the cached origin
  // so this will not be reused accidentally.
  if (url::Origin(frame_->GetSecurityOrigin()) == engagement_level_.first) {
    frame_->SetEngagementLevel(engagement_level_.second);
    engagement_level_.first = url::Origin();
  }

  FrameHostMsg_DidCommitProvisionalLoad_Params params;
  params.http_status_code = response.HttpStatusCode();
  params.url_is_unreachable = ds->HasUnreachableURL();
  params.method = "GET";
  params.intended_as_new_entry =
      navigation_state->request_params().intended_as_new_entry;
  params.did_create_new_entry = commit_type == blink::kWebStandardCommit;
  params.should_replace_current_entry = ds->ReplacesCurrentHistoryItem();
  params.post_id = -1;
  params.nav_entry_id = navigation_state->request_params().nav_entry_id;
  // We need to track the RenderViewHost routing_id because of downstream
  // dependencies (crbug.com/392171 DownloadRequestHandle, SaveFileManager,
  // ResourceDispatcherHostImpl, MediaStreamUIProxy,
  // SpeechRecognitionDispatcherHost and possibly others). They look up the view
  // based on the ID stored in the resource requests. Once those dependencies
  // are unwound or moved to RenderFrameHost (crbug.com/304341) we can move the
  // client to be based on the routing_id of the RenderFrameHost.
  params.render_view_routing_id = render_view_->routing_id();
  params.socket_address.set_host(response.RemoteIPAddress().Utf8());
  params.socket_address.set_port(response.RemotePort());
  params.was_within_same_document = navigation_state->WasWithinSameDocument();

  WebDocument frame_document = frame->GetDocument();
  // Set the origin of the frame.  This will be replicated to the corresponding
  // RenderFrameProxies in other processes.
  WebSecurityOrigin frame_origin = frame_document.GetSecurityOrigin();
  params.origin = frame_origin;

  params.insecure_request_policy = frame->GetInsecureRequestPolicy();

  params.has_potentially_trustworthy_unique_origin =
      frame_origin.IsUnique() && frame_origin.IsPotentiallyTrustworthy();

  // Set the URL to be displayed in the browser UI to the user.
  params.url = GetLoadingUrl();
  if (GURL(frame_document.BaseURL()) != params.url)
    params.base_url = frame_document.BaseURL();

  GetRedirectChain(ds, &params.redirects);
  params.should_update_history =
      !ds->HasUnreachableURL() && response.HttpStatusCode() != 404;

  params.searchable_form_url = internal_data->searchable_form_url();
  params.searchable_form_encoding = internal_data->searchable_form_encoding();

  params.gesture = render_view_->navigation_gesture_;
  render_view_->navigation_gesture_ = NavigationGestureUnknown;

  // Make navigation state a part of the DidCommitProvisionalLoad message so
  // that committed entry has it at all times.  Send a single HistoryItem for
  // this frame, rather than the whole tree.  It will be stored in the
  // corresponding FrameNavigationEntry.
  params.page_state = SingleHistoryItemToPageState(current_history_item_);

  params.content_source_id = GetRenderWidget()->GetContentSourceId();

  params.method = request.HttpMethod().Latin1();
  if (params.method == "POST")
    params.post_id = ExtractPostId(current_history_item_);

  params.frame_unique_name = current_history_item_.Target().Utf8();
  params.item_sequence_number = current_history_item_.ItemSequenceNumber();
  params.document_sequence_number =
      current_history_item_.DocumentSequenceNumber();

  // If the page contained a client redirect (meta refresh, document.loc...),
  // set the referrer appropriately.
  if (ds->IsClientRedirect()) {
    params.referrer =
        Referrer(params.redirects[0], ds->GetRequest().GetReferrerPolicy());
  } else {
    params.referrer =
        RenderViewImpl::GetReferrerFromRequest(frame, ds->GetRequest());
  }

  if (!frame->Parent()) {
    // Top-level navigation.

    // Reset the zoom limits in case a plugin had changed them previously. This
    // will also call us back which will cause us to send a message to
    // update WebContentsImpl.
    render_view_->webview()->ZoomLimitsChanged(
        ZoomFactorToZoomLevel(kMinimumZoomFactor),
        ZoomFactorToZoomLevel(kMaximumZoomFactor));

    // Set zoom level, but don't do it for full-page plugin since they don't use
    // the same zoom settings.
    HostZoomLevels::iterator host_zoom =
        host_zoom_levels_.find(GURL(request.Url()));
    if (render_view_->webview()->MainFrame()->IsWebLocalFrame() &&
        render_view_->webview()
            ->MainFrame()
            ->GetDocument()
            .IsPluginDocument()) {
      // Reset the zoom levels for plugins.
      render_view_->SetZoomLevel(0);
    } else {
      // If the zoom level is not found, then do nothing. In-page navigation
      // relies on not changing the zoom level in this case.
      if (host_zoom != host_zoom_levels_.end())
        render_view_->SetZoomLevel(host_zoom->second);
    }

    if (host_zoom != host_zoom_levels_.end()) {
      // This zoom level was merely recorded transiently for this load.  We can
      // erase it now.  If at some point we reload this page, the browser will
      // send us a new, up-to-date zoom level.
      host_zoom_levels_.erase(host_zoom);
    }

    // Update contents MIME type for main frame.
    params.contents_mime_type = ds->GetResponse().MimeType().Utf8();

    params.transition = navigation_state->GetTransitionType();
    if (!ui::PageTransitionIsMainFrame(params.transition)) {
      // If the main frame does a load, it should not be reported as a subframe
      // navigation.  This can occur in the following case:
      // 1. You're on a site with frames.
      // 2. You do a subframe navigation.  This is stored with transition type
      //    MANUAL_SUBFRAME.
      // 3. You navigate to some non-frame site, say, google.com.
      // 4. You navigate back to the page from step 2.  Since it was initially
      //    MANUAL_SUBFRAME, it will be that same transition type here.
      // We don't want that, because any navigation that changes the toplevel
      // frame should be tracked as a toplevel navigation (this allows us to
      // update the URL bar, etc).
      params.transition = ui::PAGE_TRANSITION_LINK;
    }

    // If the page contained a client redirect (meta refresh, document.loc...),
    // set the transition appropriately.
    if (ds->IsClientRedirect()) {
      params.transition = ui::PageTransitionFromInt(
          params.transition | ui::PAGE_TRANSITION_CLIENT_REDIRECT);
    }

    // Send the user agent override back.
    params.is_overriding_user_agent = internal_data->is_overriding_user_agent();

    // Track the URL of the original request.  We use the first entry of the
    // redirect chain if it exists because the chain may have started in another
    // process.
    params.original_request_url = GetOriginalRequestURL(ds);

    params.history_list_was_cleared =
        navigation_state->request_params().should_clear_history_list;

    params.report_type = static_cast<FrameMsg_UILoadMetricsReportType::Value>(
        frame->DataSource()->GetRequest().InputPerfMetricReportPolicy());
    params.ui_timestamp = base::TimeTicks() +
                          base::TimeDelta::FromSecondsD(
                              frame->DataSource()->GetRequest().UiStartTime());
  } else {
    // Subframe navigation: the type depends on whether this navigation
    // generated a new session history entry. When they do generate a session
    // history entry, it means the user initiated the navigation and we should
    // mark it as such.
    if (commit_type == blink::kWebStandardCommit)
      params.transition = ui::PAGE_TRANSITION_MANUAL_SUBFRAME;
    else
      params.transition = ui::PAGE_TRANSITION_AUTO_SUBFRAME;

    DCHECK(!navigation_state->request_params().should_clear_history_list);
    params.history_list_was_cleared = false;
    params.report_type = FrameMsg_UILoadMetricsReportType::NO_REPORT;
    // Subframes should match the zoom level of the main frame.
    render_view_->SetZoomLevel(render_view_->page_zoom_level());
  }

  // Standard URLs must match the reported origin, when it is not unique.
  // This check is very similar to RenderFrameHostImpl::CanCommitOrigin, but
  // adapted to the renderer process side.
  if (!params.origin.unique() && params.url.IsStandard() &&
      render_view_->GetWebkitPreferences().web_security_enabled) {
    // Exclude file: URLs when settings allow them access any origin.
    if (params.origin.scheme() != url::kFileScheme ||
        !render_view_->GetWebkitPreferences()
             .allow_universal_access_from_file_urls) {
      base::debug::SetCrashKeyValue("origin_mismatch_url", params.url.spec());
      base::debug::SetCrashKeyValue("origin_mismatch_origin",
                                    params.origin.Serialize());
      base::debug::SetCrashKeyValue("origin_mismatch_transition",
                                    base::IntToString(params.transition));
      base::debug::SetCrashKeyValue("origin_mismatch_redirects",
                                    base::IntToString(params.redirects.size()));
      base::debug::SetCrashKeyValue(
          "origin_mismatch_same_page",
          base::IntToString(params.was_within_same_document));
      CHECK(params.origin.IsSamePhysicalOriginWith(url::Origin(params.url)))
          << " url:" << params.url << " origin:" << params.origin;
    }
  }

  // This message needs to be sent before any of allowScripts(),
  // allowImages(), allowPlugins() is called for the new page, so that when
  // these functions send a ViewHostMsg_ContentBlocked message, it arrives
  // after the FrameHostMsg_DidCommitProvisionalLoad message.
  Send(new FrameHostMsg_DidCommitProvisionalLoad(routing_id_, params));

  // If we end up reusing this WebRequest (for example, due to a #ref click),
  // we don't want the transition type to persist.  Just clear it.
  navigation_state->set_transition_type(ui::PAGE_TRANSITION_LINK);
}

bool RenderFrameImpl::SwapIn() {
  CHECK_NE(proxy_routing_id_, MSG_ROUTING_NONE);
  CHECK(!in_frame_tree_);

  // The proxy should always exist.  If it was detached while the provisional
  // LocalFrame was being navigated, the provisional frame would've been
  // cleaned up by RenderFrameProxy::frameDetached.  See
  // https://crbug.com/526304 and https://crbug.com/568676 for context.
  RenderFrameProxy* proxy = RenderFrameProxy::FromRoutingID(proxy_routing_id_);
  CHECK(proxy);

  unique_name_helper_.set_propagated_name(proxy->unique_name());

  // Note: Calling swap() will detach and delete |proxy|, so do not reference it
  // after this.
  if (!proxy->web_frame()->Swap(frame_))
    return false;

  proxy_routing_id_ = MSG_ROUTING_NONE;
  in_frame_tree_ = true;

  // If this is the main frame going from a remote frame to a local frame,
  // it needs to set RenderViewImpl's pointer for the main frame to itself
  // and ensure RenderWidget is no longer in swapped out mode.
  if (is_main_frame_) {
    CHECK(!render_view_->main_render_frame_);
    render_view_->main_render_frame_ = this;
    if (render_view_->is_swapped_out())
      render_view_->SetSwappedOut(false);
  }

  return true;
}

void RenderFrameImpl::DidStartLoading(bool to_different_document) {
  TRACE_EVENT1("navigation,rail", "RenderFrameImpl::didStartLoading",
               "id", routing_id_);
  render_view_->FrameDidStartLoading(frame_);

  // PlzNavigate: the browser is responsible for knowing the start of all
  // non-synchronous navigations.
  if (!IsBrowserSideNavigationEnabled() || !to_different_document)
    Send(new FrameHostMsg_DidStartLoading(routing_id_, to_different_document));
}

void RenderFrameImpl::DidStopLoading() {
  TRACE_EVENT1("navigation,rail", "RenderFrameImpl::didStopLoading",
               "id", routing_id_);

  // Any subframes created after this point won't be considered part of the
  // current history navigation (if this was one), so we don't need to track
  // this state anymore.
  history_subframe_unique_names_.clear();

  render_view_->FrameDidStopLoading(frame_);
  Send(new FrameHostMsg_DidStopLoading(routing_id_));
}

void RenderFrameImpl::DidChangeLoadProgress(double load_progress) {
  Send(new FrameHostMsg_DidChangeLoadProgress(routing_id_, load_progress));
}

void RenderFrameImpl::HandleWebAccessibilityEvent(
    const blink::WebAXObject& obj, blink::WebAXEvent event) {
  if (render_accessibility_)
    render_accessibility_->HandleWebAccessibilityEvent(obj, event);
}

void RenderFrameImpl::FocusedNodeChanged(const WebNode& node) {
  bool is_editable = false;
  gfx::Rect node_bounds;
  if (!node.IsNull() && node.IsElementNode()) {
    WebElement element = const_cast<WebNode&>(node).To<WebElement>();
    blink::WebRect rect = element.BoundsInViewport();
    GetRenderWidget()->ConvertViewportToWindow(&rect);
    is_editable = element.IsEditable();
    node_bounds = gfx::Rect(rect);
  }
  Send(new FrameHostMsg_FocusedNodeChanged(routing_id_, is_editable,
                                           node_bounds));
  // Ensures that further text input state can be sent even when previously
  // focused input and the newly focused input share the exact same state.
  GetRenderWidget()->ClearTextInputState();

  for (auto& observer : observers_)
    observer.FocusedNodeChanged(node);
}

void RenderFrameImpl::FocusedNodeChangedForAccessibility(const WebNode& node) {
  if (render_accessibility())
    render_accessibility()->AccessibilityFocusedNodeChanged(node);
}

// PlzNavigate
void RenderFrameImpl::OnCommitNavigation(
    const ResourceResponseHead& response,
    const GURL& stream_url,
    mojo::DataPipeConsumerHandle handle,
    const CommonNavigationParams& common_params,
    const RequestNavigationParams& request_params) {
  CHECK(IsBrowserSideNavigationEnabled());
  // This will override the url requested by the WebURLLoader, as well as
  // provide it with the response to the request.
  std::unique_ptr<StreamOverrideParameters> stream_override(
      new StreamOverrideParameters());
  stream_override->stream_url = stream_url;
  stream_override->consumer_handle = mojo::ScopedDataPipeConsumerHandle(handle);
  stream_override->response = response;
  stream_override->redirects = request_params.redirects;
  stream_override->redirect_responses = request_params.redirect_response;
  stream_override->redirect_infos = request_params.redirect_infos;

  // If the request was initiated in the context of a user gesture then make
  // sure that the navigation also executes in the context of a user gesture.
  std::unique_ptr<blink::WebScopedUserGesture> gesture(
      request_params.has_user_gesture ? new blink::WebScopedUserGesture(frame_)
                                      : nullptr);

  browser_side_navigation_pending_ = false;

  NavigateInternal(common_params, StartNavigationParams(), request_params,
                   std::move(stream_override));

  // Don't add code after this since NavigateInternal may have destroyed this
  // RenderFrameImpl.
}

// PlzNavigate
void RenderFrameImpl::OnFailedNavigation(
    const CommonNavigationParams& common_params,
    const RequestNavigationParams& request_params,
    bool has_stale_copy_in_cache,
    int error_code) {
  DCHECK(IsBrowserSideNavigationEnabled());
  bool is_reload =
      FrameMsg_Navigate_Type::IsReload(common_params.navigation_type);
  RenderFrameImpl::PrepareRenderViewForNavigation(
      common_params.url, request_params);

  GetContentClient()->SetActiveURL(common_params.url);

  // If this frame isn't in the same process as the main frame, it may naively
  // assume that this is the first navigation in the iframe, but this may not
  // actually be the case. Inform the frame's state machine if this frame has
  // already committed other loads.
  if (request_params.has_committed_real_load && frame_->Parent())
    frame_->SetCommittedFirstRealLoad();

  pending_navigation_params_.reset(new NavigationParams(
      common_params, StartNavigationParams(), request_params));

  // Send the provisional load failure.
  blink::WebURLError error =
      CreateWebURLError(common_params.url, has_stale_copy_in_cache, error_code);
  WebURLRequest failed_request =
      CreateURLRequestForNavigation(common_params, request_params,
                                    std::unique_ptr<StreamOverrideParameters>(),
                                    frame_->IsViewSourceModeEnabled(),
                                    false);  // is_same_document_navigation

  if (!ShouldDisplayErrorPageForFailedLoad(error_code, common_params.url)) {
    // The browser expects this frame to be loading an error page. Inform it
    // that the load stopped.
    Send(new FrameHostMsg_DidStopLoading(routing_id_));
    browser_side_navigation_pending_ = false;
    return;
  }

  // On load failure, a frame can ask its owner to render fallback content.
  // When that happens, don't load an error page.
  WebLocalFrame::FallbackContentResult fallback_result =
      frame_->MaybeRenderFallbackContent(error);
  if (fallback_result != WebLocalFrame::NoFallbackContent) {
    if (fallback_result == WebLocalFrame::NoLoadInProgress) {
      // If the frame wasn't loading but was fallback-eligible, the fallback
      // content won't be shown. However, showing an error page isn't right
      // either, as the frame has already been populated with something
      // unrelated to this navigation failure. In that case, just send a stop
      // IPC to the browser to unwind its state, and leave the frame as-is.
      Send(new FrameHostMsg_DidStopLoading(routing_id_));
    }
    browser_side_navigation_pending_ = false;
    return;
  }

  // Make sure errors are not shown in view source mode.
  frame_->EnableViewSourceMode(false);

  // Replace the current history entry in reloads, and loads of the same url.
  // This corresponds to Blink's notion of a standard commit.
  // Also replace the current history entry if the browser asked for it
  // specifically.
  // TODO(clamy): see if initial commits in subframes should be handled
  // separately.
  bool replace = is_reload || common_params.url == GetLoadingUrl() ||
                 common_params.should_replace_current_entry;
  std::unique_ptr<HistoryEntry> history_entry;
  if (request_params.page_state.IsValid())
    history_entry = PageStateToHistoryEntry(request_params.page_state);

  // For renderer initiated navigations, we send out a didFailProvisionalLoad()
  // notification.
  bool had_provisional_data_source = frame_->ProvisionalDataSource();
  if (request_params.nav_entry_id == 0) {
    DidFailProvisionalLoad(
        frame_, error,
        replace ? blink::kWebHistoryInertCommit : blink::kWebStandardCommit);
  }

  // If we didn't call didFailProvisionalLoad or there wasn't a
  // provisionalDataSource(), LoadNavigationErrorPage wasn't called, so do it
  // now.
  if (request_params.nav_entry_id != 0 || !had_provisional_data_source) {
    LoadNavigationErrorPage(failed_request, error, replace,
                            history_entry.get());
  }

  browser_side_navigation_pending_ = false;
}

void RenderFrameImpl::OnReportContentSecurityPolicyViolation(
    const content::CSPViolationParams& violation_params) {
  frame_->ReportContentSecurityPolicyViolation(
      BuildWebContentSecurityPolicyViolation(violation_params));
}

WebNavigationPolicy RenderFrameImpl::DecidePolicyForNavigation(
    const NavigationPolicyInfo& info) {
  // A content initiated navigation may have originated from a link-click,
  // script, drag-n-drop operation, etc.
  // info.extraData is only non-null if this is a redirect. Use the extraData
  // initiation information for redirects, and check pending_navigation_params_
  // otherwise.
  bool is_content_initiated =
      info.extra_data ? static_cast<DocumentState*>(info.extra_data)
                            ->navigation_state()
                            ->IsContentInitiated()
                      : !IsBrowserInitiated(pending_navigation_params_.get());

  // Webkit is asking whether to navigate to a new URL.
  // This is fine normally, except if we're showing UI from one security
  // context and they're trying to navigate to a different context.
  const GURL& url = info.url_request.Url();

  // With PlzNavigate, the redirect list is available for the first url. So
  // maintain the old behavior of not classifying the first URL in the chain as
  // a redirect.
  bool is_redirect =
      info.extra_data ||
      (pending_navigation_params_ &&
       !pending_navigation_params_->request_params.redirects.empty() &&
       (!IsBrowserSideNavigationEnabled() ||
        url != pending_navigation_params_->request_params.redirects[0]));

#ifdef OS_ANDROID
  bool render_view_was_created_by_renderer =
      render_view_->was_created_by_renderer_;
  // The handlenavigation API is deprecated and will be removed once
  // crbug.com/325351 is resolved.
  if (GetContentClient()->renderer()->HandleNavigation(
          this, is_content_initiated, render_view_was_created_by_renderer,
          frame_, info.url_request, info.navigation_type, info.default_policy,
          is_redirect)) {
    if (IsBrowserSideNavigationEnabled()) {
      // Need to let the browser know so it can notify its observers.
      Send(new FrameHostMsg_NavigationHandledByEmbedder(routing_id_));
    }
    return blink::kWebNavigationPolicyIgnore;
  }
#endif

  Referrer referrer(
      RenderViewImpl::GetReferrerFromRequest(frame_, info.url_request));

  // If the browser is interested, then give it a chance to look at the request.
  if (is_content_initiated && IsTopLevelNavigation(frame_) &&
      render_view_->renderer_preferences_
          .browser_handles_all_top_level_requests) {
    OpenURL(url, IsHttpPost(info.url_request),
            GetRequestBodyForWebURLRequest(info.url_request),
            GetWebURLRequestHeaders(info.url_request), referrer,
            info.default_policy, info.replaces_current_history_item, false);
    return blink::kWebNavigationPolicyIgnore;  // Suppress the load here.
  }

  // Back/forward navigations in newly created subframes should be sent to the
  // browser if there is a matching FrameNavigationEntry, and if it isn't just
  // staying at about:blank.  If this frame isn't in the map of unique names
  // that have history items, or if it's staying at the initial about:blank URL,
  // fall back to loading the default url.  (We remove each name as we encounter
  // it, because it will only be used once as the frame is created.)
  if (info.is_history_navigation_in_new_child_frame && is_content_initiated &&
      frame_->Parent()) {
    // Check whether the browser has a history item for this frame that isn't
    // just staying at the initial about:blank document.
    bool should_ask_browser = false;
    RenderFrameImpl* parent = RenderFrameImpl::FromWebFrame(frame_->Parent());
    auto iter = parent->history_subframe_unique_names_.find(
        unique_name_helper_.value());
    if (iter != parent->history_subframe_unique_names_.end()) {
      bool history_item_is_about_blank = iter->second;
      should_ask_browser =
          !history_item_is_about_blank || url != url::kAboutBlankURL;
      parent->history_subframe_unique_names_.erase(iter);
    }

    if (should_ask_browser) {
      // Don't do this if |info| also says it is a client redirect, in which
      // case JavaScript on the page is trying to interrupt the history
      // navigation.
      if (!info.is_client_redirect) {
        OpenURL(url, IsHttpPost(info.url_request),
                GetRequestBodyForWebURLRequest(info.url_request),
                GetWebURLRequestHeaders(info.url_request), referrer,
                info.default_policy, info.replaces_current_history_item, true);
        // Suppress the load in Blink but mark the frame as loading.
        return blink::kWebNavigationPolicyHandledByClientForInitialHistory;
      } else {
        // Client redirects during an initial history load should attempt to
        // cancel the history navigation.  They will create a provisional
        // document loader, causing the history load to be ignored in
        // NavigateInternal, and this IPC will try to cancel any cross-process
        // history load.
        Send(new FrameHostMsg_CancelInitialHistoryLoad(routing_id_));
      }
    }
  }

  // Use the frame's original request's URL rather than the document's URL for
  // subsequent checks.  For a popup, the document's URL may become the opener
  // window's URL if the opener has called document.write().
  // See http://crbug.com/93517.
  GURL old_url(frame_->DataSource()->GetRequest().Url());

  // Detect when we're crossing a permission-based boundary (e.g. into or out of
  // an extension or app origin, leaving a WebUI page, etc). We only care about
  // top-level navigations (not iframes). But we sometimes navigate to
  // about:blank to clear a tab, and we want to still allow that.
  if (!frame_->Parent() && is_content_initiated &&
      !url.SchemeIs(url::kAboutScheme)) {
    bool send_referrer = false;

    // All navigations to or from WebUI URLs or within WebUI-enabled
    // RenderProcesses must be handled by the browser process so that the
    // correct bindings and data sources can be registered.
    // Similarly, navigations to view-source URLs or within ViewSource mode
    // must be handled by the browser process (except for reloads - those are
    // safe to leave within the renderer).
    // Lastly, access to file:// URLs from non-file:// URL pages must be
    // handled by the browser so that ordinary renderer processes don't get
    // blessed with file permissions.
    int cumulative_bindings = RenderProcess::current()->GetEnabledBindings();
    bool is_initial_navigation = render_view_->history_list_length_ == 0;
    bool should_fork =
        HasWebUIScheme(url) || HasWebUIScheme(old_url) ||
        (cumulative_bindings & BINDINGS_POLICY_WEB_UI) ||
        url.SchemeIs(kViewSourceScheme) ||
        (frame_->IsViewSourceModeEnabled() &&
         info.navigation_type != blink::kWebNavigationTypeReload);

    if (!should_fork && url.SchemeIs(url::kFileScheme)) {
      // Fork non-file to file opens.  Note that this may fork unnecessarily if
      // another tab (hosting a file or not) targeted this one before its
      // initial navigation, but that shouldn't cause a problem.
      should_fork = !old_url.SchemeIs(url::kFileScheme);
    }

    if (!should_fork) {
      // Give the embedder a chance.
      should_fork = GetContentClient()->renderer()->ShouldFork(
          frame_, url, info.url_request.HttpMethod().Utf8(),
          is_initial_navigation, is_redirect, &send_referrer);
    }

    if (should_fork) {
      OpenURL(url, IsHttpPost(info.url_request),
              GetRequestBodyForWebURLRequest(info.url_request),
              GetWebURLRequestHeaders(info.url_request),
              send_referrer ? referrer : Referrer(), info.default_policy,
              info.replaces_current_history_item, false);
      return blink::kWebNavigationPolicyIgnore;  // Suppress the load here.
    }
  }

  // Detect when a page is "forking" a new tab that can be safely rendered in
  // its own process.  This is done by sites like Gmail that try to open links
  // in new windows without script connections back to the original page.  We
  // treat such cases as browser navigations (in which we will create a new
  // renderer for a cross-site navigation), rather than WebKit navigations.
  //
  // We use the following heuristic to decide whether to fork a new page in its
  // own process:
  // The parent page must open a new tab to about:blank, set the new tab's
  // window.opener to null, and then redirect the tab to a cross-site URL using
  // JavaScript.
  //
  // TODO(creis): Deprecate this logic once we can rely on rel=noreferrer
  // (see below).
  bool is_fork =
      // Must start from a tab showing about:blank, which is later redirected.
      old_url == url::kAboutBlankURL &&
      // Must be the first real navigation of the tab.
      render_view_->HistoryBackListCount() < 1 &&
      render_view_->HistoryForwardListCount() < 1 &&
      // The parent page must have set the child's window.opener to null before
      // redirecting to the desired URL.
      frame_->Opener() == NULL &&
      // Must be a top-level frame.
      frame_->Parent() == NULL &&
      // Must not have issued the request from this page.
      is_content_initiated &&
      // Must be targeted at the current tab.
      info.default_policy == blink::kWebNavigationPolicyCurrentTab &&
      // Must be a JavaScript navigation, which appears as "other".
      info.navigation_type == blink::kWebNavigationTypeOther;

  if (is_fork) {
    // Open the URL via the browser, not via WebKit.
    OpenURL(url, IsHttpPost(info.url_request),
            GetRequestBodyForWebURLRequest(info.url_request),
            GetWebURLRequestHeaders(info.url_request), Referrer(),
            info.default_policy, info.replaces_current_history_item, false);
    return blink::kWebNavigationPolicyIgnore;
  }

  bool should_dispatch_before_unload =
      info.default_policy == blink::kWebNavigationPolicyCurrentTab &&
      // There is no need to execute the BeforeUnload event during a redirect,
      // since it was already executed at the start of the navigation.
      !is_redirect &&
      // PlzNavigate: this should not be executed when commiting the navigation.
      (!IsBrowserSideNavigationEnabled() ||
       info.url_request.CheckForBrowserSideNavigation()) &&
      // No need to dispatch beforeunload if the frame has not committed a
      // navigation and contains an empty initial document.
      (has_accessed_initial_document_ || !current_history_item_.IsNull());

  if (should_dispatch_before_unload) {
    // Execute the BeforeUnload event. If asked not to proceed or the frame is
    // destroyed, ignore the navigation.
    // Keep a WeakPtr to this RenderFrameHost to detect if executing the
    // BeforeUnload event destriyed this frame.
    base::WeakPtr<RenderFrameImpl> weak_self = weak_factory_.GetWeakPtr();

    if (!frame_->DispatchBeforeUnloadEvent(info.navigation_type ==
                                           blink::kWebNavigationTypeReload) ||
        !weak_self) {
      return blink::kWebNavigationPolicyIgnore;
    }

    // |navigation_start| must be recorded immediately after dispatching the
    // beforeunload event.
    if (pending_navigation_params_) {
      pending_navigation_params_->common_params.navigation_start =
          base::TimeTicks::Now();
    }
  }

  // PlzNavigate: if the navigation is not synchronous, send it to the browser.
  // This includes navigations with no request being sent to the network stack.
  if (IsBrowserSideNavigationEnabled() &&
      info.url_request.CheckForBrowserSideNavigation() &&
      ShouldMakeNetworkRequestForURL(url)) {
    if (info.default_policy == blink::kWebNavigationPolicyCurrentTab) {
      // The BeginNavigation() call happens in didStartProvisionalLoad(). We
      // need to save information about the navigation here.
      pending_navigation_info_.reset(new PendingNavigationInfo(info));
      return blink::kWebNavigationPolicyHandledByClient;
    } else {
      LoadURLExternally(info.url_request, info.default_policy);
      return blink::kWebNavigationPolicyIgnore;
    }
  }

  return info.default_policy;
}

void RenderFrameImpl::OnGetSavableResourceLinks() {
  std::vector<GURL> resources_list;
  std::vector<SavableSubframe> subframes;
  SavableResourcesResult result(&resources_list, &subframes);

  if (!GetSavableResourceLinksForFrame(frame_, &result)) {
    Send(new FrameHostMsg_SavableResourceLinksError(routing_id_));
    return;
  }

  Referrer referrer = Referrer(frame_->GetDocument().Url(),
                               frame_->GetDocument().GetReferrerPolicy());

  Send(new FrameHostMsg_SavableResourceLinksResponse(
      routing_id_, resources_list, referrer, subframes));
}

void RenderFrameImpl::OnGetSerializedHtmlWithLocalLinks(
    const std::map<GURL, base::FilePath>& url_to_local_path,
    const std::map<int, base::FilePath>& frame_routing_id_to_local_path) {
  // Convert input to the canonical way of passing a map into a Blink API.
  LinkRewritingDelegate delegate(url_to_local_path,
                                 frame_routing_id_to_local_path);

  // Serialize the frame (without recursing into subframes).
  WebFrameSerializer::Serialize(GetWebFrame(),
                                this,  // WebFrameSerializerClient.
                                &delegate);
}

void RenderFrameImpl::OnSerializeAsMHTML(
    const FrameMsg_SerializeAsMHTML_Params& params) {
  TRACE_EVENT0("page-serialization", "RenderFrameImpl::OnSerializeAsMHTML");
  base::TimeTicks start_time = base::TimeTicks::Now();
  // Unpack IPC payload.
  base::File file = IPC::PlatformFileForTransitToFile(params.destination_file);
  const WebString mhtml_boundary =
      WebString::FromUTF8(params.mhtml_boundary_marker);
  DCHECK(!mhtml_boundary.IsEmpty());

  // Holds WebThreadSafeData instances for some or all of header, contents and
  // footer.
  std::vector<WebThreadSafeData> mhtml_contents;
  std::set<std::string> serialized_resources_uri_digests;
  MHTMLPartsGenerationDelegate delegate(params,
                                        &serialized_resources_uri_digests);

  MhtmlSaveStatus save_status = MhtmlSaveStatus::SUCCESS;
  bool has_some_data = false;

  // Generate MHTML header if needed.
  if (IsMainFrame()) {
    TRACE_EVENT0("page-serialization",
                 "RenderFrameImpl::OnSerializeAsMHTML header");
    // The returned data can be empty if the main frame should be skipped. If
    // the main frame is skipped, then the whole archive is bad.
    mhtml_contents.emplace_back(WebFrameSerializer::GenerateMHTMLHeader(
        mhtml_boundary, GetWebFrame(), &delegate));
    if (mhtml_contents.back().IsEmpty())
      save_status = MhtmlSaveStatus::FRAME_SERIALIZATION_FORBIDDEN;
    else
      has_some_data = true;
  }

  // Generate MHTML parts.  Note that if this is not the main frame, then even
  // skipping the whole parts generation step is not an error - it simply
  // results in an omitted resource in the final file.
  if (save_status == MhtmlSaveStatus::SUCCESS) {
    TRACE_EVENT0("page-serialization",
                 "RenderFrameImpl::OnSerializeAsMHTML parts serialization");
    // The returned data can be empty if the frame should be skipped, but this
    // is OK.
    mhtml_contents.emplace_back(WebFrameSerializer::GenerateMHTMLParts(
        mhtml_boundary, GetWebFrame(), &delegate));
    has_some_data |= !mhtml_contents.back().IsEmpty();
  }

  // Note: the MHTML footer is written by the browser process, after the last
  // frame is serialized by a renderer process.

  // Note: we assume RenderFrameImpl::OnWriteMHTMLToDiskComplete and the rest of
  // this function will be fast enough to not need to be accounted for in this
  // metric.
  base::TimeDelta main_thread_use_time = base::TimeTicks::Now() - start_time;
  UMA_HISTOGRAM_TIMES(
      "PageSerialization.MhtmlGeneration.RendererMainThreadTime.SingleFrame",
      main_thread_use_time);

  if (save_status == MhtmlSaveStatus::SUCCESS && has_some_data) {
    base::PostTaskAndReplyWithResult(
        RenderThreadImpl::current()->GetFileThreadTaskRunner().get(), FROM_HERE,
        base::Bind(&WriteMHTMLToDisk, base::Passed(&mhtml_contents),
                   base::Passed(&file)),
        base::Bind(&RenderFrameImpl::OnWriteMHTMLToDiskComplete,
                   weak_factory_.GetWeakPtr(), params.job_id,
                   base::Passed(&serialized_resources_uri_digests),
                   main_thread_use_time));
  } else {
    file.Close();
    OnWriteMHTMLToDiskComplete(params.job_id, serialized_resources_uri_digests,
                               main_thread_use_time, save_status);
  }
}

void RenderFrameImpl::OnWriteMHTMLToDiskComplete(
    int job_id,
    std::set<std::string> serialized_resources_uri_digests,
    base::TimeDelta main_thread_use_time,
    MhtmlSaveStatus save_status) {
  TRACE_EVENT1("page-serialization",
               "RenderFrameImpl::OnWriteMHTMLToDiskComplete",
               "frame save status", GetMhtmlSaveStatusLabel(save_status));
  DCHECK(RenderThread::Get()) << "Must run in the main renderer thread";
  // Notify the browser process about completion.
  // Note: we assume this method is fast enough to not need to be accounted for
  // in PageSerialization.MhtmlGeneration.RendererMainThreadTime.SingleFrame.
  Send(new FrameHostMsg_SerializeAsMHTMLResponse(
      routing_id_, job_id, save_status, serialized_resources_uri_digests,
      main_thread_use_time));
}

void RenderFrameImpl::OnFind(int request_id,
                             const base::string16& search_text,
                             const WebFindOptions& options) {
  DCHECK(!search_text.empty());

  blink::WebPlugin* plugin = GetWebPluginForFind();
  // Check if the plugin still exists in the document.
  if (plugin) {
    if (options.find_next) {
      // Just navigate back/forward.
      plugin->SelectFindResult(options.forward, request_id);
      render_view_->webview()->SetFocusedFrame(frame_);
    } else if (!plugin->StartFind(WebString::FromUTF16(search_text),
                                  options.match_case, request_id)) {
      // Send "no results".
      SendFindReply(request_id, 0 /* match_count */, 0 /* ordinal */,
                    gfx::Rect(), true /* final_status_update */);
    }
    return;
  }

  frame_->RequestFind(request_id, WebString::FromUTF16(search_text), options);
}

void RenderFrameImpl::OnClearActiveFindMatch() {
  frame_->ExecuteCommand(WebString::FromUTF8("CollapseSelection"));
  frame_->ClearActiveFindMatch();
}

// Ensure that content::StopFindAction and blink::WebLocalFrame::StopFindAction
// are kept in sync.
STATIC_ASSERT_ENUM(STOP_FIND_ACTION_CLEAR_SELECTION,
                   WebLocalFrame::kStopFindActionClearSelection);
STATIC_ASSERT_ENUM(STOP_FIND_ACTION_KEEP_SELECTION,
                   WebLocalFrame::kStopFindActionKeepSelection);
STATIC_ASSERT_ENUM(STOP_FIND_ACTION_ACTIVATE_SELECTION,
                   WebLocalFrame::kStopFindActionActivateSelection);

void RenderFrameImpl::OnStopFinding(StopFindAction action) {
  blink::WebPlugin* plugin = GetWebPluginForFind();
  if (plugin) {
    plugin->StopFind();
    return;
  }

  frame_->StopFinding(static_cast<WebLocalFrame::StopFindAction>(action));
}

void RenderFrameImpl::OnEnableViewSourceMode() {
  DCHECK(frame_);
  DCHECK(!frame_->Parent());
  frame_->EnableViewSourceMode(true);
}

void RenderFrameImpl::OnSuppressFurtherDialogs() {
  suppress_further_dialogs_ = true;
}

void RenderFrameImpl::OnFileChooserResponse(
    const std::vector<content::FileChooserFileInfo>& files) {
  // This could happen if we navigated to a different page before the user
  // closed the chooser.
  if (file_chooser_completions_.empty())
    return;

  // Convert Chrome's SelectedFileInfo list to WebKit's.
  WebVector<blink::WebFileChooserCompletion::SelectedFileInfo> selected_files(
      files.size());
  size_t current_size = 0;
  for (size_t i = 0; i < files.size(); ++i) {
    blink::WebFileChooserCompletion::SelectedFileInfo selected_file;
    selected_file.path = blink::FilePathToWebString(files[i].file_path);

    // Exclude files whose paths can't be converted into WebStrings. Blink won't
    // be able to handle these, and the browser process would kill the renderer
    // when it claims to have chosen an empty file path.
    if (selected_file.path.IsEmpty())
      continue;

    selected_file.display_name =
        blink::FilePathToWebString(base::FilePath(files[i].display_name));
    if (files[i].file_system_url.is_valid()) {
      selected_file.file_system_url = files[i].file_system_url;
      selected_file.length = files[i].length;
      selected_file.modification_time = files[i].modification_time.ToDoubleT();
      selected_file.is_directory = files[i].is_directory;
    }

    selected_files[current_size] = selected_file;
    current_size++;
  }

  // If not all files were included, truncate the WebVector.
  if (current_size < selected_files.size()) {
    WebVector<blink::WebFileChooserCompletion::SelectedFileInfo> truncated_list(
        selected_files.Data(), current_size);
    selected_files.Swap(truncated_list);
  }

  if (file_chooser_completions_.front()->completion) {
    file_chooser_completions_.front()->completion->DidChooseFile(
        selected_files);
  }
  file_chooser_completions_.pop_front();

  // If there are more pending file chooser requests, schedule one now.
  if (!file_chooser_completions_.empty()) {
    Send(new FrameHostMsg_RunFileChooser(
        routing_id_, file_chooser_completions_.front()->params));
  }
}

void RenderFrameImpl::OnClearFocusedElement() {
  // TODO(ekaramad): Should we add a method to WebLocalFrame instead and avoid
  // calling this on the WebView?
  if (auto* webview = render_view_->GetWebView())
    webview->ClearFocusedElement();
}

void RenderFrameImpl::OnBlinkFeatureUsageReport(const std::set<int>& features) {
  frame_->BlinkFeatureUsageReport(features);
}

void RenderFrameImpl::OnMixedContentFound(
    const FrameMsg_MixedContentFound_Params& params) {
  blink::WebSourceLocation source_location;
  source_location.url = WebString::FromLatin1(params.source_location.url);
  source_location.line_number = params.source_location.line_number;
  source_location.column_number = params.source_location.column_number;
  auto request_context = static_cast<blink::WebURLRequest::RequestContext>(
      params.request_context_type);
  frame_->MixedContentFound(params.main_resource_url, params.mixed_content_url,
                            request_context, params.was_allowed,
                            params.had_redirect, source_location);
}

#if defined(OS_ANDROID)
void RenderFrameImpl::OnActivateNearestFindResult(int request_id,
                                                  float x,
                                                  float y) {
  WebRect selection_rect;
  int ordinal =
      frame_->SelectNearestFindMatch(WebFloatPoint(x, y), &selection_rect);
  if (ordinal == -1) {
    // Something went wrong, so send a no-op reply (force the frame to report
    // the current match count) in case the host is waiting for a response due
    // to rate-limiting.
    frame_->IncreaseMatchCount(0, request_id);
    return;
  }

  SendFindReply(request_id, -1 /* number_of_matches */, ordinal, selection_rect,
                true /* final_update */);
}

void RenderFrameImpl::OnGetNearestFindResult(int nfr_request_id,
                                             float x,
                                             float y) {
  float distance = frame_->DistanceToNearestFindMatch(WebFloatPoint(x, y));
  Send(new FrameHostMsg_GetNearestFindResult_Reply(
      routing_id_, nfr_request_id, distance));
}

void RenderFrameImpl::OnFindMatchRects(int current_version) {
  std::vector<gfx::RectF> match_rects;

  int rects_version = frame_->FindMatchMarkersVersion();
  if (current_version != rects_version) {
    WebVector<WebFloatRect> web_match_rects;
    frame_->FindMatchRects(web_match_rects);
    match_rects.reserve(web_match_rects.size());
    for (size_t i = 0; i < web_match_rects.size(); ++i)
      match_rects.push_back(gfx::RectF(web_match_rects[i]));
  }

  gfx::RectF active_rect = frame_->ActiveFindMatchRect();
  Send(new FrameHostMsg_FindMatchRects_Reply(routing_id_, rects_version,
                                             match_rects, active_rect));
}
#endif

#if BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
#if defined(OS_MACOSX)
void RenderFrameImpl::OnSelectPopupMenuItem(int selected_index) {
  if (external_popup_menu_ == NULL)
    return;
  external_popup_menu_->DidSelectItem(selected_index);
  external_popup_menu_.reset();
}
#else
void RenderFrameImpl::OnSelectPopupMenuItems(
    bool canceled,
    const std::vector<int>& selected_indices) {
  // It is possible to receive more than one of these calls if the user presses
  // a select faster than it takes for the show-select-popup IPC message to make
  // it to the browser UI thread. Ignore the extra-messages.
  // TODO(jcivelli): http:/b/5793321 Implement a better fix, as detailed in bug.
  if (!external_popup_menu_)
    return;

  external_popup_menu_->DidSelectItems(canceled, selected_indices);
  external_popup_menu_.reset();
}
#endif
#endif

void RenderFrameImpl::OpenURL(
    const GURL& url,
    bool uses_post,
    const scoped_refptr<ResourceRequestBodyImpl>& resource_request_body,
    const std::string& extra_headers,
    const Referrer& referrer,
    WebNavigationPolicy policy,
    bool should_replace_current_entry,
    bool is_history_navigation_in_new_child) {
  FrameHostMsg_OpenURL_Params params;
  params.url = url;
  params.uses_post = uses_post;
  params.resource_request_body = resource_request_body;
  params.extra_headers = extra_headers;
  params.referrer = referrer;
  params.disposition = RenderViewImpl::NavigationPolicyToDisposition(policy);

  if (IsBrowserInitiated(pending_navigation_params_.get())) {
    // This is necessary to preserve the should_replace_current_entry value on
    // cross-process redirects, in the event it was set by a previous process.
    WebDataSource* ds = frame_->ProvisionalDataSource();
    DCHECK(ds);
    params.should_replace_current_entry = ds->ReplacesCurrentHistoryItem();
  } else {
    params.should_replace_current_entry =
        should_replace_current_entry && render_view_->history_list_length_;
  }
  params.user_gesture = WebUserGestureIndicator::IsProcessingUserGesture();
  if (GetContentClient()->renderer()->AllowPopup())
    params.user_gesture = true;

  if (policy == blink::kWebNavigationPolicyNewBackgroundTab ||
      policy == blink::kWebNavigationPolicyNewForegroundTab ||
      policy == blink::kWebNavigationPolicyNewWindow ||
      policy == blink::kWebNavigationPolicyNewPopup) {
    WebUserGestureIndicator::ConsumeUserGesture();
  }

  if (is_history_navigation_in_new_child)
    params.is_history_navigation_in_new_child = true;

  Send(new FrameHostMsg_OpenURL(routing_id_, params));
}

void RenderFrameImpl::NavigateInternal(
    const CommonNavigationParams& common_params,
    const StartNavigationParams& start_params,
    const RequestNavigationParams& request_params,
    std::unique_ptr<StreamOverrideParameters> stream_params) {
  bool browser_side_navigation = IsBrowserSideNavigationEnabled();

  // PlzNavigate
  // Clear pending navigations which weren't sent to the browser because we
  // did not get a didStartProvisionalLoad() notification for them.
  pending_navigation_info_.reset(nullptr);

  // Lower bound for browser initiated navigation start time.
  base::TimeTicks renderer_navigation_start = base::TimeTicks::Now();
  bool is_reload =
      FrameMsg_Navigate_Type::IsReload(common_params.navigation_type);
  bool is_history_navigation = request_params.page_state.IsValid();
  WebCachePolicy cache_policy = WebCachePolicy::kUseProtocolCachePolicy;
  RenderFrameImpl::PrepareRenderViewForNavigation(
      common_params.url, request_params);

  GetContentClient()->SetActiveURL(common_params.url);

  // If this frame isn't in the same process as the main frame, it may naively
  // assume that this is the first navigation in the iframe, but this may not
  // actually be the case. Inform the frame's state machine if this frame has
  // already committed other loads.
  if (request_params.has_committed_real_load && frame_->Parent())
    frame_->SetCommittedFirstRealLoad();

  if (is_reload && current_history_item_.IsNull()) {
    // We cannot reload if we do not have any history state.  This happens, for
    // example, when recovering from a crash.
    is_reload = false;
    cache_policy = WebCachePolicy::kValidatingCacheData;
  }

  // If the navigation is for "view source", the WebLocalFrame needs to be put
  // in a special mode.
  if (request_params.is_view_source)
    frame_->EnableViewSourceMode(true);

  pending_navigation_params_.reset(
      new NavigationParams(common_params, start_params, request_params));

  // Sanitize navigation start and store in |pending_navigation_params_|.
  // It will be picked up in UpdateNavigationState.
  pending_navigation_params_->common_params.navigation_start =
      SanitizeNavigationTiming(common_params.navigation_start,
                               renderer_navigation_start);

  // Create parameters for a standard navigation, indicating whether it should
  // replace the current NavigationEntry.
  blink::WebFrameLoadType load_type =
      common_params.should_replace_current_entry
          ? blink::WebFrameLoadType::kReplaceCurrentItem
          : blink::WebFrameLoadType::kStandard;
  blink::WebHistoryLoadType history_load_type =
      blink::kWebHistoryDifferentDocumentLoad;
  bool should_load_request = false;
  WebHistoryItem item_for_history_navigation;

  // Enforce same-document navigation from the browser only if
  // browser-side-navigation is enabled.
  bool is_same_document =
      IsBrowserSideNavigationEnabled() &&
      FrameMsg_Navigate_Type::IsSameDocument(common_params.navigation_type);

  WebURLRequest request = CreateURLRequestForNavigation(
      common_params, request_params, std::move(stream_params),
      frame_->IsViewSourceModeEnabled(), is_same_document);
  request.SetFrameType(IsTopLevelNavigation(frame_)
                           ? blink::WebURLRequest::kFrameTypeTopLevel
                           : blink::WebURLRequest::kFrameTypeNested);

  if (IsBrowserSideNavigationEnabled() && common_params.post_data)
    request.SetHTTPBody(GetWebHTTPBodyForRequestBody(common_params.post_data));

  // Used to determine whether this frame is actually loading a request as part
  // of a history navigation.
  bool has_history_navigation_in_frame = false;

#if defined(OS_ANDROID)
  request.SetHasUserGesture(request_params.has_user_gesture);
#endif

  if (browser_side_navigation) {
    // PlzNavigate: Make sure that Blink's loader will not try to use browser
    // side navigation for this request (since it already went to the browser).
    request.SetCheckForBrowserSideNavigation(false);

    request.SetNavigationStartTime(
        ConvertToBlinkTime(common_params.navigation_start));
  }

  // If we are reloading, then use the history state of the current frame.
  // Otherwise, if we have history state, then we need to navigate to it, which
  // corresponds to a back/forward navigation event. Update the parameters
  // depending on the navigation type.
  if (is_reload) {
    load_type = ReloadFrameLoadTypeFor(common_params.navigation_type);

    if (!browser_side_navigation) {
      const GURL override_url =
          (common_params.navigation_type ==
           FrameMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL)
              ? common_params.url
              : GURL();
      request = frame_->RequestForReload(load_type, override_url);
    }
    should_load_request = true;
  } else if (is_history_navigation) {
    // We must know the nav entry ID of the page we are navigating back to,
    // which should be the case because history navigations are routed via the
    // browser.
    DCHECK_NE(0, request_params.nav_entry_id);
    std::unique_ptr<HistoryEntry> entry =
        PageStateToHistoryEntry(request_params.page_state);
    if (entry) {
      // The browser process sends a single WebHistoryItem for this frame.
      // TODO(creis): Change PageState to FrameState.  In the meantime, we
      // store the relevant frame's WebHistoryItem in the root of the
      // PageState.
      item_for_history_navigation = entry->root();
      switch (common_params.navigation_type) {
        case FrameMsg_Navigate_Type::RELOAD:
        case FrameMsg_Navigate_Type::RELOAD_BYPASSING_CACHE:
        case FrameMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL:
        case FrameMsg_Navigate_Type::RESTORE:
        case FrameMsg_Navigate_Type::RESTORE_WITH_POST:
        case FrameMsg_Navigate_Type::HISTORY_DIFFERENT_DOCUMENT:
          history_load_type = blink::kWebHistoryDifferentDocumentLoad;
          break;
        case FrameMsg_Navigate_Type::HISTORY_SAME_DOCUMENT:
          history_load_type = blink::kWebHistorySameDocumentLoad;
          break;
        default:
          NOTREACHED();
          history_load_type = blink::kWebHistoryDifferentDocumentLoad;
      }
      load_type = request_params.is_history_navigation_in_new_child
                      ? blink::WebFrameLoadType::kInitialHistoryLoad
                      : blink::WebFrameLoadType::kBackForward;
      should_load_request = true;

      // Keep track of which subframes the browser process has history items
      // for during a history navigation.
      history_subframe_unique_names_ = request_params.subframe_unique_names;

      if (history_load_type == blink::kWebHistorySameDocumentLoad) {
        // If this is marked as a same document load but we haven't committed
        // anything, treat it as a new load.  The browser shouldn't let this
        // happen.
        if (current_history_item_.IsNull()) {
          history_load_type = blink::kWebHistoryDifferentDocumentLoad;
          NOTREACHED();
        } else {
          // Additionally, if the |current_history_item_|'s document
          // sequence number doesn't match the one sent from the browser, it
          // is possible that this renderer has committed a different
          // document. In such case, don't use WebHistorySameDocumentLoad.
          if (current_history_item_.DocumentSequenceNumber() !=
              item_for_history_navigation.DocumentSequenceNumber()) {
            history_load_type = blink::kWebHistoryDifferentDocumentLoad;
          }
        }
      }

      // If this navigation is to a history item for a new child frame, we may
      // want to ignore it in some cases.  If a Javascript navigation (i.e.,
      // client redirect) interrupted it and has either been scheduled,
      // started loading, or has committed, we should ignore the history item.
      bool interrupted_by_client_redirect =
          frame_->IsNavigationScheduledWithin(0) ||
          frame_->ProvisionalDataSource() || !current_history_item_.IsNull();
      if (request_params.is_history_navigation_in_new_child &&
          interrupted_by_client_redirect) {
        should_load_request = false;
        has_history_navigation_in_frame = false;
      }

      // Generate the request for the load from the HistoryItem.
      // PlzNavigate: use the data sent by the browser for the url and the
      // HTTP state. The restoration of user state such as scroll position
      // will be done based on the history item during the load.
      if (!browser_side_navigation && should_load_request) {
        request = frame_->RequestFromHistoryItem(item_for_history_navigation,
                                                 cache_policy);
      }
    }
  } else {
    // Navigate to the given URL.
    if (!start_params.extra_headers.empty() && !browser_side_navigation) {
      for (net::HttpUtil::HeadersIterator i(start_params.extra_headers.begin(),
                                            start_params.extra_headers.end(),
                                            "\n");
           i.GetNext();) {
        request.AddHTTPHeaderField(WebString::FromUTF8(i.name()),
                                   WebString::FromUTF8(i.values()));
      }
    }

    if (common_params.method == "POST" && !browser_side_navigation &&
        common_params.post_data) {
      request.SetHTTPBody(
          GetWebHTTPBodyForRequestBody(common_params.post_data));
    }

    should_load_request = true;
  }

  if (should_load_request) {
    // PlzNavigate: check if the navigation being committed originated as a
    // client redirect.
    bool is_client_redirect =
        browser_side_navigation
            ? !!(common_params.transition & ui::PAGE_TRANSITION_CLIENT_REDIRECT)
            : false;

    // Perform a navigation to a data url if needed.
    // Note: the base URL might be invalid, so also check the data URL string.
    bool should_load_data_url = !common_params.base_url_for_data_url.is_empty();
#if defined(OS_ANDROID)
    should_load_data_url |= !request_params.data_url_as_string.empty();
#endif
    if (should_load_data_url) {
      LoadDataURL(common_params, request_params, frame_, load_type,
                  item_for_history_navigation, history_load_type,
                  is_client_redirect);
    } else {
      // The load of the URL can result in this frame being removed. Use a
      // WeakPtr as an easy way to detect whether this has occured. If so, this
      // method should return immediately and not touch any part of the object,
      // otherwise it will result in a use-after-free bug.
      base::WeakPtr<RenderFrameImpl> weak_this = weak_factory_.GetWeakPtr();

      // Load the request.
      frame_->Load(request, load_type, item_for_history_navigation,
                   history_load_type, is_client_redirect);

      if (!weak_this)
        return;
    }
  } else {
    // The browser expects the frame to be loading this navigation. Inform it
    // that the load stopped if needed.
    // Note: in the case of history navigations, |should_load_request| will be
    // false, and the frame may not have been set in a loading state. Do not
    // send a stop message if a history navigation is loading in this frame
    // nonetheless. This behavior will go away with subframe navigation
    // entries.
    if (frame_ && !frame_->IsLoading() && !has_history_navigation_in_frame)
      Send(new FrameHostMsg_DidStopLoading(routing_id_));
  }

  // In case LoadRequest failed before didCreateDataSource was called.
  pending_navigation_params_.reset();

  // PlzNavigate: reset the source location now that the commit checks have been
  // processed.
  if (IsBrowserSideNavigationEnabled()) {
    frame_->DataSource()->ResetSourceLocation();
    if (frame_->ProvisionalDataSource())
      frame_->ProvisionalDataSource()->ResetSourceLocation();
  }
}

void RenderFrameImpl::UpdateEncoding(WebFrame* frame,
                                     const std::string& encoding_name) {
  // Only update main frame's encoding_name.
  if (!frame->Parent())
    Send(new FrameHostMsg_UpdateEncoding(routing_id_, encoding_name));
}

void RenderFrameImpl::SyncSelectionIfRequired() {
  base::string16 text;
  size_t offset;
  gfx::Range range;
#if BUILDFLAG(ENABLE_PLUGINS)
  if (focused_pepper_plugin_) {
    focused_pepper_plugin_->GetSurroundingText(&text, &range);
    offset = 0;  // Pepper API does not support offset reporting.
    // TODO(kinaba): cut as needed.
  } else
#endif
  {
    WebRange selection =
        GetRenderWidget()->GetWebWidget()->CaretOrSelectionRange();
    if (selection.IsNull())
      return;

    range = gfx::Range(selection.StartOffset(), selection.EndOffset());

    if (frame_->GetInputMethodController()->TextInputType() !=
        blink::kWebTextInputTypeNone) {
      // If current focused element is editable, we will send 100 more chars
      // before and after selection. It is for input method surrounding text
      // feature.
      if (selection.StartOffset() > kExtraCharsBeforeAndAfterSelection)
        offset = selection.StartOffset() - kExtraCharsBeforeAndAfterSelection;
      else
        offset = 0;
      size_t length =
          selection.EndOffset() - offset + kExtraCharsBeforeAndAfterSelection;
      text = frame_->RangeAsText(WebRange(offset, length)).Utf16();
    } else {
      offset = selection.StartOffset();
      text = frame_->SelectionAsText().Utf16();
      // http://crbug.com/101435
      // In some case, frame->selectionAsText() returned text's length is not
      // equal to the length returned from
      // GetWebWidget()->caretOrSelectionRange().
      // So we have to set the range according to text.length().
      range.set_end(range.start() + text.length());
    }
  }

  // TODO(dglazkov): Investigate if and why this would be happening,
  // and resolve this. We shouldn't be carrying selection text here.
  // http://crbug.com/632920.
  // Sometimes we get repeated didChangeSelection calls from webkit when
  // the selection hasn't actually changed. We don't want to report these
  // because it will cause us to continually claim the X clipboard.
  if (selection_text_offset_ != offset ||
      selection_range_ != range ||
      selection_text_ != text) {
    selection_text_ = text;
    selection_text_offset_ = offset;
    selection_range_ = range;
    SetSelectedText(text, offset, range);
  }
  GetRenderWidget()->UpdateSelectionBounds();
}

void RenderFrameImpl::InitializeUserMediaClient() {
  RenderThreadImpl* render_thread = RenderThreadImpl::current();
  if (!render_thread)  // Will be NULL during unit tests.
    return;

#if BUILDFLAG(ENABLE_WEBRTC)
  DCHECK(!web_user_media_client_);
  web_user_media_client_ = new UserMediaClientImpl(
      this, RenderThreadImpl::current()->GetPeerConnectionDependencyFactory(),
      base::MakeUnique<MediaStreamDispatcher>(this),
      render_thread->GetWorkerTaskRunner());
  GetInterfaceRegistry()->AddInterface(
      base::Bind(&MediaDevicesListenerImpl::Create, GetRoutingID()));
#endif
}

WebMediaPlayer* RenderFrameImpl::CreateWebMediaPlayerForMediaStream(
    WebMediaPlayerClient* client,
    const WebString& sink_id,
    const WebSecurityOrigin& security_origin) {
#if BUILDFLAG(ENABLE_WEBRTC)
  RenderThreadImpl* const render_thread = RenderThreadImpl::current();

  scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner =
      render_thread->compositor_task_runner();
  if (!compositor_task_runner.get())
    compositor_task_runner = base::ThreadTaskRunnerHandle::Get();

  return new WebMediaPlayerMS(
      frame_, client, GetWebMediaPlayerDelegate(),
      new RenderMediaLog(url::Origin(security_origin).GetURL()),
      CreateRendererFactory(), render_thread->GetIOTaskRunner(),
      compositor_task_runner, render_thread->GetMediaThreadTaskRunner(),
      render_thread->GetWorkerTaskRunner(), render_thread->GetGpuFactories(),
      sink_id, security_origin);
#else
  return NULL;
#endif  // BUILDFLAG(ENABLE_WEBRTC)
}

std::unique_ptr<MediaStreamRendererFactory>
RenderFrameImpl::CreateRendererFactory() {
  std::unique_ptr<MediaStreamRendererFactory> factory =
      GetContentClient()->renderer()->CreateMediaStreamRendererFactory();
  if (factory.get())
    return factory;
#if BUILDFLAG(ENABLE_WEBRTC)
  return std::unique_ptr<MediaStreamRendererFactory>(
      new MediaStreamRendererFactoryImpl());
#else
  return std::unique_ptr<MediaStreamRendererFactory>(
      static_cast<MediaStreamRendererFactory*>(NULL));
#endif
}

void RenderFrameImpl::PrepareRenderViewForNavigation(
    const GURL& url,
    const RequestNavigationParams& request_params) {
  DCHECK(render_view_->webview());

  MaybeHandleDebugURL(url);

  if (is_main_frame_) {
    for (auto& observer : render_view_->observers_)
      observer.Navigate(url);
  }

  render_view_->history_list_offset_ =
      request_params.current_history_list_offset;
  render_view_->history_list_length_ =
      request_params.current_history_list_length;
  if (request_params.should_clear_history_list) {
    CHECK_EQ(-1, render_view_->history_list_offset_);
    CHECK_EQ(0, render_view_->history_list_length_);
  }
}

void RenderFrameImpl::BeginNavigation(const NavigationPolicyInfo& info) {
  CHECK(IsBrowserSideNavigationEnabled());
  browser_side_navigation_pending_ = true;

  // Note: At this stage, the goal is to apply all the modifications the
  // renderer wants to make to the request, and then send it to the browser, so
  // that the actual network request can be started. Ideally, all such
  // modifications should take place in willSendRequest, and in the
  // implementation of willSendRequest for the various InspectorAgents
  // (devtools).
  //
  // TODO(clamy): Apply devtools override.
  // TODO(clamy): Make sure that navigation requests are not modified somewhere
  // else in blink.
  WillSendRequest(frame_, info.url_request);

  // Update the transition type of the request for client side redirects.
  if (!info.url_request.GetExtraData())
    info.url_request.SetExtraData(new RequestExtraData());
  if (info.is_client_redirect) {
    RequestExtraData* extra_data =
        static_cast<RequestExtraData*>(info.url_request.GetExtraData());
    extra_data->set_transition_type(ui::PageTransitionFromInt(
        extra_data->transition_type() | ui::PAGE_TRANSITION_CLIENT_REDIRECT));
  }

  // TODO(clamy): Same-document navigations should not be sent back to the
  // browser.
  // TODO(clamy): Data urls should not be sent back to the browser either.
  // These values are assumed on the browser side for navigations. These checks
  // ensure the renderer has the correct values.
  DCHECK_EQ(FETCH_REQUEST_MODE_NAVIGATE,
            GetFetchRequestModeForWebURLRequest(info.url_request));
  DCHECK_EQ(FETCH_CREDENTIALS_MODE_INCLUDE,
            GetFetchCredentialsModeForWebURLRequest(info.url_request));
  DCHECK(GetFetchRedirectModeForWebURLRequest(info.url_request) ==
         FetchRedirectMode::MANUAL_MODE);
  DCHECK(frame_->Parent() ||
         GetRequestContextFrameTypeForWebURLRequest(info.url_request) ==
             REQUEST_CONTEXT_FRAME_TYPE_TOP_LEVEL);
  DCHECK(!frame_->Parent() ||
         GetRequestContextFrameTypeForWebURLRequest(info.url_request) ==
             REQUEST_CONTEXT_FRAME_TYPE_NESTED);

  base::Optional<url::Origin> initiator_origin =
      info.url_request.RequestorOrigin().IsNull()
          ? base::Optional<url::Origin>()
          : base::Optional<url::Origin>(info.url_request.RequestorOrigin());

  int load_flags = GetLoadFlagsForWebURLRequest(info.url_request);

  // Requests initiated via devtools can have caching disabled.
  if (info.is_cache_disabled) {
    // Turn off all caching related flags and set LOAD_BYPASS_CACHE.
    load_flags &= ~(net::LOAD_VALIDATE_CACHE | net::LOAD_SKIP_CACHE_VALIDATION |
                    net::LOAD_ONLY_FROM_CACHE | net::LOAD_DISABLE_CACHE);
    load_flags |= net::LOAD_BYPASS_CACHE;
  }

  bool is_form_submission =
      info.navigation_type == blink::kWebNavigationTypeFormSubmitted ||
      info.navigation_type == blink::kWebNavigationTypeFormResubmitted;

  BeginNavigationParams begin_navigation_params(
      GetWebURLRequestHeaders(info.url_request), load_flags,
      info.url_request.HasUserGesture(),
      info.url_request.GetServiceWorkerMode() !=
          blink::WebURLRequest::ServiceWorkerMode::kAll,
      GetRequestContextTypeForWebURLRequest(info.url_request),
      GetMixedContentContextTypeForWebURLRequest(info.url_request),
      is_form_submission, initiator_origin);

  if (!info.form.IsNull()) {
    WebSearchableFormData web_searchable_form_data(info.form);
    begin_navigation_params.searchable_form_url =
        web_searchable_form_data.Url();
    begin_navigation_params.searchable_form_encoding =
        web_searchable_form_data.Encoding().Utf8();
  }

  if (info.is_client_redirect)
    begin_navigation_params.client_side_redirect_url =
        frame_->GetDocument().Url();

  Send(new FrameHostMsg_BeginNavigation(
      routing_id_, MakeCommonNavigationParams(info, load_flags),
      begin_navigation_params));
}

void RenderFrameImpl::LoadDataURL(
    const CommonNavigationParams& params,
    const RequestNavigationParams& request_params,
    WebLocalFrame* frame,
    blink::WebFrameLoadType load_type,
    blink::WebHistoryItem item_for_history_navigation,
    blink::WebHistoryLoadType history_load_type,
    bool is_client_redirect) {
  // A loadData request with a specified base URL.
  GURL data_url = params.url;
#if defined(OS_ANDROID)
  if (!request_params.data_url_as_string.empty()) {
#if DCHECK_IS_ON()
    {
      std::string mime_type, charset, data;
      DCHECK(net::DataURL::Parse(data_url, &mime_type, &charset, &data));
      DCHECK(data.empty());
    }
#endif
    data_url = GURL(request_params.data_url_as_string);
    if (!data_url.is_valid() || !data_url.SchemeIs(url::kDataScheme)) {
      data_url = params.url;
    }
  }
#endif
  std::string mime_type, charset, data;
  if (net::DataURL::Parse(data_url, &mime_type, &charset, &data)) {
    const GURL base_url = params.base_url_for_data_url.is_empty() ?
        params.url : params.base_url_for_data_url;
    bool replace = load_type == WebFrameLoadType::kReloadBypassingCache ||
                   load_type == WebFrameLoadType::kReload;

    frame->LoadData(
        WebData(data.c_str(), data.length()), WebString::FromUTF8(mime_type),
        WebString::FromUTF8(charset), base_url,
        // Needed so that history-url-only changes don't become reloads.
        params.history_url_for_data_url, replace, load_type,
        item_for_history_navigation, history_load_type, is_client_redirect);
  } else {
    CHECK(false) << "Invalid URL passed: "
                 << params.url.possibly_invalid_spec();
  }
}

void RenderFrameImpl::SendUpdateState() {
  if (current_history_item_.IsNull())
    return;

  Send(new FrameHostMsg_UpdateState(
      routing_id_, SingleHistoryItemToPageState(current_history_item_)));
}

void RenderFrameImpl::MaybeEnableMojoBindings() {
  // BINDINGS_POLICY_WEB_UI, BINDINGS_POLICY_MOJO and BINDINGS_POLICY_HEADLESS
  // are mutually exclusive. They provide access to Mojo bindings, but do so in
  // incompatible ways.
  const int kAllBindingsTypes =
      BINDINGS_POLICY_WEB_UI | BINDINGS_POLICY_MOJO | BINDINGS_POLICY_HEADLESS;

  // Make sure that at most one of BINDINGS_POLICY_WEB_UI, BINDINGS_POLICY_MOJO
  // and BINDINGS_POLICY_HEADLESS have been set.
  // NOTE x & (x - 1) == 0 is true iff x is zero or a power of two.
  DCHECK_EQ((enabled_bindings_ & kAllBindingsTypes) &
                ((enabled_bindings_ & kAllBindingsTypes) - 1),
            0);

  DCHECK_EQ(RenderProcess::current()->GetEnabledBindings(), enabled_bindings_);

  // If an MojoBindingsController already exists for this RenderFrameImpl, avoid
  // creating another one. It is not kept as a member, as it deletes itself when
  // the frame is destroyed.
  if (RenderFrameObserverTracker<MojoBindingsController>::Get(this))
    return;

  if (IsMainFrame() && enabled_bindings_ & BINDINGS_POLICY_WEB_UI) {
    new MojoBindingsController(this, MojoBindingsType::FOR_WEB_UI);
  } else if (enabled_bindings_ & BINDINGS_POLICY_MOJO) {
    new MojoBindingsController(this, MojoBindingsType::FOR_LAYOUT_TESTS);
  } else if (enabled_bindings_ & BINDINGS_POLICY_HEADLESS) {
    new MojoBindingsController(this, MojoBindingsType::FOR_HEADLESS);
  }
}

void RenderFrameImpl::SendFailedProvisionalLoad(
    const blink::WebURLRequest& request,
    const blink::WebURLError& error,
    blink::WebLocalFrame* frame) {
  bool show_repost_interstitial =
      (error.reason == net::ERR_CACHE_MISS &&
       base::EqualsASCII(request.HttpMethod().Utf16(), "POST"));

  FrameHostMsg_DidFailProvisionalLoadWithError_Params params;
  params.error_code = error.reason;
  GetContentClient()->renderer()->GetNavigationErrorStrings(
      this, request, error, nullptr, &params.error_description);
  params.url = error.unreachable_url;
  params.showing_repost_interstitial = show_repost_interstitial;
  params.was_ignored_by_handler = error.was_ignored_by_handler;
  Send(new FrameHostMsg_DidFailProvisionalLoadWithError(routing_id_, params));
}

bool RenderFrameImpl::ShouldDisplayErrorPageForFailedLoad(
    int error_code,
    const GURL& unreachable_url) {
  // Don't display an error page if this is simply a cancelled load.  Aside
  // from being dumb, Blink doesn't expect it and it will cause a crash.
  if (error_code == net::ERR_ABORTED)
    return false;

  // Don't display "client blocked" error page if browser has asked us not to.
  if (error_code == net::ERR_BLOCKED_BY_CLIENT &&
      render_view_->renderer_preferences_.disable_client_blocked_error_page) {
    return false;
  }

  // Allow the embedder to suppress an error page.
  if (GetContentClient()->renderer()->ShouldSuppressErrorPage(
          this, unreachable_url)) {
    return false;
  }

  return true;
}

GURL RenderFrameImpl::GetLoadingUrl() const {
  WebDataSource* ds = frame_->DataSource();

  GURL overriden_url;
  if (MaybeGetOverriddenURL(ds, &overriden_url))
    return overriden_url;

  const WebURLRequest& request = ds->GetRequest();
  return request.Url();
}

void RenderFrameImpl::PopulateDocumentStateFromPending(
    DocumentState* document_state) {
  InternalDocumentStateData* internal_data =
      InternalDocumentStateData::FromDocumentState(document_state);

  if (!pending_navigation_params_->common_params.url.SchemeIs(
          url::kJavaScriptScheme) &&
      pending_navigation_params_->common_params.navigation_type ==
          FrameMsg_Navigate_Type::RESTORE) {
    // We're doing a load of a page that was restored from the last session.
    // By default this prefers the cache over loading
    // (LOAD_SKIP_CACHE_VALIDATION) which can result in stale data for pages
    // that are set to expire. We explicitly override that by setting the
    // policy here so that as necessary we load from the network.
    //
    // TODO(davidben): Remove this in favor of passing a cache policy to the
    // loadHistoryItem call in OnNavigate. That requires not overloading
    // UseProtocolCachePolicy to mean both "normal load" and "determine cache
    // policy based on load type, etc".
    internal_data->set_cache_policy_override(
        WebCachePolicy::kUseProtocolCachePolicy);
  }

  internal_data->set_is_overriding_user_agent(
      pending_navigation_params_->request_params.is_overriding_user_agent);
  internal_data->set_must_reset_scroll_and_scale_state(
      pending_navigation_params_->common_params.navigation_type ==
      FrameMsg_Navigate_Type::RELOAD_ORIGINAL_REQUEST_URL);
  document_state->set_can_load_local_resources(
      pending_navigation_params_->request_params.can_load_local_resources);
}

NavigationState* RenderFrameImpl::CreateNavigationStateFromPending() {
  if (IsBrowserInitiated(pending_navigation_params_.get())) {
    return NavigationStateImpl::CreateBrowserInitiated(
        pending_navigation_params_->common_params,
        pending_navigation_params_->start_params,
        pending_navigation_params_->request_params);
  }
  return NavigationStateImpl::CreateContentInitiated();
}

void RenderFrameImpl::UpdateNavigationState(DocumentState* document_state,
                                            bool was_within_same_page,
                                            bool content_initiated) {
  // If this was a browser-initiated navigation, then there could be pending
  // navigation params, so use them. Otherwise, just reset the document state
  // here, since if pending navigation params exist they are for some other
  // navigation <https://crbug.com/597239>.
  if (!pending_navigation_params_ || content_initiated) {
    document_state->set_navigation_state(
        NavigationStateImpl::CreateContentInitiated());
    return;
  }

  DCHECK(!pending_navigation_params_->common_params.navigation_start.is_null());
  document_state->set_navigation_state(CreateNavigationStateFromPending());

  // The |set_was_load_data_with_base_url_request| state should not change for
  // an in-page navigation, so skip updating it from the in-page navigation
  // params in this case.
  if (!was_within_same_page) {
    const CommonNavigationParams& common_params =
        pending_navigation_params_->common_params;
    bool load_data = !common_params.base_url_for_data_url.is_empty() &&
                     !common_params.history_url_for_data_url.is_empty() &&
                     common_params.url.SchemeIs(url::kDataScheme);
    document_state->set_was_load_data_with_base_url_request(load_data);
    if (load_data)
      document_state->set_data_url(common_params.url);
  }

  pending_navigation_params_.reset();
}

#if defined(OS_ANDROID)
RendererMediaPlayerManager* RenderFrameImpl::GetMediaPlayerManager() {
  if (!media_player_manager_)
    media_player_manager_ = new RendererMediaPlayerManager(this);
  return media_player_manager_;
}
#endif  // defined(OS_ANDROID)

media::MediaPermission* RenderFrameImpl::GetMediaPermission() {
  if (!media_permission_dispatcher_) {
    media_permission_dispatcher_.reset(new MediaPermissionDispatcher(base::Bind(
        &RenderFrameImpl::GetInterface<blink::mojom::PermissionService>,
        base::Unretained(this))));
  }
  return media_permission_dispatcher_.get();
}

#if defined(ENABLE_MOJO_MEDIA)
service_manager::mojom::InterfaceProvider*
RenderFrameImpl::GetMediaInterfaceProvider() {
  if (!media_interface_provider_) {
    media_interface_provider_.reset(
        new MediaInterfaceProvider(GetRemoteInterfaces()));
  }

  return media_interface_provider_.get();
}
#endif  // defined(ENABLE_MOJO_MEDIA)

bool RenderFrameImpl::AreSecureCodecsSupported() {
#if defined(OS_ANDROID)
  // Hardware-secure codecs are only supported if secure surfaces are enabled.
  return render_view_->renderer_preferences_
      .use_video_overlay_for_embedded_encrypted_video;
#else
  return false;
#endif  // defined(OS_ANDROID)
}

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
media::mojom::RemoterFactory* RenderFrameImpl::GetRemoterFactory() {
  if (!remoter_factory_)
    GetRemoteInterfaces()->GetInterface(&remoter_factory_);
  return remoter_factory_.get();
}
#endif

media::CdmFactory* RenderFrameImpl::GetCdmFactory() {
  if (cdm_factory_)
    return cdm_factory_.get();

#if defined(ENABLE_MOJO_CDM)
  cdm_factory_.reset(new media::MojoCdmFactory(GetMediaInterfaceProvider()));
  return cdm_factory_.get();
#endif  //  defined(ENABLE_MOJO_CDM)

#if BUILDFLAG(ENABLE_PEPPER_CDMS)
  DCHECK(frame_);
  cdm_factory_.reset(
      new RenderCdmFactory(base::Bind(&PepperCdmWrapperImpl::Create, frame_)));
#endif  // BUILDFLAG(ENABLE_PEPPER_CDMS)

#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  cdm_factory_.reset(new media::remoting::RemotingCdmFactory(
      std::move(cdm_factory_), GetRemoterFactory(),
      std::move(remoting_sink_observer_)));
#endif  // BUILDFLAG(ENABLE_MEDIA_REMOTING)

  return cdm_factory_.get();
}

media::DecoderFactory* RenderFrameImpl::GetDecoderFactory() {
#if defined(ENABLE_MOJO_AUDIO_DECODER) || defined(ENABLE_MOJO_VIDEO_DECODER)
  if (!decoder_factory_) {
    decoder_factory_.reset(
        new media::MojoDecoderFactory(GetMediaInterfaceProvider()));
  }
#endif
  return decoder_factory_.get();
}

#if BUILDFLAG(ENABLE_PLUGINS)
void RenderFrameImpl::HandlePepperImeCommit(const base::string16& text) {
  if (text.empty())
    return;

  if (!IsPepperAcceptingCompositionEvents()) {
    // For pepper plugins unable to handle IME events, send the plugin a
    // sequence of characters instead.
    base::i18n::UTF16CharIterator iterator(&text);
    int32_t i = 0;
    while (iterator.Advance()) {
      blink::WebKeyboardEvent char_event(
          blink::WebInputEvent::kChar, blink::WebInputEvent::kNoModifiers,
          ui::EventTimeStampToSeconds(ui::EventTimeForNow()));
      char_event.windows_key_code = text[i];
      char_event.native_key_code = text[i];

      const int32_t char_start = i;
      for (; i < iterator.array_pos(); ++i) {
        char_event.text[i - char_start] = text[i];
        char_event.unmodified_text[i - char_start] = text[i];
      }

      if (GetRenderWidget()->GetWebWidget())
        GetRenderWidget()->GetWebWidget()->HandleInputEvent(
            blink::WebCoalescedInputEvent(char_event));
    }
  } else {
    // Mimics the order of events sent by WebKit.
    // See WebCore::Editor::setComposition() for the corresponding code.
    focused_pepper_plugin_->HandleCompositionEnd(text);
    focused_pepper_plugin_->HandleTextInput(text);
  }
  pepper_composition_text_.clear();
}
#endif  // ENABLE_PLUGINS

void RenderFrameImpl::RegisterMojoInterfaces() {
  GetAssociatedInterfaceRegistry()->AddInterface(
      base::Bind(&RenderFrameImpl::BindEngagement, weak_factory_.GetWeakPtr()));

  GetAssociatedInterfaceRegistry()->AddInterface(base::Bind(
      &RenderFrameImpl::BindFrameBindingsControl, weak_factory_.GetWeakPtr()));

  if (!frame_->Parent()) {
    // Only main frame have ImageDownloader service.
    GetInterfaceRegistry()->AddInterface(base::Bind(
        &ImageDownloaderImpl::CreateMojoService, base::Unretained(this)));

    // Host zoom is per-page, so only added on the main frame.
    GetAssociatedInterfaceRegistry()->AddInterface(base::Bind(
        &RenderFrameImpl::OnHostZoomClientRequest, weak_factory_.GetWeakPtr()));
  }
}

template <typename Interface>
void RenderFrameImpl::GetInterface(mojo::InterfaceRequest<Interface> request) {
  GetRemoteInterfaces()->GetInterface(std::move(request));
}

void RenderFrameImpl::OnHostZoomClientRequest(
    mojom::HostZoomAssociatedRequest request) {
  DCHECK(!host_zoom_binding_.is_bound());
  host_zoom_binding_.Bind(std::move(request));
}

media::RendererWebMediaPlayerDelegate*
RenderFrameImpl::GetWebMediaPlayerDelegate() {
  if (!media_player_delegate_)
    media_player_delegate_ = new media::RendererWebMediaPlayerDelegate(this);
  return media_player_delegate_;
}

void RenderFrameImpl::CheckIfAudioSinkExistsAndIsAuthorized(
    const blink::WebString& sink_id,
    const blink::WebSecurityOrigin& security_origin,
    blink::WebSetSinkIdCallbacks* web_callbacks) {
  media::OutputDeviceStatusCB callback =
      media::ConvertToOutputDeviceStatusCB(web_callbacks);
  callback.Run(AudioDeviceFactory::GetOutputDeviceInfo(
                   routing_id_, 0, sink_id.Utf8(), security_origin)
                   .device_status());
}

blink::WebPageVisibilityState RenderFrameImpl::VisibilityState() const {
  const RenderFrameImpl* local_root = GetLocalRoot();
  blink::WebPageVisibilityState current_state =
      local_root->render_widget_->is_hidden()
          ? blink::kWebPageVisibilityStateHidden
          : blink::kWebPageVisibilityStateVisible;
  blink::WebPageVisibilityState override_state = current_state;
  if (GetContentClient()->renderer()->ShouldOverridePageVisibilityState(
          this, &override_state))
    return override_state;
  return current_state;
}

blink::WebPageVisibilityState RenderFrameImpl::GetVisibilityState() const {
  return VisibilityState();
}

bool RenderFrameImpl::IsBrowserSideNavigationPending() {
  return browser_side_navigation_pending_;
}

base::SingleThreadTaskRunner* RenderFrameImpl::GetTimerTaskRunner() {
  return GetWebFrame()->TimerTaskRunner();
}

base::SingleThreadTaskRunner* RenderFrameImpl::GetLoadingTaskRunner() {
  return GetWebFrame()->LoadingTaskRunner();
}

base::SingleThreadTaskRunner* RenderFrameImpl::GetUnthrottledTaskRunner() {
  return GetWebFrame()->UnthrottledTaskRunner();
}

int RenderFrameImpl::GetEnabledBindings() const {
  return enabled_bindings_;
}

blink::WebPlugin* RenderFrameImpl::GetWebPluginForFind() {
  if (frame_->GetDocument().IsPluginDocument())
    return frame_->GetDocument().To<WebPluginDocument>().Plugin();

#if BUILDFLAG(ENABLE_PLUGINS)
  if (plugin_find_handler_)
    return plugin_find_handler_->container()->Plugin();
#endif

  return nullptr;
}

void RenderFrameImpl::SendFindReply(int request_id,
                                    int match_count,
                                    int ordinal,
                                    const WebRect& selection_rect,
                                    bool final_status_update) {
  DCHECK(ordinal >= -1);

  Send(new FrameHostMsg_Find_Reply(routing_id_,
                                   request_id,
                                   match_count,
                                   selection_rect,
                                   ordinal,
                                   final_status_update));
}

#if BUILDFLAG(ENABLE_PLUGINS)
void RenderFrameImpl::PepperInstanceCreated(
    PepperPluginInstanceImpl* instance) {
  active_pepper_instances_.insert(instance);

  Send(new FrameHostMsg_PepperInstanceCreated(
      routing_id_, instance->pp_instance()));
}

void RenderFrameImpl::PepperInstanceDeleted(
    PepperPluginInstanceImpl* instance) {
  active_pepper_instances_.erase(instance);

  if (pepper_last_mouse_event_target_ == instance)
    pepper_last_mouse_event_target_ = nullptr;
  if (focused_pepper_plugin_ == instance)
    PepperFocusChanged(instance, false);

  RenderFrameImpl* const render_frame = instance->render_frame();
  if (render_frame) {
    render_frame->Send(
        new FrameHostMsg_PepperInstanceDeleted(
            render_frame->GetRoutingID(),
            instance->pp_instance()));
  }
}

void RenderFrameImpl::PepperFocusChanged(PepperPluginInstanceImpl* instance,
                                         bool focused) {
  if (focused)
    focused_pepper_plugin_ = instance;
  else if (focused_pepper_plugin_ == instance)
    focused_pepper_plugin_ = nullptr;

  GetRenderWidget()->set_focused_pepper_plugin(focused_pepper_plugin_);

  GetRenderWidget()->UpdateTextInputState();
  GetRenderWidget()->UpdateSelectionBounds();
}

void RenderFrameImpl::PepperStartsPlayback(PepperPluginInstanceImpl* instance) {
  RenderFrameImpl* const render_frame = instance->render_frame();
  if (render_frame) {
    render_frame->Send(
        new FrameHostMsg_PepperStartsPlayback(
            render_frame->GetRoutingID(),
            instance->pp_instance()));
  }
}

void RenderFrameImpl::PepperStopsPlayback(PepperPluginInstanceImpl* instance) {
  RenderFrameImpl* const render_frame = instance->render_frame();
  if (render_frame) {
    render_frame->Send(
        new FrameHostMsg_PepperStopsPlayback(
            render_frame->GetRoutingID(),
            instance->pp_instance()));
  }
}

void RenderFrameImpl::OnSetPepperVolume(int32_t pp_instance, double volume) {
  PepperPluginInstanceImpl* instance = static_cast<PepperPluginInstanceImpl*>(
      PepperPluginInstance::Get(pp_instance));
  if (instance)
    instance->audio_controller().SetVolume(volume);
}
#endif  // ENABLE_PLUGINS

void RenderFrameImpl::ShowCreatedWindow(bool opened_by_user_gesture,
                                        RenderWidget* render_widget_to_show,
                                        WebNavigationPolicy policy,
                                        const gfx::Rect& initial_rect) {
  // |render_widget_to_show| is the main RenderWidget for a pending window
  // created by this object, but not yet shown. The tab is currently offscreen,
  // and still owned by the opener. Sending |FrameHostMsg_ShowCreatedWindow|
  // will move it off the opener's pending list, and put it in its own tab or
  // window.
  //
  // This call happens only for renderer-created windows; for example, when a
  // tab is created by script via window.open().
  Send(new FrameHostMsg_ShowCreatedWindow(
      GetRoutingID(), render_widget_to_show->routing_id(),
      RenderViewImpl::NavigationPolicyToDisposition(policy), initial_rect,
      opened_by_user_gesture));
}

void RenderFrameImpl::RenderWidgetSetFocus(bool enable) {
#if BUILDFLAG(ENABLE_PLUGINS)
  // Notify all Pepper plugins.
  for (auto* plugin : active_pepper_instances_)
    plugin->SetContentAreaFocus(enable);
#endif
}

void RenderFrameImpl::RenderWidgetWillHandleMouseEvent() {
#if BUILDFLAG(ENABLE_PLUGINS)
  // This method is called for every mouse event that the RenderWidget receives.
  // And then the mouse event is forwarded to blink, which dispatches it to the
  // event target. Potentially a Pepper plugin will receive the event.
  // In order to tell whether a plugin gets the last mouse event and which it
  // is, we set |pepper_last_mouse_event_target_| to null here. If a plugin gets
  // the event, it will notify us via DidReceiveMouseEvent() and set itself as
  // |pepper_last_mouse_event_target_|.
  pepper_last_mouse_event_target_ = nullptr;
#endif
}

RenderFrameImpl::PendingNavigationInfo::PendingNavigationInfo(
    const NavigationPolicyInfo& info)
    : navigation_type(info.navigation_type),
      policy(info.default_policy),
      replaces_current_history_item(info.replaces_current_history_item),
      history_navigation_in_new_child_frame(
          info.is_history_navigation_in_new_child_frame),
      client_redirect(info.is_client_redirect),
      cache_disabled(info.is_cache_disabled),
      form(info.form),
      source_location(info.source_location) {}

}  // namespace content
