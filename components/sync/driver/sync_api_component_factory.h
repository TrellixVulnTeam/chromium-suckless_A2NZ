// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_SYNC_API_COMPONENT_FACTORY_H_
#define COMPONENTS_SYNC_DRIVER_SYNC_API_COMPONENT_FACTORY_H_

#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "components/sync/api/data_type_error_handler.h"
#include "components/sync/api/syncable_service.h"
#include "components/sync/base/model_type.h"
#include "components/sync/core/attachments/attachment_service.h"
#include "components/sync/driver/data_type_controller.h"

namespace base {
class FilePath;
}  // namespace base

namespace history {
class HistoryBackend;
}

namespace invalidation {
class InvalidationService;
}  // namespace invalidation

namespace syncer {

class AssociatorInterface;
class ChangeProcessor;
class DataTypeDebugInfoListener;
class DataTypeEncryptionHandler;
class DataTypeManager;
class DataTypeManagerObserver;
class DataTypeStatusTable;
class GenericChangeProcessor;
class LocalDeviceInfoProvider;
class SyncBackendHost;
class SyncClient;
class SyncPrefs;
class SyncService;
class SyncableService;
struct UserShare;

// This factory provides sync driver code with the model type specific sync/api
// service (like SyncableService) implementations.
class SyncApiComponentFactory {
 public:
  virtual ~SyncApiComponentFactory() {}
  // Callback to allow platform-specific datatypes to register themselves as
  // data type controllers.
  // |disabled_types| and |enabled_types| control the disable/enable state of
  // types that are on or off by default (respectively).
  typedef base::Callback<void(SyncService* sync_service,
                              ModelTypeSet disabled_types,
                              ModelTypeSet enabled_types)>
      RegisterDataTypesMethod;

  // The various factory methods for the data type model associators
  // and change processors all return this struct.  This is needed
  // because the change processors typically require a type-specific
  // model associator at construction time.
  //
  // Note: This interface is deprecated in favor of the SyncableService API.
  // New datatypes that do not live on the UI thread should directly return a
  // weak pointer to a SyncableService. All others continue to return
  // SyncComponents. It is safe to assume that the factory methods below are
  // called on the same thread in which the datatype resides.
  //
  // TODO(zea): Have all datatypes using the new API switch to returning
  // SyncableService weak pointers instead of SyncComponents (crbug.com/100114).
  struct SyncComponents {
    AssociatorInterface* model_associator;
    ChangeProcessor* change_processor;
    SyncComponents(AssociatorInterface* ma, ChangeProcessor* cp)
        : model_associator(ma), change_processor(cp) {}
  };

  // Creates and registers enabled datatypes with the provided SyncClient.
  virtual void RegisterDataTypes(
      SyncService* sync_service,
      const RegisterDataTypesMethod& register_platform_types_method) = 0;

  // Instantiates a new DataTypeManager with a SyncBackendHost, a list of data
  // type controllers and a DataTypeManagerObserver.  The return pointer is
  // owned by the caller.
  virtual DataTypeManager* CreateDataTypeManager(
      const WeakHandle<DataTypeDebugInfoListener>& debug_info_listener,
      const DataTypeController::TypeMap* controllers,
      const DataTypeEncryptionHandler* encryption_handler,
      SyncBackendHost* backend,
      DataTypeManagerObserver* observer) = 0;

  // Creating this in the factory helps us mock it out in testing.
  virtual SyncBackendHost* CreateSyncBackendHost(
      const std::string& name,
      invalidation::InvalidationService* invalidator,
      const base::WeakPtr<SyncPrefs>& sync_prefs,
      const base::FilePath& sync_folder) = 0;

  // Creating this in the factory helps us mock it out in testing.
  virtual std::unique_ptr<LocalDeviceInfoProvider>
  CreateLocalDeviceInfoProvider() = 0;

  // Legacy datatypes that need to be converted to the SyncableService API.
  virtual SyncComponents CreateBookmarkSyncComponents(
      SyncService* sync_service,
      std::unique_ptr<DataTypeErrorHandler> error_handler) = 0;

  // Creates attachment service.
  // Note: Should only be called from the model type thread.
  //
  // |store_birthday| is the store birthday.  Must not be empty.
  //
  // |model_type| is the model type this AttachmentService will be used with.
  //
  // |delegate| is optional delegate for AttachmentService to notify about
  // asynchronous events (AttachmentUploaded). Pass NULL if delegate is not
  // provided. AttachmentService doesn't take ownership of delegate, the pointer
  // must be valid throughout AttachmentService lifetime.
  virtual std::unique_ptr<AttachmentService> CreateAttachmentService(
      std::unique_ptr<AttachmentStoreForSync> attachment_store,
      const UserShare& user_share,
      const std::string& store_birthday,
      ModelType model_type,
      AttachmentService::Delegate* delegate) = 0;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_DRIVER_SYNC_API_COMPONENT_FACTORY_H_
