// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_PROTOCOL_EVENT_OBSERVER_H_
#define COMPONENTS_SYNC_DRIVER_PROTOCOL_EVENT_OBSERVER_H_

namespace syncer {

class ProtocolEvent;

class ProtocolEventObserver {
 public:
  ProtocolEventObserver();
  virtual ~ProtocolEventObserver();

  virtual void OnProtocolEvent(const ProtocolEvent& event) = 0;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_DRIVER_PROTOCOL_EVENT_OBSERVER_H_
