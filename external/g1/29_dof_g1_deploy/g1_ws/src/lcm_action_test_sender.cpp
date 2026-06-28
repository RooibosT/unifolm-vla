#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <lcm/lcm.h>

#include "lcm_protocol.hpp"

namespace {

int64_t NowUnixMicros() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

void FillBodyAction(std::array<float, G1_NUM_MOTOR>& action) {
  action.fill(0.0f);
  action[0] = 0.01f;
  action[1] = -0.01f;
  action[15] = 0.02f;
  action[22] = -0.02f;
}

void FillHandJoints(std::array<float, g1_lcm::kHandMotorCount>& left,
                    std::array<float, g1_lcm::kHandMotorCount>& right) {
  for (int i = 0; i < g1_lcm::kHandMotorCount; ++i) {
    left[i] = 0.1f * static_cast<float>(i + 1);
    right[i] = -0.1f * static_cast<float>(i + 1);
  }
}

bool Publish(lcm_t* lcm, const void* data, int size) {
  const int result = lcm_publish(lcm, g1_lcm::kDefaultActionChannel, data, size);
  if (result != 0) {
    std::cerr << "lcm_publish failed: " << result << '\n';
    return false;
  }
  return true;
}

bool SendV1Binary(lcm_t* lcm, uint64_t sequence) {
  g1_lcm::PolicyActionPacketV1 packet;
  packet.sequence = sequence;
  packet.send_time_us = NowUnixMicros();
  packet.flags = g1_lcm::kActionFlagLeftGrip;
  FillBodyAction(packet.action);
  if (!Publish(lcm, &packet, sizeof(packet))) {
    return false;
  }
  std::cout << "sent v1-binary seq=" << sequence << " size=" << sizeof(packet)
            << " left=close right=open\n";
  return true;
}

bool SendV2Binary(lcm_t* lcm, uint64_t sequence) {
  g1_lcm::PolicyActionPacketV2 packet;
  packet.sequence = sequence;
  packet.send_time_us = NowUnixMicros();
  packet.flags = g1_lcm::kActionFlagRightGrip;
  FillBodyAction(packet.action);
  if (!Publish(lcm, &packet, sizeof(packet))) {
    return false;
  }
  std::cout << "sent v2-binary seq=" << sequence << " size=" << sizeof(packet)
            << " left=open right=close\n";
  return true;
}

bool SendV2Joint(lcm_t* lcm, uint64_t sequence) {
  g1_lcm::PolicyActionPacketV2 packet;
  packet.sequence = sequence;
  packet.send_time_us = NowUnixMicros();
  packet.flags = g1_lcm::kActionFlagHasLeftHandQ | g1_lcm::kActionFlagHasRightHandQ;
  FillBodyAction(packet.action);
  FillHandJoints(packet.left_hand_q, packet.right_hand_q);
  if (!Publish(lcm, &packet, sizeof(packet))) {
    return false;
  }
  std::cout << "sent v2-joint seq=" << sequence << " size=" << sizeof(packet)
            << " left_q[0]=" << packet.left_hand_q[0]
            << " right_q[0]=" << packet.right_hand_q[0] << '\n';
  return true;
}

void FillDex3Absolute(std::array<float, g1_lcm::kDex3ActionDim>& q_target,
                      const std::string& pattern,
                      uint64_t sequence) {
  q_target.fill(0.0f);
  if (pattern == "sine-small") {
    const float phase = static_cast<float>(sequence) * 0.1f;
    for (size_t i = 0; i < q_target.size(); ++i) {
      q_target[i] = 0.05f * std::sin(phase + static_cast<float>(i) * 0.2f);
    }
  }
}

bool SendV3Dex3(lcm_t* lcm, uint64_t sequence, const std::string& pattern) {
  g1_lcm::PolicyActionPacketV3 packet;
  packet.sequence = sequence;
  packet.send_time_us = NowUnixMicros();
  packet.flags = g1_lcm::kActionFlagDex3AbsoluteQ;
  FillDex3Absolute(packet.q_target, pattern, sequence);
  if (!Publish(lcm, &packet, sizeof(packet))) {
    return false;
  }
  std::cout << "sent v3-dex3 seq=" << sequence << " size=" << sizeof(packet)
            << " pattern=" << pattern
            << " left_arm[0]=" << packet.q_target[0]
            << " right_hand[6]=" << packet.q_target[27] << '\n';
  return true;
}

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0
            << " [v1-binary|v2-binary|v2-joint|v3-dex3|all] [lcm_url] [zero|sine-small]\n"
            << "Default mode: all\n"
            << "Default V3 pattern: zero\n"
            << "Default URL: udpm://239.255.76.67:7667?ttl=0\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = "all";
  std::string lcm_url = "udpm://239.255.76.67:7667?ttl=0";
  std::string pattern = "zero";

  if (argc > 1) {
    mode = argv[1];
  }
  if (mode == "-h" || mode == "--help") {
    PrintUsage(argv[0]);
    return 0;
  }
  if (argc > 2) {
    lcm_url = argv[2];
  }
  if (argc > 3) {
    pattern = argv[3];
  }

  lcm_t* lcm = lcm_create(lcm_url.c_str());
  if (!lcm) {
    std::cerr << "failed to create LCM: " << lcm_url << '\n';
    return 1;
  }

  bool ok = true;
  uint64_t sequence = 1;
  if (mode == "v1-binary") {
    ok = SendV1Binary(lcm, sequence);
  } else if (mode == "v2-binary") {
    ok = SendV2Binary(lcm, sequence);
  } else if (mode == "v2-joint") {
    ok = SendV2Joint(lcm, sequence);
  } else if (mode == "v3-dex3") {
    ok = SendV3Dex3(lcm, sequence, pattern);
  } else if (mode == "all") {
    ok = SendV1Binary(lcm, sequence++);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ok = ok && SendV2Binary(lcm, sequence++);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ok = ok && SendV2Joint(lcm, sequence++);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ok = ok && SendV3Dex3(lcm, sequence++, pattern);
  } else {
    std::cerr << "unknown mode: " << mode << '\n';
    PrintUsage(argv[0]);
    ok = false;
  }

  lcm_destroy(lcm);
  return ok ? 0 : 1;
}
