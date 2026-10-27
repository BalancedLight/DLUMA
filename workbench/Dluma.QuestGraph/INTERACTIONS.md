# Graph editing behavior

The companion treats PXSL source as authoritative. Structural commands create localized byte edits, reparse the resulting source, and enter the existing application-owned undo stack. Layout groups, node positions, collapse state, notes, and viewport state are stored only in the `.pxsl.dlumagraph.json` sidecar.

## Structural commands

- **Copy / Paste / Paste child** copies a complete editable source subtree in process memory and reindents it at the destination. It does not use or modify the Windows clipboard.
- **Up / Down** swaps adjacent siblings only in an ordered PXSL container.
- **Connect** supports direct output-to-input connector dragging as well as the toolbar command. Reconnecting a target atomically moves its exact PXSL subtree from the old relation to the new compatible port in one undo entry.
- **Output to empty canvas** opens a searchable, category-grouped creation picker at the drop point. Choosing an entry inserts its minimum stock-derived template and connects it in one source transaction; cancelling changes nothing.
- **Disconnect** is available from connectors, the toolbar, and node context menus. It removes the selected structural relation and stages the target subtree intact at the PXSL root, where it is clearly marked **Detached** until reconnected. Temporary compile errors are expected while a graph is detached.
- **Delete** removes all selected source-backed subtrees atomically. Descendants covered by a selected ancestor are coalesced, and opaque nodes use their exact bounded source region.

Structural actions reject containment cycles, illegal parents, incompatible semantic ports, and stale source receipts. Exact-region delete and disconnect remain available for opaque nodes; editing their contents still belongs in the PXSL source editor.

## Layout groups

Select nodes and choose **Group**. A node belongs to at most one group; grouping it again moves it to the new group. **Ungroup** removes selected nodes, or all members of the selected Groups-table row. Group title and bounds are editable in the Groups table. These actions do not change PXSL bytes and participate in the same undo/redo history as other graph layout changes.
