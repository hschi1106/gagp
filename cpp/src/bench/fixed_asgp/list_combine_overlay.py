"""Generate an opt-in ASGP source overlay; never edit the external checkout.

list-combine-v1 adds general head/last/singleton productions to IntList combine.
The existing unary kHead op field distinguishes first (0) and last (1).
Strict anchors reject incompatible source versions instead of silently patching.
"""
from pathlib import Path
import sys


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f'ASGP list-combine-v1 source anchor mismatch: {old!r}')
    return text.replace(old, new)


def generate(source: Path, destination: Path) -> None:
    edits = {
        'lang/grammar.cc': (
            '  const Type carrier = config.carrier;\n  std::vector<Production> out;',
            '''  const Type carrier = config.carrier;
  std::vector<Production> out;
  // Explicit list-combine-v1 benchmark contract, not original ASGP grammar.
  if (carrier == Type::kIntList) {
    out.push_back(Unary(NodeKind::kHead, Type::kInt, Type::kIntList, 0));
    out.push_back(Unary(NodeKind::kHead, Type::kInt, Type::kIntList, 1));
    out.push_back(Unary(NodeKind::kSingleton, Type::kIntList, Type::kInt));
  }'''),
        'eval/interpreter.cc': (
            '      return state.arena->At(list)[0];',
            '      return state.arena->At(list)[n.op == 1 ? list.len - 1 : 0];'),
        'lang/printer.cc': (
            '    case NodeKind::kBoolOp:\n      out += Symbol',
            '''    case NodeKind::kHead:
      out += n.op == 1 ? "last" : "head";
      break;
    case NodeKind::kBoolOp:
      out += Symbol'''),
    }
    # Validate every source before publishing any generated file.
    generated = {name: replace_once((source / name).read_text(), *edit)
                 for name, edit in edits.items()}
    for name, contents in generated.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents)


if __name__ == '__main__':
    generate(Path(sys.argv[1]), Path(sys.argv[2]))
