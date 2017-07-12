// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "headless/public/util/generic_url_request_job.h"

#include <string.h>
#include <algorithm>

#include "base/logging.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/browser/web_contents.h"
#include "headless/public/util/url_request_dispatcher.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/cookies/cookie_store.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_context.h"

namespace headless {
namespace {

// True if the request method is "safe" (per section 4.2.1 of RFC 7231).
bool IsMethodSafe(const std::string& method) {
  return method == "GET" || method == "HEAD" || method == "OPTIONS" ||
         method == "TRACE";
}

// Keep in sync with X_DevTools_Request_Id defined in HTTPNames.json5.
const char kDevtoolsRequestId[] = "X-DevTools-Request-Id";

}  // namespace

GenericURLRequestJob::GenericURLRequestJob(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    URLRequestDispatcher* url_request_dispatcher,
    std::unique_ptr<URLFetcher> url_fetcher,
    Delegate* delegate)
    : ManagedDispatchURLRequestJob(request,
                                   network_delegate,
                                   url_request_dispatcher),
      url_fetcher_(std::move(url_fetcher)),
      origin_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      delegate_(delegate),
      request_resource_info_(
          content::ResourceRequestInfo::ForRequest(request_)),
      weak_factory_(this) {}

GenericURLRequestJob::~GenericURLRequestJob() {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
}

void GenericURLRequestJob::SetExtraRequestHeaders(
    const net::HttpRequestHeaders& headers) {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
  extra_request_headers_ = headers;

  // TODO(alexclarke): Remove kDevtoolsRequestId
  extra_request_headers_.RemoveHeader(kDevtoolsRequestId);
}

void GenericURLRequestJob::Start() {
  PrepareCookies(request_->url(), request_->method(),
                 url::Origin(request_->first_party_for_cookies()),
                 base::Bind(&Delegate::OnPendingRequest,
                            base::Unretained(delegate_), this));
}

void GenericURLRequestJob::PrepareCookies(const GURL& rewritten_url,
                                          const std::string& method,
                                          const url::Origin& site_for_cookies,
                                          const base::Closure& done_callback) {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
  net::CookieStore* cookie_store = request_->context()->cookie_store();
  net::CookieOptions options;
  options.set_include_httponly();

  // See net::URLRequestHttpJob::AddCookieHeaderAndStart().
  url::Origin requested_origin(rewritten_url);
  if (net::registry_controlled_domains::SameDomainOrHost(
          requested_origin, site_for_cookies,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    if (net::registry_controlled_domains::SameDomainOrHost(
            requested_origin, request_->initiator(),
            net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
      options.set_same_site_cookie_mode(
          net::CookieOptions::SameSiteCookieMode::INCLUDE_STRICT_AND_LAX);
    } else if (IsMethodSafe(request_->method())) {
      options.set_same_site_cookie_mode(
          net::CookieOptions::SameSiteCookieMode::INCLUDE_LAX);
    }
  }

  cookie_store->GetCookieListWithOptionsAsync(
      rewritten_url, options,
      base::Bind(&GenericURLRequestJob::OnCookiesAvailable,
                 weak_factory_.GetWeakPtr(), rewritten_url, method,
                 done_callback));
}

void GenericURLRequestJob::OnCookiesAvailable(
    const GURL& rewritten_url,
    const std::string& method,
    const base::Closure& done_callback,
    const net::CookieList& cookie_list) {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
  // TODO(alexclarke): Set user agent.
  // Pass cookies, the referrer and any extra headers into the fetch request.
  extra_request_headers_.SetHeader(
      net::HttpRequestHeaders::kCookie,
      net::CookieStore::BuildCookieLine(cookie_list));

  extra_request_headers_.SetHeader(net::HttpRequestHeaders::kReferer,
                                   request_->referrer());

  done_callback.Run();
}

void GenericURLRequestJob::OnFetchStartError(net::Error error) {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
  DispatchStartError(error);
  delegate_->OnResourceLoadFailed(this, error);
}

void GenericURLRequestJob::OnFetchComplete(
    const GURL& final_url,
    int http_response_code,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    const char* body,
    size_t body_size) {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
  response_time_ = base::TimeTicks::Now();
  http_response_code_ = http_response_code;
  response_headers_ = response_headers;
  body_ = body;
  body_size_ = body_size;

  DispatchHeadersComplete();

  delegate_->OnResourceLoadComplete(this, final_url, http_response_code,
                                    response_headers_, body_, body_size_);
}

int GenericURLRequestJob::ReadRawData(net::IOBuffer* buf, int buf_size) {
  DCHECK(origin_task_runner_->RunsTasksOnCurrentThread());
  // TODO(skyostil): Implement ranged fetches.
  // TODO(alexclarke): Add coverage for all the cases below.
  size_t bytes_available = body_size_ - read_offset_;
  size_t bytes_to_copy =
      std::min(static_cast<size_t>(buf_size), bytes_available);
  if (bytes_to_copy) {
    std::memcpy(buf->data(), &body_[read_offset_], bytes_to_copy);
    read_offset_ += bytes_to_copy;
  }
  return bytes_to_copy;
}

int GenericURLRequestJob::GetResponseCode() const {
  return http_response_code_;
}

void GenericURLRequestJob::GetResponseInfo(net::HttpResponseInfo* info) {
  info->headers = response_headers_;
}

bool GenericURLRequestJob::GetMimeType(std::string* mime_type) const {
  if (!response_headers_)
    return false;
  return response_headers_->GetMimeType(mime_type);
}

bool GenericURLRequestJob::GetCharset(std::string* charset) {
  if (!response_headers_)
    return false;
  return response_headers_->GetCharset(charset);
}

void GenericURLRequestJob::GetLoadTimingInfo(
    net::LoadTimingInfo* load_timing_info) const {
  // TODO(alexclarke): Investigate setting the other members too where possible.
  load_timing_info->receive_headers_end = response_time_;
}

const net::URLRequest* GenericURLRequestJob::GetURLRequest() const {
  return request_;
}

int GenericURLRequestJob::GetFrameTreeNodeId() const {
  return request_resource_info_->GetFrameTreeNodeId();
}

std::string GenericURLRequestJob::GetDevToolsAgentHostId() const {
  return content::DevToolsAgentHost::GetOrCreateFor(
             request_resource_info_->GetWebContentsGetterForRequest().Run())
      ->GetId();
}

Request::ResourceType GenericURLRequestJob::GetResourceType() const {
  switch (request_resource_info_->GetResourceType()) {
    case content::RESOURCE_TYPE_MAIN_FRAME:
      return Request::ResourceType::MAIN_FRAME;
    case content::RESOURCE_TYPE_SUB_FRAME:
      return Request::ResourceType::SUB_FRAME;
    case content::RESOURCE_TYPE_STYLESHEET:
      return Request::ResourceType::STYLESHEET;
    case content::RESOURCE_TYPE_SCRIPT:
      return Request::ResourceType::SCRIPT;
    case content::RESOURCE_TYPE_IMAGE:
      return Request::ResourceType::IMAGE;
    case content::RESOURCE_TYPE_FONT_RESOURCE:
      return Request::ResourceType::FONT_RESOURCE;
    case content::RESOURCE_TYPE_SUB_RESOURCE:
      return Request::ResourceType::SUB_RESOURCE;
    case content::RESOURCE_TYPE_OBJECT:
      return Request::ResourceType::OBJECT;
    case content::RESOURCE_TYPE_MEDIA:
      return Request::ResourceType::MEDIA;
    case content::RESOURCE_TYPE_WORKER:
      return Request::ResourceType::WORKER;
    case content::RESOURCE_TYPE_SHARED_WORKER:
      return Request::ResourceType::SHARED_WORKER;
    case content::RESOURCE_TYPE_PREFETCH:
      return Request::ResourceType::PREFETCH;
    case content::RESOURCE_TYPE_FAVICON:
      return Request::ResourceType::FAVICON;
    case content::RESOURCE_TYPE_XHR:
      return Request::ResourceType::XHR;
    case content::RESOURCE_TYPE_PING:
      return Request::ResourceType::PING;
    case content::RESOURCE_TYPE_SERVICE_WORKER:
      return Request::ResourceType::SERVICE_WORKER;
    case content::RESOURCE_TYPE_CSP_REPORT:
      return Request::ResourceType::CSP_REPORT;
    case content::RESOURCE_TYPE_PLUGIN_RESOURCE:
      return Request::ResourceType::PLUGIN_RESOURCE;
    default:
      NOTREACHED() << "Unrecognized resource type";
      return Request::ResourceType::MAIN_FRAME;
  }
}

namespace {
std::string GetUploadData(net::URLRequest* request) {
  if (!request->has_upload())
    return "";

  const net::UploadDataStream* stream = request->get_upload();
  if (!stream->GetElementReaders())
    return "";

  DCHECK_EQ(1u, stream->GetElementReaders()->size());
  const net::UploadBytesElementReader* reader =
      (*stream->GetElementReaders())[0]->AsBytesReader();
  return std::string(reader->bytes(), reader->length());
}
}  // namespace

const Request* GenericURLRequestJob::GetRequest() const {
  return this;
}

void GenericURLRequestJob::AllowRequest() {
  if (!origin_task_runner_->RunsTasksOnCurrentThread()) {
    origin_task_runner_->PostTask(
        FROM_HERE, base::Bind(&GenericURLRequestJob::AllowRequest,
                              weak_factory_.GetWeakPtr()));
    return;
  }

  url_fetcher_->StartFetch(request_->url(), request_->method(),
                           GetUploadData(request_), extra_request_headers_,
                           this);
}

void GenericURLRequestJob::BlockRequest(net::Error error) {
  if (!origin_task_runner_->RunsTasksOnCurrentThread()) {
    origin_task_runner_->PostTask(
        FROM_HERE, base::Bind(&GenericURLRequestJob::BlockRequest,
                              weak_factory_.GetWeakPtr(), error));
    return;
  }

  DispatchStartError(error);
}

void GenericURLRequestJob::ModifyRequest(
    const GURL& url,
    const std::string& method,
    const std::string& post_data,
    const net::HttpRequestHeaders& request_headers) {
  if (!origin_task_runner_->RunsTasksOnCurrentThread()) {
    origin_task_runner_->PostTask(
        FROM_HERE, base::Bind(&GenericURLRequestJob::ModifyRequest,
                              weak_factory_.GetWeakPtr(), url, method,
                              post_data, request_headers));
    return;
  }

  extra_request_headers_ = request_headers;
  PrepareCookies(
      request_->url(), request_->method(),
      url::Origin(request_->first_party_for_cookies()),
      base::Bind(&URLFetcher::StartFetch, base::Unretained(url_fetcher_.get()),
                 url, method, post_data, request_headers, this));
}

void GenericURLRequestJob::MockResponse(
    std::unique_ptr<MockResponseData> mock_response) {
  if (!origin_task_runner_->RunsTasksOnCurrentThread()) {
    origin_task_runner_->PostTask(
        FROM_HERE, base::Bind(&GenericURLRequestJob::MockResponse,
                              weak_factory_.GetWeakPtr(),
                              base::Passed(std::move(mock_response))));
    return;
  }

  mock_response_ = std::move(mock_response);

  OnFetchCompleteExtractHeaders(request_->url(),
                                mock_response_->http_response_code,
                                mock_response_->response_data.data(),
                                mock_response_->response_data.size());
}

}  // namespace headless
