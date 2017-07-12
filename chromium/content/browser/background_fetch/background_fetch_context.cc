// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/background_fetch/background_fetch_context.h"

#include "base/memory/ptr_util.h"
#include "content/browser/background_fetch/background_fetch_data_manager.h"
#include "content/browser/background_fetch/background_fetch_event_dispatcher.h"
#include "content/browser/background_fetch/background_fetch_job_controller.h"
#include "content/browser/background_fetch/background_fetch_registration_id.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/blob_handle.h"
#include "content/public/browser/browser_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/origin.h"

namespace content {

namespace {

// Records the |error| status issued by the DataManager after it was requested
// to create and store a new Background Fetch registration.
void RecordRegistrationCreatedError(blink::mojom::BackgroundFetchError error) {
  // TODO(peter): Add UMA.
}

// Records the |error| status issued by the DataManager after the storage
// associated with a registration has been completely deleted.
void RecordRegistrationDeletedError(blink::mojom::BackgroundFetchError error) {
  // TODO(peter): Add UMA.
}

}  // namespace

BackgroundFetchContext::BackgroundFetchContext(
    BrowserContext* browser_context,
    StoragePartitionImpl* storage_partition,
    scoped_refptr<ServiceWorkerContextWrapper> service_worker_context)
    : browser_context_(browser_context),
      data_manager_(
          base::MakeUnique<BackgroundFetchDataManager>(browser_context)),
      event_dispatcher_(base::MakeUnique<BackgroundFetchEventDispatcher>(
          std::move(service_worker_context))) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

BackgroundFetchContext::~BackgroundFetchContext() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

void BackgroundFetchContext::InitializeOnIOThread(
    scoped_refptr<net::URLRequestContextGetter> request_context_getter) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  request_context_getter_ = request_context_getter;
}

void BackgroundFetchContext::Shutdown() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&BackgroundFetchContext::ShutdownOnIO, this));
}

void BackgroundFetchContext::ShutdownOnIO() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  active_fetches_.clear();
}

void BackgroundFetchContext::StartFetch(
    const BackgroundFetchRegistrationId& registration_id,
    const std::vector<ServiceWorkerFetchRequest>& requests,
    const BackgroundFetchOptions& options,
    const blink::mojom::BackgroundFetchService::FetchCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  data_manager_->CreateRegistration(
      registration_id, requests, options,
      base::BindOnce(&BackgroundFetchContext::DidCreateRegistration, this,
                     registration_id, options, callback));
}

void BackgroundFetchContext::DidCreateRegistration(
    const BackgroundFetchRegistrationId& registration_id,
    const BackgroundFetchOptions& options,
    const blink::mojom::BackgroundFetchService::FetchCallback& callback,
    blink::mojom::BackgroundFetchError error,
    std::vector<scoped_refptr<BackgroundFetchRequestInfo>> initial_requests) {
  RecordRegistrationCreatedError(error);
  if (error != blink::mojom::BackgroundFetchError::NONE) {
    callback.Run(error, base::nullopt /* registration */);
    return;
  }

  // Create the BackgroundFetchJobController, which will do the actual fetching.
  CreateController(registration_id, options, std::move(initial_requests));

  // Create the BackgroundFetchRegistration the renderer process will receive,
  // which enables it to resolve the promise telling the developer it worked.
  BackgroundFetchRegistration registration;
  registration.tag = registration_id.tag();
  registration.icons = options.icons;
  registration.title = options.title;
  registration.total_download_size = options.total_download_size;

  callback.Run(blink::mojom::BackgroundFetchError::NONE, registration);
}

std::vector<std::string>
BackgroundFetchContext::GetActiveTagsForServiceWorkerRegistration(
    int64_t service_worker_registration_id,
    const url::Origin& origin) const {
  std::vector<std::string> tags;
  for (const auto& pair : active_fetches_) {
    const BackgroundFetchRegistrationId& registration_id =
        pair.second->registration_id();

    // Only return the tags when the origin and SW registration id match.
    if (registration_id.origin() == origin &&
        registration_id.service_worker_registration_id() ==
            service_worker_registration_id) {
      tags.push_back(pair.second->registration_id().tag());
    }
  }

  return tags;
}

BackgroundFetchJobController* BackgroundFetchContext::GetActiveFetch(
    const BackgroundFetchRegistrationId& registration_id) const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  auto iter = active_fetches_.find(registration_id);
  if (iter == active_fetches_.end())
    return nullptr;

  BackgroundFetchJobController* controller = iter->second.get();
  if (controller->state() == BackgroundFetchJobController::State::ABORTED ||
      controller->state() == BackgroundFetchJobController::State::COMPLETED) {
    return nullptr;
  }

  return controller;
}

void BackgroundFetchContext::CreateController(
    const BackgroundFetchRegistrationId& registration_id,
    const BackgroundFetchOptions& options,
    std::vector<scoped_refptr<BackgroundFetchRequestInfo>> initial_requests) {
  std::unique_ptr<BackgroundFetchJobController> controller =
      base::MakeUnique<BackgroundFetchJobController>(
          registration_id, options, data_manager_.get(), browser_context_,
          request_context_getter_,
          base::BindOnce(&BackgroundFetchContext::DidCompleteJob, this));

  // TODO(peter): We should actually be able to use Background Fetch in layout
  // tests. That requires a download manager and a request context.
  if (request_context_getter_) {
    // Start fetching the |initial_requests| immediately. At some point in the
    // future we may want a more elaborate scheduling mechanism here.
    controller->Start(std::move(initial_requests));
  }

  active_fetches_.insert(
      std::make_pair(registration_id, std::move(controller)));
}

void BackgroundFetchContext::DidCompleteJob(
    BackgroundFetchJobController* controller) {
  const BackgroundFetchRegistrationId& registration_id =
      controller->registration_id();

  DCHECK_GT(active_fetches_.count(registration_id), 0u);

  // TODO(peter): Fire `backgroundfetchabort` if the |controller|'s state is
  // ABORTED, which does not require a sequence of the settled fetches.

  // The `backgroundfetched` and/or `backgroundfetchfail` event will only be
  // invoked for Background Fetch jobs which have been completed.
  if (controller->state() != BackgroundFetchJobController::State::COMPLETED) {
    DeleteRegistration(registration_id,
                       std::vector<std::unique_ptr<BlobHandle>>());
    return;
  }

  // Get the sequence of settled fetches from the data manager.
  data_manager_->GetSettledFetchesForRegistration(
      registration_id,
      base::BindOnce(&BackgroundFetchContext::DidGetSettledFetches, this,
                     registration_id));
}

void BackgroundFetchContext::DidGetSettledFetches(
    const BackgroundFetchRegistrationId& registration_id,
    blink::mojom::BackgroundFetchError error,
    std::vector<BackgroundFetchSettledFetch> settled_fetches,
    std::vector<std::unique_ptr<BlobHandle>> blob_handles) {
  if (error != blink::mojom::BackgroundFetchError::NONE) {
    DeleteRegistration(registration_id, std::move(blob_handles));
    return;
  }

  // TODO(peter): Distinguish between the `backgroundfetched` and
  // `backgroundfetchfail` events based on the status code of all fetches. We
  // don't populate that field yet, so always assume it's successful for now.

  event_dispatcher_->DispatchBackgroundFetchedEvent(
      registration_id, std::move(settled_fetches),
      base::Bind(&BackgroundFetchContext::DeleteRegistration, this,
                 registration_id, std::move(blob_handles)));
}

void BackgroundFetchContext::DeleteRegistration(
    const BackgroundFetchRegistrationId& registration_id,
    const std::vector<std::unique_ptr<BlobHandle>>& blob_handles) {
  DCHECK_GT(active_fetches_.count(registration_id), 0u);

  // Delete all persistent information associated with the |registration_id|.
  data_manager_->DeleteRegistration(
      registration_id, base::BindOnce(&RecordRegistrationDeletedError));

  // Delete the local state associated with the |registration_id|.
  active_fetches_.erase(registration_id);
}

}  // namespace content
