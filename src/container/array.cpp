#include <prequel/container/array.hpp>

#include <prequel/container/extent.hpp>
#include <prequel/exception.hpp>

namespace prequel {

namespace {

u32 calc_block_capacity(u32 block_size, u32 value_size) {
    return block_size / value_size;
}

u32 calc_offset_in_block(u32 value_size, u32 index) {
    return value_size * index;
}

} // namespace

namespace detail {

class raw_array_impl {
public:
    using anchor = detail::raw_array_anchor;

    raw_array_impl(anchor_handle<anchor> _anchor, u32 value_size, allocator& alloc)
        : m_anchor(std::move(_anchor))
        , m_extent(m_anchor.member<&anchor::storage>(), alloc)
        , m_value_size(value_size)
        , m_block_capacity(calc_block_capacity(m_extent.block_size(), m_value_size)) {
        if (m_block_capacity == 0)
            PREQUEL_THROW(bad_argument("Block size too small to fit a single value."));
    }

    ~raw_array_impl() = default;

    raw_array_impl(const raw_array_impl&) = delete;
    raw_array_impl& operator=(const raw_array_impl&) = delete;

public:
    allocator& get_allocator() const { return m_extent.get_allocator(); }
    engine& get_engine() const { return m_extent.get_engine(); }
    u32 block_size() const { return get_allocator().block_size(); }

    u32 value_size() const { return m_value_size; }
    u32 block_capacity() const { return m_block_capacity; }

    bool empty() const { return size() == 0; }
    u64 size() const { return m_anchor.get<&anchor::size>(); }
    u64 capacity() const { return blocks() * block_capacity(); }

    growth_strategy growth() const { return m_growth; }
    void growth(const growth_strategy& g) { m_growth = g; }

    u64 blocks() const { return m_extent.size(); }
    double fill_factor() const { return capacity() == 0 ? 0 : double(size()) / double(capacity()); }
    u64 byte_size() const { return blocks() * block_size(); }
    double overhead() const {
        return capacity() == 0 ? 1.0 : double(byte_size()) / double(size() * value_size());
    }

    void get(u64 index, byte* value) {
        check_index(index);

        u64 blk_index = block_index(index);
        u32 blk_offset = block_offset(index);

        auto handle = read(blk_index);
        handle.read(calc_offset_in_block(m_value_size, blk_offset), value, m_value_size);
    }

    void set(u64 index, const byte* value) {
        check_index(index);

        u64 blk_index = block_index(index);
        u32 blk_offset = block_offset(index);

        auto handle = read(blk_index);
        handle.write(calc_offset_in_block(m_value_size, blk_offset), value, m_value_size);
    }

    void reserve(u64 n) {
        u64 needed_blocks = ceil_div(n, u64(block_capacity()));
        if (needed_blocks > blocks())
            resize_extent(needed_blocks);

        PREQUEL_ASSERT(capacity() >= n, "Capacity invariant");
    }

    void shrink(bool exact) {
        u64 needed_blocks = ceil_div(size(), u64(block_capacity()));
        resize_extent(needed_blocks, exact);
    }

    void push_back(const byte* value) {
        const u64 sz = size();
        const u32 value_size = m_value_size;

        u64 blk_index = block_index(sz);
        u32 blk_offset = block_offset(sz);
        if (blk_index == blocks())
            resize_extent(blocks() + 1);

        auto handle = blk_offset == 0 ? create(blk_index) : read(blk_index);
        handle.write(calc_offset_in_block(value_size, blk_offset), value, m_value_size);

        m_anchor.set<&anchor::size>(sz + 1);

        PREQUEL_ASSERT(size() <= capacity(), "Size invariant");
    }

    void pop_back() {
        const u64 sz = size();
        if (sz == 0)
            PREQUEL_THROW(bad_operation("Array is empty."));

        m_anchor.set<&anchor::size>(sz - 1);
    }

    void clear() { resize(0, nullptr); }

    void reset() {
        m_extent.reset();
        m_anchor.set<&anchor::size>(0);
    }

    void resize(u64 n, const byte* value) {
        const u64 sz = size();
        if (n == sz)
            return;

        if (n < sz) {
            m_anchor.set<&anchor::size>(n);
            return;
        }

        reserve(n);
        {
            const u32 blk_capacity = m_block_capacity;
            const u32 value_size = m_value_size;

            u64 remaining = n - sz;
            u64 blk_index = block_index(sz);
            u32 blk_offset = block_offset(sz);

            while (remaining > 0) {
                auto handle = blk_offset == 0 ? create(blk_index) : read(blk_index);

                // Pointer to the first writable value.
                byte* data = handle.writable_data() + calc_offset_in_block(value_size, blk_offset);

                // Number of writable values.
                const u32 writable = std::min(remaining, u64(blk_capacity - blk_offset));

                if (value) {
                    for (u32 i = 0; i < writable; ++i) {
                        // value might overlap
                        std::memmove(data, value, value_size);
                        data += value_size;
                    }
                } else {
                    std::memset(data, 0, writable * value_size);
                }

                remaining -= writable;
                blk_index += 1;
                blk_offset = 0;
            }
        }

        m_anchor.set<&anchor::size>(n);
    }

private:
    // Block index / offset in index for the given value index.
    u64 block_index(u64 index) const { return index / m_block_capacity; }
    u32 block_offset(u64 index) const { return index % m_block_capacity; }

    block_handle create(u64 blk_index) const { return m_extent.overwrite_zero(blk_index); }

    block_handle read(u64 blk_index) const { return m_extent.read(blk_index); }

    // Adjust the minimum size (in blocks) according to the growth strategy.
    u64 new_size(u64 minimum) const {
        struct visitor_t {
            u64 minimum;

            u64 operator()(const linear_growth& g) {
                // Round up to multiple of chunk size.
                return ceil_div(minimum, g.chunk_size()) * g.chunk_size();
            }

            u64 operator()(const exponential_growth&) { return round_towards_pow2(minimum); }
        };

        visitor_t v{minimum};
        return std::visit(v, m_growth);
    }

    void resize_extent(u64 minimum, bool exact = false) {
        u64 size = exact ? minimum : new_size(minimum);
        m_extent.resize(size);
    }

    void check_index(u64 index) const {
        if (index >= size())
            PREQUEL_THROW(bad_argument("Index out of bounds."));
    }

private:
    anchor_handle<anchor> m_anchor;
    extent m_extent;
    u32 m_value_size = 0;
    u32 m_block_capacity = 0;
    growth_strategy m_growth = exponential_growth();
};

} // namespace detail

raw_array::raw_array(anchor_handle<anchor> _anchor, u32 value_size, allocator& alloc)
    : m_impl(std::make_unique<detail::raw_array_impl>(std::move(_anchor), value_size, alloc)) {}

raw_array::~raw_array() {}

raw_array::raw_array(raw_array&& other) noexcept
    : m_impl(std::move(other.m_impl)) {}

raw_array& raw_array::operator=(raw_array&& other) noexcept {
    if (this != &other)
        m_impl = std::move(other.m_impl);
    return *this;
}

allocator& raw_array::get_allocator() const {
    return impl().get_allocator();
}
engine& raw_array::get_engine() const {
    return impl().get_engine();
}

u32 raw_array::value_size() const {
    return impl().value_size();
}
u32 raw_array::block_capacity() const {
    return impl().block_capacity();
}

bool raw_array::empty() const {
    return impl().empty();
}
u64 raw_array::size() const {
    return impl().size();
}
u64 raw_array::capacity() const {
    return impl().capacity();
}
u64 raw_array::blocks() const {
    return impl().blocks();
}
double raw_array::fill_factor() const {
    return impl().fill_factor();
}
u64 raw_array::byte_size() const {
    return impl().byte_size();
}
double raw_array::overhead() const {
    return impl().overhead();
}

void raw_array::get(u64 index, byte* value) const {
    impl().get(index, value);
}
void raw_array::set(u64 index, const byte* value) {
    impl().set(index, value);
}
void raw_array::reset() {
    impl().reset();
}
void raw_array::clear() {
    impl().clear();
}
void raw_array::resize(u64 n, const byte* value) {
    impl().resize(n, value);
}
void raw_array::reserve(u64 n) {
    impl().reserve(n);
}
void raw_array::reserve_additional(u64 n) {
    impl().reserve(checked_add(n, size()));
}
void raw_array::shrink() {
    impl().shrink(false);
}
void raw_array::shrink_to_fit() {
    impl().shrink(true);
}
void raw_array::growth(const growth_strategy& g) {
    impl().growth(g);
}
growth_strategy raw_array::growth() const {
    return impl().growth();
}

void raw_array::push_back(const byte* value) {
    impl().push_back(value);
}
void raw_array::pop_back() {
    impl().pop_back();
}

detail::raw_array_impl& raw_array::impl() const {
    if (!m_impl)
        PREQUEL_THROW(bad_operation("Bad array instance."));
    return *m_impl;
}

} // namespace prequel
