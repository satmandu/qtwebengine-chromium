// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/runtime_features.h"

#include <vector>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "content/common/content_switches_internal.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "services/device/public/cpp/device_features.h"
#include "third_party/WebKit/public/web/WebRuntimeFeatures.h"
#include "ui/gl/gl_switches.h"
#include "ui/native_theme/native_theme_features.h"

using blink::WebRuntimeFeatures;

namespace content {

static void SetRuntimeFeatureDefaultsForPlatform() {
#if defined(OS_ANDROID)
  // Android does not have support for PagePopup
  WebRuntimeFeatures::EnablePagePopup(false);
  // No plan to support complex UI for date/time INPUT types.
  WebRuntimeFeatures::EnableInputMultipleFieldsUI(false);
  // Android does not yet support SharedWorker. crbug.com/154571
  WebRuntimeFeatures::EnableSharedWorker(false);
  // Android does not yet support NavigatorContentUtils.
  WebRuntimeFeatures::EnableNavigatorContentUtils(false);
  WebRuntimeFeatures::EnableOrientationEvent(true);
  WebRuntimeFeatures::EnableFastMobileScrolling(true);
  WebRuntimeFeatures::EnableMediaCapture(true);
  // Android won't be able to reliably support non-persistent notifications, the
  // intended behavior for which is in flux by itself.
  WebRuntimeFeatures::EnableNotificationConstructor(false);
  // Android does not yet support switching of audio output devices
  WebRuntimeFeatures::EnableAudioOutputDevices(false);
  WebRuntimeFeatures::EnableAutoplayMutedVideos(true);
  // Android does not yet support SystemMonitor.
  WebRuntimeFeatures::EnableOnDeviceChange(false);
  WebRuntimeFeatures::EnableMediaSession(true);
  WebRuntimeFeatures::EnableMediaControlsOverlayPlayButton(true);
#else  // defined(OS_ANDROID)
  WebRuntimeFeatures::EnableNavigatorContentUtils(true);
  if (base::FeatureList::IsEnabled(
          features::kCrossOriginMediaPlaybackRequiresUserGesture)) {
    WebRuntimeFeatures::EnableAutoplayMutedVideos(true);
  }
#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID) || defined(USE_AURA)
  WebRuntimeFeatures::EnableCompositedSelectionUpdate(true);
#endif

#if !(defined OS_ANDROID || defined OS_CHROMEOS)
    // Only Android, ChromeOS support NetInfo right now.
  WebRuntimeFeatures::EnableNetworkInformation(false);
#endif

// Web Bluetooth is shipped on Android, ChromeOS & MacOS, experimental
// otherwise.
#if defined(OS_CHROMEOS) || defined(OS_ANDROID) || defined(OS_MACOSX)
  WebRuntimeFeatures::EnableWebBluetooth(true);
#endif

#if defined(OS_CHROMEOS)
  WebRuntimeFeatures::EnableForceTallerSelectPopup(true);
#endif

// The Notification Center on Mac OS X does not support content images.
#if !defined(OS_MACOSX)
  WebRuntimeFeatures::EnableNotificationContentImage(true);
#endif
}

void SetRuntimeFeaturesDefaultsAndUpdateFromArgs(
    const base::CommandLine& command_line) {
  bool enableExperimentalWebPlatformFeatures = command_line.HasSwitch(
      switches::kEnableExperimentalWebPlatformFeatures);
  if (enableExperimentalWebPlatformFeatures)
    WebRuntimeFeatures::EnableExperimentalFeatures(true);

  WebRuntimeFeatures::EnableOriginTrials(
      base::FeatureList::IsEnabled(features::kOriginTrials));

  WebRuntimeFeatures::EnableFeaturePolicy(
      base::FeatureList::IsEnabled(features::kFeaturePolicy));

  if (!base::FeatureList::IsEnabled(features::kWebUsb))
    WebRuntimeFeatures::EnableWebUsb(false);

  SetRuntimeFeatureDefaultsForPlatform();

  if (command_line.HasSwitch(switches::kDisableDatabases))
    WebRuntimeFeatures::EnableDatabase(false);

  if (command_line.HasSwitch(switches::kDisableNotifications)) {
    WebRuntimeFeatures::EnableNotifications(false);

    // Chrome's Push Messaging implementation relies on Web Notifications.
    WebRuntimeFeatures::EnablePushMessaging(false);
  }

  if (!base::FeatureList::IsEnabled(features::kNotificationContentImage))
    WebRuntimeFeatures::EnableNotificationContentImage(false);

  // For the time being, wasm serialization is separately controlled
  // by this flag. WebAssembly APIs and compilation is now enabled
  // unconditionally in V8.
  if (base::FeatureList::IsEnabled(features::kWebAssembly))
    WebRuntimeFeatures::EnableWebAssemblySerialization(true);

  WebRuntimeFeatures::EnableSharedArrayBuffer(
      base::FeatureList::IsEnabled(features::kSharedArrayBuffer));

  if (command_line.HasSwitch(switches::kDisableSharedWorkers))
    WebRuntimeFeatures::EnableSharedWorker(false);

  if (command_line.HasSwitch(switches::kDisableSpeechAPI))
    WebRuntimeFeatures::EnableScriptedSpeech(false);

  if (command_line.HasSwitch(switches::kDisableFileSystem))
    WebRuntimeFeatures::EnableFileSystem(false);

  if (command_line.HasSwitch(switches::kEnableExperimentalCanvasFeatures))
    WebRuntimeFeatures::EnableExperimentalCanvasFeatures(true);

  if (!command_line.HasSwitch(switches::kDisableAcceleratedJpegDecoding))
    WebRuntimeFeatures::EnableDecodeToYUV(true);

  if (command_line.HasSwitch(switches::kEnableDisplayList2dCanvas))
    WebRuntimeFeatures::EnableDisplayList2dCanvas(true);

  if (command_line.HasSwitch(switches::kDisableDisplayList2dCanvas))
    WebRuntimeFeatures::EnableDisplayList2dCanvas(false);

  if (command_line.HasSwitch(switches::kForceDisplayList2dCanvas))
    WebRuntimeFeatures::ForceDisplayList2dCanvas(true);

  if (command_line.HasSwitch(
      switches::kEnableCanvas2dDynamicRenderingModeSwitching))
    WebRuntimeFeatures::EnableCanvas2dDynamicRenderingModeSwitching(true);

  if (command_line.HasSwitch(switches::kEnableWebGLDraftExtensions))
    WebRuntimeFeatures::EnableWebGLDraftExtensions(true);

#if defined(OS_MACOSX)
  bool enable_canvas_2d_image_chromium = command_line.HasSwitch(
      switches::kEnableGpuMemoryBufferCompositorResources) &&
      !command_line.HasSwitch(switches::kDisable2dCanvasImageChromium) &&
      !command_line.HasSwitch(switches::kDisableGpu);

  if (enable_canvas_2d_image_chromium) {
    enable_canvas_2d_image_chromium =
        base::FeatureList::IsEnabled(features::kCanvas2DImageChromium);
  }
#else
  bool enable_canvas_2d_image_chromium = false;
#endif
  WebRuntimeFeatures::EnableCanvas2dImageChromium(
      enable_canvas_2d_image_chromium);

#if defined(OS_MACOSX)
  bool enable_web_gl_image_chromium = command_line.HasSwitch(
      switches::kEnableGpuMemoryBufferCompositorResources) &&
      !command_line.HasSwitch(switches::kDisableWebGLImageChromium) &&
      !command_line.HasSwitch(switches::kDisableGpu);

  if (enable_web_gl_image_chromium) {
    enable_web_gl_image_chromium =
        base::FeatureList::IsEnabled(features::kWebGLImageChromium);
  }
#else
  bool enable_web_gl_image_chromium =
      command_line.HasSwitch(switches::kEnableWebGLImageChromium);
#endif
  WebRuntimeFeatures::EnableWebGLImageChromium(enable_web_gl_image_chromium);

  if (command_line.HasSwitch(switches::kForceOverlayFullscreenVideo))
    WebRuntimeFeatures::ForceOverlayFullscreenVideo(true);

  if (ui::IsOverlayScrollbarEnabled())
    WebRuntimeFeatures::EnableOverlayScrollbars(true);

  if (command_line.HasSwitch(switches::kEnablePreciseMemoryInfo))
    WebRuntimeFeatures::EnablePreciseMemoryInfo(true);

  if (command_line.HasSwitch(switches::kEnablePrintBrowser))
    WebRuntimeFeatures::EnablePrintBrowser(true);

  if (command_line.HasSwitch(switches::kEnableNetworkInformation) ||
      enableExperimentalWebPlatformFeatures) {
    WebRuntimeFeatures::EnableNetworkInformation(true);
  }

  if (!base::FeatureList::IsEnabled(features::kCredentialManagementAPI))
    WebRuntimeFeatures::EnableCredentialManagerAPI(false);

  if (command_line.HasSwitch(switches::kReducedReferrerGranularity))
    WebRuntimeFeatures::EnableReducedReferrerGranularity(true);

  if (command_line.HasSwitch(switches::kRootLayerScrolls))
    WebRuntimeFeatures::EnableRootLayerScrolling(true);

  if (command_line.HasSwitch(switches::kDisablePermissionsAPI))
    WebRuntimeFeatures::EnablePermissionsAPI(false);

  if (command_line.HasSwitch(switches::kDisableV8IdleTasks))
    WebRuntimeFeatures::EnableV8IdleTasks(false);
  else
    WebRuntimeFeatures::EnableV8IdleTasks(true);

  if (command_line.HasSwitch(switches::kEnableWebVR))
    WebRuntimeFeatures::EnableWebVR(true);

  WebRuntimeFeatures::EnableWebVRExperimentalRendering(
      base::FeatureList::IsEnabled(features::kWebVRExperimentalRendering));

  if (command_line.HasSwitch(switches::kDisablePresentationAPI))
    WebRuntimeFeatures::EnablePresentationAPI(false);

  if (command_line.HasSwitch(switches::kDisableRemotePlaybackAPI))
    WebRuntimeFeatures::EnableRemotePlaybackAPI(false);

  const std::string webfonts_intervention_v2_group_name =
      base::FieldTrialList::FindFullName("WebFontsInterventionV2");
  const std::string webfonts_intervention_v2_about_flag =
      command_line.GetSwitchValueASCII(switches::kEnableWebFontsInterventionV2);
  if (!webfonts_intervention_v2_about_flag.empty()) {
    WebRuntimeFeatures::EnableWebFontsInterventionV2With2G(
        webfonts_intervention_v2_about_flag.compare(
            switches::kEnableWebFontsInterventionV2SwitchValueEnabledWith2G) ==
        0);
    WebRuntimeFeatures::EnableWebFontsInterventionV2With3G(
        webfonts_intervention_v2_about_flag.compare(
            switches::kEnableWebFontsInterventionV2SwitchValueEnabledWith3G) ==
        0);
    WebRuntimeFeatures::EnableWebFontsInterventionV2WithSlow2G(
        webfonts_intervention_v2_about_flag.compare(
            switches::
                kEnableWebFontsInterventionV2SwitchValueEnabledWithSlow2G) ==
        0);
  } else {
    WebRuntimeFeatures::EnableWebFontsInterventionV2With2G(base::StartsWith(
        webfonts_intervention_v2_group_name,
        switches::kEnableWebFontsInterventionV2SwitchValueEnabledWith2G,
        base::CompareCase::INSENSITIVE_ASCII));
    WebRuntimeFeatures::EnableWebFontsInterventionV2With3G(base::StartsWith(
        webfonts_intervention_v2_group_name,
        switches::kEnableWebFontsInterventionV2SwitchValueEnabledWith3G,
        base::CompareCase::INSENSITIVE_ASCII));
    WebRuntimeFeatures::EnableWebFontsInterventionV2WithSlow2G(base::StartsWith(
        webfonts_intervention_v2_group_name,
        switches::kEnableWebFontsInterventionV2SwitchValueEnabledWithSlow2G,
        base::CompareCase::INSENSITIVE_ASCII));
  }
  if (command_line.HasSwitch(switches::kEnableWebFontsInterventionTrigger))
    WebRuntimeFeatures::EnableWebFontsInterventionTrigger(true);

  WebRuntimeFeatures::EnableScrollAnchoring(
      base::FeatureList::IsEnabled(features::kScrollAnchoring) ||
      enableExperimentalWebPlatformFeatures);

  if (command_line.HasSwitch(switches::kEnableSlimmingPaintV2))
    WebRuntimeFeatures::EnableSlimmingPaintV2(true);

  WebRuntimeFeatures::EnableSlimmingPaintInvalidation(
      base::FeatureList::IsEnabled(features::kSlimmingPaintInvalidation));

  if (command_line.HasSwitch(switches::kEnableSlimmingPaintInvalidation))
    WebRuntimeFeatures::EnableSlimmingPaintInvalidation(true);

  if (command_line.HasSwitch(switches::kDisableSlimmingPaintInvalidation))
    WebRuntimeFeatures::EnableSlimmingPaintInvalidation(false);

  if (base::FeatureList::IsEnabled(features::kDocumentWriteEvaluator))
    WebRuntimeFeatures::EnableDocumentWriteEvaluator(true);

  if (base::FeatureList::IsEnabled(features::kLazyParseCSS))
    WebRuntimeFeatures::EnableLazyParseCSS(true);

  WebRuntimeFeatures::EnableMediaDocumentDownloadButton(
      base::FeatureList::IsEnabled(features::kMediaDocumentDownloadButton));

  WebRuntimeFeatures::EnablePointerEvent(
      base::FeatureList::IsEnabled(features::kPointerEvents));

  WebRuntimeFeatures::EnablePassiveDocumentEventListeners(
      base::FeatureList::IsEnabled(features::kPassiveDocumentEventListeners));

  WebRuntimeFeatures::EnableFeatureFromString(
      "FontCacheScaling",
      base::FeatureList::IsEnabled(features::kFontCacheScaling));

  WebRuntimeFeatures::EnableFeatureFromString(
      "FramebustingNeedsSameOriginOrUserGesture",
      base::FeatureList::IsEnabled(
          features::kFramebustingNeedsSameOriginOrUserGesture));

  WebRuntimeFeatures::EnableFeatureFromString(
      "VibrateRequiresUserGesture",
      base::FeatureList::IsEnabled(features::kVibrateRequiresUserGesture));

  if (command_line.HasSwitch(switches::kDisableBackgroundTimerThrottling))
    WebRuntimeFeatures::EnableTimerThrottlingForBackgroundTabs(false);

  WebRuntimeFeatures::EnableExpensiveBackgroundTimerThrottling(
      base::FeatureList::IsEnabled(
          features::kExpensiveBackgroundTimerThrottling));

  if (base::FeatureList::IsEnabled(features::kHeapCompaction))
    WebRuntimeFeatures::EnableHeapCompaction(true);

  WebRuntimeFeatures::EnableRenderingPipelineThrottling(
      base::FeatureList::IsEnabled(features::kRenderingPipelineThrottling));

  WebRuntimeFeatures::EnableTimerThrottlingForHiddenFrames(
      base::FeatureList::IsEnabled(features::kTimerThrottlingForHiddenFrames));

  WebRuntimeFeatures::EnableTouchpadAndWheelScrollLatching(
      base::FeatureList::IsEnabled(features::kTouchpadAndWheelScrollLatching));

  if (base::FeatureList::IsEnabled(
          features::kSendBeaconThrowForBlobWithNonSimpleType))
    WebRuntimeFeatures::EnableSendBeaconThrowForBlobWithNonSimpleType(true);

  WebRuntimeFeatures::EnableAccessibilityObjectModel(
      base::FeatureList::IsEnabled(features::kAccessibilityObjectModel));

#if defined(OS_ANDROID)
  if (command_line.HasSwitch(switches::kDisableMediaSessionAPI))
    WebRuntimeFeatures::EnableMediaSession(false);

  WebRuntimeFeatures::EnablePaymentRequest(
      base::FeatureList::IsEnabled(features::kWebPayments));
#endif

  WebRuntimeFeatures::EnableServiceWorkerNavigationPreload(
      base::FeatureList::IsEnabled(features::kServiceWorkerNavigationPreload));

  if (base::FeatureList::IsEnabled(features::kGamepadExtensions))
    WebRuntimeFeatures::EnableGamepadExtensions(true);

  if (base::FeatureList::IsEnabled(features::kCompositeOpaqueFixedPosition))
    WebRuntimeFeatures::EnableFeatureFromString("CompositeOpaqueFixedPosition",
                                                true);

  if (!base::FeatureList::IsEnabled(features::kCompositeOpaqueScrollers))
    WebRuntimeFeatures::EnableFeatureFromString("CompositeOpaqueScrollers",
                                                false);

  if (base::FeatureList::IsEnabled(features::kGenericSensor))
    WebRuntimeFeatures::EnableGenericSensor(true);

  // Enable features which VrShell depends on.
  if (base::FeatureList::IsEnabled(features::kVrShell)) {
    WebRuntimeFeatures::EnableGamepadExtensions(true);
    WebRuntimeFeatures::EnableWebVR(true);
  }

  if (base::FeatureList::IsEnabled(features::kLoadingWithMojo))
    WebRuntimeFeatures::EnableLoadingWithMojo(true);

  if (!base::FeatureList::IsEnabled(features::kBlockCredentialedSubresources)) {
    WebRuntimeFeatures::EnableFeatureFromString("BlockCredentialedSubresources",
                                                false);
  }

  WebRuntimeFeatures::EnableLocationHardReload(
      base::FeatureList::IsEnabled(features::kLocationHardReload));

  // Enable explicitly enabled features, and then disable explicitly disabled
  // ones.
  if (command_line.HasSwitch(switches::kEnableBlinkFeatures)) {
    std::vector<std::string> enabled_features = base::SplitString(
        command_line.GetSwitchValueASCII(switches::kEnableBlinkFeatures),
        ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const std::string& feature : enabled_features)
      WebRuntimeFeatures::EnableFeatureFromString(feature, true);
  }
  if (command_line.HasSwitch(switches::kDisableBlinkFeatures)) {
    std::vector<std::string> disabled_features = base::SplitString(
        command_line.GetSwitchValueASCII(switches::kDisableBlinkFeatures),
        ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const std::string& feature : disabled_features)
      WebRuntimeFeatures::EnableFeatureFromString(feature, false);
  }
}

}  // namespace content
