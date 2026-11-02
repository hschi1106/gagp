// Engine profile lookup: explicit variables win, unset switches take the
// mainline value, GAGP_ENGINE_PROFILE=legacy restores "unset", and names
// outside the profile are plain getenv.
#include "gagp/core/engine_profile.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const std::string& what) {
  if (!ok) throw std::runtime_error(what);
}
bool same(const char* value, const char* expected) {
  return value && expected ? std::strcmp(value, expected) == 0 : value == expected;
}
}  // namespace

int main() {
  try {
    const char* member = "GAGP_GPU_WARP_VARIATION";
    ::unsetenv("GAGP_ENGINE_PROFILE");
    ::unsetenv(member);
    require(same(gagp::engine_env(member), "1"), "mainline default");
    ::setenv(member, "0", 1);
    require(same(gagp::engine_env(member), "0"), "explicit value must win");
    ::unsetenv(member);
    ::setenv("GAGP_ENGINE_PROFILE", "legacy", 1);
    require(same(gagp::engine_env(member), nullptr), "legacy profile must leave switches unset");
    ::setenv("GAGP_ENGINE_PROFILE", "mainline", 1);
    require(same(gagp::engine_env(member), "1"), "explicit mainline profile");
    require(same(gagp::engine_env("GAGP_GPU_DIAGNOSTICS"), nullptr), "diagnostics are not profile members");
    require(same(gagp::engine_env("GAGP_COUNTED_SITE_DRAW"), nullptr), "search semantics are not profile members");
    for (const auto& entry : gagp::kMainlineEngineProfile)
      require(std::strncmp(entry.name, "GAGP_", 5) == 0 && entry.value && *entry.value, "malformed profile entry");
    ::setenv("GAGP_ENGINE_PROFILE", "fast", 1);
    bool rejected = false;
    try { (void)gagp::engine_env(member); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "unknown profile accepted");
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "engine profile tests passed\n";
  return 0;
}
