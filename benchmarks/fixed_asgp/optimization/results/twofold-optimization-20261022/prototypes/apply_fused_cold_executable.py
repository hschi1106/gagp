"""Return a private executable from the admission that just validated it."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[6]


def apply(dry_run=False):
    changes={}

    def edit(name,old,new):
        path=ROOT/name;text=changes.get(path,path.read_text())
        assert text.count(old)==1,(name,old[:100],text.count(old))
        changes[path]=text.replace(old,new)

    header='cpp/src/evolution/grammar/cold_admission.hpp'
    edit(header,'    const GenerationRequest& request,VerifiedAst* verified,std::vector<std::vector<int>>* environments);',
         '''    const GenerationRequest& request,VerifiedAst* verified,std::vector<std::vector<int>>* environments,
    std::shared_ptr<const BytecodeProgram>* executable=nullptr,const std::vector<std::string>* inputs=nullptr);''')
    edit(header,'  const char* enabled=std::getenv("GAGP_COLD_PAYLOAD_PROOFS");', '''  const char* fused_setting=std::getenv("GAGP_FUSED_COLD_EXECUTABLE");
  const bool fused=executable && fused_setting && std::string(fused_setting)=="1";
  const auto has_registry=[&] {return std::any_of(genome.ast.consts.begin(),genome.ast.consts.end(),[](const auto& value) {
    return value.tag==ValueTag::String || value.tag==ValueTag::IntList ||
        value.tag==ValueTag::FloatList || value.tag==ValueTag::StringList;
  });};
  const char* enabled=std::getenv("GAGP_COLD_PAYLOAD_PROOFS");''')
    edit(header,'  const auto ordinary=[&] {\n    auto witness=reconstruct_derivation(grammar,genome,request,verified,environments);', '''  const auto ordinary=[&] {
    // Scalar artifacts need no registry read transaction. The private callee
    // still validates this invocation's full identity before publishing one.
    if(fused && !has_registry())return reconstruct_cold_import(grammar,genome,request,verified,environments,executable,inputs);
    auto witness=reconstruct_derivation(grammar,genome,request,verified,environments);''')
    edit(header,'''    result.witness=reconstruct_cold_import(grammar,genome,request,verified?&prepared:nullptr,environments?&lexical:nullptr);
    if(executable)result.executable=cold_import_bytecode(grammar,genome,result.witness,request,inputs);''', '''    result.witness=reconstruct_cold_import(grammar,genome,request,verified?&prepared:nullptr,environments?&lexical:nullptr,
        fused?&result.executable:nullptr,inputs);
    if(executable && !fused)result.executable=cold_import_bytecode(grammar,genome,result.witness,request,inputs);''')
    source='cpp/src/evolution/grammar/membership.cpp'
    marker='static DerivationMetadata reconstruct_derivation_impl('
    helper='''// Called only after this very admission validated the complete identity or
// constructed a fresh certificate. No caller supplies a witness or "verified"
// boolean to this accessor, and no second identity serialization is needed.
static std::shared_ptr<const BytecodeProgram> fused_cold_executable_after_admission(
    const CompiledGrammar& grammar,const ProgramGenome& genome,const DerivationCertificate& proof,
    const std::vector<std::string>* inputs) {
  if(!cold_executables_enabled() || !certificates_enabled() ||
      !std::getenv("GAGP_REUSE_ADMISSION_COMPILE") || !proof.executable)return nullptr;
  if(inputs) {
    if(inputs->size()!=grammar.inputs().size())return nullptr;
    for(std::size_t i=0;i<inputs->size();++i)if((*inputs)[i]!=grammar.inputs()[i].name)return nullptr;
  }
  const bool registry=std::any_of(genome.ast.consts.begin(),genome.ast.consts.end(),[](const auto& v) {
    return v.tag==ValueTag::String || v.tag==ValueTag::IntList || v.tag==ValueTag::FloatList || v.tag==ValueTag::StringList;
  });
  if(registry) {
    if(!payload::StagedPayloads::has_active_scope() || proof.executable_constants.size()!=genome.ast.consts.size())return nullptr;
    for(std::size_t i=0;i<genome.ast.consts.size();++i) {
      const auto a=proof.executable_constants[i],b=genome.ast.consts[i];
      if(a.tag!=b.tag || a.b!=b.b || std::memcmp(&a.i,&b.i,sizeof(a.i))!=0)return nullptr;
    }
  } else for(const auto& v:genome.ast.consts) {
    if(v.tag!=ValueTag::Int && v.tag!=ValueTag::Float && v.tag!=ValueTag::Bool && v.tag!=ValueTag::Char)return nullptr;
  }
  return proof.executable;
}

'''
    edit(source,marker,helper+marker)
    edit(source,'    ProgramGenome* compact_owned = nullptr,bool retain_registry_executable = false) {', '''    ProgramGenome* compact_owned = nullptr,bool retain_registry_executable = false,
    std::shared_ptr<const BytecodeProgram>* validated_executable=nullptr,
    const std::vector<std::string>* execution_inputs=nullptr) {
  if(validated_executable)validated_executable->reset();''')
    edit(source,'      witness.certificate = genome.derivation->certificate;\n      return witness;', '''      witness.certificate = genome.derivation->certificate;
      if(validated_executable)*validated_executable=fused_cold_executable_after_admission(grammar,genome,proof,execution_inputs);
      return witness;''')
    edit(source,'  if (verified) *verified = matcher.take_verified();\n  return witness;\n}\n\nDerivationMetadata detail::reconstruct_cold_import', '''  if (verified) *verified = matcher.take_verified();
  if(validated_executable && witness.certificate)
    *validated_executable=fused_cold_executable_after_admission(grammar,genome,*witness.certificate,execution_inputs);
  return witness;
}

DerivationMetadata detail::reconstruct_cold_import''')
    edit(source,'''    const GenerationRequest& request,VerifiedAst* verified,std::vector<std::vector<int>>* environments) {
  return reconstruct_derivation_impl(grammar,genome,request,verified,environments,nullptr,
      cold_executables_enabled() && payload::StagedPayloads::has_active_scope());''', '''    const GenerationRequest& request,VerifiedAst* verified,std::vector<std::vector<int>>* environments,
    std::shared_ptr<const BytecodeProgram>* executable,const std::vector<std::string>* inputs) {
  return reconstruct_derivation_impl(grammar,genome,request,verified,environments,nullptr,
      cold_executables_enabled() && payload::StagedPayloads::has_active_scope(),executable,inputs);''')
    edit('cpp/src/evolution/repro/gpu/phase_layout.hpp',
         '  const auto executable = std::getenv("GAGP_REUSE_IMPORT_CERTIFICATE")',
         '  const auto executable = !cold_executable && std::getenv("GAGP_REUSE_IMPORT_CERTIFICATE")')

    test='cpp/tests/evolution/test_grammar_membership.cpp'
    edit(test,'void test_cold_import_executables() {\n  const char* flags[]={"GAGP_COLD_PAYLOAD_PROOFS","GAGP_COLD_IMPORT_EXECUTABLES","GAGP_REUSE_ADMISSION_COMPILE"};',
         '''void test_cold_import_executables(bool fused=false) {
  const char* flags[]={"GAGP_COLD_PAYLOAD_PROOFS","GAGP_COLD_IMPORT_EXECUTABLES","GAGP_REUSE_ADMISSION_COMPILE","GAGP_FUSED_COLD_EXECUTABLE"};''')
    edit(test,'''    const char** flags;std::optional<std::string> saved[3];
    explicit Restore(const char** f):flags(f){for(int i=0;i<3;++i){const auto* v=std::getenv(f[i]);if(v)saved[i]=v;setenv(f[i],"1",1);}}
    ~Restore(){for(int i=0;i<3;++i){if(saved[i])setenv(flags[i],saved[i]->c_str(),1);else unsetenv(flags[i]);}}''', '''    const char** flags;std::optional<std::string> saved[4];
    explicit Restore(const char** f):flags(f){for(int i=0;i<4;++i){const auto* v=std::getenv(f[i]);if(v)saved[i]=v;setenv(f[i],"1",1);}}
    ~Restore(){for(int i=0;i<4;++i){if(saved[i])setenv(flags[i],saved[i]->c_str(),1);else unsetenv(flags[i]);}}''')
    anchor='  } restore(flags);\n  const auto grammar=compile_grammar(parse_definition(constant_grammar("IntList","[[]]")));'
    # Restrict this edit to the executable test; other proof tests share a fixture.
    path=ROOT/test;text=changes[path];start=text.index('void test_cold_import_executables(bool fused=false)')
    stop=text.index('\n}  // namespace',start);part=text[start:stop];assert part.count(anchor)==1
    scalar_test=(Path(__file__).parent/'fused_cold_scalar_probe.txt').read_text()
    replacement='  } restore(flags);\n  setenv("GAGP_FUSED_COLD_EXECUTABLE",fused?"1":"0",1);\n'+scalar_test+'\n  const auto grammar=compile_grammar(parse_definition(constant_grammar("IntList","[[]]")));'
    changes[path]=text[:start]+part.replace(anchor,replacement)+text[stop:]
    edit(test,'    test_cold_import_executables();','    test_cold_import_executables();\n    test_cold_import_executables(true);')

    gpu='cpp/tests/gpu/test_gpu_phase_population.cu'
    edit(gpu,'void check_pool_evolution(','''template<class Function>
auto with_fused_cold(Function function)->decltype(function()) {
 const char* flag="GAGP_FUSED_COLD_EXECUTABLE";const char* value=std::getenv(flag);
 struct Restore {const char* flag;bool present;std::string value;~Restore(){if(present)setenv(flag,value.c_str(),1);else unsetenv(flag);}}
     restore{flag,value!=nullptr,value?value:""};
 setenv(flag,"1",1);return function();
}
void check_pool_evolution(''')
    edit(gpu,'   require(bool(artifact)==registry,"cold executable retention differs from actual constant storage");', '''   const char* fused=std::getenv("GAGP_FUSED_COLD_EXECUTABLE");
   require(bool(artifact)==(registry || (fused && std::string(fused)=="1")),"cold executable retention differs from actual admission mode");''')
    edit(gpu,' require(retained_cold_artifact==!sequence_windows && certified_cold_miss==!sequence_windows,', ''' const char* fused=std::getenv("GAGP_FUSED_COLD_EXECUTABLE");
 require(retained_cold_artifact==(!sequence_windows || (fused && std::string(fused)=="1")) && certified_cold_miss==!sequence_windows,''')
    edit(gpu,' test_private_intlist_generations(true,true);', ''' test_private_intlist_generations(true,true);
 with_fused_cold([]{test_private_intlist_generations(false,true);});
 with_fused_cold([]{test_private_intlist_generations(true);});''')
    edit(gpu,'  repro::NativePhasePopulation reordered(grammar,parents,reversed_names);',
         '  auto reordered=with_fused_cold([&]{return std::make_unique<repro::NativePhasePopulation>(grammar,parents,reversed_names);});')
    edit(gpu,' reversed=reordered.evaluate(reordered_session,true);',' reversed=reordered->evaluate(reordered_session,true);')
    if not dry_run:
        for path,text in changes.items():path.write_text(text)
    return [str(p.relative_to(ROOT)) for p in changes]
