// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_HID_DEVICE_MONITOR_LINUX_H_
#define DEVICE_HID_DEVICE_MONITOR_LINUX_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/observer_list.h"
#include "base/threading/thread_checker.h"
#include "device/core/device_core_export.h"
#include "device/udev_linux/scoped_udev.h"

struct udev_device;

namespace device {

// This class listends for notifications from libudev about
// connected/disconnected devices. This class is *NOT* thread-safe.
class DEVICE_CORE_EXPORT DeviceMonitorLinux
    : public base::MessageLoop::DestructionObserver {
 public:
  typedef base::Callback<void(udev_device* device)> EnumerateCallback;

  class Observer {
   public:
    virtual ~Observer() {}
    virtual void OnDeviceAdded(udev_device* device) = 0;
    virtual void OnDeviceRemoved(udev_device* device) = 0;
    virtual void WillDestroyMonitorMessageLoop() = 0;
  };

  DeviceMonitorLinux();

  static DeviceMonitorLinux* GetInstance();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  ScopedUdevDevicePtr GetDeviceFromPath(const std::string& path);
  void Enumerate(const EnumerateCallback& callback);

  // Implements base::MessageLoop::DestructionObserver
  void WillDestroyCurrentMessageLoop() override;

 private:
  friend std::default_delete<DeviceMonitorLinux>;

  ~DeviceMonitorLinux() override;

  void OnMonitorCanReadWithoutBlocking();

  ScopedUdevPtr udev_;
  ScopedUdevMonitorPtr monitor_;
  int monitor_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      monitor_watch_controller_;

  base::ObserverList<Observer, true> observers_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(DeviceMonitorLinux);
};

}  // namespace device

#endif  // DEVICE_HID_DEVICE_MONITOR_LINUX_H_
