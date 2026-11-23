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
    // An invented save: a queue of two fields with a list of two-field elements, litter of one field, a class
    // with no objects, and a colour cache, which a load leaves on purpose. Only the type tags and the cache's
    // class and field names are the format's own.
    std::vector<std::byte> small_save()
    {
        const std::vector<Class> classes {
            { .name = "TestQueueLine", .attributes = { { .name = "Length", .type = "uint32", .size = 4 }, { .name = "Spots", .type = "list", .size = 0, .children = { { .name = "Place", .type = "uint32", .size = 4 }, { .name = "Wait", .type = "float32", .size = 4 } } } } },
            { .name = "TestLitter", .attributes = { { .name = "Kind", .type = "uint32", .size = 4 } } },
            { .name = "TestUnused", .attributes = { { .name = "Thing", .type = "uint32", .size = 4 } } },
            { .name = "FlexiColourGroupCache", .attributes = { { .name = "FlexiCache", .type = "uint32", .size = 4 } } },
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
        std::vector<std::byte> cache;
        u32_bytes(cache, 0);
        const std::vector<ObjectRecord> objects { { .class_index = 0, .id = 1, .payload = queue }, { .class_index = 1, .id = 2, .payload = litter }, { .class_index = 3, .id = 3, .payload = cache } };
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

    const std::vector<LeftOutClass> left = left_out(*file, reads).classes;
    REQUIRE(left.size() == 2); // The class with no objects leaves nothing, the cache is left on purpose
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
    const auto left = left_out(*file, reads).classes;
    REQUIRE(left.size() == 2);
    CHECK(left[1].untouched);
}

TEST_CASE("a load's totals count classes and fields unread, left on purpose and read")
{
    const std::vector<std::byte> bytes = small_save();
    const FieldReads reads(bytes);
    const auto file = read_save(bytes);
    REQUIRE(file);
    (void)decode_object(*file, file->objects.at(0))->find("Spots");

    const LeftOutTotals totals = left_out(*file, reads).totals;
    // The queue's list is read, so its two parts count apiece: Spots read, Length, Place and Wait not.
    CHECK(totals.fields.read == 1);
    CHECK(totals.fields.unread == 4); // Length, Spots.Place, Spots.Wait, Kind
    CHECK(totals.fields.skipped == 1);
    CHECK(totals.classes.unread == 2);
    CHECK(totals.classes.skipped == 1);
    CHECK(totals.classes.read == 0);
}

TEST_CASE("a skipped field covers its nested parts, not fields that only start with its name")
{
    CHECK(skip_reason("FlexiColourGroupCache", "FlexiCache"));
    CHECK(skip_reason("FlexiColourGroupCache", "Anything.Inside")); // The whole class
    CHECK_FALSE(skip_reason("TestQueueLine", "Length"));
    CHECK(skip_reason("AIDDatabaseEntry", "ATTRTYPE"));
    CHECK_FALSE(skip_reason("AIDDatabaseEntry", "ATTRTYPEX"));
    CHECK_FALSE(skip_reason("AIDDatabaseEntry", "InventibleAnims"));
    CHECK(skip_reason("RideParticleManager", "RideCarEntries.Entry"));
}

TEST_CASE("classes are told as what a player calls them")
{
    CHECK(left_out_group("SomeQueueEntertainer") == "Rct3pp_LeftOutQueues");
    CHECK(left_out_group("Handyman") == "Rct3pp_LeftOutStaff");
    CHECK(left_out_group("TrackedRidePersonThing") == "Rct3pp_LeftOutRides");
    CHECK(left_out_group("Weather") == "Rct3pp_LeftOutOther");
}
