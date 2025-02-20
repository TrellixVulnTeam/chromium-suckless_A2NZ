// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/api/metadata_batch.h"

#include <utility>

namespace syncer {

MetadataBatch::MetadataBatch() {}
MetadataBatch::~MetadataBatch() {}

EntityMetadataMap&& MetadataBatch::TakeAllMetadata() {
  return std::move(metadata_map_);
}

void MetadataBatch::AddMetadata(const std::string& storage_key,
                                const sync_pb::EntityMetadata& metadata) {
  metadata_map_.insert(std::make_pair(storage_key, metadata));
}

const sync_pb::ModelTypeState& MetadataBatch::GetModelTypeState() const {
  return state_;
}

void MetadataBatch::SetModelTypeState(const sync_pb::ModelTypeState& state) {
  state_ = state;
}

}  // namespace syncer
