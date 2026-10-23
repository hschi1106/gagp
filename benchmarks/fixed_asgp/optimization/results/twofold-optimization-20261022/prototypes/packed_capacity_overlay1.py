"""Copied26-header prototype: charge logical work, check capacity only for actual writes."""
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
      const bool packed_capacity=st.private_adjacent_concat && st.private_packed_capacity &&
          st.logical_list_limit>0 && st.logical_list_limit<=65536;''')
    anchor = '        if(st.list_values_used>=st.kMaxListValues) {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '        if((packed_capacity?st.private_cells_used:st.list_values_used)>=st.kMaxListValues) {')
    # Preserve semantic/range guards and legacy capacity order when disabled.
    anchor = 'n+m>st.kMaxListValues-st.list_values_used) {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '(!packed_capacity && n+m>st.kMaxListValues-st.list_values_used)) {')
    # The adjacent result returns above without writing any new cells.
    # Only the non-adjacent copying path needs this actual-cell reservation.
    anchor = '      const int offset=st.private_adjacent_concat?st.private_cells_used:st.list_values_used;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '''      if(packed_capacity && n+m>st.kMaxListValues-st.private_cells_used) {
        st.physical_capacity_exceeded=true;st.physical_capacity_reason|=DPAYLOAD_OVERFLOW_VALUES;
        err=ErrCode::Value;return false;
      }
'''+anchor)
    anchor = '    st.private_cells_used=values;st.private_cursor_checkpoint=values;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '''    const bool packed=st.private_packed_capacity && st.logical_list_limit>0 && st.logical_list_limit<=65536;
    // Both boundaries are >= every pre-region live span. Retaining some dead
    // temporary cells is conservative; never jump beyond actual capacity.
    st.private_cells_used=packed && st.private_cells_used<values?st.private_cells_used:values;
    st.private_cursor_checkpoint=values;''')
    path.write_text(text)
