// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cryptauth/fake_secure_channel.h"

#include "base/logging.h"

namespace cryptauth {

FakeSecureChannel::SentMessage::SentMessage(const std::string& feature,
                                            const std::string& payload)
    : feature(feature), payload(payload) {}

FakeSecureChannel::FakeSecureChannel(std::unique_ptr<Connection> connection,
                                     std::unique_ptr<Delegate> delegate)
    : SecureChannel(std::move(connection), std::move(delegate)) {}

FakeSecureChannel::~FakeSecureChannel() {}

void FakeSecureChannel::ChangeStatus(const Status& new_status) {
  Status old_status = status_;
  status_ = new_status;

  // Copy to prevent channel from being removed during handler.
  std::vector<Observer*> observers_copy = observers_;
  for (auto* observer : observers_copy) {
    observer->OnSecureChannelStatusChanged(this, old_status, status_);
  }
}

void FakeSecureChannel::ReceiveMessage(const std::string& feature,
                                       const std::string& payload) {
  // Copy to prevent channel from being removed during handler.
  std::vector<Observer*> observers_copy = observers_;
  for (auto* observer : observers_copy) {
    observer->OnMessageReceived(this, feature, payload);
  }
}

void FakeSecureChannel::Initialize() {}

void FakeSecureChannel::SendMessage(const std::string& feature,
                                    const std::string& payload) {
  sent_messages_.push_back(SentMessage(feature, payload));
}

void FakeSecureChannel::Disconnect() {
  ChangeStatus(Status::DISCONNECTED);
}

void FakeSecureChannel::AddObserver(Observer* observer) {
  observers_.push_back(observer);
}

void FakeSecureChannel::RemoveObserver(Observer* observer) {
  observers_.erase(std::find(observers_.begin(), observers_.end(), observer),
                   observers_.end());
}

}  // namespace cryptauth
