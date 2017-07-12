// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IDBValue_h
#define IDBValue_h

#include <memory>
#include "modules/ModulesExport.h"
#include "modules/indexeddb/IDBKey.h"
#include "modules/indexeddb/IDBKeyPath.h"
#include "platform/SharedBuffer.h"
#include "platform/wtf/RefPtr.h"
#include "public/platform/WebVector.h"

namespace blink {

class BlobDataHandle;
class SerializedScriptValue;
class WebBlobInfo;
struct WebIDBValue;

class MODULES_EXPORT IDBValue final : public RefCounted<IDBValue> {
 public:
  static PassRefPtr<IDBValue> Create();
  static PassRefPtr<IDBValue> Create(const WebIDBValue&, v8::Isolate*);
  static PassRefPtr<IDBValue> Create(const IDBValue*,
                                     IDBKey*,
                                     const IDBKeyPath&);
  ~IDBValue();

  bool IsNull() const;
  Vector<String> GetUUIDs() const;
  RefPtr<SerializedScriptValue> CreateSerializedValue() const;
  Vector<WebBlobInfo>* BlobInfo() const { return blob_info_.get(); }
  const IDBKey* PrimaryKey() const { return primary_key_; }
  const IDBKeyPath& KeyPath() const { return key_path_; }

 private:
  IDBValue();
  IDBValue(const WebIDBValue&, v8::Isolate*);
  IDBValue(PassRefPtr<SharedBuffer>,
           const WebVector<WebBlobInfo>&,
           IDBKey*,
           const IDBKeyPath&);
  IDBValue(const IDBValue*, IDBKey*, const IDBKeyPath&);

  // Keep this private to prevent new refs because we manually bookkeep the
  // memory to V8.
  const RefPtr<SharedBuffer> data_;
  const std::unique_ptr<Vector<RefPtr<BlobDataHandle>>> blob_data_;
  const std::unique_ptr<Vector<WebBlobInfo>> blob_info_;
  const Persistent<IDBKey> primary_key_;
  const IDBKeyPath key_path_;
  int64_t external_allocated_size_ = 0;
  // Used to register memory externally allocated by the WebIDBValue, and to
  // unregister that memory in the destructor. Unused in other construction
  // paths.
  v8::Isolate* isolate_ = nullptr;
};

}  // namespace blink

#endif
