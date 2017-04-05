// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/security_interstitials/core/safe_browsing_error_ui.h"

#include "base/i18n/time_formatting.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "components/google/core/browser/google_util.h"
#include "components/security_interstitials/core/common_string_util.h"
#include "components/security_interstitials/core/metrics_helper.h"
#include "components/strings/grit/components_strings.h"
#include "net/base/escape.h"
#include "ui/base/l10n/l10n_util.h"

namespace security_interstitials {
namespace {

// URL for the Help Center article on Safe Browsing warnings.
const char kLearnMore[] = "https://support.google.com/chrome/answer/99020";

// For malware interstitial pages, we link the problematic URL to Google's
// diagnostic page.
#if defined(GOOGLE_CHROME_BUILD)
const char kSbDiagnosticUrl[] =
    "https://www.google.com/safebrowsing/"
    "diagnostic?site=%s&client=googlechrome";
#else
const char kSbDiagnosticUrl[] =
    "https://www.google.com/safebrowsing/diagnostic?site=%s&client=chromium";
#endif

// Constants for the V4 phishing string upgrades.
const char kReportPhishingErrorUrl[] =
    "https://www.google.com/safebrowsing/report_error/";

void RecordExtendedReportingPrefChanged(bool report, bool is_scout) {
  if (is_scout) {
    UMA_HISTOGRAM_BOOLEAN(
        "SafeBrowsing.Pref.Scout.SetPref.SBER2Pref.SecurityInterstitial",
        report);
  } else {
    UMA_HISTOGRAM_BOOLEAN(
        "SafeBrowsing.Pref.Scout.SetPref.SBER1Pref.SecurityInterstitial",
        report);
  }
}

}  // namespace

SafeBrowsingErrorUI::SafeBrowsingErrorUI(
    const GURL& request_url,
    const GURL& main_frame_url,
    SBInterstitialReason reason,
    const SBErrorDisplayOptions& display_options,
    const std::string& app_locale,
    const base::Time& time_triggered,
    ControllerClient* controller)
    : request_url_(request_url),
      main_frame_url_(main_frame_url),
      interstitial_reason_(reason),
      display_options_(display_options),
      app_locale_(app_locale),
      time_triggered_(time_triggered),
      controller_(controller) {
  controller_->metrics_helper()->RecordUserDecision(MetricsHelper::SHOW);
  controller_->metrics_helper()->RecordUserInteraction(
      MetricsHelper::TOTAL_VISITS);
  if (display_options_.is_proceed_anyway_disabled)
    controller_->metrics_helper()->RecordUserDecision(
        security_interstitials::MetricsHelper::PROCEEDING_DISABLED);
}

SafeBrowsingErrorUI::~SafeBrowsingErrorUI() {
  controller_->metrics_helper()->RecordShutdownMetrics();
}

void SafeBrowsingErrorUI::PopulateStringsForHTML(
    base::DictionaryValue* load_time_data) {
  DCHECK(load_time_data);

  load_time_data->SetString("type", "SAFEBROWSING");
  load_time_data->SetString(
      "tabTitle", l10n_util::GetStringUTF16(IDS_SAFEBROWSING_V3_TITLE));
  load_time_data->SetString(
      "openDetails",
      l10n_util::GetStringUTF16(IDS_SAFEBROWSING_V3_OPEN_DETAILS_BUTTON));
  load_time_data->SetString(
      "closeDetails",
      l10n_util::GetStringUTF16(IDS_SAFEBROWSING_V3_CLOSE_DETAILS_BUTTON));
  load_time_data->SetString(
      "primaryButtonText",
      l10n_util::GetStringUTF16(IDS_SAFEBROWSING_OVERRIDABLE_SAFETY_BUTTON));
  load_time_data->SetBoolean("overridable",
                             !display_options_.is_proceed_anyway_disabled);
  security_interstitials::common_string_util::PopulateNewIconStrings(
      load_time_data);

  switch (interstitial_reason_) {
    case SB_REASON_MALWARE:
      PopulateMalwareLoadTimeData(load_time_data);
      break;
    case SB_REASON_HARMFUL:
      PopulateHarmfulLoadTimeData(load_time_data);
      break;
    case SB_REASON_PHISHING:
      PopulatePhishingLoadTimeData(load_time_data);
      break;
  }

  PopulateExtendedReportingOption(load_time_data);
}

void SafeBrowsingErrorUI::HandleCommand(SecurityInterstitialCommands command) {
  switch (command) {
    case CMD_PROCEED: {
      // User pressed on the button to proceed.
      if (!display_options_.is_proceed_anyway_disabled) {
        controller_->metrics_helper()->RecordUserDecision(
            MetricsHelper::PROCEED);
        controller_->Proceed();
        break;
      }
    }
    // If the user can't proceed, fall through to CMD_DONT_PROCEED.
    case CMD_DONT_PROCEED: {
      // User pressed on the button to return to safety.
      // Don't record the user action here because there are other ways of
      // triggering DontProceed, like clicking the back button.
      if (display_options_.is_main_frame_load_blocked) {
        // If the load is blocked, we want to close the interstitial and discard
        // the pending entry.
        controller_->GoBack();
      } else {
        // Otherwise the offending entry has committed, and we need to go back
        // or to a safe page.  We will close the interstitial when that page
        // commits.
        controller_->GoBackAfterNavigationCommitted();
      }
      break;
    }
    case CMD_DO_REPORT: {
      // User enabled SB Extended Reporting via the checkbox.
      display_options_.is_extended_reporting_enabled = true;
      controller_->SetReportingPreference(true);
      RecordExtendedReportingPrefChanged(
          true, display_options_.is_scout_reporting_enabled);
      break;
    }
    case CMD_DONT_REPORT: {
      // User disabled SB Extended Reporting via the checkbox.
      display_options_.is_extended_reporting_enabled = false;
      controller_->SetReportingPreference(false);
      RecordExtendedReportingPrefChanged(
          false, display_options_.is_scout_reporting_enabled);
      break;
    }
    case CMD_SHOW_MORE_SECTION: {
      controller_->metrics_helper()->RecordUserInteraction(
          security_interstitials::MetricsHelper::SHOW_ADVANCED);
      break;
    }
    case CMD_OPEN_HELP_CENTER: {
      // User pressed "Learn more".
      controller_->metrics_helper()->RecordUserInteraction(
          security_interstitials::MetricsHelper::SHOW_LEARN_MORE);
      GURL learn_more_url(kLearnMore);
      learn_more_url =
          google_util::AppendGoogleLocaleParam(learn_more_url, app_locale_);
      controller_->OpenUrlInCurrentTab(learn_more_url);
      break;
    }
    case CMD_RELOAD: {
      controller_->metrics_helper()->RecordUserInteraction(
          security_interstitials::MetricsHelper::RELOAD);
      controller_->Reload();
      break;
    }
    case CMD_OPEN_REPORTING_PRIVACY: {
      // User pressed on the SB Extended Reporting "privacy policy" link.
      controller_->OpenExtendedReportingPrivacyPolicy();
      break;
    }
    case CMD_OPEN_WHITEPAPER: {
      controller_->OpenExtendedReportingWhitepaper();
      break;
    }
    case CMD_OPEN_DIAGNOSTIC: {
      controller_->metrics_helper()->RecordUserInteraction(
          security_interstitials::MetricsHelper::SHOW_DIAGNOSTIC);
      std::string diagnostic = base::StringPrintf(
          kSbDiagnosticUrl,
          net::EscapeQueryParamValue(request_url_.spec(), true).c_str());
      GURL diagnostic_url(diagnostic);
      diagnostic_url =
          google_util::AppendGoogleLocaleParam(diagnostic_url, app_locale_);
      controller_->OpenUrlInCurrentTab(diagnostic_url);
      break;
    }
    case CMD_REPORT_PHISHING_ERROR: {
      controller_->metrics_helper()->RecordUserInteraction(
          security_interstitials::MetricsHelper::REPORT_PHISHING_ERROR);
      GURL phishing_error_url(kReportPhishingErrorUrl);
      phishing_error_url =
          google_util::AppendGoogleLocaleParam(phishing_error_url, app_locale_);
      controller_->OpenUrlInCurrentTab(phishing_error_url);
      break;
    }
    case CMD_OPEN_DATE_SETTINGS:
    case CMD_OPEN_LOGIN:
    case CMD_ERROR:
    case CMD_TEXT_FOUND:
    case CMD_TEXT_NOT_FOUND:
      break;
  }
}

bool SafeBrowsingErrorUI::CanShowExtendedReportingOption() {
  return !is_off_the_record() && is_extended_reporting_opt_in_allowed();
}

void SafeBrowsingErrorUI::PopulateMalwareLoadTimeData(
    base::DictionaryValue* load_time_data) {
  load_time_data->SetBoolean("phishing", false);
  load_time_data->SetString("heading",
                            l10n_util::GetStringUTF16(IDS_MALWARE_V3_HEADING));
  load_time_data->SetString(
      "primaryParagraph",
      l10n_util::GetStringFUTF16(
          IDS_MALWARE_V3_PRIMARY_PARAGRAPH,
          common_string_util::GetFormattedHostName(request_url_)));
  load_time_data->SetString(
      "explanationParagraph",
      display_options_.is_main_frame_load_blocked
          ? l10n_util::GetStringFUTF16(
                IDS_MALWARE_V3_EXPLANATION_PARAGRAPH,
                common_string_util::GetFormattedHostName(request_url_))
          : l10n_util::GetStringFUTF16(
                IDS_MALWARE_V3_EXPLANATION_PARAGRAPH_SUBRESOURCE,
                base::UTF8ToUTF16(main_frame_url_.host()),
                common_string_util::GetFormattedHostName(request_url_)));
  load_time_data->SetString(
      "finalParagraph",
      l10n_util::GetStringUTF16(IDS_MALWARE_V3_PROCEED_PARAGRAPH));
}

void SafeBrowsingErrorUI::PopulateHarmfulLoadTimeData(
    base::DictionaryValue* load_time_data) {
  load_time_data->SetBoolean("phishing", false);
  load_time_data->SetString("heading",
                            l10n_util::GetStringUTF16(IDS_HARMFUL_V3_HEADING));
  load_time_data->SetString(
      "primaryParagraph",
      l10n_util::GetStringFUTF16(
          IDS_HARMFUL_V3_PRIMARY_PARAGRAPH,
          common_string_util::GetFormattedHostName(request_url_)));
  load_time_data->SetString(
      "explanationParagraph",
      l10n_util::GetStringFUTF16(
          IDS_HARMFUL_V3_EXPLANATION_PARAGRAPH,
          common_string_util::GetFormattedHostName(request_url_)));
  load_time_data->SetString(
      "finalParagraph",
      l10n_util::GetStringUTF16(IDS_HARMFUL_V3_PROCEED_PARAGRAPH));
}

void SafeBrowsingErrorUI::PopulatePhishingLoadTimeData(
    base::DictionaryValue* load_time_data) {
  load_time_data->SetBoolean("phishing", true);
  load_time_data->SetString("heading",
                            l10n_util::GetStringUTF16(IDS_PHISHING_V4_HEADING));
  load_time_data->SetString(
      "primaryParagraph",
      l10n_util::GetStringFUTF16(
          IDS_PHISHING_V4_PRIMARY_PARAGRAPH,
          common_string_util::GetFormattedHostName(request_url_)));
  load_time_data->SetString(
      "explanationParagraph",
      l10n_util::GetStringFUTF16(
          IDS_PHISHING_V4_EXPLANATION_PARAGRAPH,
          common_string_util::GetFormattedHostName(request_url_)));
  load_time_data->SetString(
      "finalParagraph",
      l10n_util::GetStringUTF16(IDS_PHISHING_V4_PROCEED_AND_REPORT_PARAGRAPH));
}

void SafeBrowsingErrorUI::PopulateExtendedReportingOption(
    base::DictionaryValue* load_time_data) {
  bool can_show_extended_reporting_option = CanShowExtendedReportingOption();
  load_time_data->SetBoolean(security_interstitials::kDisplayCheckBox,
                             can_show_extended_reporting_option);
  if (!can_show_extended_reporting_option)
    return;

  const std::string privacy_link = base::StringPrintf(
      security_interstitials::kPrivacyLinkHtml,
      security_interstitials::CMD_OPEN_REPORTING_PRIVACY,
      l10n_util::GetStringUTF8(IDS_SAFE_BROWSING_PRIVACY_POLICY_PAGE).c_str());
  load_time_data->SetString(security_interstitials::kOptInLink,
                            l10n_util::GetStringFUTF16(
                                display_options_.is_scout_reporting_enabled
                                    ? IDS_SAFE_BROWSING_SCOUT_REPORTING_AGREE
                                    : IDS_SAFE_BROWSING_MALWARE_REPORTING_AGREE,
                                base::UTF8ToUTF16(privacy_link)));
  load_time_data->SetBoolean(security_interstitials::kBoxChecked,
                             display_options_.is_extended_reporting_enabled);
}

}  // security_interstitials
