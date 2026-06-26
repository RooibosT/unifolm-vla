#ifndef G1_DEPLOY_REAL_DEX3_HANDS_HPP
#define G1_DEPLOY_REAL_DEX3_HANDS_HPP

#include <unitree/idl/hg/HandCmd_.hpp>
#include <unitree/idl/hg/HandState_.hpp>
#include <unitree/robot/channel/channel_publisher.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>

namespace dex3 {

using unitree::robot::ChannelPublisher;
using unitree::robot::ChannelPublisherPtr;
using unitree::robot::ChannelSubscriber;
using unitree::robot::ChannelSubscriberPtr;
using unitree_hg::msg::dds_::HandCmd_;
using unitree_hg::msg::dds_::HandState_;

constexpr int kMotorCount = 7;
constexpr int kSensorCount = 9;

static const std::string kLeftCommandTopic = "rt/dex3/left/cmd";
static const std::string kRightCommandTopic = "rt/dex3/right/cmd";

// Unitree's Dex3 example uses rt/lf/... for state. Some older local code used
// rt/dex3/.../state, so subscribe to both to make the smoke test less brittle.
static const std::array<std::string, 2> kLeftStateTopics = {
    "rt/lf/dex3/left/state",
    "rt/dex3/left/state",
};
static const std::array<std::string, 2> kRightStateTopics = {
    "rt/lf/dex3/right/state",
    "rt/dex3/right/state",
};

inline uint8_t MakeMode(uint8_t motor_id, uint8_t status, uint8_t timeout) {
  uint8_t mode = 0;
  mode |= (motor_id & 0x0F);
  mode |= (status & 0x07) << 4;
  mode |= (timeout & 0x01) << 7;
  return mode;
}

class Dex3Hands {
 public:
  Dex3Hands() {
    SetStopCommand(left_);
    SetStopCommand(right_);
  }

  void Initialize() {
    InitializeHand(left_, true, kLeftCommandTopic, kLeftStateTopics);
    InitializeHand(right_, false, kRightCommandTopic, kRightStateTopics);
    initialized_.store(true);

    std::cout << "[dex3] Initialized left/right hand publishers and subscribers.\n"
              << "[dex3] Command topics: " << kLeftCommandTopic << ", "
              << kRightCommandTopic << '\n'
              << "[dex3] State topics: " << kLeftStateTopics[0] << " (+ "
              << kLeftStateTopics[1] << "), " << kRightStateTopics[0] << " (+ "
              << kRightStateTopics[1] << ")\n"
              << "[dex3] Default command is timeout stop; no finger motion is commanded.\n";
  }

  void WriteOnce() {
    if (!initialized_.load()) {
      return;
    }

    WriteHand(left_);
    WriteHand(right_);
  }

  void StopAll() {
    SetStopCommand(left_);
    SetStopCommand(right_);
  }

  // ── Grip 제어 ──────────────────────────────────────────────────────────────
  // SONIC dex3_hands.hpp의 close()/open() 구현을 이식
  // kp=1.5, kd=0.1, mode=makeMode(i, status=0x01, timeout=0) — SONIC 동일 값
  //
  // close target = 0.5 * (max_limit + min_limit)  [mid-range, SONIC 동일]
  // open  target = 0.0  (모든 joint)
  //
  // Left  [thumb_0, thumb_1, thumb_2, middle_0, middle_1, index_0, index_1]
  // Right [thumb_0, thumb_1, thumb_2, middle_0, middle_1, index_0, index_1]
  void SetGripCommand(bool is_left, bool do_close, double kp = 1.5, double kd = 0.1) {
    // mid-range close targets (SONIC 동일)
    static constexpr float LEFT_CLOSE[kMotorCount]  = { 0.0f,  0.1614f,  0.8727f, -0.7854f, -0.8727f, -0.7854f, -0.8727f};
    static constexpr float RIGHT_CLOSE[kMotorCount] = { 0.0f, -0.1614f, -0.8727f,  0.7854f,  0.8727f,  0.7854f,  0.8727f};
    static constexpr float OPEN[kMotorCount]        = { 0.0f,  0.0f,     0.0f,     0.0f,     0.0f,     0.0f,     0.0f   };

    HandContext& hand = is_left ? left_ : right_;
    const float* targets = do_close ? (is_left ? LEFT_CLOSE : RIGHT_CLOSE) : OPEN;

    std::lock_guard<std::mutex> lock(hand.mutex);
    SizeCommand(hand.command);
    for (int i = 0; i < kMotorCount; ++i) {
      auto& motor = hand.command.motor_cmd()[i];
      motor.mode(MakeMode(static_cast<uint8_t>(i), 0x01, 0x00));  // status=0x01, timeout=0
      motor.q(targets[i]);
      motor.dq(0.0f);
      motor.tau(0.0f);
      motor.kp(static_cast<float>(kp));
      motor.kd(static_cast<float>(kd));
    }
  }

  void SetJointPositions(bool is_left, const std::array<float, kMotorCount>& q, double kp = 1.5, double kd = 0.1) {
    HandContext& hand = is_left ? left_ : right_;

    std::lock_guard<std::mutex> lock(hand.mutex);
    SizeCommand(hand.command);

    for (int i = 0; i < kMotorCount; ++i) {
      auto& motor = hand.command.motor_cmd()[i];
      motor.mode(MakeMode(static_cast<uint8_t>(i), 0x01, 0x00));  // status=0x01, timeout=0
      motor.q(q[i]);
      motor.dq(0.0f);
      motor.tau(0.0f);
      motor.kp(static_cast<float>(kp));
      motor.kd(static_cast<float>(kd));
    }
  }

  bool HasLeftState() const {
    return left_.state_count.load() > 0;
  }

  bool HasRightState() const {
    return right_.state_count.load() > 0;
  }

  uint64_t LeftStateCount() const {
    return left_.state_count.load();
  }

  uint64_t RightStateCount() const {
    return right_.state_count.load();
  }

  // Snapshot of the latest measured hand joint state (positions, velocities,
  // estimated torques). valid is false until at least one HandState has arrived.
  struct HandStateSnapshot {
    bool valid = false;
    std::array<float, kMotorCount> q{};
    std::array<float, kMotorCount> dq{};
    std::array<float, kMotorCount> tau{};
  };

  HandStateSnapshot GetLeftState() const { return GetState(left_); }
  HandStateSnapshot GetRightState() const { return GetState(right_); }

 private:
  struct HandContext {
    mutable std::mutex mutex;
    HandCmd_ command;
    HandState_ latest_state;
    ChannelPublisherPtr<HandCmd_> publisher;
    std::vector<ChannelSubscriberPtr<HandState_>> subscribers;
    std::atomic<uint64_t> state_count{0};
  };

  static HandStateSnapshot GetState(const HandContext& hand) {
    HandStateSnapshot snapshot;
    std::lock_guard<std::mutex> lock(hand.mutex);
    if (hand.state_count.load() == 0 ||
        static_cast<int>(hand.latest_state.motor_state().size()) < kMotorCount) {
      return snapshot;
    }
    for (int i = 0; i < kMotorCount; ++i) {
      const auto& motor = hand.latest_state.motor_state()[i];
      snapshot.q[i] = motor.q();
      snapshot.dq[i] = motor.dq();
      snapshot.tau[i] = motor.tau_est();
    }
    snapshot.valid = true;
    return snapshot;
  }

  static void SizeCommand(HandCmd_& command) {
    command.motor_cmd().resize(kMotorCount);
  }

  static void SizeState(HandState_& state) {
    state.motor_state().resize(kMotorCount);
    state.press_sensor_state().resize(kSensorCount);
  }

  static void SetStopCommand(HandContext& hand) {
    std::lock_guard<std::mutex> lock(hand.mutex);
    SizeCommand(hand.command);
    for (int i = 0; i < kMotorCount; ++i) {
      auto& motor = hand.command.motor_cmd()[i];
      motor.mode(MakeMode(static_cast<uint8_t>(i), 0x01, 0x01));
      motor.tau(0.0F);
      motor.q(0.0F);
      motor.dq(0.0F);
      motor.kp(0.0F);
      motor.kd(0.0F);
    }
  }

  void InitializeHand(HandContext& hand,
                      bool is_left,
                      const std::string& command_topic,
                      const std::array<std::string, 2>& state_topics) {
    SizeCommand(hand.command);
    SizeState(hand.latest_state);

    hand.publisher.reset(new ChannelPublisher<HandCmd_>(command_topic));
    hand.publisher->InitChannel();

    hand.subscribers.clear();
    for (const auto& state_topic : state_topics) {
      ChannelSubscriberPtr<HandState_> subscriber(
          new ChannelSubscriber<HandState_>(state_topic));
      subscriber->InitChannel(
          [this, is_left, state_topic](const void* message) {
            this->StateHandler(is_left, state_topic, message);
          },
          1);
      hand.subscribers.push_back(subscriber);
    }
  }

  void StateHandler(bool is_left, const std::string& topic, const void* message) {
    HandContext& hand = is_left ? left_ : right_;
    {
      std::lock_guard<std::mutex> lock(hand.mutex);
      hand.latest_state = *(const HandState_*)message;
    }

    const uint64_t count = ++hand.state_count;
    if (count == 1) {
      std::cout << "[dex3] First " << (is_left ? "left" : "right")
                << " HandState received on " << topic << '\n';
    } else if (count % 500 == 0) {
      std::cout << "[dex3] " << (is_left ? "left" : "right")
                << " HandState OK count=" << count << '\n';
    }
  }

  // static void WriteHand(HandContext& hand) {
  //   if (!hand.publisher) {
  //     return;
  //   }

  //   HandCmd_ command;
  //   {
  //     std::lock_guard<std::mutex> lock(hand.mutex);
  //     command = hand.command;
  //   }
  //   hand.publisher->Write(command);
  // }
  static void WriteHand(HandContext& hand) {
    if (!hand.publisher) {
      return;
    }

    static constexpr float kMaxDeltaQ = 0.25f;

    HandCmd_ command;
    HandState_ state;
    bool has_state = false;
    {
      std::lock_guard<std::mutex> lock(hand.mutex);
      command = hand.command;
      state = hand.latest_state;
      has_state = (hand.state_count.load() > 0);
    }

    if (has_state &&
        static_cast<int>(state.motor_state().size()) == kMotorCount &&
        static_cast<int>(command.motor_cmd().size()) == kMotorCount) {
      for (int i = 0; i < kMotorCount; ++i) {
        const float current_q = state.motor_state()[i].q();
        const float desired_q = command.motor_cmd()[i].q();
        const float delta = desired_q - current_q;
        const float clamped_delta = std::max(-kMaxDeltaQ, std::min(kMaxDeltaQ, delta));
        command.motor_cmd()[i].q(current_q + clamped_delta);
      }
    }
    hand.publisher->Write(command);
  }

  HandContext left_;
  HandContext right_;
  std::atomic_bool initialized_{false};
};

}  // namespace dex3

#endif
