// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/indexed_db_database_callbacks.h"

#include "content/browser/indexed_db/indexed_db_context_impl.h"
#include "content/browser/indexed_db/indexed_db_database_error.h"
#include "content/browser/indexed_db/indexed_db_dispatcher_host.h"
#include "content/browser/indexed_db/indexed_db_transaction.h"

using ::indexed_db::mojom::DatabaseCallbacksAssociatedPtrInfo;

namespace content {

class IndexedDBDatabaseCallbacks::IOThreadHelper {
 public:
  explicit IOThreadHelper(DatabaseCallbacksAssociatedPtrInfo callbacks_info);
  ~IOThreadHelper();

  void SendForcedClose();
  void SendVersionChange(int64_t old_version, int64_t new_version);
  void SendAbort(int64_t transaction_id, const IndexedDBDatabaseError& error);
  void SendComplete(int64_t transaction_id);
  void SendChanges(::indexed_db::mojom::ObserverChangesPtr changes);
  void OnConnectionError();

 private:
  ::indexed_db::mojom::DatabaseCallbacksAssociatedPtr callbacks_;

  DISALLOW_COPY_AND_ASSIGN(IOThreadHelper);
};

IndexedDBDatabaseCallbacks::IndexedDBDatabaseCallbacks(
    scoped_refptr<IndexedDBContextImpl> context,
    DatabaseCallbacksAssociatedPtrInfo callbacks_info)
    : indexed_db_context_(std::move(context)),
      io_helper_(new IOThreadHelper(std::move(callbacks_info))) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  thread_checker_.DetachFromThread();
}

IndexedDBDatabaseCallbacks::~IndexedDBDatabaseCallbacks() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void IndexedDBDatabaseCallbacks::OnForcedClose() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (complete_)
    return;

  DCHECK(io_helper_);
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&IOThreadHelper::SendForcedClose,
                                     base::Unretained(io_helper_.get())));
  complete_ = true;
}

void IndexedDBDatabaseCallbacks::OnVersionChange(int64_t old_version,
                                                 int64_t new_version) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (complete_)
    return;

  DCHECK(io_helper_);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendVersionChange,
                 base::Unretained(io_helper_.get()), old_version, new_version));
}

void IndexedDBDatabaseCallbacks::OnAbort(
    const IndexedDBTransaction& transaction,
    const IndexedDBDatabaseError& error) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (complete_)
    return;

  DCHECK(io_helper_);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendAbort, base::Unretained(io_helper_.get()),
                 transaction.id(), error));
}

void IndexedDBDatabaseCallbacks::OnComplete(
    const IndexedDBTransaction& transaction) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (complete_)
    return;

  indexed_db_context_->TransactionComplete(transaction.database()->origin());
  DCHECK(io_helper_);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendComplete,
                 base::Unretained(io_helper_.get()), transaction.id()));
}

void IndexedDBDatabaseCallbacks::OnDatabaseChange(
    ::indexed_db::mojom::ObserverChangesPtr changes) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(io_helper_);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadHelper::SendChanges,
                 base::Unretained(io_helper_.get()), base::Passed(&changes)));
}

IndexedDBDatabaseCallbacks::IOThreadHelper::IOThreadHelper(
    DatabaseCallbacksAssociatedPtrInfo callbacks_info) {
  if (!callbacks_info.is_valid())
    return;
  callbacks_.Bind(std::move(callbacks_info));
  callbacks_.set_connection_error_handler(
      base::Bind(&IOThreadHelper::OnConnectionError, base::Unretained(this)));
}

IndexedDBDatabaseCallbacks::IOThreadHelper::~IOThreadHelper() {}

void IndexedDBDatabaseCallbacks::IOThreadHelper::SendForcedClose() {
  if (callbacks_)
    callbacks_->ForcedClose();
}

void IndexedDBDatabaseCallbacks::IOThreadHelper::SendVersionChange(
    int64_t old_version,
    int64_t new_version) {
  if (callbacks_)
    callbacks_->VersionChange(old_version, new_version);
}

void IndexedDBDatabaseCallbacks::IOThreadHelper::SendAbort(
    int64_t transaction_id,
    const IndexedDBDatabaseError& error) {
  if (callbacks_)
    callbacks_->Abort(transaction_id, error.code(), error.message());
}

void IndexedDBDatabaseCallbacks::IOThreadHelper::SendComplete(
    int64_t transaction_id) {
  if (callbacks_)
    callbacks_->Complete(transaction_id);
}

void IndexedDBDatabaseCallbacks::IOThreadHelper::SendChanges(
    ::indexed_db::mojom::ObserverChangesPtr changes) {
  if (callbacks_)
    callbacks_->Changes(std::move(changes));
}

void IndexedDBDatabaseCallbacks::IOThreadHelper::OnConnectionError() {
  callbacks_.reset();
}

}  // namespace content
