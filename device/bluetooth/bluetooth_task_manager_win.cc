// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_task_manager_win.h"

#include <stddef.h>
#include <winsock2.h>

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/sequenced_worker_pool.h"
#include "device/bluetooth/bluetooth_classic_win.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_init_win.h"
#include "device/bluetooth/bluetooth_service_record_win.h"
#include "net/base/winsock_init.h"

namespace {

const int kNumThreadsInWorkerPool = 3;
const char kBluetoothThreadName[] = "BluetoothPollingThreadWin";
const int kMaxNumDeviceAddressChar = 127;
const int kServiceDiscoveryResultBufferSize = 5000;

// See http://goo.gl/iNTRQe: cTimeoutMultiplier: A value that indicates the time
// out for the inquiry, expressed in increments of 1.28 seconds. For example, an
// inquiry of 12.8 seconds has a cTimeoutMultiplier value of 10. The maximum
// value for this member is 48. When a value greater than 48 is used, the
// calling function immediately fails and returns
const int kMaxDeviceDiscoveryTimeoutMultiplier = 48;

typedef device::BluetoothTaskManagerWin::ServiceRecordState ServiceRecordState;

// Note: The string returned here must have the same format as
// BluetoothDevice::CanonicalizeAddress.
std::string BluetoothAddressToCanonicalString(const BLUETOOTH_ADDRESS& btha) {
  std::string result = base::StringPrintf("%02X:%02X:%02X:%02X:%02X:%02X",
                                          btha.rgBytes[5],
                                          btha.rgBytes[4],
                                          btha.rgBytes[3],
                                          btha.rgBytes[2],
                                          btha.rgBytes[1],
                                          btha.rgBytes[0]);
  DCHECK_EQ(result, device::BluetoothDevice::CanonicalizeAddress(result));
  return result;
}

bool BluetoothUUIDToWinBLEUUID(const device::BluetoothUUID& uuid,
                               BTH_LE_UUID* out_win_uuid) {
  if (!uuid.IsValid())
    return false;

  if (uuid.format() == device::BluetoothUUID::kFormat16Bit) {
    out_win_uuid->IsShortUuid = TRUE;
    unsigned int data = 0;
    int result = sscanf_s(uuid.value().c_str(), "%04x", &data);
    if (result != 1)
      return false;
    out_win_uuid->Value.ShortUuid = data;
  } else {
    out_win_uuid->IsShortUuid = FALSE;
    unsigned int data[11];
    int result =
        sscanf_s(uuid.value().c_str(),
                 "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", &data[0],
                 &data[1], &data[2], &data[3], &data[4], &data[5], &data[6],
                 &data[7], &data[8], &data[9], &data[10]);
    if (result != 11)
      return false;
    out_win_uuid->Value.LongUuid.Data1 = data[0];
    out_win_uuid->Value.LongUuid.Data2 = data[1];
    out_win_uuid->Value.LongUuid.Data3 = data[2];
    out_win_uuid->Value.LongUuid.Data4[0] = data[3];
    out_win_uuid->Value.LongUuid.Data4[1] = data[4];
    out_win_uuid->Value.LongUuid.Data4[2] = data[5];
    out_win_uuid->Value.LongUuid.Data4[3] = data[6];
    out_win_uuid->Value.LongUuid.Data4[4] = data[7];
    out_win_uuid->Value.LongUuid.Data4[5] = data[8];
    out_win_uuid->Value.LongUuid.Data4[6] = data[9];
    out_win_uuid->Value.LongUuid.Data4[7] = data[10];
  }

  return true;
}

// Populates bluetooth adapter state using adapter_handle.
void GetAdapterState(HANDLE adapter_handle,
                     device::BluetoothTaskManagerWin::AdapterState* state) {
  std::string name;
  std::string address;
  bool powered = false;
  BLUETOOTH_RADIO_INFO adapter_info = {sizeof(BLUETOOTH_RADIO_INFO)};
  if (adapter_handle &&
      ERROR_SUCCESS ==
          device::win::BluetoothClassicWrapper::GetInstance()->GetRadioInfo(
              adapter_handle, &adapter_info)) {
    name = base::SysWideToUTF8(adapter_info.szName);
    address = BluetoothAddressToCanonicalString(adapter_info.address);
    powered =
        !!device::win::BluetoothClassicWrapper::GetInstance()->IsConnectable(
            adapter_handle);
  }
  state->name = name;
  state->address = address;
  state->powered = powered;
}

void GetDeviceState(const BLUETOOTH_DEVICE_INFO& device_info,
                    device::BluetoothTaskManagerWin::DeviceState* state) {
  state->name = base::SysWideToUTF8(device_info.szName);
  state->address = BluetoothAddressToCanonicalString(device_info.Address);
  state->bluetooth_class = device_info.ulClassofDevice;
  state->visible = true;
  state->connected = !!device_info.fConnected;
  state->authenticated = !!device_info.fAuthenticated;
}

struct CharacteristicValueChangedRegistration {
  CharacteristicValueChangedRegistration();
  ~CharacteristicValueChangedRegistration();

  BLUETOOTH_GATT_EVENT_HANDLE win_event_handle;
  device::BluetoothTaskManagerWin::GattCharacteristicValueChangedCallback
      callback;
  // The task runner the callback should run on.
  scoped_refptr<base::SequencedTaskRunner> callback_task_runner;
};

CharacteristicValueChangedRegistration::
    CharacteristicValueChangedRegistration() {}
CharacteristicValueChangedRegistration::
    ~CharacteristicValueChangedRegistration() {}

// The key of CharacteristicValueChangedRegistrationMap is a
// GattCharacteristicValueChangedCallback pointer (cast to PVOID) to make it
// unique for different callbacks. It is also the context value passed into OS
// when registering event.
typedef std::unordered_map<
    PVOID,
    std::unique_ptr<CharacteristicValueChangedRegistration>>
    CharacteristicValueChangedRegistrationMap;

CharacteristicValueChangedRegistrationMap
    g_characteristic_value_changed_registrations;
base::Lock g_characteristic_value_changed_registrations_lock;

// Function to be registered to OS to monitor Bluetooth LE GATT event. It is
// invoked in BluetoothApis.dll thread.
void CALLBACK OnGetGattEventWin(BTH_LE_GATT_EVENT_TYPE type,
                                PVOID event_parameter,
                                PVOID context) {
  if (type != CharacteristicValueChangedEvent) {
    // Right now, only characteristic value changed event is supported.
    NOTREACHED();
    return;
  }

  BLUETOOTH_GATT_VALUE_CHANGED_EVENT* event =
      (BLUETOOTH_GATT_VALUE_CHANGED_EVENT*)event_parameter;
  PBTH_LE_GATT_CHARACTERISTIC_VALUE new_value_win = event->CharacteristicValue;
  std::unique_ptr<std::vector<uint8_t>> new_value(
      new std::vector<uint8_t>(new_value_win->DataSize));
  for (ULONG i = 0; i < new_value_win->DataSize; i++)
    (*new_value)[i] = new_value_win->Data[i];

  base::AutoLock auto_lock(g_characteristic_value_changed_registrations_lock);
  CharacteristicValueChangedRegistrationMap::const_iterator it =
      g_characteristic_value_changed_registrations.find(context);
  if (it == g_characteristic_value_changed_registrations.end())
    return;

  it->second->callback_task_runner->PostTask(
      FROM_HERE, base::Bind(it->second->callback, base::Passed(&new_value)));
}

}  // namespace

namespace device {

// static
const int BluetoothTaskManagerWin::kPollIntervalMs = 500;

BluetoothTaskManagerWin::AdapterState::AdapterState() : powered(false) {
}

BluetoothTaskManagerWin::AdapterState::~AdapterState() {
}

BluetoothTaskManagerWin::ServiceRecordState::ServiceRecordState() {
}

BluetoothTaskManagerWin::ServiceRecordState::~ServiceRecordState() {
}

BluetoothTaskManagerWin::DeviceState::DeviceState()
    : visible(false),
      connected(false),
      authenticated(false),
      bluetooth_class(0) {
}

BluetoothTaskManagerWin::DeviceState::~DeviceState() {
}

BluetoothTaskManagerWin::BluetoothTaskManagerWin(
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner)
    : ui_task_runner_(ui_task_runner),
      adapter_handle_(NULL),
      discovering_(false),
      current_logging_batch_count_(0) {}

BluetoothTaskManagerWin::~BluetoothTaskManagerWin() {
  win::BluetoothLowEnergyWrapper::DeleteInstance();
  win::BluetoothClassicWrapper::DeleteInstance();
}

BluetoothUUID BluetoothTaskManagerWin::BluetoothLowEnergyUuidToBluetoothUuid(
    const BTH_LE_UUID& bth_le_uuid) {
  if (bth_le_uuid.IsShortUuid) {
    std::string uuid_hex =
        base::StringPrintf("%04x", bth_le_uuid.Value.ShortUuid);
    return BluetoothUUID(uuid_hex);
  } else {
    return BluetoothUUID(base::StringPrintf(
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bth_le_uuid.Value.LongUuid.Data1, bth_le_uuid.Value.LongUuid.Data2,
        bth_le_uuid.Value.LongUuid.Data3, bth_le_uuid.Value.LongUuid.Data4[0],
        bth_le_uuid.Value.LongUuid.Data4[1],
        bth_le_uuid.Value.LongUuid.Data4[2],
        bth_le_uuid.Value.LongUuid.Data4[3],
        bth_le_uuid.Value.LongUuid.Data4[4],
        bth_le_uuid.Value.LongUuid.Data4[5],
        bth_le_uuid.Value.LongUuid.Data4[6],
        bth_le_uuid.Value.LongUuid.Data4[7]));
  }
}

void BluetoothTaskManagerWin::AddObserver(Observer* observer) {
  DCHECK(observer);
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  observers_.AddObserver(observer);
}

void BluetoothTaskManagerWin::RemoveObserver(Observer* observer) {
  DCHECK(observer);
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  observers_.RemoveObserver(observer);
}

void BluetoothTaskManagerWin::Initialize() {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  worker_pool_ = new base::SequencedWorkerPool(
      kNumThreadsInWorkerPool, kBluetoothThreadName,
      base::TaskPriority::USER_VISIBLE);
  InitializeWithBluetoothTaskRunner(
      worker_pool_->GetSequencedTaskRunnerWithShutdownBehavior(
          worker_pool_->GetSequenceToken(),
          base::SequencedWorkerPool::CONTINUE_ON_SHUTDOWN));
}

void BluetoothTaskManagerWin::InitializeWithBluetoothTaskRunner(
    scoped_refptr<base::SequencedTaskRunner> bluetooth_task_runner) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_ = bluetooth_task_runner;
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::StartPolling, this));
}

void BluetoothTaskManagerWin::StartPolling() {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());

  if (device::bluetooth_init_win::HasBluetoothStack()) {
    PollAdapter();
  } else {
    // IF the bluetooth stack is not available, we still send an empty state
    // to BluetoothAdapter so that it is marked initialized, but the adapter
    // will not be present.
    AdapterState* state = new AdapterState();
    ui_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::OnAdapterStateChanged,
                 this,
                 base::Owned(state)));
  }
}

void BluetoothTaskManagerWin::Shutdown() {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  if (worker_pool_.get())
    worker_pool_->Shutdown();
}

void BluetoothTaskManagerWin::PostSetPoweredBluetoothTask(
    bool powered,
    const base::Closure& callback,
    const BluetoothAdapter::ErrorCallback& error_callback) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::SetPowered,
                 this,
                 powered,
                 callback,
                 error_callback));
}

void BluetoothTaskManagerWin::PostStartDiscoveryTask() {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::StartDiscovery, this));
}

void BluetoothTaskManagerWin::PostStopDiscoveryTask() {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::StopDiscovery, this));
}

void BluetoothTaskManagerWin::LogPollingError(const char* message,
                                              int win32_error) {
  const int kLogPeriodInMilliseconds = 60 * 1000;
  const int kMaxMessagesPerLogPeriod = 10;

  // Check if we need to discard this message
  if (!current_logging_batch_ticks_.is_null()) {
    if (base::TimeTicks::Now() - current_logging_batch_ticks_ <=
        base::TimeDelta::FromMilliseconds(kLogPeriodInMilliseconds)) {
      if (current_logging_batch_count_ >= kMaxMessagesPerLogPeriod)
        return;
    } else {
      // The batch expired, reset it to "null".
      current_logging_batch_ticks_ = base::TimeTicks();
    }
  }

  // Keep track of this batch of messages
  if (current_logging_batch_ticks_.is_null()) {
    current_logging_batch_ticks_ = base::TimeTicks::Now();
    current_logging_batch_count_ = 0;
  }
  ++current_logging_batch_count_;

  // Log the message
  if (win32_error == 0)
    LOG(WARNING) << message;
  else
    LOG(WARNING) << message << ": "
                 << logging::SystemErrorCodeToString(win32_error);
}

void BluetoothTaskManagerWin::OnAdapterStateChanged(const AdapterState* state) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  FOR_EACH_OBSERVER(BluetoothTaskManagerWin::Observer, observers_,
                    AdapterStateChanged(*state));
}

void BluetoothTaskManagerWin::OnDiscoveryStarted(bool success) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  FOR_EACH_OBSERVER(BluetoothTaskManagerWin::Observer, observers_,
                    DiscoveryStarted(success));
}

void BluetoothTaskManagerWin::OnDiscoveryStopped() {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  FOR_EACH_OBSERVER(BluetoothTaskManagerWin::Observer, observers_,
                    DiscoveryStopped());
}

void BluetoothTaskManagerWin::OnDevicesPolled(
    const ScopedVector<DeviceState>* devices) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  FOR_EACH_OBSERVER(
      BluetoothTaskManagerWin::Observer, observers_, DevicesPolled(*devices));
}

void BluetoothTaskManagerWin::PollAdapter() {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());

  // Skips updating the adapter info if the adapter is in discovery mode.
  if (!discovering_) {
    const BLUETOOTH_FIND_RADIO_PARAMS adapter_param =
        { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
    HANDLE temp_adapter_handle;
    HBLUETOOTH_RADIO_FIND handle =
        win::BluetoothClassicWrapper::GetInstance()->FindFirstRadio(
            &adapter_param, &temp_adapter_handle);

    if (handle) {
      adapter_handle_ = temp_adapter_handle;
      GetKnownDevices();
      win::BluetoothClassicWrapper::GetInstance()->FindRadioClose(handle);
    }

    PostAdapterStateToUi();
  }

  // Re-poll.
  bluetooth_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::PollAdapter,
                 this),
      base::TimeDelta::FromMilliseconds(kPollIntervalMs));
}

void BluetoothTaskManagerWin::PostAdapterStateToUi() {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  AdapterState* state = new AdapterState();
  GetAdapterState(adapter_handle_, state);
  ui_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::OnAdapterStateChanged,
                 this,
                 base::Owned(state)));
}

void BluetoothTaskManagerWin::SetPowered(
    bool powered,
    const base::Closure& callback,
    const BluetoothAdapter::ErrorCallback& error_callback) {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  bool success = false;
  if (adapter_handle_) {
    if (!powered) {
      win::BluetoothClassicWrapper::GetInstance()->EnableDiscovery(
          adapter_handle_, false);
    }
    success = !!win::BluetoothClassicWrapper::GetInstance()
                    ->EnableIncomingConnections(adapter_handle_, powered);
  }

  if (success) {
    PostAdapterStateToUi();
    ui_task_runner_->PostTask(FROM_HERE, callback);
  } else {
    ui_task_runner_->PostTask(FROM_HERE, error_callback);
  }
}

void BluetoothTaskManagerWin::StartDiscovery() {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  ui_task_runner_->PostTask(
      FROM_HERE, base::Bind(&BluetoothTaskManagerWin::OnDiscoveryStarted, this,
                            !!adapter_handle_));
  if (!adapter_handle_)
    return;
  discovering_ = true;

  DiscoverDevices(1);
}

void BluetoothTaskManagerWin::StopDiscovery() {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  discovering_ = false;
  ui_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::OnDiscoveryStopped, this));
}

void BluetoothTaskManagerWin::DiscoverDevices(int timeout_multiplier) {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  if (!discovering_ || !adapter_handle_) {
    ui_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&BluetoothTaskManagerWin::OnDiscoveryStopped, this));
    return;
  }

  std::unique_ptr<ScopedVector<DeviceState>> device_list(
      new ScopedVector<DeviceState>());
  if (SearchDevices(timeout_multiplier, false, device_list.get())) {
    ui_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&BluetoothTaskManagerWin::OnDevicesPolled,
                   this,
                   base::Owned(device_list.release())));
  }

  if (timeout_multiplier < kMaxDeviceDiscoveryTimeoutMultiplier)
    ++timeout_multiplier;
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(
          &BluetoothTaskManagerWin::DiscoverDevices, this, timeout_multiplier));
}

void BluetoothTaskManagerWin::GetKnownDevices() {
  std::unique_ptr<ScopedVector<DeviceState>> device_list(
      new ScopedVector<DeviceState>());
  if (SearchDevices(1, true, device_list.get())) {
    ui_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&BluetoothTaskManagerWin::OnDevicesPolled,
                   this,
                   base::Owned(device_list.release())));
  }
}

bool BluetoothTaskManagerWin::SearchDevices(
    int timeout_multiplier,
    bool search_cached_devices_only,
    ScopedVector<DeviceState>* device_list) {
  return SearchClassicDevices(
             timeout_multiplier, search_cached_devices_only, device_list) &&
         SearchLowEnergyDevices(device_list) &&
         DiscoverServices(device_list, search_cached_devices_only);
}

bool BluetoothTaskManagerWin::SearchClassicDevices(
    int timeout_multiplier,
    bool search_cached_devices_only,
    ScopedVector<DeviceState>* device_list) {
  // Issues a device inquiry and waits for |timeout_multiplier| * 1.28 seconds.
  BLUETOOTH_DEVICE_SEARCH_PARAMS device_search_params;
  ZeroMemory(&device_search_params, sizeof(device_search_params));
  device_search_params.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
  device_search_params.fReturnAuthenticated = 1;
  device_search_params.fReturnRemembered = 1;
  device_search_params.fReturnUnknown = (search_cached_devices_only ? 0 : 1);
  device_search_params.fReturnConnected = 1;
  device_search_params.fIssueInquiry = (search_cached_devices_only ? 0 : 1);
  device_search_params.cTimeoutMultiplier = timeout_multiplier;

  BLUETOOTH_DEVICE_INFO device_info;
  ZeroMemory(&device_info, sizeof(device_info));
  device_info.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
  HBLUETOOTH_DEVICE_FIND handle =
      win::BluetoothClassicWrapper::GetInstance()->FindFirstDevice(
          &device_search_params, &device_info);
  if (!handle) {
    int last_error = win::BluetoothClassicWrapper::GetInstance()->LastError();
    if (last_error == ERROR_NO_MORE_ITEMS) {
      return true;  // No devices is not an error.
    }
    LogPollingError("Error calling BluetoothFindFirstDevice", last_error);
    return false;
  }

  while (true) {
    DeviceState* device_state = new DeviceState();
    GetDeviceState(device_info, device_state);
    device_list->push_back(device_state);

    // Reset device info before next call (as a safety precaution).
    ZeroMemory(&device_info, sizeof(device_info));
    device_info.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
    if (!win::BluetoothClassicWrapper::GetInstance()->FindNextDevice(
            handle, &device_info)) {
      int last_error = win::BluetoothClassicWrapper::GetInstance()->LastError();
      if (last_error == ERROR_NO_MORE_ITEMS) {
        break;  // No more items is expected error when done enumerating.
      }
      LogPollingError("Error calling BluetoothFindNextDevice", last_error);
      win::BluetoothClassicWrapper::GetInstance()->FindDeviceClose(handle);
      return false;
    }
  }

  if (!win::BluetoothClassicWrapper::GetInstance()->FindDeviceClose(handle)) {
    LogPollingError("Error calling BluetoothFindDeviceClose",
                    win::BluetoothClassicWrapper::GetInstance()->LastError());
    return false;
  }
  return true;
}

bool BluetoothTaskManagerWin::SearchLowEnergyDevices(
    ScopedVector<DeviceState>* device_list) {
  if (!win::BluetoothLowEnergyWrapper::GetInstance()
           ->IsBluetoothLowEnergySupported()) {
    return true;  // Bluetooth LE not supported is not an error.
  }

  ScopedVector<win::BluetoothLowEnergyDeviceInfo> btle_devices;
  std::string error;
  bool success =
      win::BluetoothLowEnergyWrapper::GetInstance()
          ->EnumerateKnownBluetoothLowEnergyDevices(&btle_devices, &error);
  if (!success) {
    LogPollingError(error.c_str(), 0);
    return false;
  }

  for (ScopedVector<win::BluetoothLowEnergyDeviceInfo>::iterator iter =
           btle_devices.begin();
       iter != btle_devices.end();
       ++iter) {
    win::BluetoothLowEnergyDeviceInfo* device_info = (*iter);
    DeviceState* device_state = new DeviceState();
    device_state->name = device_info->friendly_name;
    device_state->address =
        BluetoothAddressToCanonicalString(device_info->address);
    device_state->visible = device_info->visible;
    device_state->authenticated = device_info->authenticated;
    device_state->connected = device_info->connected;
    device_state->path = device_info->path;
    device_list->push_back(device_state);
  }
  return true;
}

bool BluetoothTaskManagerWin::DiscoverServices(
    ScopedVector<DeviceState>* device_list,
    bool search_cached_services_only) {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  net::EnsureWinsockInit();
  for (ScopedVector<DeviceState>::iterator iter = device_list->begin();
      iter != device_list->end();
      ++iter) {
    DeviceState* device = (*iter);
    ScopedVector<ServiceRecordState>* service_record_states =
        &(*iter)->service_record_states;

    if ((*iter)->is_bluetooth_classic()) {
      if (!DiscoverClassicDeviceServices(device->address,
                                         L2CAP_PROTOCOL_UUID,
                                         search_cached_services_only,
                                         service_record_states)) {
        return false;
      }
    } else {
      if (!DiscoverLowEnergyDeviceServices(device->path,
                                           service_record_states)) {
        return false;
      }
      if (!SearchForGattServiceDevicePaths(device->address,
                                           service_record_states)) {
        return false;
      }
    }
  }
  return true;
}

bool BluetoothTaskManagerWin::DiscoverClassicDeviceServices(
    const std::string& device_address,
    const GUID& protocol_uuid,
    bool search_cached_services_only,
    ScopedVector<ServiceRecordState>* service_record_states) {
  int error_code =
      DiscoverClassicDeviceServicesWorker(device_address,
                                          protocol_uuid,
                                          search_cached_services_only,
                                          service_record_states);
  // If the device is "offline", no services are returned when specifying
  // "LUP_FLUSHCACHE". Try again without flushing the cache so that the list
  // of previously known services is returned.
  if (!search_cached_services_only &&
      (error_code == WSASERVICE_NOT_FOUND || error_code == WSANO_DATA)) {
    error_code = DiscoverClassicDeviceServicesWorker(
        device_address, protocol_uuid, true, service_record_states);
  }

  return (error_code == ERROR_SUCCESS);
}

int BluetoothTaskManagerWin::DiscoverClassicDeviceServicesWorker(
    const std::string& device_address,
    const GUID& protocol_uuid,
    bool search_cached_services_only,
    ScopedVector<ServiceRecordState>* service_record_states) {
  // Bluetooth and WSAQUERYSET for Service Inquiry. See http://goo.gl/2v9pyt.
  WSAQUERYSET sdp_query;
  ZeroMemory(&sdp_query, sizeof(sdp_query));
  sdp_query.dwSize = sizeof(sdp_query);
  GUID protocol = protocol_uuid;
  sdp_query.lpServiceClassId = &protocol;
  sdp_query.dwNameSpace = NS_BTH;
  wchar_t device_address_context[kMaxNumDeviceAddressChar];
  std::size_t length = base::SysUTF8ToWide("(" + device_address + ")").copy(
      device_address_context, kMaxNumDeviceAddressChar);
  device_address_context[length] = NULL;
  sdp_query.lpszContext = device_address_context;
  DWORD control_flags = LUP_RETURN_ALL;
  // See http://goo.gl/t1Hulo: "Applications should generally specify
  // LUP_FLUSHCACHE. This flag instructs the system to ignore any cached
  // information and establish an over-the-air SDP connection to the specified
  // device to perform the SDP search. This non-cached operation may take
  // several seconds (whereas a cached search returns quickly)."
  // In summary, we need to specify LUP_FLUSHCACHE if we want to obtain the list
  // of services for devices which have not been discovered before.
  if (!search_cached_services_only)
    control_flags |= LUP_FLUSHCACHE;
  HANDLE sdp_handle;
  if (ERROR_SUCCESS !=
      WSALookupServiceBegin(&sdp_query, control_flags, &sdp_handle)) {
    int last_error = WSAGetLastError();
    // If the device is "offline", no services are returned when specifying
    // "LUP_FLUSHCACHE". Don't log error in that case.
    if (!search_cached_services_only &&
        (last_error == WSASERVICE_NOT_FOUND || last_error == WSANO_DATA)) {
      return last_error;
    }
    LogPollingError("Error calling WSALookupServiceBegin", last_error);
    return last_error;
  }
  char sdp_buffer[kServiceDiscoveryResultBufferSize];
  LPWSAQUERYSET sdp_result_data = reinterpret_cast<LPWSAQUERYSET>(sdp_buffer);
  while (true) {
    DWORD sdp_buffer_size = sizeof(sdp_buffer);
    if (ERROR_SUCCESS !=
        WSALookupServiceNext(
            sdp_handle, control_flags, &sdp_buffer_size, sdp_result_data)) {
      int last_error = WSAGetLastError();
      if (last_error == WSA_E_NO_MORE || last_error == WSAENOMORE) {
        break;
      }
      LogPollingError("Error calling WSALookupServiceNext", last_error);
      WSALookupServiceEnd(sdp_handle);
      return last_error;
    }
    ServiceRecordState* service_record_state = new ServiceRecordState();
    service_record_state->name =
        base::SysWideToUTF8(sdp_result_data->lpszServiceInstanceName);
    for (uint64_t i = 0; i < sdp_result_data->lpBlob->cbSize; i++) {
      service_record_state->sdp_bytes.push_back(
          sdp_result_data->lpBlob->pBlobData[i]);
    }
    service_record_states->push_back(service_record_state);
  }
  if (ERROR_SUCCESS != WSALookupServiceEnd(sdp_handle)) {
    int last_error = WSAGetLastError();
    LogPollingError("Error calling WSALookupServiceEnd", last_error);
    return last_error;
  }

  return ERROR_SUCCESS;
}

bool BluetoothTaskManagerWin::DiscoverLowEnergyDeviceServices(
    const base::FilePath& device_path,
    ScopedVector<ServiceRecordState>* service_record_states) {
  if (!win::BluetoothLowEnergyWrapper::GetInstance()
           ->IsBluetoothLowEnergySupported()) {
    return true;  // Bluetooth LE not supported is not an error.
  }

  std::string error;
  ScopedVector<win::BluetoothLowEnergyServiceInfo> services;
  bool success = win::BluetoothLowEnergyWrapper::GetInstance()
                     ->EnumerateKnownBluetoothLowEnergyServices(
                         device_path, &services, &error);
  if (!success) {
    LogPollingError(error.c_str(), 0);
    return false;
  }

  for (ScopedVector<win::BluetoothLowEnergyServiceInfo>::iterator iter2 =
           services.begin();
       iter2 != services.end();
       ++iter2) {
    ServiceRecordState* service_state = new ServiceRecordState();
    service_state->gatt_uuid =
        BluetoothLowEnergyUuidToBluetoothUuid((*iter2)->uuid);
    service_state->attribute_handle = (*iter2)->attribute_handle;
    service_record_states->push_back(service_state);
  }
  return true;
}

// Each GATT service of a BLE device will be listed on the machine as a BLE
// device interface with a matching service attribute handle. This interface
// lists all GATT service devices and matches them back to correspond GATT
// service of the BLE device according to their address and included service
// attribute handles, as we did not find a more neat way to bond them.
bool BluetoothTaskManagerWin::SearchForGattServiceDevicePaths(
    const std::string device_address,
    ScopedVector<ServiceRecordState>* service_record_states) {
  std::string error;

  // List all known GATT service devices on the machine.
  ScopedVector<win::BluetoothLowEnergyDeviceInfo> gatt_service_devices;
  bool success = win::BluetoothLowEnergyWrapper::GetInstance()
                     ->EnumerateKnownBluetoothLowEnergyGattServiceDevices(
                         &gatt_service_devices, &error);
  if (!success) {
    LogPollingError(error.c_str(), 0);
    return false;
  }

  for (auto* gatt_service_device : gatt_service_devices) {
    // Only care about the service devices with |device_address|.
    if (BluetoothAddressToCanonicalString(gatt_service_device->address) !=
        device_address) {
      continue;
    }

    // Discover this service device's contained services.
    ScopedVector<win::BluetoothLowEnergyServiceInfo> gatt_services;
    if (!win::BluetoothLowEnergyWrapper::GetInstance()
             ->EnumerateKnownBluetoothLowEnergyServices(
                 gatt_service_device->path, &gatt_services, &error)) {
      LogPollingError(error.c_str(), 0);
      continue;
    }

    // Usually each service device correspond to one Gatt service.
    if (gatt_services.size() > 1) {
      LOG(WARNING) << "This GATT service device contains more than one ("
                   << gatt_services.size() << ") services";
    }

    // Associate service device to corresponding service record. Attribute
    // handle is unique on one device.
    for (auto* gatt_service : gatt_services) {
      for (auto* service_record_state : *service_record_states) {
        if (service_record_state->attribute_handle ==
            gatt_service->attribute_handle) {
          service_record_state->path = gatt_service_device->path;
          break;
        }
      }
    }
  }

  return true;
}

void BluetoothTaskManagerWin::GetGattIncludedCharacteristics(
    base::FilePath service_path,
    BluetoothUUID uuid,
    uint16_t attribute_handle,
    const GetGattIncludedCharacteristicsCallback& callback) {
  HRESULT hr = S_OK;
  std::unique_ptr<BTH_LE_GATT_CHARACTERISTIC> win_characteristics_info;
  uint16_t number_of_charateristics = 0;

  BTH_LE_GATT_SERVICE win_service;
  if (BluetoothUUIDToWinBLEUUID(uuid, &(win_service.ServiceUuid))) {
    win_service.AttributeHandle = attribute_handle;
    hr = win::BluetoothLowEnergyWrapper::GetInstance()
             ->ReadCharacteristicsOfAService(service_path, &win_service,
                                             &win_characteristics_info,
                                             &number_of_charateristics);
  } else {
    hr = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
  }

  ui_task_runner_->PostTask(
      FROM_HERE, base::Bind(callback, base::Passed(&win_characteristics_info),
                            number_of_charateristics, hr));
}

void BluetoothTaskManagerWin::GetGattIncludedDescriptors(
    base::FilePath service_path,
    BTH_LE_GATT_CHARACTERISTIC characteristic,
    const GetGattIncludedDescriptorsCallback& callback) {
  std::unique_ptr<BTH_LE_GATT_DESCRIPTOR> win_descriptors_info;
  uint16_t number_of_descriptors = 0;

  HRESULT hr =
      win::BluetoothLowEnergyWrapper::GetInstance()
          ->ReadDescriptorsOfACharacteristic(
              service_path, (PBTH_LE_GATT_CHARACTERISTIC)(&characteristic),
              &win_descriptors_info, &number_of_descriptors);

  ui_task_runner_->PostTask(
      FROM_HERE, base::Bind(callback, base::Passed(&win_descriptors_info),
                            number_of_descriptors, hr));
}

void BluetoothTaskManagerWin::ReadGattCharacteristicValue(
    base::FilePath service_path,
    BTH_LE_GATT_CHARACTERISTIC characteristic,
    const ReadGattCharacteristicValueCallback& callback) {
  std::unique_ptr<BTH_LE_GATT_CHARACTERISTIC_VALUE> win_characteristic_value;
  HRESULT hr =
      win::BluetoothLowEnergyWrapper::GetInstance()->ReadCharacteristicValue(
          service_path, (PBTH_LE_GATT_CHARACTERISTIC)(&characteristic),
          &win_characteristic_value);

  ui_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(callback, base::Passed(&win_characteristic_value), hr));
}

void BluetoothTaskManagerWin::WriteGattCharacteristicValue(
    base::FilePath service_path,
    BTH_LE_GATT_CHARACTERISTIC characteristic,
    std::vector<uint8_t> new_value,
    const HResultCallback& callback) {
  ULONG length = (ULONG)(sizeof(ULONG) + new_value.size());
  std::unique_ptr<BTH_LE_GATT_CHARACTERISTIC_VALUE> win_new_value(
      (PBTH_LE_GATT_CHARACTERISTIC_VALUE)(new UCHAR[length]));
  win_new_value->DataSize = (ULONG)new_value.size();
  for (ULONG i = 0; i < new_value.size(); i++)
    win_new_value->Data[i] = new_value[i];

  HRESULT hr =
      win::BluetoothLowEnergyWrapper::GetInstance()->WriteCharacteristicValue(
          service_path, (PBTH_LE_GATT_CHARACTERISTIC)(&characteristic),
          win_new_value.get());

  ui_task_runner_->PostTask(FROM_HERE, base::Bind(callback, hr));
}

void BluetoothTaskManagerWin::RegisterGattCharacteristicValueChangedEvent(
    base::FilePath service_path,
    BTH_LE_GATT_CHARACTERISTIC characteristic,
    BTH_LE_GATT_DESCRIPTOR ccc_descriptor,
    const GattEventRegistrationCallback& callback,
    const GattCharacteristicValueChangedCallback& registered_callback) {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());
  BLUETOOTH_GATT_EVENT_HANDLE win_event_handle = NULL;

  BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION win_event_parameter;
  memcpy(&(win_event_parameter.Characteristics[0]), &characteristic,
         sizeof(BTH_LE_GATT_CHARACTERISTIC));
  win_event_parameter.NumCharacteristics = 1;
  PVOID user_event_handle = (PVOID)&registered_callback;
  HRESULT hr =
      win::BluetoothLowEnergyWrapper::GetInstance()->RegisterGattEvents(
          service_path, CharacteristicValueChangedEvent, &win_event_parameter,
          &OnGetGattEventWin, user_event_handle, &win_event_handle);

  // Sets the Client Characteristic Configuration descriptor.
  if (SUCCEEDED(hr)) {
    BTH_LE_GATT_DESCRIPTOR_VALUE new_cccd_value;
    RtlZeroMemory(&new_cccd_value, sizeof(new_cccd_value));
    new_cccd_value.DescriptorType = ClientCharacteristicConfiguration;
    if (characteristic.IsNotifiable) {
      new_cccd_value.ClientCharacteristicConfiguration
          .IsSubscribeToNotification = TRUE;
    } else {
      new_cccd_value.ClientCharacteristicConfiguration.IsSubscribeToIndication =
          TRUE;
    }

    hr = win::BluetoothLowEnergyWrapper::GetInstance()->WriteDescriptorValue(
        service_path, (PBTH_LE_GATT_DESCRIPTOR)(&ccc_descriptor),
        &new_cccd_value);
  }

  if (SUCCEEDED(hr)) {
    std::unique_ptr<CharacteristicValueChangedRegistration> registration(
        new CharacteristicValueChangedRegistration());
    registration->win_event_handle = win_event_handle;
    registration->callback = registered_callback;
    registration->callback_task_runner = ui_task_runner_;
    base::AutoLock auto_lock(g_characteristic_value_changed_registrations_lock);
    g_characteristic_value_changed_registrations[user_event_handle] =
        std::move(registration);
  }

  ui_task_runner_->PostTask(FROM_HERE,
                            base::Bind(callback, user_event_handle, hr));
}

void BluetoothTaskManagerWin::UnregisterGattCharacteristicValueChangedEvent(
    PVOID event_handle) {
  DCHECK(bluetooth_task_runner_->RunsTasksOnCurrentThread());

  base::AutoLock auto_lock(g_characteristic_value_changed_registrations_lock);
  CharacteristicValueChangedRegistrationMap::const_iterator it =
      g_characteristic_value_changed_registrations.find(event_handle);
  if (it != g_characteristic_value_changed_registrations.end()) {
    win::BluetoothLowEnergyWrapper::GetInstance()->UnregisterGattEvent(
        it->second->win_event_handle);
    g_characteristic_value_changed_registrations.erase(event_handle);
  }
}

void BluetoothTaskManagerWin::PostGetGattIncludedCharacteristics(
    const base::FilePath& service_path,
    const BluetoothUUID& uuid,
    uint16_t attribute_handle,
    const GetGattIncludedCharacteristicsCallback& callback) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::GetGattIncludedCharacteristics, this,
                 service_path, uuid, attribute_handle, callback));
}

void BluetoothTaskManagerWin::PostGetGattIncludedDescriptors(
    const base::FilePath& service_path,
    const PBTH_LE_GATT_CHARACTERISTIC characteristic,
    const GetGattIncludedDescriptorsCallback& callback) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::GetGattIncludedDescriptors, this,
                 service_path, *characteristic, callback));
}

void BluetoothTaskManagerWin::PostReadGattCharacteristicValue(
    const base::FilePath& service_path,
    const PBTH_LE_GATT_CHARACTERISTIC characteristic,
    const ReadGattCharacteristicValueCallback& callback) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::ReadGattCharacteristicValue, this,
                 service_path, *characteristic, callback));
}

void BluetoothTaskManagerWin::PostWriteGattCharacteristicValue(
    const base::FilePath& service_path,
    const PBTH_LE_GATT_CHARACTERISTIC characteristic,
    const std::vector<uint8_t>& new_value,
    const HResultCallback& callback) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&BluetoothTaskManagerWin::WriteGattCharacteristicValue, this,
                 service_path, *characteristic, new_value, callback));
}

void BluetoothTaskManagerWin::PostRegisterGattCharacteristicValueChangedEvent(
    const base::FilePath& service_path,
    const PBTH_LE_GATT_CHARACTERISTIC characteristic,
    const PBTH_LE_GATT_DESCRIPTOR ccc_descriptor,
    const GattEventRegistrationCallback& callback,
    const GattCharacteristicValueChangedCallback& registered_callback) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(
          &BluetoothTaskManagerWin::RegisterGattCharacteristicValueChangedEvent,
          this, service_path, *characteristic, *ccc_descriptor, callback,
          registered_callback));
}

void BluetoothTaskManagerWin::PostUnregisterGattCharacteristicValueChangedEvent(
    PVOID event_handle) {
  DCHECK(ui_task_runner_->RunsTasksOnCurrentThread());
  bluetooth_task_runner_->PostTask(
      FROM_HERE, base::Bind(&BluetoothTaskManagerWin::
                                UnregisterGattCharacteristicValueChangedEvent,
                            this, event_handle));
}

}  // namespace device
