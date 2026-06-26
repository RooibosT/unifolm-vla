#ifndef G1_DEPLOY_REAL_NULL_ACTION_SOURCE_HPP
#define G1_DEPLOY_REAL_NULL_ACTION_SOURCE_HPP

#include <optional>
#include <string>

#include "action_source.hpp"

class NullActionSource final : public ActionSource {
 public:
  std::optional<ActionCommand> Poll() override { return std::nullopt; }
  std::string Name() const override { return "null"; }
};

#endif

