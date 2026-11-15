// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which build this is, so a bug report can say. The build writes it (see nob.cpp) from
// `git describe --tags --always --dirty`: the tag for a release (v0.1.0), else the tag, the commits since and the
// commit (v0.1.0-12-g1a2b3c4d), or the commit alone before the first tag, with -dirty when files had changes;
// "unknown" when built outside a git checkout.
#pragma once

#include <string_view>

namespace rct3::core
{

    /// The build's version, as above.
    [[nodiscard]] std::string_view version();

} // namespace rct3::core
