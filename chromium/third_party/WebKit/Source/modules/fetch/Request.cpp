// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/fetch/Request.h"

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/V8PrivateProperty.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/loader/ThreadableLoader.h"
#include "modules/fetch/BodyStreamBuffer.h"
#include "modules/fetch/FetchManager.h"
#include "modules/fetch/RequestInit.h"
#include "platform/HTTPNames.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/loader/fetch/FetchUtils.h"
#include "platform/loader/fetch/ResourceLoaderOptions.h"
#include "platform/loader/fetch/ResourceRequest.h"
#include "platform/network/HTTPParsers.h"
#include "platform/weborigin/OriginAccessEntry.h"
#include "platform/weborigin/Referrer.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerRequest.h"

namespace blink {

FetchRequestData* CreateCopyOfFetchRequestDataForFetch(
    ScriptState* script_state,
    const FetchRequestData* original) {
  FetchRequestData* request = FetchRequestData::Create();
  request->SetURL(original->Url());
  request->SetMethod(original->Method());
  request->SetHeaderList(original->HeaderList()->Clone());
  request->SetUnsafeRequestFlag(true);
  // FIXME: Set client.
  DOMWrapperWorld& world = script_state->World();
  if (world.IsIsolatedWorld()) {
    request->SetOrigin(world.IsolatedWorldSecurityOrigin());
  } else {
    request->SetOrigin(
        ExecutionContext::From(script_state)->GetSecurityOrigin());
  }
  // FIXME: Set ForceOriginHeaderFlag.
  request->SetSameOriginDataURLFlag(true);
  request->SetReferrer(original->GetReferrer());
  request->SetMode(original->Mode());
  request->SetCredentials(original->Credentials());
  request->SetCacheMode(original->CacheMode());
  request->SetAttachedCredential(original->AttachedCredential());
  request->SetRedirect(original->Redirect());
  request->SetIntegrity(original->Integrity());
  return request;
}

Request* Request::CreateRequestWithRequestOrString(
    ScriptState* script_state,
    Request* input_request,
    const String& input_string,
    RequestInit& init,
    ExceptionState& exception_state) {
  // - "If |input| is a Request object and it is disturbed, throw a
  //   TypeError."
  if (input_request && input_request->bodyUsed()) {
    exception_state.ThrowTypeError(
        "Cannot construct a Request with a Request object that has already "
        "been used.");
    return nullptr;
  }
  // - "Let |temporaryBody| be |input|'s request's body if |input| is a
  //   Request object, and null otherwise."
  BodyStreamBuffer* temporary_body =
      input_request ? input_request->BodyBuffer() : nullptr;

  // "Let |request| be |input|'s request, if |input| is a Request object,
  // and a new request otherwise."

  RefPtr<SecurityOrigin> origin =
      ExecutionContext::From(script_state)->GetSecurityOrigin();

  // TODO(yhirano): Implement the following steps:
  // - "Let |window| be client."
  // - "If |request|'s window is an environment settings object and its
  //   origin is same origin with entry settings object's origin, set
  //   |window| to |request|'s window."
  // - "If |init|'s window member is present and it is not null, throw a
  //   TypeError."
  // - "If |init|'s window member is present, set |window| to no-window."
  //
  // "Set |request| to a new request whose url is |request|'s current url,
  // method is |request|'s method, header list is a copy of |request|'s
  // header list, unsafe-request flag is set, client is entry settings object,
  // window is |window|, origin is "client", omit-Origin-header flag is
  // |request|'s omit-Origin-header flag, same-origin data-URL flag is set,
  // referrer is |request|'s referrer, referrer policy is |request|'s
  // referrer policy, destination is the empty string, mode is |request|'s
  // mode, credentials mode is |request|'s credentials mode, cache mode is
  // |request|'s cache mode, redirect mode is |request|'s redirect mode, and
  // integrity metadata is |request|'s integrity metadata."
  FetchRequestData* request = CreateCopyOfFetchRequestDataForFetch(
      script_state,
      input_request ? input_request->GetRequest() : FetchRequestData::Create());

  // We don't use fallback values. We set these flags directly in below.
  // - "Let |fallbackMode| be null."
  // - "Let |fallbackCredentials| be null."
  // - "Let |baseURL| be entry settings object's API base URL."

  // "If |input| is a string, run these substeps:"
  if (!input_request) {
    // "Let |parsedURL| be the result of parsing |input| with |baseURL|."
    KURL parsed_url =
        ExecutionContext::From(script_state)->CompleteURL(input_string);
    // "If |parsedURL| is failure, throw a TypeError."
    if (!parsed_url.IsValid()) {
      exception_state.ThrowTypeError("Failed to parse URL from " +
                                     input_string);
      return nullptr;
    }
    //   "If |parsedURL| includes credentials, throw a TypeError."
    if (!parsed_url.User().IsEmpty() || !parsed_url.Pass().IsEmpty()) {
      exception_state.ThrowTypeError(
          "Request cannot be constructed from a URL that includes "
          "credentials: " +
          input_string);
      return nullptr;
    }
    // "Set |request|'s url to |parsedURL| and replace |request|'s url list
    // single URL with a copy of |parsedURL|."
    request->SetURL(parsed_url);

    // We don't use fallback values. We set these flags directly in below.
    // - "Set |fallbackMode| to "cors"."
    // - "Set |fallbackCredentials| to "omit"."
  }

  // "If any of |init|'s members are present, run these substeps:"
  if (init.are_any_members_set) {
    // "If |request|'s |mode| is "navigate", throw a TypeError."
    if (request->Mode() == WebURLRequest::kFetchRequestModeNavigate) {
      exception_state.ThrowTypeError(
          "Cannot construct a Request with a Request whose mode is 'navigate' "
          "and a non-empty RequestInit.");
      return nullptr;
    }

    // TODO(yhirano): Implement the following substep:
    //   "Unset |request|'s omit-Origin-header flag."

    // The substep "Set |request|'s referrer to "client"." is performed by
    // the code below as follows:
    // - |init.referrer.referrer| gets initialized by the RequestInit
    //   constructor to "about:client" when any of |options|'s members are
    //   present.
    // - The code below does the equivalent as the step specified in the
    //   spec by processing the "about:client".

    // The substep "Set |request|'s referrer policy to the empty string."
    // is also performed by the code below similarly.
  }

  // The following if-clause performs the following two steps:
  // - "If |init|'s referrer member is present, run these substeps:"
  //   "If |init|'s referrerPolicy member is present, set |request|'s
  //    referrer policy to it."
  //
  // The condition "if any of |init|'s members are present"
  // (areAnyMembersSet) is used for the if-clause instead of conditions
  // indicating presence of each member as specified in the spec. This is to
  // perform the substeps in the previous step together here.
  if (init.are_any_members_set) {
    // Nothing to do for the step "Let |referrer| be |init|'s referrer
    // member."

    if (init.referrer.referrer.IsEmpty()) {
      // "If |referrer| is the empty string, set |request|'s referrer to
      // "no-referrer" and terminate these substeps."
      request->SetReferrerString(AtomicString(Referrer::NoReferrer()));
    } else {
      // "Let |parsedReferrer| be the result of parsing |referrer| with
      // |baseURL|."
      KURL parsed_referrer = ExecutionContext::From(script_state)
                                 ->CompleteURL(init.referrer.referrer);
      if (!parsed_referrer.IsValid()) {
        // "If |parsedReferrer| is failure, throw a TypeError."
        exception_state.ThrowTypeError("Referrer '" + init.referrer.referrer +
                                       "' is not a valid URL.");
        return nullptr;
      }
      if (parsed_referrer.ProtocolIsAbout() &&
          parsed_referrer.Host().IsEmpty() &&
          parsed_referrer.GetPath() == "client") {
        // "If |parsedReferrer|'s non-relative flag is set, scheme is
        // "about", and path contains a single string "client", set
        // request's referrer to "client" and terminate these
        // substeps."
        request->SetReferrerString(FetchRequestData::ClientReferrerString());
      } else if (!origin->IsSameSchemeHostPortAndSuborigin(
                     SecurityOrigin::Create(parsed_referrer).Get())) {
        // "If |parsedReferrer|'s origin is not same origin with
        // |origin|, throw a TypeError."
        exception_state.ThrowTypeError(
            "The origin of '" + init.referrer.referrer +
            "' should be same as '" + origin->ToString() + "'");
        return nullptr;
      } else {
        // "Set |request|'s referrer to |parsedReferrer|."
        request->SetReferrerString(AtomicString(parsed_referrer.GetString()));
      }
    }
    request->SetReferrerPolicy(init.referrer.referrer_policy);
  }

  // The following code performs the following steps:
  // - "Let |mode| be |init|'s mode member if it is present, and
  //   |fallbackMode| otherwise."
  // - "If |mode| is "navigate", throw a TypeError."
  // - "If |mode| is non-null, set |request|'s mode to |mode|."
  if (init.mode == "navigate") {
    exception_state.ThrowTypeError(
        "Cannot construct a Request with a RequestInit whose mode member is "
        "set as 'navigate'.");
    return nullptr;
  }
  if (init.mode == "same-origin") {
    request->SetMode(WebURLRequest::kFetchRequestModeSameOrigin);
  } else if (init.mode == "no-cors") {
    request->SetMode(WebURLRequest::kFetchRequestModeNoCORS);
  } else if (init.mode == "cors") {
    request->SetMode(WebURLRequest::kFetchRequestModeCORS);
  } else {
    // |inputRequest| is directly checked here instead of setting and
    // checking |fallbackMode| as specified in the spec.
    if (!input_request)
      request->SetMode(WebURLRequest::kFetchRequestModeCORS);
  }

  // "Let |credentials| be |init|'s credentials member if it is present, and
  // |fallbackCredentials| otherwise."
  // "If |credentials| is non-null, set |request|'s credentials mode to
  // |credentials|."
  if (init.credentials == "omit") {
    request->SetCredentials(WebURLRequest::kFetchCredentialsModeOmit);
  } else if (init.credentials == "same-origin") {
    request->SetCredentials(WebURLRequest::kFetchCredentialsModeSameOrigin);
  } else if (init.credentials == "include") {
    request->SetCredentials(WebURLRequest::kFetchCredentialsModeInclude);
  } else if (init.credentials == "password") {
    if (!init.attached_credential.Get()) {
      exception_state.ThrowTypeError(
          "Cannot construct a Request with a credential mode of 'password' "
          "without a PasswordCredential.");
      return nullptr;
    }
    request->SetCredentials(WebURLRequest::kFetchCredentialsModePassword);
    request->SetAttachedCredential(init.attached_credential);
    request->SetRedirect(WebURLRequest::kFetchRedirectModeManual);
  } else {
    if (!input_request)
      request->SetCredentials(WebURLRequest::kFetchCredentialsModeOmit);
  }

  // "If |init|'s cache member is present, set |request|'s cache mode to it."
  if (init.cache == "default") {
    request->SetCacheMode(WebURLRequest::kFetchRequestCacheModeDefault);
  } else if (init.cache == "no-store") {
    request->SetCacheMode(WebURLRequest::kFetchRequestCacheModeNoStore);
  } else if (init.cache == "reload") {
    request->SetCacheMode(WebURLRequest::kFetchRequestCacheModeReload);
  } else if (init.cache == "no-cache") {
    request->SetCacheMode(WebURLRequest::kFetchRequestCacheModeNoCache);
  } else if (init.cache == "force-cache") {
    request->SetCacheMode(WebURLRequest::kFetchRequestCacheModeForceCache);
  } else if (init.cache == "only-if-cached") {
    request->SetCacheMode(WebURLRequest::kFetchRequestCacheModeOnlyIfCached);
  }

  // "If |init|'s redirect member is present, set |request|'s redirect mode
  // to it."
  if (init.redirect == "follow") {
    request->SetRedirect(WebURLRequest::kFetchRedirectModeFollow);
  } else if (init.redirect == "error") {
    request->SetRedirect(WebURLRequest::kFetchRedirectModeError);
  } else if (init.redirect == "manual") {
    request->SetRedirect(WebURLRequest::kFetchRedirectModeManual);
  }

  // "If |init|'s integrity member is present, set |request|'s
  // integrity metadata to it."
  if (!init.integrity.IsNull())
    request->SetIntegrity(init.integrity);

  // "If |init|'s method member is present, let |method| be it and run these
  // substeps:"
  if (!init.method.IsNull()) {
    // "If |method| is not a method or method is a forbidden method, throw
    // a TypeError."
    if (!IsValidHTTPToken(init.method)) {
      exception_state.ThrowTypeError("'" + init.method +
                                     "' is not a valid HTTP method.");
      return nullptr;
    }
    if (FetchUtils::IsForbiddenMethod(init.method)) {
      exception_state.ThrowTypeError("'" + init.method +
                                     "' HTTP method is unsupported.");
      return nullptr;
    }
    // "Normalize |method|."
    // "Set |request|'s method to |method|."
    request->SetMethod(FetchUtils::NormalizeMethod(AtomicString(init.method)));
  }
  // "Let |r| be a new Request object associated with |request| and a new
  // Headers object whose guard is "request"."
  Request* r = Request::Create(script_state, request);
  // Perform the following steps:
  // - "Let |headers| be a copy of |r|'s Headers object."
  // - "If |init|'s headers member is present, set |headers| to |init|'s
  //   headers member."
  //
  // We don't create a copy of r's Headers object when init's headers member
  // is present.
  Headers* headers = nullptr;
  if (!init.headers) {
    headers = r->getHeaders()->Clone();
  }
  // "Empty |r|'s request's header list."
  r->request_->HeaderList()->ClearList();
  // "If |r|'s request's mode is "no-cors", run these substeps:
  if (r->GetRequest()->Mode() == WebURLRequest::kFetchRequestModeNoCORS) {
    // "If |r|'s request's method is not a simple method, throw a
    // TypeError."
    if (!FetchUtils::IsSimpleMethod(r->GetRequest()->Method())) {
      exception_state.ThrowTypeError("'" + r->GetRequest()->Method() +
                                     "' is unsupported in no-cors mode.");
      return nullptr;
    }
    // "If |request|'s integrity metadata is not the empty string, throw a
    // TypeError."
    if (!request->Integrity().IsEmpty()) {
      exception_state.ThrowTypeError(
          "The integrity attribute is unsupported in no-cors mode.");
      return nullptr;
    }
    // "Set |r|'s Headers object's guard to "request-no-cors"."
    r->getHeaders()->SetGuard(Headers::kRequestNoCORSGuard);
  }
  // "Fill |r|'s Headers object with |headers|. Rethrow any exceptions."
  if (init.headers) {
    r->getHeaders()->FillWith(init.headers.Get(), exception_state);
  } else {
    ASSERT(headers);
    r->getHeaders()->FillWith(headers, exception_state);
  }
  if (exception_state.HadException())
    return nullptr;

  // "If either |init|'s body member is present or |temporaryBody| is
  // non-null, and |request|'s method is `GET` or `HEAD`, throw a TypeError.
  if (init.body || temporary_body ||
      request->Credentials() == WebURLRequest::kFetchCredentialsModePassword) {
    if (request->Method() == HTTPNames::GET ||
        request->Method() == HTTPNames::HEAD) {
      exception_state.ThrowTypeError(
          "Request with GET/HEAD method cannot have body.");
      return nullptr;
    }
  }

  // TODO(mkwst): See the comment in RequestInit about serializing the attached
  // credential prior to hitting the Service Worker machinery.
  if (request->Credentials() == WebURLRequest::kFetchCredentialsModePassword) {
    r->getHeaders()->append(HTTPNames::Content_Type, init.content_type,
                            exception_state);

    const OriginAccessEntry access_entry =
        OriginAccessEntry(r->url().Protocol(), r->url().Host(),
                          OriginAccessEntry::kAllowRegisterableDomains);
    if (access_entry.MatchesDomain(*origin) ==
        OriginAccessEntry::kDoesNotMatchOrigin) {
      exception_state.ThrowTypeError(
          "Credentials may only be submitted to endpoints on the same "
          "registrable domain.");
      return nullptr;
    }
  }

  // "If |init|'s body member is present, run these substeps:"
  if (init.body) {
    // Perform the following steps:
    // - "Let |stream| and |Content-Type| be the result of extracting
    //   |init|'s body member."
    // - "Set |temporaryBody| to |stream|.
    // - "If |Content-Type| is non-null and |r|'s request's header list
    //   contains no header named `Content-Type`, append
    //   `Content-Type`/|Content-Type| to |r|'s Headers object. Rethrow any
    //   exception."
    temporary_body = new BodyStreamBuffer(script_state, std::move(init.body));
    if (!init.content_type.IsEmpty() &&
        !r->getHeaders()->has(HTTPNames::Content_Type, exception_state)) {
      r->getHeaders()->append(HTTPNames::Content_Type, init.content_type,
                              exception_state);
    }
    if (exception_state.HadException())
      return nullptr;
  }

  // "Set |r|'s request's body to |temporaryBody|.
  if (temporary_body) {
    r->request_->SetBuffer(temporary_body);
    r->RefreshBody(script_state);
  }

  // "Set |r|'s MIME type to the result of extracting a MIME type from |r|'s
  // request's header list."
  r->request_->SetMIMEType(r->request_->HeaderList()->ExtractMIMEType());

  // "If |input| is a Request object and |input|'s request's body is
  // non-null, run these substeps:"
  if (input_request && input_request->BodyBuffer()) {
    // "Let |dummyStream| be an empty ReadableStream object."
    auto dummy_stream =
        new BodyStreamBuffer(script_state, BytesConsumer::CreateClosed());
    // "Set |input|'s request's body to a new body whose stream is
    // |dummyStream|."
    input_request->request_->SetBuffer(dummy_stream);
    input_request->RefreshBody(script_state);
    // "Let |reader| be the result of getting reader from |dummyStream|."
    // "Read all bytes from |dummyStream| with |reader|."
    input_request->BodyBuffer()->CloseAndLockAndDisturb();
  }

  // "Return |r|."
  return r;
}

Request* Request::Create(ScriptState* script_state,
                         const RequestInfo& input,
                         const Dictionary& init,
                         ExceptionState& exception_state) {
  ASSERT(!input.isNull());
  if (input.isUSVString())
    return Create(script_state, input.getAsUSVString(), init, exception_state);
  return Create(script_state, input.getAsRequest(), init, exception_state);
}

Request* Request::Create(ScriptState* script_state,
                         const String& input,
                         ExceptionState& exception_state) {
  return Create(script_state, input, Dictionary(), exception_state);
}

Request* Request::Create(ScriptState* script_state,
                         const String& input,
                         const Dictionary& init,
                         ExceptionState& exception_state) {
  RequestInit request_init(ExecutionContext::From(script_state), init,
                           exception_state);
  return CreateRequestWithRequestOrString(script_state, nullptr, input,
                                          request_init, exception_state);
}

Request* Request::Create(ScriptState* script_state,
                         Request* input,
                         ExceptionState& exception_state) {
  return Create(script_state, input, Dictionary(), exception_state);
}

Request* Request::Create(ScriptState* script_state,
                         Request* input,
                         const Dictionary& init,
                         ExceptionState& exception_state) {
  RequestInit request_init(ExecutionContext::From(script_state), init,
                           exception_state);
  return CreateRequestWithRequestOrString(script_state, input, String(),
                                          request_init, exception_state);
}

Request* Request::Create(ScriptState* script_state, FetchRequestData* request) {
  return new Request(script_state, request);
}

Request* Request::Create(ScriptState* script_state,
                         const WebServiceWorkerRequest& web_request) {
  FetchRequestData* request =
      FetchRequestData::Create(script_state, web_request);
  return new Request(script_state, request);
}

Request::Request(ScriptState* script_state,
                 FetchRequestData* request,
                 Headers* headers)
    : Body(ExecutionContext::From(script_state)),
      request_(request),
      headers_(headers) {
  RefreshBody(script_state);
}

Request::Request(ScriptState* script_state, FetchRequestData* request)
    : Request(script_state, request, Headers::Create(request->HeaderList())) {
  headers_->SetGuard(Headers::kRequestGuard);
}

String Request::method() const {
  // "The method attribute's getter must return request's method."
  return request_->Method();
}

KURL Request::url() const {
  return request_->Url();
}

String Request::Context() const {
  // "The context attribute's getter must return request's context"
  switch (request_->Context()) {
    case WebURLRequest::kRequestContextUnspecified:
      return "";
    case WebURLRequest::kRequestContextAudio:
      return "audio";
    case WebURLRequest::kRequestContextBeacon:
      return "beacon";
    case WebURLRequest::kRequestContextCSPReport:
      return "cspreport";
    case WebURLRequest::kRequestContextDownload:
      return "download";
    case WebURLRequest::kRequestContextEmbed:
      return "embed";
    case WebURLRequest::kRequestContextEventSource:
      return "eventsource";
    case WebURLRequest::kRequestContextFavicon:
      return "favicon";
    case WebURLRequest::kRequestContextFetch:
      return "fetch";
    case WebURLRequest::kRequestContextFont:
      return "font";
    case WebURLRequest::kRequestContextForm:
      return "form";
    case WebURLRequest::kRequestContextFrame:
      return "frame";
    case WebURLRequest::kRequestContextHyperlink:
      return "hyperlink";
    case WebURLRequest::kRequestContextIframe:
      return "iframe";
    case WebURLRequest::kRequestContextImage:
      return "image";
    case WebURLRequest::kRequestContextImageSet:
      return "imageset";
    case WebURLRequest::kRequestContextImport:
      return "import";
    case WebURLRequest::kRequestContextInternal:
      return "internal";
    case WebURLRequest::kRequestContextLocation:
      return "location";
    case WebURLRequest::kRequestContextManifest:
      return "manifest";
    case WebURLRequest::kRequestContextObject:
      return "object";
    case WebURLRequest::kRequestContextPing:
      return "ping";
    case WebURLRequest::kRequestContextPlugin:
      return "plugin";
    case WebURLRequest::kRequestContextPrefetch:
      return "prefetch";
    case WebURLRequest::kRequestContextScript:
      return "script";
    case WebURLRequest::kRequestContextServiceWorker:
      return "serviceworker";
    case WebURLRequest::kRequestContextSharedWorker:
      return "sharedworker";
    case WebURLRequest::kRequestContextSubresource:
      return "subresource";
    case WebURLRequest::kRequestContextStyle:
      return "style";
    case WebURLRequest::kRequestContextTrack:
      return "track";
    case WebURLRequest::kRequestContextVideo:
      return "video";
    case WebURLRequest::kRequestContextWorker:
      return "worker";
    case WebURLRequest::kRequestContextXMLHttpRequest:
      return "xmlhttprequest";
    case WebURLRequest::kRequestContextXSLT:
      return "xslt";
  }
  ASSERT_NOT_REACHED();
  return "";
}

String Request::referrer() const {
  // "The referrer attribute's getter must return the empty string if
  // request's referrer is no referrer, "about:client" if request's referrer
  // is client and request's referrer, serialized, otherwise."
  ASSERT(FetchRequestData::NoReferrerString() == AtomicString());
  ASSERT(FetchRequestData::ClientReferrerString() ==
         AtomicString("about:client"));
  return request_->ReferrerString();
}

String Request::getReferrerPolicy() const {
  switch (request_->GetReferrerPolicy()) {
    case kReferrerPolicyAlways:
      return "unsafe-url";
    case kReferrerPolicyDefault:
      return "";
    case kReferrerPolicyNoReferrerWhenDowngrade:
      return "no-referrer-when-downgrade";
    case kReferrerPolicyNever:
      return "no-referrer";
    case kReferrerPolicyOrigin:
      return "origin";
    case kReferrerPolicyOriginWhenCrossOrigin:
      return "origin-when-cross-origin";
    case kReferrerPolicyNoReferrerWhenDowngradeOriginWhenCrossOrigin:
      ASSERT(RuntimeEnabledFeatures::reducedReferrerGranularityEnabled());
      return "no-referrer-when-downgrade-origin-when-cross-origin";
  }
  ASSERT_NOT_REACHED();
  return String();
}

String Request::mode() const {
  // "The mode attribute's getter must return the value corresponding to the
  // first matching statement, switching on request's mode:"
  switch (request_->Mode()) {
    case WebURLRequest::kFetchRequestModeSameOrigin:
      return "same-origin";
    case WebURLRequest::kFetchRequestModeNoCORS:
      return "no-cors";
    case WebURLRequest::kFetchRequestModeCORS:
    case WebURLRequest::kFetchRequestModeCORSWithForcedPreflight:
      return "cors";
    case WebURLRequest::kFetchRequestModeNavigate:
      return "navigate";
  }
  ASSERT_NOT_REACHED();
  return "";
}

String Request::credentials() const {
  // "The credentials attribute's getter must return the value corresponding
  // to the first matching statement, switching on request's credentials
  // mode:"
  switch (request_->Credentials()) {
    case WebURLRequest::kFetchCredentialsModeOmit:
      return "omit";
    case WebURLRequest::kFetchCredentialsModeSameOrigin:
      return "same-origin";
    case WebURLRequest::kFetchCredentialsModeInclude:
      return "include";
    case WebURLRequest::kFetchCredentialsModePassword:
      return "password";
  }
  ASSERT_NOT_REACHED();
  return "";
}

String Request::cache() const {
  // "The cache attribute's getter must return request's cache mode."
  switch (request_->CacheMode()) {
    case WebURLRequest::kFetchRequestCacheModeDefault:
      return "default";
    case WebURLRequest::kFetchRequestCacheModeNoStore:
      return "no-store";
    case WebURLRequest::kFetchRequestCacheModeReload:
      return "reload";
    case WebURLRequest::kFetchRequestCacheModeNoCache:
      return "no-cache";
    case WebURLRequest::kFetchRequestCacheModeForceCache:
      return "force-cache";
    case WebURLRequest::kFetchRequestCacheModeOnlyIfCached:
      return "only-if-cached";
  }
  NOTREACHED();
  return "";
}

String Request::redirect() const {
  // "The redirect attribute's getter must return request's redirect mode."
  switch (request_->Redirect()) {
    case WebURLRequest::kFetchRedirectModeFollow:
      return "follow";
    case WebURLRequest::kFetchRedirectModeError:
      return "error";
    case WebURLRequest::kFetchRedirectModeManual:
      return "manual";
  }
  ASSERT_NOT_REACHED();
  return "";
}

String Request::integrity() const {
  return request_->Integrity();
}

Request* Request::clone(ScriptState* script_state,
                        ExceptionState& exception_state) {
  if (IsBodyLocked() || bodyUsed()) {
    exception_state.ThrowTypeError("Request body is already used");
    return nullptr;
  }

  FetchRequestData* request = request_->Clone(script_state);
  RefreshBody(script_state);
  Headers* headers = Headers::Create(request->HeaderList());
  headers->SetGuard(headers_->GetGuard());
  return new Request(script_state, request, headers);
}

FetchRequestData* Request::PassRequestData(ScriptState* script_state) {
  ASSERT(!bodyUsed());
  FetchRequestData* data = request_->Pass(script_state);
  RefreshBody(script_state);
  // |data|'s buffer('s js wrapper) has no retainer, but it's OK because
  // the only caller is the fetch function and it uses the body buffer
  // immediately.
  return data;
}

bool Request::HasBody() const {
  return BodyBuffer();
}

void Request::PopulateWebServiceWorkerRequest(
    WebServiceWorkerRequest& web_request) const {
  web_request.SetMethod(method());
  web_request.SetRequestContext(request_->Context());

  // Strip off the fragment part of URL. So far, all users of
  // WebServiceWorkerRequest expect the fragment to be excluded.
  KURL url(request_->Url());
  if (request_->Url().HasFragmentIdentifier())
    url.RemoveFragmentIdentifier();
  web_request.SetURL(url);

  const FetchHeaderList* header_list = headers_->HeaderList();
  for (size_t i = 0, size = header_list->size(); i < size; ++i) {
    const FetchHeaderList::Header& header = header_list->Entry(i);
    web_request.AppendHeader(header.first, header.second);
  }

  web_request.SetReferrer(
      request_->ReferrerString(),
      static_cast<WebReferrerPolicy>(request_->GetReferrerPolicy()));
  // FIXME: How can we set isReload properly? What is the correct place to load
  // it in to the Request object? We should investigate the right way to plumb
  // this information in to here.
}

String Request::MimeType() const {
  return request_->MimeType();
}

void Request::RefreshBody(ScriptState* script_state) {
  v8::Local<v8::Value> request = ToV8(this, script_state);
  if (request.IsEmpty()) {
    // |toV8| can return an empty handle when the worker is terminating.
    // We don't want the renderer to crash in such cases.
    // TODO(yhirano): Delete this block after the graceful shutdown
    // mechanism is introduced.
    return;
  }
  DCHECK(request->IsObject());
  v8::Local<v8::Value> body_buffer = ToV8(this->BodyBuffer(), script_state);
  V8PrivateProperty::GetInternalBodyBuffer(script_state->GetIsolate())
      .Set(request.As<v8::Object>(), body_buffer);
}

DEFINE_TRACE(Request) {
  Body::Trace(visitor);
  visitor->Trace(request_);
  visitor->Trace(headers_);
}

}  // namespace blink
