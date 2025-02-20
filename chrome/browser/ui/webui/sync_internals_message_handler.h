// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_INTERNALS_MESSAGE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_INTERNALS_MESSAGE_HANDLER_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "base/values.h"
#include "components/sync/driver/protocol_event_observer.h"
#include "components/sync/driver/sync_service_observer.h"
#include "components/sync/engine/cycle/type_debug_info_observer.h"
#include "components/sync/js/js_controller.h"
#include "components/sync/js/js_event_handler.h"
#include "content/public/browser/web_ui_message_handler.h"

class SigninManagerBase;

namespace browser_sync {
class ProfileSyncService;
}  // namespace browser_sync

namespace syncer {
class SyncService;
}  //  namespace syncer

// Interface to abstract away the creation of the about-sync value dictionary.
class AboutSyncDataExtractor {
 public:
  // Given state about sync, extracts various interesting fields and populates
  // a tree of base::Value objects.
  virtual std::unique_ptr<base::DictionaryValue> ConstructAboutInformation(
      syncer::SyncService* service,
      SigninManagerBase* signin) = 0;
  virtual ~AboutSyncDataExtractor() {}
};

// The implementation for the chrome://sync-internals page.
class SyncInternalsMessageHandler : public content::WebUIMessageHandler,
                                    public syncer::JsEventHandler,
                                    public syncer::SyncServiceObserver,
                                    public syncer::ProtocolEventObserver,
                                    public syncer::TypeDebugInfoObserver {
 public:
  SyncInternalsMessageHandler();
  ~SyncInternalsMessageHandler() override;

  void RegisterMessages() override;

  // Sets up observers to receive events and forward them to the UI.
  void HandleRegisterForEvents(const base::ListValue* args);

  // Sets up observers to receive per-type counters and forward them to the UI.
  void HandleRegisterForPerTypeCounters(const base::ListValue* args);

  // Fires an event to send updated info back to the page.
  void HandleRequestUpdatedAboutInfo(const base::ListValue* args);

  // Fires and event to send the list of types back to the page.
  void HandleRequestListOfTypes(const base::ListValue* args);

  // Handler for getAllNodes message.  Needs a |request_id| argument.
  void HandleGetAllNodes(const base::ListValue* args);

  // syncer::JsEventHandler implementation.
  void HandleJsEvent(const std::string& name,
                     const syncer::JsEventDetails& details) override;

  // Callback used in GetAllNodes.
  void OnReceivedAllNodes(int request_id,
                          std::unique_ptr<base::ListValue> nodes);

  // syncer::SyncServiceObserver implementation.
  void OnStateChanged() override;

  // ProtocolEventObserver implementation.
  void OnProtocolEvent(const syncer::ProtocolEvent& e) override;

  // TypeDebugInfoObserver implementation.
  void OnCommitCountersUpdated(syncer::ModelType type,
                               const syncer::CommitCounters& counters) override;
  void OnUpdateCountersUpdated(syncer::ModelType type,
                               const syncer::UpdateCounters& counters) override;
  void OnStatusCountersUpdated(syncer::ModelType type,
                               const syncer::StatusCounters& counters) override;

  // Helper to emit counter updates.
  //
  // Used in implementation of On*CounterUpdated methods.  Emits the given
  // dictionary value with additional data to specify the model type and
  // counter type.
  void EmitCounterUpdate(syncer::ModelType type,
                         const std::string& counter_type,
                         std::unique_ptr<base::DictionaryValue> value);

 protected:
  // Constructor used for unit testing to override the about sync info.
  SyncInternalsMessageHandler(
      std::unique_ptr<AboutSyncDataExtractor> about_sync_data_extractor);

 private:
  // Fetches updated aboutInfo and sends it to the page in the form of an
  // onAboutInfoUpdated event.
  void SendAboutInfo();

  browser_sync::ProfileSyncService* GetProfileSyncService();

  base::WeakPtr<syncer::JsController> js_controller_;

  // A flag used to prevent double-registration with ProfileSyncService.
  bool is_registered_ = false;

  // A flag used to prevent double-registration as TypeDebugInfoObserver with
  // ProfileSyncService.
  bool is_registered_for_counters_ = false;

  // An abstraction of who creates the about sync info value map.
  std::unique_ptr<AboutSyncDataExtractor> about_sync_data_extractor_;

  base::WeakPtrFactory<SyncInternalsMessageHandler> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SyncInternalsMessageHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_INTERNALS_MESSAGE_HANDLER_H_
