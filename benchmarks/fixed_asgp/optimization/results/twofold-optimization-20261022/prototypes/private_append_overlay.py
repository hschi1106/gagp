"""Patch ONLY a copied GPU tree for the independent adjacency/cursor probe."""
from pathlib import Path


def apply(gpu_root):
    path = Path(gpu_root) / 'device/builtins_device.cuh'
    original = path.read_text()
    text = original
    anchor = '  bool fused_list_concat = false;'
    assert text.count(anchor) == 2
    text = text.replace(anchor, anchor + '''
  bool private_adjacent_concat = false;
  int private_cells_used = 0;
  int private_cursor_checkpoint = 0;''')
    anchor = '  st.list_values_used=values;\n}'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '''  st.list_values_used=values;
  if(st.private_adjacent_concat) {
    // A scalar-return rewind invalidates newly allocated lists. Older live
    // spans end before this reservation boundary. Conservatively restart
    // physical allocation at the ordinary boundary, losing only compression.
    st.private_cells_used=values;st.private_cursor_checkpoint=values;
  }
}''')
    anchor = '      DPrivateIntListSpan a,b;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + '''
      if(st.private_adjacent_concat && st.list_values_used!=st.private_cursor_checkpoint) {
        // A generic allocation or external initialization changed the ordinary
        // cursor. Its writes are beyond all older compressed spans.
        st.private_cells_used=st.list_values_used;
        st.private_cursor_checkpoint=st.list_values_used;
      }''')
    anchor = '        const int offset=st.list_values_used++;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '''        const int offset=st.private_adjacent_concat?st.private_cells_used++:st.list_values_used;
        ++st.list_values_used;
        if(st.private_adjacent_concat)st.private_cursor_checkpoint=st.list_values_used;''')
    anchor = '''      const int offset=st.list_values_used;
      if(st.compact_intlist_cells) {
        d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset)*st.private_cell_stride,a,st.private_warp_copy,st.private_cell_stride);
        d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset+n)*st.private_cell_stride,b,st.private_warp_copy,st.private_cell_stride);
      } else {
        for(int i=0;i<n;++i)st.list_values[offset+i]=a.boxed[i];
        for(int i=0;i<m;++i)st.list_values[offset+n+i]=b.boxed[i];
      }
      st.list_values_used+=n+m;out=d_private_intlist(offset,n+m);return true;'''
    assert text.count(anchor) == 1
    replacement = '''      if(st.private_adjacent_concat) {
        const auto abits=Value::container_hash48(args[0]);
        const auto bbits=Value::container_hash48(args[1]);
        const bool aglobal=!args[0].b || (abits&DPRIVATE_LIST_GLOBAL)!=0;
        const bool bglobal=!args[1].b || (bbits&DPRIVATE_LIST_GLOBAL)!=0;
        const bool adjacent=aglobal==bglobal && (aglobal || !st.compact_intlist_cells?
            a.boxed+n==b.boxed:
            a.stride==b.stride && a.integers+static_cast<std::size_t>(n)*a.stride==b.integers);
        if(adjacent) {
          const int offset=args[0].b?static_cast<int>(abits&DPRIVATE_LIST_OFFSET_MASK):
              static_cast<int>(a.boxed-tables.list_values);
          // Keep EXACT ordinary reservation/charge/overflow behavior. Only
          // physical writes and the opaque private backing offset change.
          st.list_values_used+=n+m;st.private_cursor_checkpoint=st.list_values_used;
          out=d_private_intlist(offset,n+m,aglobal);return true;
        }
      }
      const int offset=st.private_adjacent_concat?st.private_cells_used:st.list_values_used;
      if(st.compact_intlist_cells) {
        d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset)*st.private_cell_stride,a,st.private_warp_copy,st.private_cell_stride);
        d_copy_private_intlist(st.intlist_cells+static_cast<std::size_t>(offset+n)*st.private_cell_stride,b,st.private_warp_copy,st.private_cell_stride);
      } else {
        for(int i=0;i<n;++i)st.list_values[offset+i]=a.boxed[i];
        for(int i=0;i<m;++i)st.list_values[offset+n+i]=b.boxed[i];
      }
      st.list_values_used+=n+m;
      if(st.private_adjacent_concat) {
        st.private_cells_used+=n+m;st.private_cursor_checkpoint=st.list_values_used;
      }
      out=d_private_intlist(offset,n+m);return true;'''
    text = text.replace(anchor, replacement)
    assert text != original
    path.write_text(text)
