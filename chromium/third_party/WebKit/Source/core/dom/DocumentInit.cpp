/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "core/dom/DocumentInit.h"

#include "core/dom/Document.h"
#include "core/dom/custom/V0CustomElementRegistrationContext.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/imports/HTMLImportsController.h"
#include "core/loader/DocumentLoader.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/network/NetworkUtils.h"

namespace blink {

// FIXME: Broken with OOPI.
static Document* ParentDocument(LocalFrame* frame) {
  if (!frame)
    return 0;
  Element* owner_element = frame->DeprecatedLocalOwner();
  if (!owner_element)
    return 0;
  return &owner_element->GetDocument();
}

DocumentInit::DocumentInit(const KURL& url,
                           LocalFrame* frame,
                           Document* context_document,
                           HTMLImportsController* imports_controller)
    : DocumentInit(nullptr, url, frame, context_document, imports_controller) {}

DocumentInit::DocumentInit(Document* owner_document,
                           const KURL& url,
                           LocalFrame* frame,
                           Document* context_document,
                           HTMLImportsController* imports_controller)
    : url_(url),
      frame_(frame),
      parent_(ParentDocument(frame)),
      owner_(owner_document),
      context_document_(context_document),
      imports_controller_(imports_controller),
      create_new_registration_context_(false),
      should_reuse_default_view_(frame && frame->ShouldReuseDefaultView(url)) {}

DocumentInit::DocumentInit(const DocumentInit&) = default;

DocumentInit::~DocumentInit() {}

bool DocumentInit::ShouldSetURL() const {
  LocalFrame* frame = FrameForSecurityContext();
  return (frame && frame->Owner()) || !url_.IsEmpty();
}

bool DocumentInit::ShouldTreatURLAsSrcdocDocument() const {
  return parent_ && frame_->Loader().ShouldTreatURLAsSrcdocDocument(url_);
}

LocalFrame* DocumentInit::FrameForSecurityContext() const {
  if (frame_)
    return frame_;
  if (imports_controller_)
    return imports_controller_->Master()->GetFrame();
  return 0;
}

SandboxFlags DocumentInit::GetSandboxFlags() const {
  DCHECK(FrameForSecurityContext());
  FrameLoader* loader = &FrameForSecurityContext()->Loader();
  SandboxFlags flags = loader->EffectiveSandboxFlags();

  // If the load was blocked by CSP, force the Document's origin to be unique,
  // so that the blocked document appears to be a normal cross-origin document's
  // load per CSP spec: https://www.w3.org/TR/CSP3/#directive-frame-ancestors.
  if (loader->GetDocumentLoader() &&
      loader->GetDocumentLoader()->WasBlockedAfterCSP()) {
    flags |= kSandboxOrigin;
  }

  return flags;
}

WebInsecureRequestPolicy DocumentInit::GetInsecureRequestPolicy() const {
  DCHECK(FrameForSecurityContext());
  return FrameForSecurityContext()->Loader().GetInsecureRequestPolicy();
}

SecurityContext::InsecureNavigationsSet*
DocumentInit::InsecureNavigationsToUpgrade() const {
  DCHECK(FrameForSecurityContext());
  return FrameForSecurityContext()->Loader().InsecureNavigationsToUpgrade();
}

bool DocumentInit::IsHostedInReservedIPRange() const {
  if (LocalFrame* frame = FrameForSecurityContext()) {
    if (DocumentLoader* loader =
            frame->Loader().ProvisionalDocumentLoader()
                ? frame->Loader().ProvisionalDocumentLoader()
                : frame->Loader().GetDocumentLoader()) {
      if (!loader->GetResponse().RemoteIPAddress().IsEmpty())
        return NetworkUtils::IsReservedIPAddress(
            loader->GetResponse().RemoteIPAddress());
    }
  }
  return false;
}

Settings* DocumentInit::GetSettings() const {
  DCHECK(FrameForSecurityContext());
  return FrameForSecurityContext()->GetSettings();
}

KURL DocumentInit::ParentBaseURL() const {
  return parent_->BaseURL();
}

DocumentInit& DocumentInit::WithRegistrationContext(
    V0CustomElementRegistrationContext* registration_context) {
  DCHECK(!create_new_registration_context_);
  DCHECK(!registration_context_);
  registration_context_ = registration_context;
  return *this;
}

DocumentInit& DocumentInit::WithNewRegistrationContext() {
  DCHECK(!create_new_registration_context_);
  DCHECK(!registration_context_);
  create_new_registration_context_ = true;
  return *this;
}

V0CustomElementRegistrationContext* DocumentInit::RegistrationContext(
    Document* document) const {
  if (!document->IsHTMLDocument() && !document->IsXHTMLDocument())
    return nullptr;

  if (create_new_registration_context_)
    return V0CustomElementRegistrationContext::Create();

  return registration_context_.Get();
}

Document* DocumentInit::ContextDocument() const {
  return context_document_;
}

DocumentInit DocumentInit::FromContext(Document* context_document,
                                       const KURL& url) {
  return DocumentInit(url, 0, context_document, 0);
}

}  // namespace blink
