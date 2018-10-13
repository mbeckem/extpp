#ifndef EXTPP_RAW_STACK_HPP
#define EXTPP_RAW_STACK_HPP

#include <extpp/allocator.hpp>
#include <extpp/anchor_handle.hpp>
#include <extpp/binary_format.hpp>
#include <extpp/block_index.hpp>
#include <extpp/exception.hpp>
#include <extpp/handle.hpp>

#include <memory>

namespace extpp {

class raw_stack;

namespace detail {

class raw_stack_impl;

struct raw_stack_anchor {
    /// Number of values in the stack.
    u64 size = 0;

    /// Number of nodes in the stack.
    u64 nodes = 0;

    /// Topmost node on the stack.
    block_index top;
};

} // namespace detail

using raw_stack_anchor = detail::raw_stack_anchor;

class raw_stack {
public:
    using anchor = raw_stack_anchor;

public:
    explicit raw_stack(anchor_handle<anchor> _anchor, u32 value_size, allocator& alloc);
    ~raw_stack();

    raw_stack(const raw_stack&) = delete;
    raw_stack& operator=(const raw_stack&) = delete;

    raw_stack(raw_stack&&) noexcept;
    raw_stack& operator=(raw_stack&&) noexcept;

public:
    engine& get_engine() const;
    allocator& get_allocator() const;

    /**
     * Returns the size of a serialized value on disk.
     */
    u32 value_size() const;

    /**
     * Returns the number of serialized values that fit into a single stack node.
     */
    u32 node_capacity() const;

    /**
     * Returns true if the stack is empty, i.e. contains zero values.
     */
    bool empty() const;

    /**
     * Returns the nuber of values on the stack.
     */
    u64 size() const;

    /**
     * Returns the number of stack nodes currenty allocated by the stack.
     */
    u64 nodes() const;

    /**
     * The average fill factor of the stack's nodes.
     */
    double fill_factor() const;

    /**
     * Returns the total size of disk datastructure on disk, in bytes.
     */
    u64 byte_size() const;

    /**
     * Returns the relative overhead of this datastructure compared to a linear file, i.e.
     * the allocated storage (see byte_size()) dividied by the used storage (i.e. size() * value_size()).
     */
    double overhead() const;

    /**
     * Retrieves the top value and copies it into the provided value buffer.
     * The buffer must be at least `value_size()` bytes long.
     */
    void top(byte* value) const;

    /**
     * Pushes the value onto the stack, by copying `value_size()` bytes
     * from the provided buffer to disk.
     */
    void push(const byte* value);

    /**
     * Removes the top element from the stack.
     *
     * @throws bad_operation If the stack is empty.
     */
    void pop();

    /**
     * Removes all elements from the stack.
     * @post `size() == 0`.
     */
    void clear();

    /**
     * Resets the stack to its empty state and releases all allocated storage.
     * @post `size() == 0 && byte_size() == 0`.
     */
    void reset();

    /**
     * Validates this instance's basic invariants.
     */
    void validate() const;

    /**
     * Writes the state of this stack into the provided output stream (for debugging purposes).
     */
    void dump(std::ostream& os) const;

private:
    detail::raw_stack_impl& impl() const;

private:
    std::unique_ptr<detail::raw_stack_impl> m_impl;
};

} // namespace extpp

#endif // EXTPP_RAW_STACK_HPP