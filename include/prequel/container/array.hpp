#ifndef PREQUEL_CONTAINER_ARRAY_HPP
#define PREQUEL_CONTAINER_ARRAY_HPP

#include <prequel/anchor_handle.hpp>
#include <prequel/binary_format.hpp>
#include <prequel/block_index.hpp>
#include <prequel/container/allocator.hpp>
#include <prequel/container/extent.hpp>
#include <prequel/defs.hpp>
#include <prequel/handle.hpp>
#include <prequel/serialization.hpp>

#include <memory>
#include <variant>

namespace prequel {

// TODO: Consistent naming and exporting of anchors for all container types.

class raw_array;

namespace detail {

class raw_array_impl;

struct raw_array_anchor {
    /// Raw block storage.
    extent::anchor storage;

    /// Number of elements
    u64 size = 0;

    static constexpr auto get_binary_format() {
        return binary_format(&raw_array_anchor::storage, &raw_array_anchor::size);
    }
};

} // namespace detail

using raw_array_anchor = detail::raw_array_anchor;

/// The stream allocates new blocks in chunks of the given size.
struct linear_growth {
    linear_growth(u64 chunk_size = 1)
        : m_chunk_size(chunk_size) {
        PREQUEL_ASSERT(chunk_size >= 1, "Invalid chunk size.");
    }

    u64 chunk_size() const { return m_chunk_size; }

private:
    u64 m_chunk_size;
};

/// The stream is resized exponentially (to 2^n blocks).
struct exponential_growth {};

/// Specify the growth strategy of a stream.
using growth_strategy = std::variant<linear_growth, exponential_growth>;

/**
 * A dynamic array for fixed-size values.
 * The size of values can be determined at runtime (e.g. through user input)
 * but must remain constant during the use of an array.
 *
 * A array stores a sequence of fixed-size values in contiguous storage on disk.
 * The stream can reserve capacity ahead of time to prepare for future insertions,
 * very similar to `std::vector<T>`.
 */
class raw_array {
public:
    using anchor = raw_array_anchor;

public:
    /**
     * Accesses a raw array rooted at the given anchor.
     * `value_size` and `alloc` must be equivalent every time the raw array is loaded.
     */
    explicit raw_array(anchor_handle<anchor> _anchor, u32 value_size, allocator& alloc);
    ~raw_array();

    raw_array(const raw_array&) = delete;
    raw_array& operator=(const raw_array&) = delete;

    raw_array(raw_array&&) noexcept;
    raw_array& operator=(raw_array&&) noexcept;

public:
    engine& get_engine() const;
    allocator& get_allocator() const;

    /**
     * Returns the size of a serialized value on disk.
     */
    u32 value_size() const;

    /**
     * Returns the number of serialized values that fit into a single block on disk.
     */
    u32 block_capacity() const;

    /**
     * Returns true iff the stream is empty, i.e. contains zero values.
     */
    bool empty() const;

    /**
     * Returns the number of values in this stream.
     */
    u64 size() const;

    /**
     * Returns the capacity of this stream, i.e. the maximum number of values
     * that can currently be stored without reallocating the storage on disk.
     * The stream will allocate storage according to its growth strategy when
     * an element is inserted into a full stream.
     *
     * @note `capacity() * value_size() == byte_size()` will always be true.
     */
    u64 capacity() const;

    /**
     * Returns the number of disk blocks currently allocated by the stream.
     */
    u64 blocks() const;

    /**
     * Returns the relative fill factor, i.e. the size divided by the capacity.
     */
    double fill_factor() const;

    /**
     * Returns the total size of this datastructure on disk, in bytes.
     */
    u64 byte_size() const;

    /**
     * Returns the relative overhead of this datastructure compared to a linear file, i.e.
     * the allocated storage (see capacity()) dividied by the used storage (see size()).
     */
    double overhead() const;

    /**
     * Retrieves the element at the given index and writes it into the `value` buffer,
     * which must be at least `value_size()` bytes large.
     */
    void get(u64 index, byte* value) const;

    /**
     * Sets the value at the given index to the content of `value`, which must
     * have at least `value_size()` readable bytes.
     */
    void set(u64 index, const byte* value);

    /**
     * Frees all storage allocated by the stream.
     *
     * @post `size() == 0 && byte_size() == 0`.
     */
    void reset();

    /**
     * Removes all objects from this stream, but does not
     * free the underlying storage.
     *
     * @post `size() == 0`.
     */
    void clear();

    /**
     * Resizes the stream to the size `n`. New elements are constructed by
     * initializing them with `value`, which must be at least `value_size()` bytes long.
     * @post `size() == n`.
     */
    void resize(u64 n, const byte* value);

    /**
     * Resize the underlying storage so that the stream can store at least `n` values
     * without further resize operations. Uses the current growth strategy to computed the
     * storage that needs to be allocated.
     *
     * @post `capacity() >= n`.
     */
    void reserve(u64 n);

    /**
     * Resize the underlying storage so that the stream can store at least `n` *additional* values
     * without further resize operations. Uses the current growth strategy to computed the
     * storage that needs to be allocated.
     *
     * @post `capacity() >= size() + n`
     */
    void reserve_additional(u64 n);

    /**
     * Reduces the storage space used by the array by releasing unused capacity.
     *
     * It uses the current growth strategy to determine the needed number of blocks
     * and shrinks to that value.
     */
    void shrink();

    /**
     * Reduces the storage space used by the array by releasing all unused capacity.
     *
     * Releases *all* unused blocks to reduce the storage space to the absolute minimum.
     * Ignores the growth strategy.
     */
    void shrink_to_fit();

    /**
     * Inserts a new value at the end of the stream.
     * Allocates new storage in accordance with the current growth strategy
     * if there is no free capacity remaining.
     */
    void push_back(const byte* value);

    /**
     * Removes the last value from this stream.
     *
     * @throws bad_operation If the stream is empty.
     */
    void pop_back();

    /**
     * Changes the current growth strategy. Streams support linear and exponential growth.
     */
    void growth(const growth_strategy& g);

    /**
     * Returns the current growth strategy.
     */
    growth_strategy growth() const;

private:
    detail::raw_array_impl& impl() const;

private:
    std::unique_ptr<detail::raw_array_impl> m_impl;
};

/**
 * A dynamic array of for instances of type `T`.
 *
 * A stream stores a sequence of fixed-size values in contiguous storage on disk.
 * The stream can reserve capacity ahead of time to prepare for future insertions,
 * very similar to `std::vector<T>`.
 */
template<typename T>
class array {
public:
    using value_type = T;

public:
    class anchor {
        raw_array::anchor array;

        static constexpr auto get_binary_format() { return binary_format(&anchor::array); }

        friend class array;
        friend class binary_format_access;
    };

public:
    /**
     * Accesses an array rooted at the given anchor.
     * alloc` must be equivalent every time the raw array is loaded.
     */
    explicit array(anchor_handle<anchor> _anchor, allocator& alloc)
        : inner(std::move(_anchor).template member<&anchor::array>(), value_size(), alloc) {}

public:
    engine& get_engine() const { return inner.get_engine(); }
    allocator& get_allocator() const { return inner.get_allocator(); }

    /**
     * Returns the size of a serialized value on disk.
     */
    static constexpr u32 value_size() { return serialized_size<T>(); }

    /**
     * Returns the number of serialized values that fit into a single block on disk.
     */
    u32 block_capacity() const { return inner.block_capacity(); }

    /**
     * Returns true iff the stream is empty, i.e. contains zero values.
     */
    bool empty() const { return inner.empty(); }

    /**
     * Returns the number of values in this stream.
     */
    u64 size() const { return inner.size(); }

    /**
     * Returns the capacity of this stream, i.e. the maximum number of values
     * that can currently be stored without reallocating the storage on disk.
     * The stream will allocate storage according to its growth strategy when
     * an element is inserted into a full stream.
     *
     * @note `capacity() * value_size() == byte_size()` will always be true.
     */
    u64 capacity() const { return inner.capacity(); }

    /**
     * Returns the number of disk blocks currently allocated by the stream.
     */
    u64 blocks() const { return inner.blocks(); }

    /**
     * Returns the relative fill factor, i.e. the size divided by the capacity.
     */
    double fill_factor() const { return inner.fill_factor(); }

    /**
     * Returns the total size of this datastructure on disk, in bytes.
     */
    u64 byte_size() const { return inner.byte_size(); }

    /**
     * Returns the relative overhead of this datastructure compared to a linear file, i.e.
     * the allocated storage (see capacity()) dividied by the used storage (see size()).
     */
    double overhead() const { return inner.overhead(); }

    /**
     * Retrieves the value at the given index.
     *
     * @throws bad_argument     If the index is out of bounds.
     */
    value_type get(u64 index) const {
        serialized_buffer<T> buffer;
        inner.get(index, buffer.data());
        return deserialize<value_type>(buffer.data(), buffer.size());
    }

    /**
     * Equivalent to `get(index)`.
     */
    value_type operator[](u64 index) const { return get(index); }

    /**
     * Sets the value at the given index.
     *
     * @throws bad_argument     If the index is out of bounds.
     */
    void set(u64 index, const value_type& value) {
        auto buffer = serialize_to_buffer(value);
        inner.set(index, buffer.data());
    }

    /**
     * Frees all storage allocated by the stream.
     *
     * @post `size() == 0 && byte_size() == 0`.
     */
    void reset() { inner.reset(); }

    /**
     * Removes all objects from this stream, but does not
     * necessarily free the underlying storage.
     *
     * @post `size() == 0`.
     */
    void clear() { inner.clear(); }

    /**
     * Resizes the stream to the given size `n`.
     * If `n` is greater than the current size, `value` is used as a default
     * value for new elements.
     *
     * @post `size == n`.
     */
    void resize(u64 n, const value_type& value = value_type()) {
        auto buffer = serialize_to_buffer(value);
        inner.resize(n, buffer.data());
    }

    /**
     * Reserves sufficient storage for `n` values, while respecting the current growth strategy.
     * Note that the size remains unchanged.
     *
     * @post `capacity >= n`.
     */
    void reserve(u64 n) { inner.reserve(n); }

    /**
     * Resize the underlying storage so that the stream can store at least `n` *additional* values
     * without further resize operations. Uses the current growth strategy to computed the
     * storage that needs to be allocated.
     *
     * @post `capacity() >= size() + n`
     */
    void reserve_additional(u64 n) { inner.reserve_additional(n); }

    /**
     * Reduces the storage space used by the array by releasing unused capacity.
     *
     * It uses the current growth strategy to determine the needed number of blocks
     * and shrinks to that value.
     */
    void shrink() { inner.shrink(); }

    /**
     * Reduces the storage space used by the array by releasing all unused capacity.
     *
     * Releases *all* unused blocks to reduce the storage space to the absolute minimum.
     * Ignores the growth strategy.
     */
    void shrink_to_fit() { inner.shrink_to_fit(); }

    /**
     * Inserts a new value at the end of the stream.
     * Allocates new storage in accordance with the current growth strategy
     * if there is no free capacity remaining.
     */
    void push_back(const value_type& value) {
        auto buffer = serialize_to_buffer(value);
        inner.push_back(buffer.data());
    }

    /**
     * Removes the last value from this stream.
     *
     * @throws bad_operation If the stream is empty.
     */
    void pop_back() { inner.pop_back(); }

    /**
     * Changes the current growth strategy. Streams support linear and exponential growth.
     */
    void growth(const growth_strategy& g) { inner.growth(g); }

    /**
     * Returns the current growth strategy.
     */
    growth_strategy growth() const { return inner.growth(); }

    /**
     * Returns the raw, byte oriented inner stream.
     */
    const raw_array& raw() const { return inner; }

private:
    raw_array inner;
};

} // namespace prequel

#endif // PREQUEL_CONTAINER_ARRAY_HPP
