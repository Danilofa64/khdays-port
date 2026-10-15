#include "core/files/catalog_tree.h"

#include <algorithm>
#include <map>

namespace studio {

std::string leaf_name(const std::string& key) {
    const auto slash = key.find_last_of('/');
    return slash == std::string::npos ? key : key.substr(slash + 1);
}

CatalogTree build_catalog_tree(const Catalog& catalog, const std::vector<int>& results, bool by_kind) {
    CatalogTree t;
    t.nodes.push_back(TreeNode{});
    t.node_of_item.assign(catalog.items.size(), -1);
    // a node's children by name while building
    std::vector<std::map<std::string, int>> named(1);
    const auto child = [&](int parent, const std::string& name, int kind) {
        auto& m = named[static_cast<std::size_t>(parent)];
        const std::string key = kind >= 0 ? "\x01" + std::to_string(kind) : name;
        const auto found = m.find(key);
        if (found != m.end()) return found->second;
        const int n = static_cast<int>(t.nodes.size());
        TreeNode node;
        node.name = name;
        node.kind = kind;
        node.parent = parent;
        t.nodes.push_back(node);
        named.emplace_back();
        named[static_cast<std::size_t>(parent)][key] = n;
        t.nodes[static_cast<std::size_t>(parent)].children.push_back(n);
        return n;
    };
    // the folders first, so a key that is a folder's name becomes that node's item
    std::vector<int> leaves;
    for (const int index : results) {
        const Item& item = catalog.items[static_cast<std::size_t>(index)];
        int at = by_kind ? child(0, std::string(), static_cast<int>(item.kind)) : 0;
        std::size_t from = 0;
        for (auto slash = item.key.find('/'); slash != std::string::npos; slash = item.key.find('/', from)) {
            at = child(at, item.key.substr(from, slash - from), -1);
            from = slash + 1;
        }
        t.node_of_item[static_cast<std::size_t>(index)] = at;
        leaves.push_back(index);
    }
    for (const int index : leaves) {
        const Item& item = catalog.items[static_cast<std::size_t>(index)];
        const int parent = t.node_of_item[static_cast<std::size_t>(index)];
        const auto& m = named[static_cast<std::size_t>(parent)];
        const auto same = m.find(leaf_name(item.key));
        if (same != m.end() && t.nodes[static_cast<std::size_t>(same->second)].item < 0) {
            t.nodes[static_cast<std::size_t>(same->second)].item = index;  // a model and its textures' folder
            t.node_of_item[static_cast<std::size_t>(index)] = same->second;
        } else {
            t.nodes[static_cast<std::size_t>(parent)].items.push_back(index);
        }
    }
    // counts (children were added after their parents: count from the last)
    for (std::size_t n = t.nodes.size(); n-- > 0;) {
        TreeNode& node = t.nodes[n];
        node.count += static_cast<int>(node.items.size()) + (node.item >= 0 ? 1 : 0);
        if (node.parent >= 0) t.nodes[static_cast<std::size_t>(node.parent)].count += node.count;
    }
    // order: kinds by kind, folders and leaves by name
    for (auto& node : t.nodes) {
        std::sort(node.children.begin(), node.children.end(), [&](int a, int b) {
            const TreeNode& x = t.nodes[static_cast<std::size_t>(a)];
            const TreeNode& y = t.nodes[static_cast<std::size_t>(b)];
            if (x.kind != y.kind) return x.kind < y.kind;
            return x.name < y.name;
        });
        std::sort(node.items.begin(), node.items.end(), [&](int a, int b) {
            return leaf_name(catalog.items[static_cast<std::size_t>(a)].key) < leaf_name(catalog.items[static_cast<std::size_t>(b)].key);
        });
    }
    return t;
}

}  // namespace studio
