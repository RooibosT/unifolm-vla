#include <iostream>
#include <thread>
#include <chrono>
#include <unitree/robot/channel/channel_factory.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>
#include <unitree/idl/hg/LowCmd_.hpp>

using namespace unitree::robot;
using namespace unitree_hg::msg::dds_;

int main() {
  ChannelFactory::Instance()->Init(0, "eth0");
  uint64_t count = 0;

  ChannelSubscriber<LowCmd_> sub("rt/lowcmd");
  sub.InitChannel([&](const void* msg) {
    auto* cmd = static_cast<const LowCmd_*>(msg);
    float kp0 = cmd->motor_cmd()[0].kp();
    float kd0 = cmd->motor_cmd()[0].kd();
    float q0  = cmd->motor_cmd()[0].q();
    if (++count % 500 == 0 || kp0 < 0.1f) {
      std::cout << "count=" << count
                << " kp0=" << kp0
                << " kd0=" << kd0
                << " q0="  << q0;
      if (kp0 < 0.1f) std::cout << "  ← DAMPING!";
      std::cout << '\n';
    }
  }, 1);

  std::cout << "Monitoring rt/lowcmd...\n";
  while(true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
