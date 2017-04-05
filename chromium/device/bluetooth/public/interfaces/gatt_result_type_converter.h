// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_GATT_RESULT_TYPE_CONVERTER_H_
#define DEVICE_BLUETOOTH_GATT_RESULT_TYPE_CONVERTER_H_

#include "device/bluetooth/bluetooth_gatt_service.h"
#include "device/bluetooth/public/interfaces/device.mojom.h"
#include "mojo/public/cpp/bindings/type_converter.h"

namespace mojo {

// TypeConverter to translate from
// device::BluetoothGattService::GattErrorCode to bluetooth.mojom.GattResult.
// TODO(crbug.com/666561): Replace because TypeConverter is deprecated.
// This TypeConverter is needed to work around the fact that the Mojo enum,
// GattResult, has more values than the C++ enum, GattErrorCode.
template <>
struct TypeConverter<bluetooth::mojom::GattResult,
                     device::BluetoothGattService::GattErrorCode> {
  static bluetooth::mojom::GattResult Convert(
      const device::BluetoothGattService::GattErrorCode& input) {
    switch (input) {
      case device::BluetoothGattService::GattErrorCode::GATT_ERROR_UNKNOWN:
        return bluetooth::mojom::GattResult::UNKNOWN;
      case device::BluetoothGattService::GattErrorCode::GATT_ERROR_FAILED:
        return bluetooth::mojom::GattResult::FAILED;
      case device::BluetoothGattService::GattErrorCode::GATT_ERROR_IN_PROGRESS:
        return bluetooth::mojom::GattResult::IN_PROGRESS;
      case device::BluetoothGattService::GattErrorCode::
          GATT_ERROR_INVALID_LENGTH:
        return bluetooth::mojom::GattResult::INVALID_LENGTH;
      case device::BluetoothGattService::GattErrorCode::
          GATT_ERROR_NOT_PERMITTED:
        return bluetooth::mojom::GattResult::NOT_PERMITTED;
      case device::BluetoothGattService::GattErrorCode::
          GATT_ERROR_NOT_AUTHORIZED:
        return bluetooth::mojom::GattResult::NOT_AUTHORIZED;
      case device::BluetoothGattService::GattErrorCode::GATT_ERROR_NOT_PAIRED:
        return bluetooth::mojom::GattResult::NOT_PAIRED;
      case device::BluetoothGattService::GattErrorCode::
          GATT_ERROR_NOT_SUPPORTED:
        return bluetooth::mojom::GattResult::NOT_SUPPORTED;
    }
    NOTREACHED();
    return bluetooth::mojom::GattResult::NOT_SUPPORTED;
  }
};
}

#endif  // DEVICE_BLUETOOTH_GATT_RESULT_TYPE_CONVERTER_H_
