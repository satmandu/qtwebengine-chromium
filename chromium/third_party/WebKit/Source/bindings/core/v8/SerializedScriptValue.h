/*
 * Copyright (C) 2009, 2010 Google Inc. All rights reserved.
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

#ifndef SerializedScriptValue_h
#define SerializedScriptValue_h

#include <memory>

#include "bindings/core/v8/NativeValueTraits.h"
#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/Transferables.h"
#include "core/CoreExport.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/ThreadSafeRefCounted.h"
#include "platform/wtf/allocator/Partitions.h"
#include "platform/wtf/typed_arrays/ArrayBufferContents.h"
#include "v8/include/v8.h"

namespace blink {

class BlobDataHandle;
class Transferables;
class ExceptionState;
class StaticBitmapImage;
class WebBlobInfo;

typedef HashMap<String, RefPtr<BlobDataHandle>> BlobDataHandleMap;
typedef Vector<WebBlobInfo> WebBlobInfoArray;

class CORE_EXPORT SerializedScriptValue
    : public ThreadSafeRefCounted<SerializedScriptValue> {
 public:
  using ArrayBufferContentsArray = Vector<WTF::ArrayBufferContents, 1>;
  using ImageBitmapContentsArray = Vector<RefPtr<StaticBitmapImage>, 1>;
  using TransferredWasmModulesArray =
      WTF::Vector<v8::WasmCompiledModule::TransferrableModule>;

  // Increment this for each incompatible change to the wire format.
  // Version 2: Added StringUCharTag for UChar v8 strings.
  // Version 3: Switched to using uuids as blob data identifiers.
  // Version 4: Extended File serialization to be complete.
  // Version 5: Added CryptoKeyTag for Key objects.
  // Version 6: Added indexed serialization for File, Blob, and FileList.
  // Version 7: Extended File serialization with user visibility.
  // Version 8: File.lastModified in milliseconds (seconds-based in earlier
  //            versions.)
  // Version 9: Added Map and Set support.
  // [versions skipped]
  // Version 16: Separate versioning between V8 and Blink.
  static const uint32_t kWireFormatVersion = 16;

  struct SerializeOptions {
    STACK_ALLOCATED();
    Transferables* transferables = nullptr;
    WebBlobInfoArray* blob_info = nullptr;
    bool write_wasm_to_stream = false;
  };
  static PassRefPtr<SerializedScriptValue> Serialize(v8::Isolate*,
                                                     v8::Local<v8::Value>,
                                                     const SerializeOptions&,
                                                     ExceptionState&);
  static PassRefPtr<SerializedScriptValue> SerializeAndSwallowExceptions(
      v8::Isolate*,
      v8::Local<v8::Value>);

  static PassRefPtr<SerializedScriptValue> Create();
  static PassRefPtr<SerializedScriptValue> Create(const String&);
  static PassRefPtr<SerializedScriptValue> Create(const char* data,
                                                  size_t length);

  ~SerializedScriptValue();

  static PassRefPtr<SerializedScriptValue> NullValue();

  String ToWireString() const;
  void ToWireBytes(Vector<char>&) const;

  // Deserializes the value (in the current context). Returns a null value in
  // case of failure.
  struct DeserializeOptions {
    STACK_ALLOCATED();
    MessagePortArray* message_ports = nullptr;
    const WebBlobInfoArray* blob_info = nullptr;
    bool read_wasm_from_stream = false;
  };
  v8::Local<v8::Value> Deserialize(v8::Isolate* isolate) {
    return Deserialize(isolate, DeserializeOptions());
  }
  v8::Local<v8::Value> Deserialize(v8::Isolate*, const DeserializeOptions&);

  // Helper function which pulls the values out of a JS sequence and into a
  // MessagePortArray.  Also validates the elements per sections 4.1.13 and
  // 4.1.15 of the WebIDL spec and section 8.3.3 of the HTML5 spec and generates
  // exceptions as appropriate.
  // Returns true if the array was filled, or false if the passed value was not
  // of an appropriate type.
  static bool ExtractTransferables(v8::Isolate*,
                                   v8::Local<v8::Value>,
                                   int,
                                   Transferables&,
                                   ExceptionState&);

  // Helper function which pulls ArrayBufferContents out of an ArrayBufferArray
  // and neuters the ArrayBufferArray.  Returns nullptr if there is an
  // exception.
  static std::unique_ptr<ArrayBufferContentsArray> TransferArrayBufferContents(
      v8::Isolate*,
      const ArrayBufferArray&,
      ExceptionState&);

  static std::unique_ptr<ImageBitmapContentsArray> TransferImageBitmapContents(
      v8::Isolate*,
      const ImageBitmapArray&,
      ExceptionState&);

  // Informs V8 about external memory allocated and owned by this object.
  // Large values should contribute to GC counters to eventually trigger a GC,
  // otherwise flood of postMessage() can cause OOM.
  // Ok to invoke multiple times (only adds memory once).
  // The memory registration is revoked automatically in destructor.
  void RegisterMemoryAllocatedWithCurrentScriptContext();

  // The dual, unregistering / subtracting the external memory allocation costs
  // of this SerializedScriptValue with the current context. This includes
  // discounting the cost of the transferables.
  //
  // The value is updated and marked as having no allocations registered,
  // hence subsequent calls will be no-ops.
  void UnregisterMemoryAllocatedWithCurrentScriptContext();

  const uint8_t* Data() const { return data_buffer_.get(); }
  size_t DataLengthInBytes() const { return data_buffer_size_; }

  BlobDataHandleMap& BlobDataHandles() { return blob_data_handles_; }
  ArrayBufferContentsArray* GetArrayBufferContentsArray() {
    return array_buffer_contents_array_.get();
  }
  ImageBitmapContentsArray* GetImageBitmapContentsArray() {
    return image_bitmap_contents_array_.get();
  }

  TransferredWasmModulesArray& WasmModules() { return wasm_modules_; }

 private:
  friend class ScriptValueSerializer;
  friend class V8ScriptValueSerializer;

  struct BufferDeleter {
    void operator()(uint8_t* buffer) { WTF::Partitions::BufferFree(buffer); }
  };
  using DataBufferPtr = std::unique_ptr<uint8_t[], BufferDeleter>;

  SerializedScriptValue();
  explicit SerializedScriptValue(const String& wire_data);

  void SetData(DataBufferPtr data, size_t size) {
    data_buffer_ = std::move(data);
    data_buffer_size_ = size;
  }

  void TransferArrayBuffers(v8::Isolate*,
                            const ArrayBufferArray&,
                            ExceptionState&);
  void TransferImageBitmaps(v8::Isolate*,
                            const ImageBitmapArray&,
                            ExceptionState&);
  void TransferOffscreenCanvas(v8::Isolate*,
                               const OffscreenCanvasArray&,
                               ExceptionState&);

  DataBufferPtr data_buffer_;
  size_t data_buffer_size_ = 0;

  std::unique_ptr<ArrayBufferContentsArray> array_buffer_contents_array_;
  std::unique_ptr<ImageBitmapContentsArray> image_bitmap_contents_array_;
  TransferredWasmModulesArray wasm_modules_;

  BlobDataHandleMap blob_data_handles_;

  bool has_registered_external_allocation_;
  bool transferables_need_external_allocation_registration_;
};

template <>
struct NativeValueTraits<SerializedScriptValue>
    : public NativeValueTraitsBase<SerializedScriptValue> {
  CORE_EXPORT static inline PassRefPtr<SerializedScriptValue> NativeValue(
      v8::Isolate* isolate,
      v8::Local<v8::Value> value,
      ExceptionState& exception_state) {
    return SerializedScriptValue::Serialize(
        isolate, value, SerializedScriptValue::SerializeOptions(),
        exception_state);
  }
};

}  // namespace blink

#endif  // SerializedScriptValue_h
