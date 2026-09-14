#pragma once

#include <string>

#include "gagp/evolution/ast_program.hpp"

namespace gagp::evo {

const char* fuel_event_name(FuelEvent event);
bool parse_fuel_event(const std::string& name, FuelEvent* out);
bool supports_fuel_event(NodeKind kind, FuelEvent event);

}  // namespace gagp::evo
