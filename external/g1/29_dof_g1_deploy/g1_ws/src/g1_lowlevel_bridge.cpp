#include <unitree/idl/hg/IMUState_.hpp>
#include <unitree/idl/hg/LowCmd_.hpp>
#include <unitree/idl/hg/LowState_.hpp>
#include <unitree/robot/channel/channel_publisher.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "lcm_action_source.hpp"
#include "lcm_protocol.hpp"
#include "lcm_robot_state_publisher.hpp"
#include "null_action_source.hpp"
#include "policy_parameters.hpp"
#include "robot_parameters.hpp"
#include "dex3_hands.hpp"

// OpenCV — D435i RGB 캡처 (optional)
#ifdef G1_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

using unitree::robot::ChannelFactory;
using unitree::robot::ChannelPublisher;
using unitree::robot::ChannelPublisherPtr;
using unitree::robot::ChannelSubscriber;
using unitree::robot::ChannelSubscriberPtr;
using unitree_hg::msg::dds_::IMUState_;
using unitree_hg::msg::dds_::LowCmd_;
using unitree_hg::msg::dds_::LowState_;

// ============================================================================
// CameraStreamer — /dev/video2 (D435i) → LCM G1_CAMERA_FRAME 송신
// 패킷: magic(4)+w(4)+h(4)+jpeg_size(4)+timestamp_us(8)+jpeg_data = 24+jpeg bytes
// ============================================================================
constexpr uint32_t kCameraFrameMagic   = 0x4D524143U;  // "CRAM"
constexpr const char* kDefaultCameraChannel = "G1_CAMERA_FRAME";

#if defined(G1_HAS_OPENCV) && defined(G1_HAS_LCM)
class CameraStreamer {
 public:
  CameraStreamer(std::string lcm_url,
                std::string channel    = kDefaultCameraChannel,
                int device_index       = 2,
                int target_fps         = 30,
                int jpeg_quality       = 80)
      : lcm_url_(std::move(lcm_url)), channel_(std::move(channel)),
        device_index_(device_index), target_fps_(target_fps),
        jpeg_quality_(jpeg_quality) {}

  bool Start() {
    lcm_ = lcm_create(lcm_url_.empty() ? nullptr : lcm_url_.c_str());
    if (!lcm_) {
      std::cerr << "[camera] Failed to create LCM instance\n";
      return false;
    }
    cap_.open(device_index_, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
      std::cerr << "[camera] Failed to open /dev/video" << device_index_ << '\n';
      lcm_destroy(lcm_); lcm_ = nullptr;
      return false;
    }
    cap_.set(cv::CAP_PROP_FRAME_WIDTH,  640);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap_.set(cv::CAP_PROP_FPS, target_fps_);
    running_.store(true);
    thread_ = std::thread(&CameraStreamer::StreamLoop, this);
    std::cout << "[camera] Started. channel=" << channel_
              << " device=/dev/video" << device_index_
              << " fps=" << target_fps_ << '\n';
    return true;
  }

  void Stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    cap_.release();
    if (lcm_) { lcm_destroy(lcm_); lcm_ = nullptr; }
    std::cout << "[camera] Stopped. frames_sent=" << frames_sent_.load() << '\n';
  }

  ~CameraStreamer() { Stop(); }

 private:
  void StreamLoop() {
    const auto period = std::chrono::microseconds(1000000 / target_fps_);
    auto next_tick    = std::chrono::steady_clock::now();
    std::vector<int>     encode_params = {cv::IMWRITE_JPEG_QUALITY, jpeg_quality_};
    std::vector<uint8_t> jpeg_buf;
    cv::Mat frame;

    while (running_.load()) {
      next_tick += period;
      if (!cap_.read(frame) || frame.empty()) {
        std::this_thread::sleep_until(next_tick);
        continue;
      }
      cv::imencode(".jpg", frame, jpeg_buf, encode_params);

      const uint32_t w         = static_cast<uint32_t>(frame.cols);
      const uint32_t h         = static_cast<uint32_t>(frame.rows);
      const uint32_t jpeg_size = static_cast<uint32_t>(jpeg_buf.size());
      const uint64_t ts        = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();

      std::vector<uint8_t> packet(24 + jpeg_size);
      uint8_t* p = packet.data();
      std::memcpy(p,      &kCameraFrameMagic, 4);
      std::memcpy(p + 4,  &w,                 4);
      std::memcpy(p + 8,  &h,                 4);
      std::memcpy(p + 12, &jpeg_size,          4);
      std::memcpy(p + 16, &ts,                 8);
      std::memcpy(p + 24, jpeg_buf.data(), jpeg_size);

      lcm_publish(lcm_, channel_.c_str(), packet.data(),
                  static_cast<uint32_t>(packet.size()));

      const uint64_t cnt = ++frames_sent_;
      if (cnt == 1 || cnt % (static_cast<uint64_t>(target_fps_) * 10) == 0)
        std::cout << "[camera] frames_sent=" << cnt
                  << " jpeg_bytes=" << jpeg_size << '\n';

      std::this_thread::sleep_until(next_tick);
    }
  }

  std::string  lcm_url_, channel_;
  int          device_index_, target_fps_, jpeg_quality_;
  lcm_t*       lcm_ = nullptr;
  cv::VideoCapture cap_;
  std::atomic_bool running_{false};
  std::atomic<uint64_t> frames_sent_{0};
  std::thread  thread_;
};
#endif  // defined(G1_HAS_OPENCV) && defined(G1_HAS_LCM)

namespace {

std::atomic_bool g_should_stop{false};

void SignalHandler(int) {
  g_should_stop.store(true);
}

struct BridgeOptions {
  std::string network_interface;
  std::string action_source = "null";
  std::string lcm_url;
  std::string lcm_action_channel = g1_lcm::kDefaultActionChannel;
  std::string lcm_state_channel = g1_lcm::kDefaultStateChannel;
  bool publish_lcm_state = false;
  bool dry_run_actions = false;
  int lcm_state_rate_hz = 500;
  // 카메라 옵션
  bool publish_camera = false;
  std::string lcm_camera_channel = kDefaultCameraChannel;
  int camera_device       = 2;
  int camera_fps          = 30;
  int camera_jpeg_quality = 80;
};

template <typename T>
class TimestampedBuffer {
 public:
  void Set(const T& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ = value;
    updated_at_ = std::chrono::steady_clock::now();
  }

  std::optional<T> Get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
  }

  std::optional<std::chrono::steady_clock::time_point> UpdatedAt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!value_) {
      return std::nullopt;
    }
    return updated_at_;
  }

  bool FreshFor(std::chrono::milliseconds max_age) const {
    auto updated_at = UpdatedAt();
    if (!updated_at) {
      return false;
    }
    return (std::chrono::steady_clock::now() - *updated_at) <= max_age;
  }

 private:
  mutable std::mutex mutex_;
  std::optional<T> value_;
  std::chrono::steady_clock::time_point updated_at_{};
};

class G1LowLevelBridge {
 public:
  explicit G1LowLevelBridge(BridgeOptions options)
      : options_(std::move(options)),
        network_interface_(options_.network_interface) {
    if (options_.action_source == "lcm") {
      action_source_ =
          std::make_unique<LcmActionSource>(options_.lcm_url, options_.lcm_action_channel);
    } else {
      action_source_ = std::make_unique<NullActionSource>();
    }

    if (options_.publish_lcm_state) {
      lcm_state_publisher_ =
          std::make_unique<LcmRobotStatePublisher>(options_.lcm_url, options_.lcm_state_channel);
    }
  }

  bool Start() {
    std::cout << "[bridge] Initializing Unitree DDS on interface: " << network_interface_ << '\n';
    ChannelFactory::Instance()->Init(0, network_interface_);

    lowcmd_publisher_.reset(new ChannelPublisher<LowCmd_>(HG_CMD_TOPIC));
    lowcmd_publisher_->InitChannel();

    lowstate_subscriber_.reset(new ChannelSubscriber<LowState_>(HG_STATE_TOPIC));
    lowstate_subscriber_->InitChannel(
        std::bind(&G1LowLevelBridge::LowStateHandler, this, std::placeholders::_1), 1);

    imutorso_subscriber_.reset(new ChannelSubscriber<IMUState_>(HG_IMU_TORSO));
    imutorso_subscriber_->InitChannel(
        std::bind(&G1LowLevelBridge::ImuTorsoHandler, this, std::placeholders::_1), 1);

    dex3_hands_.Initialize();

    if (!action_source_->Start()) {
      std::cerr << "[bridge] Failed to start action source: " << action_source_->Name() << '\n';
      return false;
    }

    if (lcm_state_publisher_ && !lcm_state_publisher_->Start()) {
      std::cerr << "[bridge] Failed to start LCM state publisher.\n";
      return false;
    }

#if defined(G1_HAS_OPENCV) && defined(G1_HAS_LCM)
    if (options_.publish_camera) {
      camera_streamer_ = std::make_unique<CameraStreamer>(
          options_.lcm_url, options_.lcm_camera_channel,
          options_.camera_device, options_.camera_fps, options_.camera_jpeg_quality);
      if (!camera_streamer_->Start()) {
        std::cerr << "[bridge] Camera streamer failed (continuing without camera).\n";
        camera_streamer_.reset();
      }
    }
#else
    if (options_.publish_camera)
      std::cerr << "[bridge] --publish-camera requires OpenCV and LCM support.\n";
#endif

    SetDampingCommand();
    running_.store(true);
    command_writer_thread_ = std::thread(&G1LowLevelBridge::CommandWriterLoop, this);
    control_thread_ = std::thread(&G1LowLevelBridge::ControlLoop, this);
    if (lcm_state_publisher_) {
      lcm_state_thread_ = std::thread(&G1LowLevelBridge::LcmStatePublisherLoop, this);
    }

    std::cout << "[bridge] Started. Action source: " << action_source_->Name() << '\n';
    if (options_.dry_run_actions) {
      std::cout << "[bridge] Dry-run actions enabled; received actions will not move motors.\n";
    } else if (options_.action_source == "null") {
      std::cout << "[bridge] v1 uses NullActionSource, so it will not command motion.\n";
    } else {
      std::cout << "[bridge] External actions are enabled; stale/missing actions fall back to damping.\n";
    }
    std::cout << "[bridge] Dex3 left/right hands are included in safe timeout-stop mode.\n";
    return true;
  }

  void Wait() {
    while (!g_should_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void Stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
      return;
    }

    std::cout << "[bridge] Stopping. Sending damping command before exit.\n";
    action_source_->Stop();

    if (lcm_state_thread_.joinable()) {
      lcm_state_thread_.join();
    }
    if (control_thread_.joinable()) {
      control_thread_.join();
    }
    if (command_writer_thread_.joinable()) {
      command_writer_thread_.join();
    }
    if (lcm_state_publisher_) {
      lcm_state_publisher_->Stop();
    }

#if defined(G1_HAS_OPENCV) && defined(G1_HAS_LCM)
    if (camera_streamer_) {
      camera_streamer_->Stop();
    }
#endif

    SetDampingCommand();
    dex3_hands_.StopAll();
    for (int i = 0; i < 100; ++i) {
      WriteLowCommand();
      dex3_hands_.WriteOnce();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  ~G1LowLevelBridge() {
    Stop();
  }

 private:
  static constexpr auto kCommandWriterPeriod = std::chrono::microseconds(2000);
  static constexpr auto kControlPeriod = std::chrono::microseconds(20000);
  static constexpr auto kLowStateTimeout = std::chrono::milliseconds(10000000000000);
  static constexpr auto kActionTimeout = std::chrono::milliseconds(10000000000000);

  void LowStateHandler(const void* message) {
    LowState_ low_state = *(const LowState_*)message;
    const uint32_t received_crc = low_state.crc();
    const uint32_t calculated_crc =
        Crc32Core((uint32_t*)&low_state, (sizeof(LowState_) >> 2) - 1);

    if (received_crc != calculated_crc) {
      ++crc_error_count_;
      if (crc_error_count_ <= 5 || crc_error_count_ % 100 == 0) {
        std::cerr << "[bridge] LowState CRC error count=" << crc_error_count_.load() << '\n';
      }
      return;
    }

    low_state_buffer_.Set(low_state);
    mode_machine_.store(low_state.mode_machine());

    const uint64_t count = ++lowstate_count_;
    if (count == 1) {
      std::cout << "[bridge] First LowState received. G1 type: "
                << static_cast<unsigned>(low_state.mode_machine()) << '\n';
    } else if (count % 2000 == 0) {
      const auto& rpy = low_state.imu_state().rpy();
      std::cout << "[bridge] LowState OK count=" << count
                << " pelvis_rpy=[" << rpy[0] << ", " << rpy[1] << ", " << rpy[2]
                << "]\n";
    }
  }

  void ImuTorsoHandler(const void* message) {
    IMUState_ imu = *(const IMUState_*)message;
    imu_torso_buffer_.Set(imu);
  }

  void CommandWriterLoop() {
    auto next_tick = std::chrono::steady_clock::now();
    while (running_.load()) {
      next_tick += kCommandWriterPeriod;
      WriteLowCommand();
      dex3_hands_.WriteOnce();
      std::this_thread::sleep_until(next_tick);
    }
  }

  void ControlLoop() {
    auto next_tick = std::chrono::steady_clock::now();
    while (running_.load()) {
      next_tick += kControlPeriod;
      UpdateCommandFromActionSource();
      std::this_thread::sleep_until(next_tick);
    }
  }

  void LcmStatePublisherLoop() {
    const int safe_rate_hz = options_.lcm_state_rate_hz > 0 ? options_.lcm_state_rate_hz : 100;
    const auto period = std::chrono::microseconds(1000000 / safe_rate_hz);
    auto next_tick = std::chrono::steady_clock::now();
    while (running_.load()) {
      next_tick += period;
      PublishLcmRobotState();
      std::this_thread::sleep_until(next_tick);
    }
  }

  void PublishLcmRobotState() {
    if (!lcm_state_publisher_) {
      return;
    }
    auto maybe_low_state = low_state_buffer_.Get();
    if (!maybe_low_state) {
      return;
    }
    lcm_state_publisher_->Publish(*maybe_low_state,
                                  imu_torso_buffer_.Get(),
                                  lowstate_count_.load(),
                                  dex3_hands_.GetLeftState(),
                                  dex3_hands_.GetRightState());
  }

  static bool JointArrayChanged(
    const std::array<float, dex3::kMotorCount>& a,
    const std::array<float, dex3::kMotorCount>& b,
    float eps = 1e-4f) {
      for (int i = 0; i < dex3::kMotorCount; ++i) {
        if (std::abs(a[i] - b[i]) > eps) {
          return true;
        }
      }
      return false;
    }

  void UpdateCommandFromActionSource() {
    reported_waiting_for_lowstate_ = false;

    auto maybe_action = action_source_->Poll();
    if (!maybe_action) {
      // Damping Action when action source is none
      //SetDampingCommand();
      return;
    }

    const ActionCommand& command = *maybe_action;
    // if (command.emergency_stop) {
    //   std::cerr << "[bridge] Emergency stop requested by action source.\n";
    //   SetDampingCommand();
    //   return;
    // }

    // const auto age = std::chrono::steady_clock::now() - command.received_at;
    // if (age > kActionTimeout) {
    //   std::cerr << "[bridge] Action timeout. Switching to damping command.\n";
    //   //SetDampingCommand();
    //   return;
    // }

    if (options_.dry_run_actions) {
      static uint64_t last_reported_sequence = 0;
      if (command.sequence != last_reported_sequence) {
        auto print_hand_q = [](const char* name, const std::array<float, dex3::kMotorCount>& q) {
          std::cout << " " << name << "_q=[";
          for (int i = 0; i < dex3::kMotorCount; ++i) {
            if (i > 0) {
              std::cout << ", ";
            }
            std::cout << q[i];
          }
          std::cout << "]";
        };

        std::cout << "[bridge] Dry-run action received seq=" << command.sequence;
        if (command.has_left_hand_q) {
          std::cout << " left=joint";
          print_hand_q("left", command.left_hand_q);
        } else {
          std::cout << " left=binary(" << (command.left_grip ? "close" : "open") << ")";
        }
        if (command.has_right_hand_q) {
          std::cout << " right=joint";
          print_hand_q("right", command.right_hand_q);
        } else {
          std::cout << " right=binary(" << (command.right_grip ? "close" : "open") << ")";
        }
        std::cout << '\n';
        last_reported_sequence = command.sequence;
      }
      SetDampingCommand();
      return;
    }

    SetPolicyActionCommand(command.action);

    // // ── Dex3 hand grip 처리 (binary: close/open) ──────────────────────────
    // // LCM 패킷 flags bit1=left grip, bit2=right grip
    // // 상태 변화 시에만 SetGripCommand() 호출 후 WriteOnce()로 전송
    // const bool new_left_grip  = command.left_grip;
    // const bool new_right_grip = command.right_grip;
    // bool grip_changed = false;

    // if (new_left_grip != last_left_grip_) {
    //   dex3_hands_.SetGripCommand(true, new_left_grip);
    //   last_left_grip_ = new_left_grip;
    //   grip_changed = true;
    //   std::cout << "[bridge] Left hand: " << (new_left_grip ? "CLOSE" : "OPEN") << '\n';
    // }
    // if (new_right_grip != last_right_grip_) {
    //   dex3_hands_.SetGripCommand(false, new_right_grip);
    //   last_right_grip_ = new_right_grip;
    //   grip_changed = true;
    //   std::cout << "[bridge] Right hand: " << (new_right_grip ? "CLOSE" : "OPEN") << '\n';
    // }    if (grip_changed) {
    //   dex3_hands_.WriteOnce();
    // }
    // Add: Eunbin
    // Left hand
    bool hand_command_updated = false;

    if (command.has_left_hand_q) {
      if (!last_left_hand_q_ || JointArrayChanged(command.left_hand_q, *last_left_hand_q_)) {
        dex3_hands_.SetJointPositions(true, command.left_hand_q);
        last_left_hand_q_ = command.left_hand_q;
        hand_command_updated = true;
        std::cout << "[bridge] Left hand: JOINT COMMAND\n";
      }
    } else {
      const bool new_left_grip = command.left_grip;
      if (new_left_grip != last_left_grip_) {
        dex3_hands_.SetGripCommand(true, new_left_grip);
        last_left_grip_ = new_left_grip;
        hand_command_updated = true;
        std::cout << "[bridge] Left hand: " << (new_left_grip ? "CLOSE" : "OPEN") << '\n';
      }
    }

    // Right hand
    if (command.has_right_hand_q) {
      if (!last_right_hand_q_ || JointArrayChanged(command.right_hand_q, *last_right_hand_q_)) {
        dex3_hands_.SetJointPositions(false, command.right_hand_q);
        last_right_hand_q_ = command.right_hand_q;
        hand_command_updated = true;
        std::cout << "[bridge] Right hand: JOINT COMMAND\n";
      }
    } else {
      const bool new_right_grip = command.right_grip;
      if (new_right_grip != last_right_grip_) {
        dex3_hands_.SetGripCommand(false, new_right_grip);
        last_right_grip_ = new_right_grip;
        hand_command_updated = true;
        std::cout << "[bridge] Right hand: " << (new_right_grip ? "CLOSE" : "OPEN") << '\n';
      }
    }

    if (hand_command_updated) {
      dex3_hands_.WriteOnce();
    }


    const uint64_t applied_count = ++action_applied_count_;
    if (applied_count == 1 || applied_count % 500 == 0) {
      const auto action_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - command.received_at).count();
      std::cout << "[bridge] Applied action count=" << applied_count
                << " seq=" << command.sequence
                << " age_ms=" << action_age_ms << '\n';
    }
  }

  void SetPolicyActionCommand(const std::array<float, G1_NUM_MOTOR>& action) {
    MotorCommand motor_command;
    for (int i = 0; i < G1_NUM_MOTOR; ++i) {
      const double action_value =
          static_cast<double>(action[isaaclab_to_mujoco[i]]) * g1_action_scale[i];
      motor_command.q_target[i] = static_cast<float>(default_angles[i] + action_value);
      motor_command.dq_target[i] = 0.0F;
      motor_command.tau_ff[i] = 0.0F;
      motor_command.kp[i] = kps[i];
      motor_command.kd[i] = kds[i];
    }
    motor_command_buffer_.Set(motor_command);
  }

  void SetDampingCommand() {
    MotorCommand motor_command;
    for (int i = 0; i < G1_NUM_MOTOR; ++i) {
      motor_command.q_target[i] = 0.0F;
      motor_command.dq_target[i] = 0.0F;
      motor_command.tau_ff[i] = 0.0F;
      motor_command.kp[i] = 0.0F;
      motor_command.kd[i] = 8.0F;
    }
    motor_command_buffer_.Set(motor_command);
  }

  void WriteLowCommand() {
    if (!lowstate_count_.load()) {
      return;
    }
    auto maybe_command = motor_command_buffer_.Get();
    if (!maybe_command) {
      return;
    }
    const MotorCommand& raw = *maybe_command;

    LowCmd_ dds_low_command;
    dds_low_command.mode_pr(static_cast<uint8_t>(Mode::PR));
    dds_low_command.mode_machine(mode_machine_.load());

    bool is_damping = true;
    for (int i = 0; i < G1_NUM_MOTOR; ++i) {
      if (raw.q_target[i] > 0.1f) { is_damping = false; break; }
    }

    const MotorCommand* command_to_use = &raw;
    if (is_damping && last_valid_command_.has_value() && !options_.dry_run_actions) {
      command_to_use = &(*last_valid_command_);
      static uint64_t damping_skip_count = 0;
      if (++damping_skip_count % 1 == 1) {
        std::cerr << "[bridge] Damping detected — holding last valid command "
                  << "(skip_count=" << damping_skip_count << ")\n";
      }
    } else if (!is_damping) {
      last_valid_command_ = raw;
    }

    const MotorCommand& command = *command_to_use;
    for (int i = 0; i < G1_NUM_MOTOR; ++i) {
      dds_low_command.motor_cmd().at(i).mode() = 1;
      dds_low_command.motor_cmd().at(i).tau() = command.tau_ff[i];
      dds_low_command.motor_cmd().at(i).q() = command.q_target[i];
      dds_low_command.motor_cmd().at(i).dq() = command.dq_target[i];
      dds_low_command.motor_cmd().at(i).kp() = command.kp[i];
      dds_low_command.motor_cmd().at(i).kd() = command.kd[i];
    }

    dds_low_command.crc() =
        Crc32Core((uint32_t*)&dds_low_command, (sizeof(dds_low_command) >> 2) - 1);
    lowcmd_publisher_->Write(dds_low_command);
  }

  BridgeOptions options_;
  std::string network_interface_;
  std::unique_ptr<ActionSource> action_source_;
  std::unique_ptr<LcmRobotStatePublisher> lcm_state_publisher_;

  TimestampedBuffer<LowState_> low_state_buffer_;
  TimestampedBuffer<IMUState_> imu_torso_buffer_;
  TimestampedBuffer<MotorCommand> motor_command_buffer_;
  dex3::Dex3Hands dex3_hands_;

  // Dex3 grip 상태 추적
  bool last_left_grip_  = false;
  bool last_right_grip_ = false;

  // Dex3 joint position 상태 추적
  std::optional<std::array<float, dex3::kMotorCount>> last_left_hand_q_;
  std::optional<std::array<float, dex3::kMotorCount>> last_right_hand_q_;

  std::optional<MotorCommand> last_valid_command_;


#if defined(G1_HAS_OPENCV) && defined(G1_HAS_LCM)
  std::unique_ptr<CameraStreamer> camera_streamer_;
#endif

  ChannelPublisherPtr<LowCmd_> lowcmd_publisher_;
  ChannelSubscriberPtr<LowState_> lowstate_subscriber_;
  ChannelSubscriberPtr<IMUState_> imutorso_subscriber_;

  std::atomic_bool running_{false};
  std::thread command_writer_thread_;
  std::thread control_thread_;
  std::thread lcm_state_thread_;

  std::atomic<uint8_t> mode_machine_{0};
  std::atomic<uint64_t> lowstate_count_{0};
  std::atomic<uint64_t> action_applied_count_{0};
  std::atomic<uint64_t> crc_error_count_{0};
  bool reported_waiting_for_lowstate_ = false;
};

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " <network_interface> [options]\n"
            << "\n"
            << "Example:\n"
            << "  " << argv0 << " enP8p1s0\n"
            << "  " << argv0 << " eth0 --action-source lcm --publish-lcm-state --dry-run-actions\n"
            << "\n"
            << "Options:\n"
            << "  --action-source <null|lcm>       Action source (default: null)\n"
            << "  --lcm-url <url>                  LCM URL (default: LCM default/env)\n"
            << "  --lcm-action-channel <name>      Action channel (default: "
            << g1_lcm::kDefaultActionChannel << ")\n"
            << "  --publish-lcm-state              Publish robot state over LCM\n"
            << "  --lcm-state-channel <name>       State channel (default: "
            << g1_lcm::kDefaultStateChannel << ")\n"
            << "  --lcm-state-rate <hz>            State publish rate (default: 100)\n"
            << "  --dry-run-actions                Receive actions but keep damping command\n"
            << "  --publish-camera                 Stream D435i over LCM (requires OpenCV)\n"
            << "  --lcm-camera-channel <name>      Camera channel (default: G1_CAMERA_FRAME)\n"
            << "  --camera-device <n>              V4L2 device index (default: 2)\n"
            << "  --camera-fps <hz>                Capture rate (default: 30)\n"
            << "  --camera-jpeg-quality <0-100>    JPEG quality (default: 80)\n"
            << "\n"
            << "This v1 bridge uses NullActionSource, so it only verifies low-level\n"
            << "DDS connectivity and keeps a damping fallback command active.\n"
            << "Dex3 left/right hand DDS channels are also initialized, but default to\n"
            << "timeout-stop commands so the fingers are not commanded to move.\n";
}

bool ParseOptions(int argc, char** argv, BridgeOptions& options) {
  if (argc < 2 || std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0) {
    return false;
  }

  options.network_interface = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << '\n';
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--action-source") {
      const char* value = require_value("--action-source");
      if (!value) return false;
      options.action_source = value;
      if (options.action_source != "null" && options.action_source != "lcm") {
        std::cerr << "--action-source must be 'null' or 'lcm'\n";
        return false;
      }
    } else if (arg == "--lcm-url") {
      const char* value = require_value("--lcm-url");
      if (!value) return false;
      options.lcm_url = value;
    } else if (arg == "--lcm-action-channel") {
      const char* value = require_value("--lcm-action-channel");
      if (!value) return false;
      options.lcm_action_channel = value;
    } else if (arg == "--publish-lcm-state") {
      options.publish_lcm_state = true;
    } else if (arg == "--lcm-state-channel") {
      const char* value = require_value("--lcm-state-channel");
      if (!value) return false;
      options.lcm_state_channel = value;
    } else if (arg == "--lcm-state-rate") {
      const char* value = require_value("--lcm-state-rate");
      if (!value) return false;
      options.lcm_state_rate_hz = std::atoi(value);
      if (options.lcm_state_rate_hz <= 0 || options.lcm_state_rate_hz > 1000) {
        std::cerr << "--lcm-state-rate must be in 1..1000\n";
        return false;
      }
    } else if (arg == "--dry-run-actions") {
      options.dry_run_actions = true;
    } else if (arg == "--publish-camera") {
      options.publish_camera = true;
    } else if (arg == "--lcm-camera-channel" && i + 1 < argc) {
      options.lcm_camera_channel = argv[++i];
    } else if (arg == "--camera-device" && i + 1 < argc) {
      options.camera_device = std::atoi(argv[++i]);
    } else if (arg == "--camera-fps" && i + 1 < argc) {
      options.camera_fps = std::atoi(argv[++i]);
    } else if (arg == "--camera-jpeg-quality" && i + 1 < argc) {
      options.camera_jpeg_quality = std::atoi(argv[++i]);
    } else {
      std::cerr << "Unknown option: " << arg << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  BridgeOptions options;
  if (!ParseOptions(argc, argv, options)) {
    PrintUsage(argv[0]);
    return (argc >= 2 && (std::strcmp(argv[1], "-h") == 0 ||
                          std::strcmp(argv[1], "--help") == 0)) ? 0 : 1;
  }

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  try {
    G1LowLevelBridge bridge(std::move(options));
    if (!bridge.Start()) {
      return 1;
    }
    bridge.Wait();
    bridge.Stop();
  } catch (const std::exception& e) {
    std::cerr << "[bridge] Fatal error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
