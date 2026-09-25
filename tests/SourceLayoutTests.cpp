#include <visona/SourceLayout.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

using visona::SourceLayout;

TEST_CASE("SourceLayout is empty by default", "[source-layout]")
{
    const SourceLayout layout;
    CHECK(layout.sourceCount() == 0);
    CHECK(layout.totalChannelCount() == 0);
    CHECK(layout.sources().empty());
}

TEST_CASE("SourceLayout describes a single stereo source", "[source-layout]")
{
    const SourceLayout layout{2};
    REQUIRE(layout.sourceCount() == 1);
    CHECK(layout.totalChannelCount() == 2);
    CHECK(layout.source(0).firstChannel == 0);
    CHECK(layout.source(0).channelCount == 2);
}

TEST_CASE("SourceLayout numbers channels consecutively across sources", "[source-layout]")
{
    const std::array<std::size_t, 4> channelCounts{1, 2, 6, 1};
    const SourceLayout layout(channelCounts);
    REQUIRE(layout.sourceCount() == 4);
    CHECK(layout.totalChannelCount() == 10);

    CHECK(layout.source(0) == SourceLayout::Source{0, 1});
    CHECK(layout.source(1) == SourceLayout::Source{1, 2});
    CHECK(layout.source(2) == SourceLayout::Source{3, 6});
    CHECK(layout.source(3) == SourceLayout::Source{9, 1});

    // Iterating sources and their channels visits every channel exactly once, in order.
    std::vector<std::size_t> visited;
    for (const auto& source : layout.sources())
        for (std::size_t channel = 0; channel < source.channelCount; ++channel)
            visited.push_back(source.firstChannel + channel);
    CHECK(visited == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_CASE("SourceLayout rejects a source with no channels", "[source-layout]")
{
    CHECK_THROWS_AS(SourceLayout({2, 0}), std::invalid_argument);
    CHECK_THROWS_AS(SourceLayout{0}, std::invalid_argument);
}

TEST_CASE("SourceLayouts compare equal when their sources match", "[source-layout]")
{
    CHECK(SourceLayout{2} == SourceLayout{2});
    CHECK(SourceLayout({2, 1}) == SourceLayout({2, 1}));
    CHECK_FALSE(SourceLayout{2} == SourceLayout({1, 1}));
    CHECK_FALSE(SourceLayout{2} == SourceLayout{});
}

TEST_CASE("SourceLayout accessors are noexcept", "[source-layout][realtime]")
{
    const SourceLayout layout{2};
    STATIC_REQUIRE(noexcept(layout.sourceCount()));
    STATIC_REQUIRE(noexcept(layout.totalChannelCount()));
    STATIC_REQUIRE(noexcept(layout.source(0)));
    STATIC_REQUIRE(noexcept(layout.sources()));
}
