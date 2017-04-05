// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_SYSTEM_STORAGE_SYSTEM_STORAGE_API_H_
#define EXTENSIONS_BROWSER_API_SYSTEM_STORAGE_SYSTEM_STORAGE_API_H_

#include "components/storage_monitor/storage_monitor.h"
#include "extensions/browser/api/system_storage/storage_info_provider.h"
#include "extensions/browser/extension_function.h"

namespace extensions {

// Implementation of the systeminfo.storage.get API. It is an asynchronous
// call relative to browser UI thread.
class SystemStorageGetInfoFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("system.storage.getInfo", SYSTEM_STORAGE_GETINFO);
  SystemStorageGetInfoFunction();

 private:
  ~SystemStorageGetInfoFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;

  void OnGetStorageInfoCompleted(bool success);
};

class SystemStorageEjectDeviceFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("system.storage.ejectDevice",
                             SYSTEM_STORAGE_EJECTDEVICE);

 protected:
  ~SystemStorageEjectDeviceFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  void OnStorageMonitorInit(const std::string& transient_device_id);

  // Eject device request handler.
  void HandleResponse(storage_monitor::StorageMonitor::EjectStatus status);
};

class SystemStorageGetAvailableCapacityFunction
    : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("system.storage.getAvailableCapacity",
                             SYSTEM_STORAGE_GETAVAILABLECAPACITY);
  SystemStorageGetAvailableCapacityFunction();

 private:
  void OnStorageMonitorInit(const std::string& transient_id);
  void OnQueryCompleted(const std::string& transient_id,
                        double available_capacity);
  ~SystemStorageGetAvailableCapacityFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_SYSTEM_STORAGE_SYSTEM_STORAGE_API_H_
