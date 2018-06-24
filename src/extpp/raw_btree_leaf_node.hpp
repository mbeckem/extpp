#ifndef EXTPP_RAW_BTREE_LEAF_NODE_HPP
#define EXTPP_RAW_BTREE_LEAF_NODE_HPP

#include <extpp/defs.hpp>
#include <extpp/handle.hpp>
#include <extpp/serialization.hpp>

#include <cstring>

namespace extpp {

// Node layout:
// - Header
// - Array of values (N)
//
// Values are ordered by their key.
class raw_btree_leaf_node {
private:
    // Note: no next/prev pointers, no type tag, no depth info.
    struct header {
        u32 size = 0;   // Number of values in this node <= capacity.

        static constexpr auto get_binary_format() {
            return make_binary_format(&header::size);
        }
    };

public:
    raw_btree_leaf_node() = default;

    raw_btree_leaf_node(block_handle block, u32 value_size, u32 capacity)
        : m_handle(std::move(block), 0)
        , m_value_size(value_size)
        , m_capacity(capacity)
    {}

    bool valid() const { return m_handle.valid(); }
    const block_handle& block() const { return m_handle.block(); }
    block_index index() const { return block().index(); }

    void init() { m_handle.set(header()); }

    u32 get_size() const { return m_handle.get<&header::size>(); }
    void set_size(u32 new_size) const {
        EXTPP_ASSERT(new_size <= m_capacity, "Invalid size");
        m_handle.set<&header::size>(new_size);
    }

    u32 min_size() const { return m_capacity / 2; }
    u32 max_size() const { return m_capacity; }
    u32 value_size() const { return m_value_size; }

    void set(u32 index, const byte* value) const {
        EXTPP_ASSERT(index < m_capacity, "Index out of bounds.");
        m_handle.block().write(offset_of_value(index), value, m_value_size);
    }

    const byte* get(u32 index) const {
        EXTPP_ASSERT(index < m_capacity, "Index out of bounds.");
        return m_handle.block().data() + offset_of_value(index);
    }

    // Insert the new value at the given index and shift values to the right.
    void insert_nonfull(u32 index, const byte* value) const {
        EXTPP_ASSERT(index < m_capacity, "Index out of bounds.");
        EXTPP_ASSERT(index <= get_size(), "Unexpected index (not in range).");

        u32 size = get_size();
        byte* data = m_handle.block().writable_data();
        std::memmove(data + offset_of_value(index + 1),
                     data + offset_of_value(index),
                     (size - index) * m_value_size);
        std::memmove(data + offset_of_value(index), value, m_value_size);
        set_size(size + 1);
    }

    // Perform a node split and insert the new value at the appropriate position.
    // `mid` is the size of *this, after the split (other values end up in new_leaf).
    // If index < mid, then the new value is in the left node, at the given index.
    // Otherwise the new value is in new_leaf, at `index - mid`.
    void insert_full(u32 index, const byte* value, u32 mid, const raw_btree_leaf_node& new_leaf) const {
        EXTPP_ASSERT(mid <= m_capacity, "Mid out of bounds.");
        EXTPP_ASSERT(m_value_size == new_leaf.m_value_size, "Value size missmatch.");
        EXTPP_ASSERT(m_capacity == new_leaf.m_capacity, "Capacity missmatch.");
        EXTPP_ASSERT(new_leaf.get_size() == 0, "New leaf must be empty.");
        EXTPP_ASSERT(get_size() == m_capacity, "Old leaf must be full.");

        byte* left = m_handle.block().writable_data() + offset_of_value(0);
        byte* right = new_leaf.block().writable_data() + offset_of_value(0);
        sequence_insert(m_value_size, left, right, m_capacity, mid, index, value);
        set_size(mid);
        new_leaf.set_size(m_capacity + 1 - mid);
    }

    // Removes the value at the given index and shifts all values after it to the left.
    void remove(u32 index) const {
        EXTPP_ASSERT(index < m_capacity, "Index out of bounds.");
        EXTPP_ASSERT(index < get_size(), "Unexpected index (not in range).");

        u32 size = get_size();
        byte* data = m_handle.block().writable_data();
        std::memmove(data + offset_of_value(index),
                     data + offset_of_value(index + 1),
                     (size - index - 1) * m_value_size);
        set_size(size - 1);
    }

    // Append all values from the right neighbor.
    void append_from_right(const raw_btree_leaf_node& neighbor) const {
        EXTPP_ASSERT(get_size() + neighbor.get_size() <= m_capacity,  "Too many values.");
        EXTPP_ASSERT(value_size() == neighbor.value_size(), "Value size missmatch.");

        u32 size = get_size();
        u32 neighbor_size = neighbor.get_size();

        byte* data = m_handle.block().writable_data();
        const byte* neighbor_data = neighbor.m_handle.block().data();

        std::memmove(data + offset_of_value(size),
                     neighbor_data + offset_of_value(0),
                     neighbor_size * value_size());
        set_size(size + neighbor_size);
    }

    // Prepend all values from the left neighbor.
    void prepend_from_left(const raw_btree_leaf_node& neighbor) const {
        EXTPP_ASSERT(get_size() + neighbor.get_size() <= m_capacity,  "Too many values.");
        EXTPP_ASSERT(value_size() == neighbor.value_size(), "Value size missmatch.");

        u32 size = get_size();
        u32 neighbor_size = neighbor.get_size();

        byte* data = m_handle.block().writable_data();
        const byte* neighbor_data = neighbor.m_handle.block().data();

        std::memmove(data + offset_of_value(neighbor_size),
                     data + offset_of_value(0),
                     size * value_size());
        std::memmove(data + offset_of_value(0),
                     neighbor_data + offset_of_value(0),
                     neighbor_size * value_size());
        set_size(size + neighbor_size);
    }

public:
    static u32 capacity(u32 block_size, u32 value_size) {
        u32 hdr = serialized_size<header>();
        if (block_size < hdr)
            return 0;
        return (block_size - hdr) / value_size;
    }

private:
    u32 offset_of_value(u32 index) const {
        return serialized_size<header>() + m_value_size * index;
    }

    /// Insert a value into a sequence and perform a split at the same time.
    /// Values exist in `left`, and `right` is treated as empty.
    /// After the insertion, exactly `mid` entries will remain in `left` and the remaining
    /// entries will have been copied over into `right`.
    ///
    /// \param value_size       The size (in bytes) of a single value.
    /// \param left             The left sequence.
    /// \param right            The right sequence.
    /// \param count            The current size of the left sequence, without the new element.
    /// \param mid              The target size of the left sequence, after the split.
    /// \param insert_index     The target insertion index of `value` in the left sequence.
    /// \param value            The value to insert.
    ///
    /// \pre `0 <= insert_index <= count`.
    /// \pre `mid > 0 && mid <= count`.
    ///
    /// \post If `insert_index < mid`, then the new value will be stored in the left sequence at index `insert_index`.
    /// Otherwise, the value will be located in the right sequence, at index `insert_index - mid`.
    ///
    /// \note This function does not apply the new size to either sequence, it only moves elements.
    static void sequence_insert(u32 value_size, byte* left, byte* right, u32 count, u32 mid, u32 insert_index, const byte* value) {
        EXTPP_ASSERT(mid > 0 && mid <= count, "index can't be used as mid");
        EXTPP_ASSERT(insert_index <= count, "index out of bounds");

        // Move values by index from source to dest.
        auto move = [value_size](const byte* src, u32 src_index, byte* dst, u32 dst_index, u32 n) {
            std::memmove(dst + dst_index * value_size, src + src_index * value_size, n * value_size);
        };

        if (insert_index < mid) {
            // element ends up in the left node.
            move(left, mid - 1, right, 0, count - mid + 1);
            move(left, insert_index, left, insert_index + 1, mid - 1 - insert_index);
            move(value, 0, left, insert_index, 1);
        } else {
            u32 right_insert_index = insert_index - mid;

            move(left, mid, right, 0, right_insert_index);
            move(value, 0, right, right_insert_index, 1);
            move(left, mid + right_insert_index, right, right_insert_index + 1, count - mid - right_insert_index);
        }
    }

private:
    handle<header> m_handle;
    u32 m_value_size = 0;       // Size of a single value
    u32 m_capacity = 0;         // Max number of values per node
};


} // namespace extpp

#endif // EXTPP_RAW_BTREE_LEAF_NODE_HPP