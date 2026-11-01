### New
- Web config Nodes tab: a search box finds nodes by short name, long name or node ID, with or without the leading `!` (`1a2b` finds `!1a2b3c4d`). It lists the matches with a count, marks the ones without a position as *no position*, and shows the positioned ones as blue markers on the map, which zooms to fit them. All boards except the Cardputer.
- Web config Nodes tab: if only one node matches, it is shown straight away. Enter shows the first match, and Esc clears the search.
- Web config Nodes tab: three checkboxes, *Show only known*, *Show only with location* and *Show only favorites*, narrow the node dropdown. They can be combined, a line shows how many nodes are left, and the browser remembers which are ticked. All boards except the Cardputer.

### Changed
- Web config Nodes tab: the page now has a single map. The small second map under the node dropdown is gone, and the tab now reads, top to bottom: filters, dropdown, search, node details, map. All boards except the Cardputer.
- Web config Nodes tab: choosing a node from the dropdown, from the search results or by clicking its blue marker on the map all do the same thing. The node's details fill the panel above the map, and the map centres on it with a blue marker. For a node with no position, the map's status line says so.
- Web config Nodes tab: the node dropdown is sorted alphabetically, ignoring case and putting numbers in numeric order. Favorites stay first, above the divider, and nodes with no name yet (`----`) come last. Favoriting a node re-sorts the list straight away.

Pull request: #138
