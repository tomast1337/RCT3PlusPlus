// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/land_buying.hpp"

#include <utility>

namespace rct3::park
{

    LandBought buy_land(save::TerrainGrid& grid, std::span<const save::Tile> tiles, LandBuy what, std::int32_t price, std::optional<std::int64_t> cash)
    {
        const save::Ownership on_sale = what == LandBuy::Land ? save::Ownership::ForSale : save::Ownership::ForSaleRights;
        LandBought out;
        for (const save::Tile t : tiles)
        {
            if (!grid.contains(t.col, t.row))
                continue;
            save::TerrainCell& cell = grid.at(t.col, t.row);
            if (cell.ownership() != on_sale)
                continue;
            if (cash && *cash - out.spent < price)
            {
                out.short_of_cash = true;
                continue;
            }
            cell.flags = static_cast<std::uint8_t>((cell.flags & ~std::to_underlying(save::TileFlag::ForSale)) | std::to_underlying(save::TileFlag::Owned));
            out.spent += price;
            ++out.tiles;
        }
        return out;
    }

} // namespace rct3::park
