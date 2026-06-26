#ifndef G1_DEPLOY_REAL_LCM_ROBOT_STATE_PUBLISHER_HPP
#define G1_DEPLOY_REAL_LCM_ROBOT_STATE_PUBLISHER_HPP

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

#include <unitree/idl/hg/IMUState_.hpp>
#include <unitree/idl/hg/LowState_.hpp>

#include "dex3_hands.hpp"
#include "lcm_protocol.hpp"

#ifdef G1_HAS_LCM
#include <lcm/lcm.h>
#endif

class LcmRobotStatePublisher {
 public:
  LcmRobotStatePublisher(std::string lcm_url, std::string channel)
      : lcm_url_(std::move(lcm_url)), channel_(std::move(channel)) {}

  bool Start() {
#ifdef G1_HAS_LCM
    lcm_ = lcm_create(lcm_url_.empty() ? nullptr : lcm_url_.c_str());
    if (!lcm_) {
      std::cerr << "[lcm-state] Failed to create LCM instance";
      if (!lcm_url_.empty()) {
        std::cerr << " for URL: " << lcm_url_;
      }
      std::cerr << '\n';
      return false;
    }
    std::cout << "[lcm-state] Publishing channel: " << channel_ << '\n';
    return true;
#else
    std::cerr << "[lcm-state] This binary was built without LCM support.\n"
              << "[lcm-state] Install liblcm-dev/lcm-tools and rebuild g1_ws.\n";
    return false;
#endif
  }

  void Stop() {
#ifdef G1_HAS_LCM
    if (lcm_) {
      lcm_destroy(lcm_);
      lcm_ = nullptr;
    }
#endif
  }

  bool Publish(const unitree_hg::msg::dds_::LowState_& low_state,
               const std::optional<unitree_hg::msg::dds_::IMUState_>& torso_imu,
               uint64_t lowstate_sequence,
               const dex3::Dex3Hands::HandStateSnapshot& left_hand = {},
               const dex3::Dex3Hands::HandStateSnapshot& right_hand = {}) {
#ifdef G1_HAS_LCM
    if (!lcm_) {
      return false;
    }

    g1_lcm::RobotStatePacket packet;
    packet.sequence = sequence_++;
    packet.lowstate_sequence = lowstate_sequence;
    packet.send_time_us = NowUnixMicros();
    packet.mode_machine = low_state.mode_machine();

    CopyArray(low_state.imu_state().quaternion(), packet.base_quat);
    CopyArray(low_state.imu_state().gyroscope(), packet.base_gyro);
    CopyArray(low_state.imu_state().accelerometer(), packet.base_accel);

    if (torso_imu) {
      packet.flags |= g1_lcm::kStateFlagHasTorsoImu;
      CopyArray(torso_imu->quaternion(), packet.torso_quat);
      CopyArray(torso_imu->gyroscope(), packet.torso_gyro);
      CopyArray(torso_imu->accelerometer(), packet.torso_accel);
    }

    for (int i = 0; i < G1_NUM_MOTOR; ++i) {
      packet.q[i] = low_state.motor_state()[i].q();
      packet.dq[i] = low_state.motor_state()[i].dq();
      packet.tau_est[i] = low_state.motor_state()[i].tau_est();
    }

    if (left_hand.valid) {
      packet.flags |= g1_lcm::kStateFlagHasLeftHandQ;
      packet.left_hand_q = left_hand.q;
      packet.left_hand_dq = left_hand.dq;
      packet.left_hand_tau = left_hand.tau;
    }
    if (right_hand.valid) {
      packet.flags |= g1_lcm::kStateFlagHasRightHandQ;
      packet.right_hand_q = right_hand.q;
      packet.right_hand_dq = right_hand.dq;
      packet.right_hand_tau = right_hand.tau;
    }

    const int result = lcm_publish(lcm_, channel_.c_str(), &packet, sizeof(packet));
    if (result != 0) {
      ++publish_error_count_;
      if (publish_error_count_ <= 5 || publish_error_count_ % 100 == 0) {
        std::cerr << "[lcm-state] Publish failed count=" << publish_error_count_ << '\n';
      }
      return false;
    }
    return true;
#else
    (void)low_state;
    (void)torso_imu;
    (void)lowstate_sequence;
    (void)left_hand;
    (void)right_hand;
    return false;
#endif
  }

 private:
  static int64_t NowUnixMicros() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  }

  template <typename Source, typename Target>
  static void CopyArray(const Source& source, Target& target) {
    for (size_t i = 0; i < target.size(); ++i) {
      target[i] = source[i];
    }
  }

  std::string lcm_url_;
  std::string channel_;
  uint64_t sequence_ = 0;
  uint64_t publish_error_count_ = 0;

#ifdef G1_HAS_LCM
  lcm_t* lcm_ = nullptr;
#endif
};

#endif
