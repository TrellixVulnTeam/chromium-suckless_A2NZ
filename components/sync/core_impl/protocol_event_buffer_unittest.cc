// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/core_impl/protocol_event_buffer.h"

#include <stdint.h>

#include "base/time/time.h"
#include "components/sync/engine/events/poll_get_updates_request_event.h"
#include "components/sync/engine/events/protocol_event.h"
#include "components/sync/protocol/sync.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

class ProtocolEventBufferTest : public ::testing::Test {
 public:
  ProtocolEventBufferTest();
  ~ProtocolEventBufferTest() override;

  static std::unique_ptr<ProtocolEvent> MakeTestEvent(int64_t id);
  static bool HasId(const ProtocolEvent& event, int64_t id);

 protected:
  ProtocolEventBuffer buffer_;
};

ProtocolEventBufferTest::ProtocolEventBufferTest() {}

ProtocolEventBufferTest::~ProtocolEventBufferTest() {}

std::unique_ptr<ProtocolEvent> ProtocolEventBufferTest::MakeTestEvent(
    int64_t id) {
  sync_pb::ClientToServerMessage message;
  return std::unique_ptr<ProtocolEvent>(new PollGetUpdatesRequestEvent(
      base::Time::FromInternalValue(id), message));
}

bool ProtocolEventBufferTest::HasId(const ProtocolEvent& event, int64_t id) {
  return event.GetTimestamp() == base::Time::FromInternalValue(id);
}

TEST_F(ProtocolEventBufferTest, AddThenReturnEvents) {
  std::unique_ptr<ProtocolEvent> e1(MakeTestEvent(1));
  std::unique_ptr<ProtocolEvent> e2(MakeTestEvent(2));

  buffer_.RecordProtocolEvent(*e1);
  buffer_.RecordProtocolEvent(*e2);

  std::vector<std::unique_ptr<ProtocolEvent>> buffered_events(
      buffer_.GetBufferedProtocolEvents());

  ASSERT_EQ(2U, buffered_events.size());
  EXPECT_TRUE(HasId(*(buffered_events[0]), 1));
  EXPECT_TRUE(HasId(*(buffered_events[1]), 2));
}

TEST_F(ProtocolEventBufferTest, AddThenOverflowThenReturnEvents) {
  for (size_t i = 0; i < ProtocolEventBuffer::kBufferSize + 1; ++i) {
    std::unique_ptr<ProtocolEvent> e(MakeTestEvent(static_cast<int64_t>(i)));
    buffer_.RecordProtocolEvent(*e);
  }

  std::vector<std::unique_ptr<ProtocolEvent>> buffered_events(
      buffer_.GetBufferedProtocolEvents());
  ASSERT_EQ(ProtocolEventBuffer::kBufferSize, buffered_events.size());

  for (size_t i = 1; i < ProtocolEventBuffer::kBufferSize + 1; ++i) {
    EXPECT_TRUE(HasId(*(buffered_events[i - 1]), static_cast<int64_t>(i)));
  }
}

}  // namespace syncer
