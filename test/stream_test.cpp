#include <catch.hpp>

#include <extpp/exception.hpp>
#include <extpp/node_allocator.hpp>
#include <extpp/raw_stream.hpp>
#include <extpp/stream.hpp>
#include <extpp/detail/hex.hpp>

#include "./test_file.hpp"

using namespace extpp;

namespace {

static constexpr u32 block_size = 512;

using stream_t = stream<i32>;

struct header {
    node_allocator::anchor alloc;
    stream_t::anchor stream;

    static constexpr auto get_binary_format() {
        return make_binary_format(&header::alloc, &header::stream);
    }
};

using file_t = test_file<header>;

}

TEST_CASE("stream basics", "[stream][!mayfail]") {
    file_t file(block_size);
    file.open();

    auto blk = file.get_anchor().block();

    node_allocator alloc(file.get_anchor().member<&header::alloc>(), file.get_engine());
    stream_t stream(file.get_anchor().member<&header::stream>(), alloc);

    SECTION("empty stream") {
        REQUIRE(stream.size() == 0);
        REQUIRE(stream.capacity() == 0);
        REQUIRE(stream.empty());
        REQUIRE_THROWS_AS(stream.get(0), bad_element);
        REQUIRE_THROWS_AS(stream.set(0, 1), bad_element);
    }

    SECTION("stream grows when inserting") {
        for (int i = 0; i < 1000; ++i)
            stream.push_back(i);

        REQUIRE(stream.size() == 1000);
        REQUIRE(stream.capacity() >= 1000);

        for (i32 i = 0; i < 1000; ++i) {
            if (i != stream[i])
                FAIL("Unexpected value " << stream[i] << ", expected " << i);
        }

        for (int i = 0; i < 500; ++i)
            stream.pop_back();

        REQUIRE(stream[stream.size() - 1] == 499);
    }

    SECTION("stream reserve") {
        stream.reserve(5555);
        REQUIRE(stream.size() == 0);
        REQUIRE(stream.capacity() >= 5555);

        const u64 cap = stream.capacity();

        stream.reserve(5555);
        REQUIRE(cap == stream.capacity());

        stream.reserve(0);
        REQUIRE(cap == stream.capacity());
    }

    SECTION("mutate stream") {
        stream.reserve(5000);
        for (i32 i = 0; i < 5000; ++i)
            stream.push_back(i);

        for (i32 i = 0; i < 5000; ++i)
            stream.set(i, stream.get(i) * 2);

        for (i32 i = 0; i < 5000; ++i) {
            i32 expected = i * 2;
            if (stream[i] != expected)
                FAIL("Unexpected value " << stream[i] << ", expected " << expected);
        };
    }

    SECTION("resizing") {
        REQUIRE(stream.empty());

        stream.resize(12345, 1122334455);

        REQUIRE(stream.size() == 12345);
        for (i32 i = 0, e = stream.size(); i != e; ++i) {
            if (stream[i] != 1122334455)
                FAIL("Unexpected value: " << stream[i]);
        }

        stream.resize(123);
        REQUIRE(stream.size() == 123);
        REQUIRE(stream.capacity() >= 123);

        stream.resize(123456);
        REQUIRE(stream.size() == 123456);
        REQUIRE(stream.capacity() >= 123456);
        for (u64 i = 0; i < 123; ++i) {
            if (stream[i] != 1122334455)
                FAIL("Unexpected value: " << stream[i]);
        }

        for (u64 i = 123; i < 123456; ++i) {
            if (stream[i] != 0)
                FAIL("Unexpected value: " << stream[i]);
        }
    }
}

TEST_CASE("stream state is persistent", "[stream][!mayfail]") {
    file_t file(block_size);

    file.open();
    {
        node_allocator alloc(file.get_anchor().member<&header::alloc>(), file.get_engine());
        stream_t stream(file.get_anchor().member<&header::stream>(), alloc);

        stream.reserve(100000);
        for (int i = 0; i < 100000; ++i)
            stream.push_back(i);
    }
    file.close();

    file.open();
    {
        node_allocator alloc(file.get_anchor().member<&header::alloc>(), file.get_engine());
        stream_t stream(file.get_anchor().member<&header::stream>(), alloc);

        REQUIRE(stream.size() == 100000);
        for (int i = 0; i < 100000; ++i) {
            if (stream[i] != i)
                FAIL("Unexpected value " << stream[i] << ", expected " << i);
        }
    }
    file.close();
}

TEST_CASE("customizable stream growth", "[stream][!mayfail]") {
    file_t file(block_size);
    file.open();
    {
        node_allocator alloc(file.get_anchor().member<&header::alloc>(), file.get_engine());
        stream_t stream(file.get_anchor().member<&header::stream>(), alloc);
        REQUIRE(std::holds_alternative<exponential_growth>(stream.growth()));

        SECTION("exponential") {
            stream.resize(1);
            REQUIRE(stream.blocks() == 1);

            stream.resize(stream.block_capacity() * 10);
            REQUIRE(stream.blocks() == 16);

            stream.resize(stream.block_capacity() * 127);
            REQUIRE(stream.blocks() == 128);
            REQUIRE(stream.capacity() == stream.blocks() * stream.block_capacity());
        }

        SECTION("linear") {
            stream.growth(linear_growth(5));

            stream.resize(0);
            REQUIRE(stream.blocks() == 0);

            stream.resize(1);
            REQUIRE(stream.blocks() == 5);
            REQUIRE(stream.capacity() == stream.block_capacity() * 5);

            stream.resize(24 * stream.block_capacity());
            REQUIRE(stream.blocks() == 25);

            stream.growth(linear_growth(1));
            stream.resize(101 * stream.block_capacity());
            REQUIRE(stream.blocks() == 101);

            stream.growth(linear_growth(12345));
            stream.resize(101 * stream.block_capacity() + 1);
            REQUIRE(stream.blocks() == (12345 + 101));
        }
    }
    file.close();
}
