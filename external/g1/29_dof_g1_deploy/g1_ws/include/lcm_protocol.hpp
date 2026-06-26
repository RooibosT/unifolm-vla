#ifndef G1_DEPLOY_REAL_LCM_PROTOCOL_HPP
#define G1_DEPLOY_REAL_LCM_PROTOCOL_HPP

#include <array>
#include <cstdint>

#include "robot_parameters.hpp"

namespace g1_lcm {

constexpr const char* kDefaultActionChannel = "G1_POLICY_ACTION";
constexpr const char* kDefaultStateChannel = "G1_ROBOT_STATE";
constexpr uint32_t kPolicyActionMagic = 0x31414347U;  // "GCA1" little-endian.
constexpr uint32_t kRobotStateMagic = 0x31545347U;    // "GST1" little-endian.
constexpr uint16_t kProtocolVersionV1 = 1;
constexpr uint16_t kProtocolVersionV2 = 2;
constexpr uint16_t kProtocolVersion = kProtocolVersionV2;
constexpr uint16_t kActionFlagEmergencyStop = 1U << 0;
constexpr uint16_t kActionFlagLeftGrip      = 1U << 1;  // left hand close
constexpr uint16_t kActionFlagRightGrip     = 1U << 2;  // right hand close
constexpr uint16_t kStateFlagHasTorsoImu = 1U << 0;
constexpr uint16_t kStateFlagHasLeftHandQ = 1U << 1;   // left dex3 joint feedback
constexpr uint16_t kStateFlagHasRightHandQ = 1U << 2;  // right dex3 joint feedback

constexpr int kHandMotorCount = 7;  // dex3 motors per hand

// Add: eunbin
constexpr uint16_t kActionFlagHasLeftHandQ = 1U << 3; // left hand joint positions
constexpr uint16_t kActionFlagHasRightHandQ = 1U << 4; // right hand joint positions
#pragma pack(push, 1)
struct PolicyActionPacketV1 {
  uint32_t magic = kPolicyActionMagic;
  uint16_t version = kProtocolVersionV1;
  uint16_t flags = 0;
  uint64_t sequence = 0;
  int64_t send_time_us = 0;
  std::array<float, G1_NUM_MOTOR> action = {};
};
// Add: eunbin
struct PolicyActionPacketV2 {
  uint32_t magic = kPolicyActionMagic;
  uint16_t version = kProtocolVersionV2;
  uint16_t flags = 0;
  uint64_t sequence = 0;
  int64_t send_time_us = 0;
  std::array<float, G1_NUM_MOTOR> action = {};

  std::array<float, 7> left_hand_q = {};
  std::array<float, 7> right_hand_q = {};
};

using PolicyActionPacket = PolicyActionPacketV2;

struct RobotStatePacket {
  uint32_t magic = kRobotStateMagic;
  uint16_t version = kProtocolVersion;
  uint16_t flags = 0;
  uint64_t sequence = 0;
  uint64_t lowstate_sequence = 0;
  int64_t send_time_us = 0;
  uint8_t mode_machine = 0;
  std::array<float, 4> base_quat = {};
  std::array<float, 3> base_gyro = {};
  std::array<float, 3> base_accel = {};
  std::array<float, 4> torso_quat = {};
  std::array<float, 3> torso_gyro = {};
  std::array<float, 3> torso_accel = {};
  std::array<float, G1_NUM_MOTOR> q = {};
  std::array<float, G1_NUM_MOTOR> dq = {};
  std::array<float, G1_NUM_MOTOR> tau_est = {};

  // Dex3 hand joint feedback. Valid only when the matching
  // kStateFlagHasLeftHandQ / kStateFlagHasRightHandQ flag is set.
  std::array<float, kHandMotorCount> left_hand_q = {};
  std::array<float, kHandMotorCount> left_hand_dq = {};
  std::array<float, kHandMotorCount> left_hand_tau = {};
  std::array<float, kHandMotorCount> right_hand_q = {};
  std::array<float, kHandMotorCount> right_hand_dq = {};
  std::array<float, kHandMotorCount> right_hand_tau = {};
};
#pragma pack(pop)

}  // namespace g1_lcm

#endif
