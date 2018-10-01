#ifndef EXTPP_BTREE_TREE_HPP
#define EXTPP_BTREE_TREE_HPP

#include <extpp/allocator.hpp>
#include <extpp/anchor_handle.hpp>
#include <extpp/defs.hpp>
#include <extpp/engine.hpp>
#include <extpp/raw_btree.hpp>

#include <extpp/btree/base.hpp>
#include <extpp/btree/cursor.hpp>

#include <boost/intrusive/list.hpp>

namespace extpp::detail::btree_impl {

using index_iterator = detail::identity_iterator<u32>;

class tree : public uses_allocator {
    // FIXME template
    using anchor = detail::raw_btree_anchor;

public:
    tree(anchor_handle<anchor> _anchor, const raw_btree_options& opts, allocator& alloc);
    ~tree();

    tree(const tree&) = delete;
    tree& operator=(const tree&) = delete;

public:
    bool empty() const { return size() == 0; }

    u32 value_size() const { return m_options.value_size; }
    u32 key_size() const { return m_options.key_size; }

    u32 leaf_node_max_values() const { return m_leaf_capacity; }
    u32 internal_node_max_children() const { return m_internal_max_children; }
    u32 internal_node_min_chlidren() const { return m_internal_min_children; }

    // Returns left < right
    bool key_less(const byte* left_key, const byte* right_key) const {
        return m_options.key_less(left_key, right_key, m_options.user_data);
    }

    // Returns left == right (in a non-optimal way).
    bool key_equal(const byte* left_key, const byte* right_key) const {
        return !key_less(left_key, right_key) && !key_less(right_key, left_key);
    }

    // Returns left > right
    bool key_greater(const byte* left_key, const byte* right_key) const {
        return key_less(right_key, left_key);
    }

    // Returns left < right
    bool value_less(const byte* left_value, const byte* right_value) const;

    // Returns derive_key(value) == key
    bool value_equal_key(const byte* value, const byte* key) const {
        key_buffer k;
        derive_key(value, k.data());
        return key_equal(key, k.data());
    }

    // Returns key(value)
    void derive_key(const byte* value, byte* key_buffer) const {
        return m_options.derive_key(value, key_buffer, m_options.user_data);
    }

    // Seek the cursor to the lower bound of key.
    void lower_bound(const byte* key, cursor& cursor) const;

    // Seek the cursor to the upper bound of key.
    void upper_bound(const byte* key, cursor& cursor) const;

    // Find the key (or fail).
    void find(const byte* key, cursor& cursor) const;

    // Insert the value into the tree (or do nothing if the key exists).
    // Points to the value (old or new) after the operation completed.
    bool insert(const byte* value, cursor& cursor);

    /// Erase the element that is currently being pointed at by this cursor.
    void erase(cursor& cursor);

    void clear();

    void clear_subtree(block_index root, u32 level);

    std::unique_ptr<loader> bulk_load();

    std::unique_ptr<cursor> create_cursor(raw_btree::cursor_seek_t seek);

    void dump(std::ostream& os) const;

    void visit(bool (*visit_fn)(const raw_btree::node_view& node, void* user_data), void* user_data) const;

    void validate() const;

private:
    void seek_insert_location(const byte* key, cursor& cursor);

    // Create a new root node from two children (leaves or internal) and a split key.
    internal_node create_root(block_index left_child, block_index right_child, const byte* split_key);

    // Exactly like split(const leaf_node&, byte*) but for internal nodes.
    internal_node split(const internal_node& old_internal, byte* split_key);

    // The root was split. Make sure that all (valid) cursors include the new root in their path.
    void apply_root_split(const internal_node& new_root, const internal_node& left_leaf, const internal_node& right_leaf);

    // A node with a (non-full!) parent was split. The left node was at left_index and remains there. The right node
    // becomes the child at index left_index + 1 and all nodes further to the right have their index incremented by one.
    // 'level' is the level of the left node.
    void apply_child_split(const internal_node& parent, u32 left_level, u32 left_index, const internal_node& left, const internal_node& right);

private:
    // Handle the deletion of a leaf node in its parent node(s).
    void propagate_leaf_deletion(cursor& cursor, block_index child_node, u32 child_node_index);

    // Steal an entry from the neighboring node and put it into the leaf.
    void steal_leaf_entry(const internal_node& parent,
                          const leaf_node& leaf, u32 leaf_index,
                          const leaf_node& neighbor, u32 neighbor_index);
    void steal_internal_entry(const internal_node& parent, u32 stack_index,
                              const internal_node& internal, u32 internal_index,
                              const internal_node& neighbor, u32 neighbor_index);

    // Merge two neighboring nodes.
    // Note: values will end up in "leaf" and will be taken from "neighbor".
    void merge_leaf(const internal_node& parent,
                    const leaf_node& leaf, u32 leaf_index,
                    const leaf_node& neighbor, u32 neighbor_index);
    void merge_internal(const internal_node& parent, u32 stack_index,
                        const internal_node& node, u32 node_index,
                        const internal_node& neighbor, u32 neighbor_index);


private:
    // Index of the first value >= key or leaf.size() if none exists.
    u32 lower_bound(const leaf_node& leaf, const byte* key) const;

    // Index of the first child >= key or the index of the last child.
    u32 lower_bound(const internal_node& internal, const byte* key) const;

    u32 upper_bound(const leaf_node& leaf, const byte* key) const;
    u32 upper_bound(const internal_node& internal, const byte* key) const;

    // Seek to the first value with key(value) > key (upper)
    // or >= key (lower).
    enum seek_bound_t {
        seek_bound_lower,
        seek_bound_upper,
        seek_bound_find
    };

    template<seek_bound_t which>
    void seek_bound(const byte* key, cursor& cursor) const;

public:
    // TODO: Move rest of cursor navigation here too, then make this private again.
    // Also get rid of most of the code in cursor_impl.
    bool next_leaf(cursor& cursor) const;
    bool prev_leaf(cursor& cursor) const;

private:
    template<typename Func>
    void visit_nodes(Func&& fn) const;

public:
    leaf_node as_leaf(block_handle handle) const;
    internal_node as_internal(block_handle handle) const;

    leaf_node read_leaf(block_index index) const;
    internal_node read_internal(block_index) const;

private:
    friend loader;

    leaf_node create_leaf();
    internal_node create_internal();

    void free_leaf(block_index leaf);
    void free_internal(block_index internal);

private:
    // Cursor management

    friend cursor;

    using cursor_list_type = boost::intrusive::list<
        cursor,
        boost::intrusive::member_hook<
            cursor,
            decltype(cursor::m_cursors),
            &cursor::m_cursors
        >
    >;

    void link_cursor(cursor* cursor) {
        m_cursors.push_back(*cursor);
    }

    void unlink_cursor(cursor* cursor) {
        m_cursors.erase(m_cursors.iterator_to(*cursor));
    }

public:
    // Persistent tree state accessors

    u32 height() const { return m_anchor.get<&anchor::height>(); }
    u64 size() const { return m_anchor.get<&anchor::size>(); }
    block_index root() const { return m_anchor.get<&anchor::root>(); }
    block_index leftmost() const { return m_anchor.get<&anchor::leftmost>(); }
    block_index rightmost() const { return m_anchor.get<&anchor::rightmost>(); }
    u64 leaf_nodes() const { return m_anchor.get<&anchor::leaf_nodes>(); }
    u64 internal_nodes() const { return m_anchor.get<&anchor::internal_nodes>(); }

    void set_height(u32 height) {
        m_anchor.set<&anchor::height>(height);
    }

    void set_size(u64 size) {
        m_anchor.set<&anchor::size>(size);
    }

    void set_root(block_index root) {
        m_anchor.set<&anchor::root>(root);
    }

    void set_leftmost(block_index leftmost) {
        m_anchor.set<&anchor::leftmost>(leftmost);
    }

    void set_rightmost(block_index rightmost) {
        m_anchor.set<&anchor::rightmost>(rightmost);
    }

    void set_internal_nodes(u32 internal_nodes) {
        m_anchor.set<&anchor::internal_nodes>(internal_nodes);
    }

    void set_leaf_nodes(u64 leaf_nodes) {
        m_anchor.set<&anchor::leaf_nodes>(leaf_nodes);
    }

private:
    anchor_handle<anchor> m_anchor;
    raw_btree_options m_options;
    u32 m_internal_max_children;
    u32 m_internal_min_children;
    u32 m_leaf_capacity;

    // List of all active cursors.
    mutable cursor_list_type m_cursors;
};

} // namespace extpp::detail::btree_impl

#endif // EXTPP_BTREE_TREE_HPP
