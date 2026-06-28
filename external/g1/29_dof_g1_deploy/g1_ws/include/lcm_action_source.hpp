#ifndef G1_DEPLOY_REAL_LCM_ACTION_SOURCE_HPP
#define G1_DEPLOY_REAL_LCM_ACTION_SOURCE_HPP

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "action_source.hpp"
#include "lcm_protocol.hpp"

#ifdef G1_HAS_LCM
#include <lcm/lcm.h>
#endif

class LcmActionSource final : public ActionSource {
 public:
  LcmActionSource(std::string lcm_url, std::string channel)
      : lcm_url_(std::move(lcm_url)), channel_(std::move(channel)) {}

  bool Start() override {
#ifdef G1_HAS_LCM
    lcm_ = lcm_create(lcm_url_.empty() ? nullptr : lcm_url_.c_str());
    if (!lcm_) {
      std::cerr << "[lcm-action] Failed to create LCM instance";
      if (!lcm_url_.empty()) {
        std::cerr << " for URL: " << lcm_url_;
      }
      std::cerr << '\n';
      return false;
    }

    subscription_ = lcm_subscribe(lcm_, channel_.c_str(), &LcmActionSource::OnMessage, this);
    if (!subscription_) {
      std::cerr << "[lcm-action] Failed to subscribe channel: " << channel_ << '\n';
      lcm_destroy(lcm_);
      lcm_ = nullptr;
      return false;
    }

    running_.store(true);
    thread_ = std::thread(&LcmActionSource::HandleLoop, this);
    std::cout << "[lcm-action] Subscribed channel: " << channel_ << '\n';
    return true;
#else
    std::cerr << "[lcm-action] This binary was built without LCM support.\n"
              << "[lcm-action] Install liblcm-dev/lcm-tools and rebuild g1_ws.\n";
    return false;
#endif
  }

  void Stop() override {
    running_.store(false);
#ifdef G1_HAS_LCM
    if (thread_.joinable()) {
      thread_.join();
    }
    if (lcm_) {
      if (subscription_) {
        lcm_unsubscribe(lcm_, subscription_);
        subscription_ = nullptr;
      }
      lcm_destroy(lcm_);
      lcm_ = nullptr;
    }
#endif
  }

  std::optional<ActionCommand> Poll() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
  }

  std::string Name() const override { return "lcm:" + channel_; }

 private:
#ifdef G1_HAS_LCM
  static void OnMessage(const lcm_recv_buf_t* buffer, const char* channel, void* user) {
    static_cast<LcmActionSource*>(user)->HandleMessage(buffer, channel);
  }

  void HandleLoop() {
    while (running_.load()) {
      if (!lcm_) {
        return;
      }
      const int result = lcm_handle_timeout(lcm_, 100);
      if (result < 0 && running_.load()) {
        std::cerr << "[lcm-action] lcm_handle_timeout failed.\n";
      }
    }
  }

  void HandleMessage(const lcm_recv_buf_t* buffer, const char*) {
    if (!buffer) {
      ++bad_packet_count_;
      if (bad_packet_count_ <= 5 || bad_packet_count_ % 100 == 0) {
        std::cerr << "[lcm-action] Bad packet size count=" << bad_packet_count_.load()
                  << " size=0"
                  << " expected_v1=" << sizeof(g1_lcm::PolicyActionPacketV1)
                  << " expected_v2=" << sizeof(g1_lcm::PolicyActionPacketV2)
                  << " expected_v3=" << sizeof(g1_lcm::PolicyActionPacketV3) << '\n';
      }
      return;
    }

    ActionCommand command;
    uint64_t packet_sequence = 0;

    if (buffer->data_size == sizeof(g1_lcm::PolicyActionPacketV1)) {
      g1_lcm::PolicyActionPacketV1 packet;
      std::memcpy(&packet, buffer->data, sizeof(packet));
      if (packet.magic != g1_lcm::kPolicyActionMagic ||
          packet.version != g1_lcm::kProtocolVersionV1) {
        ++bad_packet_count_;
        if (bad_packet_count_ <= 5 || bad_packet_count_ % 100 == 0) {
          std::cerr << "[lcm-action] Bad v1 packet header count=" << bad_packet_count_.load()
                    << " magic=0x" << std::hex << packet.magic << std::dec
                    << " version=" << packet.version << '\n';
        }
        return;
      }

      FillBaseCommand(packet, command);
      packet_sequence = packet.sequence;
    } else if (buffer->data_size == sizeof(g1_lcm::PolicyActionPacketV2)) {
      g1_lcm::PolicyActionPacketV2 packet;
      std::memcpy(&packet, buffer->data, sizeof(packet));
      if (packet.magic != g1_lcm::kPolicyActionMagic ||
          packet.version != g1_lcm::kProtocolVersionV2) {
        ++bad_packet_count_;
        if (bad_packet_count_ <= 5 || bad_packet_count_ % 100 == 0) {
          std::cerr << "[lcm-action] Bad v2 packet header count=" << bad_packet_count_.load()
                    << " magic=0x" << std::hex << packet.magic << std::dec
                    << " version=" << packet.version << '\n';
        }
        return;
      }

      FillBaseCommand(packet, command);
      command.has_left_hand_q = (packet.flags & g1_lcm::kActionFlagHasLeftHandQ) != 0;
      command.has_right_hand_q = (packet.flags & g1_lcm::kActionFlagHasRightHandQ) != 0;
      if (command.has_left_hand_q) {
        command.left_hand_q = packet.left_hand_q;
      }
      if (command.has_right_hand_q) {
        command.right_hand_q = packet.right_hand_q;
      }
      packet_sequence = packet.sequence;
    } else if (buffer->data_size == sizeof(g1_lcm::PolicyActionPacketV3)) {
      g1_lcm::PolicyActionPacketV3 packet;
      std::memcpy(&packet, buffer->data, sizeof(packet));
      if (packet.magic != g1_lcm::kPolicyActionMagic ||
          packet.version != g1_lcm::kProtocolVersionV3 ||
          (packet.flags & g1_lcm::kActionFlagDex3AbsoluteQ) == 0) {
        ++bad_packet_count_;
        if (bad_packet_count_ <= 5 || bad_packet_count_ % 100 == 0) {
          std::cerr << "[lcm-action] Bad v3 packet header count=" << bad_packet_count_.load()
                    << " magic=0x" << std::hex << packet.magic << std::dec
                    << " version=" << packet.version
                    << " flags=0x" << std::hex << packet.flags << std::dec << '\n';
        }
        return;
      }

      command.type = ActionCommandType::kDex3Absolute28;
      command.dex3_q_target = packet.q_target;
      command.sequence = packet.sequence;
      command.received_at = std::chrono::steady_clock::now();
      command.emergency_stop = (packet.flags & g1_lcm::kActionFlagEmergencyStop) != 0;
      packet_sequence = packet.sequence;
    } else {
      ++bad_packet_count_;
      if (bad_packet_count_ <= 5 || bad_packet_count_ % 100 == 0) {
        std::cerr << "[lcm-action] Bad packet size count=" << bad_packet_count_.load()
                  << " size=" << buffer->data_size
                  << " expected_v1=" << sizeof(g1_lcm::PolicyActionPacketV1)
                  << " expected_v2=" << sizeof(g1_lcm::PolicyActionPacketV2)
                  << " expected_v3=" << sizeof(g1_lcm::PolicyActionPacketV3) << '\n';
      }
      return;
    }

    if (packet_sequence == last_sequence_ && last_sequence_ != 0) {
      ++bad_packet_count_;
      if (bad_packet_count_ <= 5 || bad_packet_count_ % 100 == 0) {
        std::cerr << "[lcm-action] Duplicate sequence rejected: last=" << last_sequence_
                  << " current=" << packet_sequence << '\n';
      }
      return;
    }
    if (packet_sequence < last_sequence_ && last_sequence_ != 0) {
      std::cerr << "[lcm-action] Sequence reset/jump back: last=" << last_sequence_
                << " current=" << packet_sequence << " (accepted)\n";
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_ = command;
    }

    const uint64_t count = ++packet_count_;
    if (count == 1) {
      std::cout << "[lcm-action] First action received. seq=" << packet_sequence << '\n';
    } else if (packet_sequence != last_sequence_ + 1 && packet_sequence > last_sequence_ + 1) {
      std::cerr << "[lcm-action] Sequence jump: last=" << last_sequence_
                << " current=" << packet_sequence << '\n';
    } else if (count % 100 == 0) {
      std::cout << "[lcm-action] Action OK count=" << count
                << " seq=" << packet_sequence << '\n';
    }
    last_sequence_ = packet_sequence;
  }

  template <typename Packet>
  static void FillBaseCommand(const Packet& packet, ActionCommand& command) {
    command.action = packet.action;
    command.sequence = packet.sequence;
    command.received_at = std::chrono::steady_clock::now();
    command.emergency_stop = (packet.flags & g1_lcm::kActionFlagEmergencyStop) != 0;
    command.left_grip = (packet.flags & g1_lcm::kActionFlagLeftGrip) != 0;
    command.right_grip = (packet.flags & g1_lcm::kActionFlagRightGrip) != 0;
  }
#endif

  std::string lcm_url_;
  std::string channel_;
  mutable std::mutex mutex_;
  std::optional<ActionCommand> latest_;
  std::atomic_bool running_{false};
  std::atomic<uint64_t> packet_count_{0};
  std::atomic<uint64_t> bad_packet_count_{0};
  uint64_t last_sequence_ = 0;

#ifdef G1_HAS_LCM
  lcm_t* lcm_ = nullptr;
  lcm_subscription_t* subscription_ = nullptr;
  std::thread thread_;
#endif
};

#endif
