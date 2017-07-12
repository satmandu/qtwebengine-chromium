// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/backend/cups_jobs.h"

#include <cups/ipp.h>

#include <array>
#include <map>
#include <memory>

#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"

namespace printing {
namespace {

using PReason = PrinterStatus::PrinterReason::Reason;
using PSeverity = PrinterStatus::PrinterReason::Severity;

// printer attributes
const char kPrinterUri[] = "printer-uri";
const char kPrinterState[] = "printer-state";
const char kPrinterStateReasons[] = "printer-state-reasons";
const char kPrinterStateMessage[] = "printer-state-message";

// job attributes
const char kJobUri[] = "job-uri";
const char kJobId[] = "job-id";
const char kJobState[] = "job-state";
const char kJobStateReasons[] = "job-state-reasons";
const char kJobStateMessage[] = "job-state-message";
const char kJobImpressionsCompleted[] = "job-impressions-completed";
const char kTimeAtProcessing[] = "time-at-processing";

// request parameters
const char kRequestedAttributes[] = "requested-attributes";
const char kWhichJobs[] = "which-jobs";
const char kLimit[] = "limit";

// request values
const char kCompleted[] = "completed";
const char kNotCompleted[] = "not-completed";

// printer state severities
const char kSeverityReport[] = "report";
const char kSeverityWarn[] = "warning";
const char kSeverityError[] = "error";

// printer state reason values
const char kNone[] = "none";
const char kMediaNeeded[] = "media-needed";
const char kMediaJam[] = "media-jam";
const char kMovingToPaused[] = "moving-to-paused";
const char kPaused[] = "paused";
const char kShutdown[] = "shutdown";
const char kConnectingToDevice[] = "connecting-to-device";
const char kTimedOut[] = "timed-out";
const char kStopping[] = "stopping";
const char kStoppedPartly[] = "stopped-partly";
const char kTonerLow[] = "toner-low";
const char kTonerEmpty[] = "toner-empty";
const char kSpoolAreaFull[] = "spool-area-full";
const char kCoverOpen[] = "cover-open";
const char kInterlockOpen[] = "interlock-open";
const char kDoorOpen[] = "door-open";
const char kInputTrayMissing[] = "input-tray-missing";
const char kMediaLow[] = "media-low";
const char kMediaEmpty[] = "media-empty";
const char kOutputTrayMissing[] = "output-tray-missing";
const char kOutputAreaAlmostFull[] = "output-area-almost-full";
const char kOutputAreaFull[] = "output-area-full";
const char kMarkerSupplyLow[] = "marker-supply-low";
const char kMarkerSupplyEmpty[] = "marker-supply-empty";
const char kMarkerWasteAlmostFull[] = "marker-waste-almost-full";
const char kMarkerWasteFull[] = "marker-waste-full";
const char kFuserOverTemp[] = "fuser-over-temp";
const char kFuserUnderTemp[] = "fuser-under-temp";
const char kOpcNearEol[] = "opc-near-eol";
const char kOpcLifeOver[] = "opc-life-over";
const char kDeveloperLow[] = "developer-low";
const char kDeveloperEmpty[] = "developer-empty";
const char kInterpreterResourceUnavailable[] =
    "interpreter-resource-unavailable";

constexpr std::array<const char* const, 3> kPrinterAttributes{
    {kPrinterState, kPrinterStateReasons, kPrinterStateMessage}};

std::unique_ptr<ipp_t, void (*)(ipp_t*)> WrapIpp(ipp_t* ipp) {
  return std::unique_ptr<ipp_t, void (*)(ipp_t*)>(ipp, &ippDelete);
}

// Converts an IPP attribute |attr| to the appropriate JobState enum.
CupsJob::JobState ToJobState(ipp_attribute_t* attr) {
  DCHECK_EQ(IPP_TAG_ENUM, ippGetValueTag(attr));
  int state = ippGetInteger(attr, 0);
  switch (state) {
    case IPP_JOB_ABORTED:
      return CupsJob::ABORTED;
    case IPP_JOB_CANCELLED:
      return CupsJob::CANCELED;
    case IPP_JOB_COMPLETED:
      return CupsJob::COMPLETED;
    case IPP_JOB_HELD:
      return CupsJob::HELD;
    case IPP_JOB_PENDING:
      return CupsJob::PENDING;
    case IPP_JOB_PROCESSING:
      return CupsJob::PROCESSING;
    case IPP_JOB_STOPPED:
      return CupsJob::STOPPED;
    default:
      NOTREACHED() << "Unidentifed state " << state;
      break;
  }

  return CupsJob::UNKNOWN;
}

// Returns a lookup map from strings to PrinterReason::Reason.
const std::map<base::StringPiece, PReason>& GetLabelToReason() {
  static const std::map<base::StringPiece, PReason> kLabelToReason =
      std::map<base::StringPiece, PReason>{
          {kNone, PReason::NONE},
          {kMediaNeeded, PReason::MEDIA_NEEDED},
          {kMediaJam, PReason::MEDIA_JAM},
          {kMovingToPaused, PReason::MOVING_TO_PAUSED},
          {kPaused, PReason::PAUSED},
          {kShutdown, PReason::SHUTDOWN},
          {kConnectingToDevice, PReason::CONNECTING_TO_DEVICE},
          {kTimedOut, PReason::TIMED_OUT},
          {kStopping, PReason::STOPPING},
          {kStoppedPartly, PReason::STOPPED_PARTLY},
          {kTonerLow, PReason::TONER_LOW},
          {kTonerEmpty, PReason::TONER_EMPTY},
          {kSpoolAreaFull, PReason::SPOOL_AREA_FULL},
          {kCoverOpen, PReason::COVER_OPEN},
          {kInterlockOpen, PReason::INTERLOCK_OPEN},
          {kDoorOpen, PReason::DOOR_OPEN},
          {kInputTrayMissing, PReason::INPUT_TRAY_MISSING},
          {kMediaLow, PReason::MEDIA_LOW},
          {kMediaEmpty, PReason::MEDIA_EMPTY},
          {kOutputTrayMissing, PReason::OUTPUT_TRAY_MISSING},
          {kOutputAreaAlmostFull, PReason::OUTPUT_AREA_ALMOST_FULL},
          {kOutputAreaFull, PReason::OUTPUT_AREA_FULL},
          {kMarkerSupplyLow, PReason::MARKER_SUPPLY_LOW},
          {kMarkerSupplyEmpty, PReason::MARKER_SUPPLY_EMPTY},
          {kMarkerWasteAlmostFull, PReason::MARKER_WASTE_ALMOST_FULL},
          {kMarkerWasteFull, PReason::MARKER_WASTE_FULL},
          {kFuserOverTemp, PReason::FUSER_OVER_TEMP},
          {kFuserUnderTemp, PReason::FUSER_UNDER_TEMP},
          {kOpcNearEol, PReason::OPC_NEAR_EOL},
          {kOpcLifeOver, PReason::OPC_LIFE_OVER},
          {kDeveloperLow, PReason::DEVELOPER_LOW},
          {kDeveloperEmpty, PReason::DEVELOPER_EMPTY},
          {kInterpreterResourceUnavailable,
           PReason::INTERPRETER_RESOURCE_UNAVAILABLE},
      };
  return kLabelToReason;
}

// Returns the Reason cooresponding to the string |reason|.  Returns
// UNKOWN_REASON if the string is not recognized.
PrinterStatus::PrinterReason::Reason ToReason(base::StringPiece reason) {
  const auto& enum_map = GetLabelToReason();
  const auto& entry = enum_map.find(reason);
  return entry != enum_map.end() ? entry->second : PReason::UNKNOWN_REASON;
}

// Returns the Severity cooresponding to |severity|.  Returns UNKNOWN_SEVERITY
// if the strin gis not recognized.
PSeverity ToSeverity(base::StringPiece severity) {
  if (severity == kSeverityError)
    return PSeverity::ERROR;

  if (severity == kSeverityWarn)
    return PSeverity::WARNING;

  if (severity == kSeverityReport)
    return PSeverity::REPORT;

  return PSeverity::UNKNOWN_SEVERITY;
}

// Parses the |reason| string into a PrinterReason.  Splits the string based on
// the last '-' to determine severity.  If a recognized severity is not
// included, severity is assumed to be ERROR per RFC2911.
PrinterStatus::PrinterReason ToPrinterReason(base::StringPiece reason) {
  PrinterStatus::PrinterReason parsed;

  if (reason == kNone) {
    parsed.reason = PReason::NONE;
    parsed.severity = PSeverity::UNKNOWN_SEVERITY;
    return parsed;
  }

  size_t last_dash = reason.rfind('-');
  auto severity = PSeverity::UNKNOWN_SEVERITY;
  if (last_dash != base::StringPiece::npos) {
    // try to parse the last part of the string as the severity.
    severity = ToSeverity(reason.substr(last_dash + 1));
  }

  if (severity == PSeverity::UNKNOWN_SEVERITY) {
    // Severity is unknown.  No severity in the reason.
    // Per spec, if there is no severity, severity is error.
    parsed.severity = PSeverity::ERROR;
    parsed.reason = ToReason(reason);
  } else {
    parsed.severity = severity;
    // reason is the beginning of the string
    parsed.reason = ToReason(reason.substr(0, last_dash));
  }

  return parsed;
}

// Populates |collection| with the collection of strings in |attr|.
void ParseCollection(ipp_attribute_t* attr,
                     std::vector<std::string>* collection) {
  int count = ippGetCount(attr);
  for (int i = 0; i < count; i++) {
    base::StringPiece value = ippGetString(attr, i, nullptr);
    collection->push_back(value.as_string());
  }
}

// Parse a field for the CupsJob |job| from IPP attribute |attr| using the
// attribute name |name|.
void ParseField(ipp_attribute_t* attr, base::StringPiece name, CupsJob* job) {
  DCHECK(!name.empty());
  if (name == kJobId) {
    job->id = ippGetInteger(attr, 0);
  } else if (name == kJobImpressionsCompleted) {
    job->current_pages = ippGetInteger(attr, 0);
  } else if (name == kJobState) {
    job->state = ToJobState(attr);
  } else if (name == kJobStateReasons) {
    ParseCollection(attr, &(job->state_reasons));
  } else if (name == kJobStateMessage) {
    job->state_message = ippGetString(attr, 0, nullptr);
  } else if (name == kTimeAtProcessing) {
    job->processing_started = ippGetInteger(attr, 0);
  }
}

// Returns a new CupsJob allocated in |jobs| with |printer_id| populated.
CupsJob* NewJob(const std::string& printer_id, std::vector<CupsJob>* jobs) {
  jobs->emplace_back();
  CupsJob* job = &jobs->back();
  job->printer_id = printer_id;
  return job;
}

void ParseJobs(ipp_t* response,
               const std::string& printer_id,
               ipp_attribute_t* starting_attr,
               std::vector<CupsJob>* jobs) {
  // We know this is a non-empty job section.  Start parsing fields for at least
  // one job.
  CupsJob* current_job = NewJob(printer_id, jobs);
  for (ipp_attribute_t* attr = starting_attr; attr != nullptr;
       attr = ippNextAttribute(response)) {
    base::StringPiece attribute_name = ippGetName(attr);
    // Separators indicate a new job.  Separators have empty names.
    if (attribute_name.empty()) {
      current_job = NewJob(printer_id, jobs);
      continue;
    }

    // Continue to populate the job fileds.
    ParseField(attr, attribute_name, current_job);
  }
}

// Returns the uri for printer with |id| as served by CUPS.  Assumes that |id|
// is a valid CUPS printer name and performs no error checking or escaping.
std::string PrinterUriFromName(const std::string& id) {
  return base::StringPrintf("ipp://localhost/printers/%s", id.c_str());
}

}  // namespace

void ParseJobsResponse(ipp_t* response,
                       const std::string& printer_id,
                       std::vector<CupsJob>* jobs) {
  // Advance the position in the response to the jobs section.
  ipp_attribute_t* attr = ippFirstAttribute(response);
  while (attr != nullptr && ippGetGroupTag(attr) != IPP_TAG_JOB) {
    attr = ippNextAttribute(response);
  }

  if (attr != nullptr) {
    ParseJobs(response, printer_id, attr, jobs);
  }
}

void ParsePrinterStatus(ipp_t* response, PrinterStatus* printer_status) {
  for (ipp_attribute_t* attr = ippFirstAttribute(response); attr != nullptr;
       attr = ippNextAttribute(response)) {
    base::StringPiece name = ippGetName(attr);
    if (name.empty()) {
      continue;
    }

    if (name == kPrinterState) {
      DCHECK_EQ(IPP_TAG_ENUM, ippGetValueTag(attr));
      printer_status->state = static_cast<ipp_pstate_t>(ippGetInteger(attr, 0));
    } else if (name == kPrinterStateReasons) {
      std::vector<std::string> reason_strings;
      ParseCollection(attr, &reason_strings);
      for (const std::string& reason : reason_strings) {
        printer_status->reasons.push_back(ToPrinterReason(reason));
      }
    } else if (name == kPrinterStateMessage) {
      printer_status->message = ippGetString(attr, 0, nullptr);
    }
  }
}

bool GetPrinterStatus(http_t* http,
                      const std::string& printer_id,
                      PrinterStatus* printer_status) {
  DCHECK(http);

  auto request = WrapIpp(ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES));

  const std::string printer_uri = PrinterUriFromName(printer_id);
  ippAddString(request.get(), IPP_TAG_OPERATION, IPP_TAG_URI, kPrinterUri,
               nullptr, printer_uri.data());

  ippAddStrings(request.get(), IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                kRequestedAttributes, kPrinterAttributes.size(), nullptr,
                kPrinterAttributes.data());

  auto response =
      WrapIpp(cupsDoRequest(http, request.release(), printer_uri.c_str()));

  if (ippGetStatusCode(response.get()) != IPP_STATUS_OK)
    return false;

  ParsePrinterStatus(response.get(), printer_status);

  return true;
}

bool GetCupsJobs(http_t* http,
                 const std::string& printer_id,
                 int limit,
                 JobCompletionState which,
                 std::vector<CupsJob>* jobs) {
  DCHECK(http);

  auto request = WrapIpp(ippNewRequest(IPP_OP_GET_JOBS));
  const std::string printer_uri = PrinterUriFromName(printer_id);
  ippAddString(request.get(), IPP_TAG_OPERATION, IPP_TAG_URI, kPrinterUri,
               nullptr, printer_uri.data());
  ippAddInteger(request.get(), IPP_TAG_OPERATION, IPP_TAG_INTEGER, kLimit,
                limit);

  std::vector<const char*> job_attributes = {
      kJobUri,          kJobId,           kJobState,
      kJobStateReasons, kJobStateMessage, kJobImpressionsCompleted,
      kTimeAtProcessing};

  ippAddStrings(request.get(), IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                kRequestedAttributes, job_attributes.size(), nullptr,
                job_attributes.data());

  ippAddString(request.get(), IPP_TAG_OPERATION, IPP_TAG_KEYWORD, kWhichJobs,
               nullptr, which == COMPLETED ? kCompleted : kNotCompleted);

  if (ippValidateAttributes(request.get()) != 1) {
    LOG(WARNING) << "Could not validate ipp request: " << cupsLastErrorString();
    return false;
  }

  // cupsDoRequest will delete the request.
  auto response =
      WrapIpp(cupsDoRequest(http, request.release(), printer_uri.c_str()));

  ipp_status_t status = ippGetStatusCode(response.get());

  if (status != IPP_OK) {
    LOG(WARNING) << "IPP Error: " << cupsLastErrorString();
    return false;
  }

  ParseJobsResponse(response.get(), printer_id, jobs);

  return true;
}

}  // namespace printing
