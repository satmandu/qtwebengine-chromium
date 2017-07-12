/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#include "web/DedicatedWorkerMessagingProxyProviderImpl.h"

#include "core/dom/Document.h"
#include "core/workers/DedicatedWorkerMessagingProxy.h"
#include "core/workers/Worker.h"
#include "core/workers/WorkerClients.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebContentSettingsClient.h"
#include "public/platform/WebString.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebWorkerContentSettingsClientProxy.h"
#include "web/IndexedDBClientImpl.h"
#include "web/LocalFileSystemClient.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebViewImpl.h"
#include "web/WorkerContentSettingsClient.h"

namespace blink {

DedicatedWorkerMessagingProxyProviderImpl::
    DedicatedWorkerMessagingProxyProviderImpl(Page& page)
    : DedicatedWorkerMessagingProxyProvider(page) {}

InProcessWorkerMessagingProxy*
DedicatedWorkerMessagingProxyProviderImpl::CreateWorkerMessagingProxy(
    Worker* worker) {
  if (worker->GetExecutionContext()->IsDocument()) {
    Document* document = ToDocument(worker->GetExecutionContext());
    WebLocalFrameImpl* web_frame =
        WebLocalFrameImpl::FromFrame(document->GetFrame());
    WorkerClients* worker_clients = WorkerClients::Create();
    ProvideIndexedDBClientToWorker(
        worker_clients, IndexedDBClientImpl::Create(*worker_clients));
    ProvideLocalFileSystemToWorker(worker_clients,
                                   LocalFileSystemClient::Create());
    ProvideContentSettingsClientToWorker(
        worker_clients,
        WTF::WrapUnique(
            web_frame->Client()->CreateWorkerContentSettingsClientProxy()));
    // FIXME: call provideServiceWorkerContainerClientToWorker here when we
    // support ServiceWorker in dedicated workers (http://crbug.com/371690)
    return new DedicatedWorkerMessagingProxy(worker, worker_clients);
  }
  NOTREACHED();
  return 0;
}

}  // namespace blink
