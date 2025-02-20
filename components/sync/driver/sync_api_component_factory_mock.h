// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_SYNC_API_COMPONENT_FACTORY_MOCK_H__
#define COMPONENTS_SYNC_DRIVER_SYNC_API_COMPONENT_FACTORY_MOCK_H__

#include <memory>
#include <string>

#include "components/sync/api/data_type_error_handler.h"
#include "components/sync/base/model_type.h"
#include "components/sync/driver/data_type_controller.h"
#include "components/sync/driver/sync_api_component_factory.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace syncer {

class AssociatorInterface;
class ChangeProcessor;
class DataTypeEncryptionHandler;
class DataTypeStatusTable;
class SyncClient;

class SyncApiComponentFactoryMock : public SyncApiComponentFactory {
 public:
  SyncApiComponentFactoryMock();
  SyncApiComponentFactoryMock(AssociatorInterface* model_associator,
                              ChangeProcessor* change_processor);
  ~SyncApiComponentFactoryMock() override;

  MOCK_METHOD2(RegisterDataTypes,
               void(SyncService* sync_service, const RegisterDataTypesMethod&));
  MOCK_METHOD5(CreateDataTypeManager,
               DataTypeManager*(const WeakHandle<DataTypeDebugInfoListener>&,
                                const DataTypeController::TypeMap*,
                                const DataTypeEncryptionHandler*,
                                SyncBackendHost*,
                                DataTypeManagerObserver* observer));
  MOCK_METHOD4(CreateSyncBackendHost,
               SyncBackendHost*(const std::string& name,
                                invalidation::InvalidationService* invalidator,
                                const base::WeakPtr<SyncPrefs>& sync_prefs,
                                const base::FilePath& sync_folder));

  std::unique_ptr<LocalDeviceInfoProvider> CreateLocalDeviceInfoProvider()
      override;
  void SetLocalDeviceInfoProvider(
      std::unique_ptr<LocalDeviceInfoProvider> local_device);

  std::unique_ptr<AttachmentService> CreateAttachmentService(
      std::unique_ptr<AttachmentStoreForSync> attachment_store,
      const UserShare& user_share,
      const std::string& store_birthday,
      ModelType model_type,
      AttachmentService::Delegate* delegate) override;
  SyncComponents CreateBookmarkSyncComponents(
      SyncService* sync_service,
      std::unique_ptr<DataTypeErrorHandler> error_handler) override;

 private:
  SyncApiComponentFactory::SyncComponents MakeSyncComponents();

  std::unique_ptr<AssociatorInterface> model_associator_;
  std::unique_ptr<ChangeProcessor> change_processor_;
  // LocalDeviceInfoProvider is initially owned by this class,
  // transferred to caller when CreateLocalDeviceInfoProvider is called.
  std::unique_ptr<LocalDeviceInfoProvider> local_device_;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_DRIVER_SYNC_API_COMPONENT_FACTORY_MOCK_H__
