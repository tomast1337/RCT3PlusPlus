// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cursor.hpp"

#include <doctest/doctest.h>
#include <initializer_list>
#include <set>
#include <string_view>
#include <vector>

using namespace rct3::ui;

namespace
{
    StandardCursorSet with(std::initializer_list<StandardCursor> shapes)
    {
        StandardCursorSet set = required_cursors();
        for (const StandardCursor s : shapes)
            set.set(static_cast<std::size_t>(s));
        return set;
    }

    StandardCursorSet everything()
    {
        StandardCursorSet set;
        set.set();
        return set;
    }

    // Records what it was asked to show, as a platform would.
    struct FakeBackend
    {
        StandardCursorSet shapes = everything();
        std::vector<StandardCursor> shown;

        [[nodiscard]] StandardCursorSet available() const
        {
            return shapes;
        }
        void show(StandardCursor shape)
        {
            shown.push_back(shape);
        }
    };
    static_assert(CursorBackend<FakeBackend>);
} // namespace

TEST_CASE("with every shape each kind takes its best one")
{
    CHECK(resolve_cursor(CursorKind::Arrow, everything()) == StandardCursor::Arrow);
    CHECK(resolve_cursor(CursorKind::Hand, everything()) == StandardCursor::PointingHand);
    CHECK(resolve_cursor(CursorKind::Text, everything()) == StandardCursor::IBeam);
    CHECK(resolve_cursor(CursorKind::NotAllowed, everything()) == StandardCursor::NotAllowed);
    CHECK(resolve_cursor(CursorKind::Move, everything()) == StandardCursor::ResizeAll);
    CHECK(resolve_cursor(CursorKind::ResizeHorizontal, everything()) == StandardCursor::ResizeEW);
    CHECK(resolve_cursor(CursorKind::ResizeVertical, everything()) == StandardCursor::ResizeNS);
    CHECK(resolve_cursor(CursorKind::ResizeDiagonalDown, everything()) == StandardCursor::ResizeNWSE);
    CHECK(resolve_cursor(CursorKind::ResizeDiagonalUp, everything()) == StandardCursor::ResizeNESW);
    CHECK(resolve_cursor(CursorKind::Grabbing, everything()) == StandardCursor::ResizeAll);
    CHECK(resolve_cursor(CursorKind::Hidden, everything()) == StandardCursor::Hidden);
}

TEST_CASE("kinds without a shape of their own fall back to the closest one")
{
    CHECK(resolve_cursor(CursorKind::Grab, everything()) == StandardCursor::PointingHand);
    CHECK(resolve_cursor(CursorKind::ZoomIn, everything()) == StandardCursor::Crosshair);
    CHECK(resolve_cursor(CursorKind::ZoomOut, everything()) == StandardCursor::Crosshair);
}

TEST_CASE("a backend with only the older shapes falls back further")
{
    // What a platform without the four-way, diagonal and not-allowed shapes has.
    const StandardCursorSet older = with({ StandardCursor::PointingHand, StandardCursor::IBeam, StandardCursor::Crosshair, StandardCursor::ResizeEW, StandardCursor::ResizeNS });
    CHECK(resolve_cursor(CursorKind::NotAllowed, older) == StandardCursor::Arrow);
    CHECK(resolve_cursor(CursorKind::Move, older) == StandardCursor::PointingHand);
    CHECK(resolve_cursor(CursorKind::Grabbing, older) == StandardCursor::PointingHand);
    CHECK(resolve_cursor(CursorKind::ResizeDiagonalDown, older) == StandardCursor::Arrow);
    CHECK(resolve_cursor(CursorKind::ResizeHorizontal, older) == StandardCursor::ResizeEW);

    SUBCASE("a diagonal takes the four-way arrow when only that is there")
    {
        CHECK(resolve_cursor(CursorKind::ResizeDiagonalUp, with({ StandardCursor::ResizeAll })) == StandardCursor::ResizeAll);
    }
}

TEST_CASE("every kind ends on a shape every backend has, and has a name")
{
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < kCursorKinds; ++i)
    {
        const auto kind = static_cast<CursorKind>(i);
        CHECK(required_cursors().test(static_cast<std::size_t>(cursor_shapes(kind).back())));
        CHECK(resolve_cursor(kind, required_cursors()) == cursor_shapes(kind).back());
        names.insert(cursor_name(kind));
    }
    CHECK(names.size() == kCursorKinds);
}

TEST_CASE("the switch tells the backend only when the shape changes")
{
    FakeBackend backend;
    CursorSwitch cursor;
    CHECK_FALSE(cursor.shown());
    CHECK(cursor.set(backend, CursorKind::Hand) == StandardCursor::PointingHand);
    CHECK_FALSE(cursor.set(backend, CursorKind::Hand));
    CHECK(cursor.set(backend, CursorKind::Text) == StandardCursor::IBeam);
    CHECK(backend.shown == std::vector { StandardCursor::PointingHand, StandardCursor::IBeam });

    SUBCASE("two kinds with the same shape are one shape")
    {
        // Grab falls back to the pointing hand, as Hand shows.
        CHECK(cursor.set(backend, CursorKind::Hand));
        CHECK_FALSE(cursor.set(backend, CursorKind::Grab));
        CHECK(backend.shown.size() == 3);
    }
}
