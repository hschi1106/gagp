#pragma once

#include <string>
#include <vector>
#include "gp/individual.h"
#include "task/registry.h"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"

namespace fixed_asgp {
namespace gg = gagp::evo::grammar;
using Json = gagp::cli_detail::JsonValue;
Json string(std::string value);
Json number(double value);
Json array(std::vector<Json> values);
Json object(std::initializer_list<std::pair<const std::string, Json>> values);
Json read(const std::string& path);
void write(const std::string& path, const Json& value);
Json encode(const asgp::Individual& individual, int phases);
asgp::Individual decode(const Json& value);
asgp::TaskSpec task(const std::string& name);
Json translated_root(const asgp::Individual& individual, const std::string& name);
gagp::evo::ProgramGenome translate(const asgp::Individual& individual, const std::string& name,
                                  const gg::ResolvedDefinition& definition,
                                  const gg::CompiledGrammar& grammar);
asgp::Individual solution(const std::string& name);
}  // namespace fixed_asgp
