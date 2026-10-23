"""Patch ONLY a copied admitted26 GPU tree for physical-capacity replay diagnosis."""
from pathlib import Path


def apply(gpu_root):
    path = Path(gpu_root) / 'device/builtins_device.cuh'
    text = path.read_text()
    anchor = '  bool private_adjacent_concat = false;'
    assert text.count(anchor) == 2
    text = text.replace(anchor, anchor + '\n  bool private_packed_capacity = false;')
    anchor = '      DPrivateIntListSpan a,b;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + '''
      // The positive semantic budget bounds reservation growth. Unlimited
      // allocation keeps the original conservative capacity/replay rule.
      const bool packed_capacity=st.private_adjacent_concat && st.private_packed_capacity &&
          st.logical_list_limit>0 && st.logical_list_limit<=65536;''')
    anchor = '        if(st.list_values_used>=st.kMaxListValues) {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '        if((packed_capacity?st.private_cells_used:st.list_values_used)>=st.kMaxListValues) {')
    anchor = 'n+m>st.kMaxListValues-st.list_values_used) {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, 'n+m>st.kMaxListValues-(packed_capacity?st.private_cells_used:st.list_values_used)) {')
    path.write_text(text)
