// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/left_out.hpp"

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3;
using namespace rct3::save;

namespace
{
    // An invented save: a queue of two fields with a list of two-field elements, litter of one field, and a
    // class with no objects. Only the type tags are the format's own.
    std::vector<std::byte> small_save()
    {
        const std::vector<Class> classes {
            { .name = "TestQueueLine", .attributes = { { .name = "Length", .type = "uint32", .size = 4 }, { .name = "Spots", .type = "list", .size = 0, .children = { { .name = "Place", .type = "uint32", .size = 4 }, { .name = "Wait", .type = "float32", .size = 4 } } } } },
            { .name = "TestLitter", .attributes = { { .name = "Kind", .type = "uint32", .size = 4 } } },
            { .name = "TestUnused", .attributes = { { .name = "Thing", .type = "uint32", .size = 4 } } },
        };
        const auto u32_bytes = [](std::vector<std::byte>& out, u32 v) {
            const auto at = out.size();
            out.resize(at + 4);
            std::memcpy(out.data() + at, &v, 4);
        };
        std::vector<std::byte> queue;
        u32_bytes(queue, 3); // Length
        std::vector<std::byte> spots;
        u32_bytes(spots, 1); // One spot
        u32_bytes(spots, 5);
        u32_bytes(spots, 0);
        u32_bytes(queue, static_cast<u32>(spots.size()));
        queue.insert(queue.end(), spots.begin(), spots.end());
        std::vector<std::byte> litter;
        u32_bytes(litter, 2);
        const std::vector<ObjectRecord> objects { { .class_index = 0, .id = 1, .payload = queue }, { .class_index = 1, .id = 2, .payload = litter } };
        auto bytes = write_save(std::nullopt, classes, objects);
        REQUIRE(bytes);
        return *bytes;
    }
} // namespace

TEST_CASE("a load tells the fields it never looked up")
{
    const std::vector<std::byte> bytes = small_save();
    const FieldReads reads(bytes);
    const auto file = read_save(bytes);
    REQUIRE(file);
    const auto queue = decode_object(*file, file->objects.at(0));
    REQUIRE(queue);
    // Read the length and the spots' places, as a loader would; never the waits, nor the litter.
    CHECK(queue->find("Length"));
    REQUIRE(queue->find("Spots"));
    CHECK(queue->find("Spots")->items().at(0).field("Place"));

    const std::vector<LeftOutClass> left = left_out(*file, reads);
    REQUIRE(left.size() == 2); // The class with no objects leaves nothing
    CHECK(left[0].name == "TestQueueLine");
    CHECK(left[0].fields == std::vector<std::string> { "Spots.Wait" });
    CHECK_FALSE(left[0].untouched);
    CHECK(left[1].name == "TestLitter");
    CHECK(left[1].objects == 1);
    CHECK(left[1].fields == std::vector<std::string> { "Kind" });
    CHECK(left[1].untouched);
    CHECK(left_out_groups(left) == std::vector<std::string_view> { "Rct3pp_LeftOutQueues", "Rct3pp_LeftOutLitter" });
}

TEST_CASE("lookups of another file's fields, or after the log is gone, are not counted")
{
    const std::vector<std::byte> bytes = small_save();
    const std::vector<std::byte> other = small_save();
    const auto file = read_save(bytes);
    REQUIRE(file);
    const auto elsewhere = read_save(other);
    REQUIRE(elsewhere);
    {
        const FieldReads before(bytes);
    }
    (void)decode_object(*file, file->objects.at(1))->find("Kind"); // No log recording
    const FieldReads reads(bytes);
    (void)decode_object(*elsewhere, elsewhere->objects.at(1))->find("Kind");
    const auto left = left_out(*file, reads);
    REQUIRE(left.size() == 2);
    CHECK(left[1].untouched);
}

TEST_CASE("classes are told as what a player calls them")
{
    CHECK(left_out_group("SomeQueueEntertainer") == "Rct3pp_LeftOutQueues");
    CHECK(left_out_group("Handyman") == "Rct3pp_LeftOutStaff");
    CHECK(left_out_group("TrackedRidePersonThing") == "Rct3pp_LeftOutRides");
    CHECK(left_out_group("Weather") == "Rct3pp_LeftOutOther");
}
