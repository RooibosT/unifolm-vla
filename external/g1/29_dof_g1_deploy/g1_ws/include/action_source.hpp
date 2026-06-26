#ifndef G1_DEPLOY_REAL_ACTION_SOURCE_HPP
#define G1_DEPLOY_REAL_ACTION_SOURCE_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "robot_parameters.hpp"

// struct ActionCommand {
//   std::array<float, G1_NUM_MOTOR> action = {};
//   uint64_t sequence = 0;
//   std::chrono::steady_clock::time_point received_at = std::chrono::steady_clock::now();
//   bool emergency_stop = false;
//   bool left_grip  = false;  // kActionFlagLeftGrip  bit
//   bool right_grip = false;  // kActionFlagRightGrip bit
// };
struct ActionCommand {
  std::array<float, G1_NUM_MOTOR> action = {};
  std::array<float, 7> left_hand_q = {};
  std::array<float, 7> right_hand_q = {};
  bool has_left_hand_q = false;
  bool has_right_hand_q = false;

  uint64_t sequence = 0;
  std::chrono::steady_clock::time_point received_at = std::chrono::steady_clock::now();
  bool emergency_stop = false;
  bool left_grip  = false;  // kActionFlagLeftGrip  bit
  bool right_grip = false;  // kActionFlagRightGrip bit
};

class ActionSource {
 public:
  virtual ~ActionSource() = default;

  virtual bool Start() { return true; }
  virtual void Stop() {}
  virtual std::optional<ActionCommand> Poll() = 0;
  virtual std::string Name() const = 0;
};

#endif

