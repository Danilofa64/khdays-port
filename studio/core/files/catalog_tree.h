// The catalog as folders: the items found, by kind, then by the folders of
// their keys (`ba/ch/ro/def.p.z/tex/ro_head@ro_head_pl` sits in ba > ch > ro >
// def.p.z > tex), so a person opens what they look for instead of reading
// thousands of lines. A key that is also a folder (a model and its textures)
// is one node with its item and its children.
#pragma once

#include "core/files/catalog.h"

#include <string>
#include <vector>

namespace studio {

struct TreeNode {
    std::string name;           // the folder's (or the kind's, empty for a kind node: see `kind`)
    int kind = -1;              // a kind's node (top level, all kinds shown): its Kind
    int item = -1;              // the catalog item this node is, or -1
    std::vector<int> children;  // nodes, folders first then by name
    std::vector<int> items;     // leaves: catalog items, by name
    int count = 0;              // items in it and under it
    int parent = -1;
};

struct CatalogTree {
    std::vector<TreeNode> nodes;  // nodes[0] the root
    std::vector<int> node_of_item;  // per catalog item: the node holding it (leaf parent or its own node), -1 when not found
};

// The tree of `results` (catalog item indices); with `by_kind`, a node per
// kind at the top.
CatalogTree build_catalog_tree(const Catalog& catalog, const std::vector<int>& results, bool by_kind);

// The leaf name of an item's key (after its last '/').
std::string leaf_name(const std::string& key);

}  // namespace studio
