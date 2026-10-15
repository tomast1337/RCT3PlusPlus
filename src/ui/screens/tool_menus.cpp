// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_menus.hpp"

#include <array>

namespace rct3::ui
{

    namespace
    {
        using enum ToolItem;

        constexpr std::array<ToolMenuItem, 2> kDelete { {
            { DeleteOne, "GUI2_DeleteSingleItem", { }, { }, false },
            { DeleteArea, "GUI2_DeleteSelection", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 4> kLand { {
            { Waterfalls, "Land_Waterfall", { }, { }, false },
            { Water, "Land_Water", { }, { }, false },
            { PaintGround, "Land_Brush_Paint", { }, { }, false },
            { ShapeGround, "Land_Brush_Height", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 10> kScenery { {
            { Fireworks, "Obj_Scenery_FireworkSources", { }, { }, false },
            { SavedStructures, "SavedSelection_SubMenu", { }, { }, false },
            { Billboards, "Obj_Scenery_Adverts", { }, { }, false },
            { GroundSurfaces, "Obj_Scenery_TerrainSurfaces", { }, { }, false },
            { RideEvents, "Obj_Scenery_RideEvents", { }, { }, false },
            { SceneryItems, "Obj_Scenery_ThemeProps", { }, { }, false },
            { PathExtras, "Obj_Scenery_PathExtras", { }, { }, false },
            { Buildings, "Obj_Scenery_WallsRoofsAndBuildings", { }, { }, false },
            { Fences, "Obj_Scenery_Fences", { }, { }, false },
            { Plants, "Obj_Scenery_TreesNPlants", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 4> kShops { {
            { Facilities, "Obj_Shops_Utilities", { }, { }, false },
            { Stalls, "Obj_Shops_Stalls", { }, { }, false },
            { DrinkShops, "Obj_Shops_DrinkShops", { }, { }, false },
            { FoodShops, "Obj_Shops_FoodShops", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 12> kRides { {
            { Enclosures, "Obj_Enclosures", { }, { }, false },
            { Animals, "Obj_Rides_Animals", { }, { }, false },
            { Pools, "Obj_Pools", { }, { }, false },
            { ImportedTracks, "Obj_Rides_TrackImport", { }, { }, false },
            { OtherRides, "Obj_Rides_OtherRides", { }, { }, false },
            { PoolRides, "Obj_Rides_PoolRides", { }, { }, false },
            { TransportRides, "Obj_Rides_TransportRides", { }, { }, false },
            { GentleRides, "Obj_Rides_GentleRides", { }, { }, false },
            { WaterRides, "Obj_Rides_WaterRides", { }, { }, false },
            { ThrillRides, "Obj_Rides_ThrillRides", { }, { }, false },
            { JuniorRides, "Obj_Rides_JuniorCoasters", { }, { }, false },
            { Coasters, "Obj_Rides_RollerCoasters", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 8> kManagement { {
            { Challenges, "Man_Challenges", { }, { }, false },
            { Guests, "Man_Guests", { }, { }, false },
            { Attractions, "Man_Rides", { }, { }, false },
            { Research, "Man_Research", { }, { }, false },
            { Objectives, "Man_Objectives", { }, { }, false },
            { Staff, "Man_Staff", { }, { }, false },
            { Finances, "Man_Finances", { }, { }, false },
            { ParkOperations, "Man_Park", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 4> kFiles { {
            { Save, "File_SaveGame", { }, { }, false },
            { Load, "File_LoadGame", { }, { }, false },
            { Options, "File_GameOptions", { }, { }, false },
            { About, "File_AboutRCT", { }, { }, false },
        } };
        constexpr std::array<ToolMenuItem, 9> kView { {
            { AnimalTiles, "AnimalWalkingGridOff", "AnimalWalkingGridOn", "ToggleEnclosureWalkInfo_TT", false },
            { SeeThroughGround, "Land_TerrainVisibleOff", "Land_TerrainVisible", "Land_TerrainVisibleHelp", true },
            { Scenery, "Land_SceneryVisibleOff", "Land_SceneryVisible", "Land_SceneryVisibleHelp", true },
            { Supports, "Land_SupportsOff", "Land_Supports", "Land_SupportsHelp", true },
            { People, "Land_PeopleOff", "Land_People", "Land_PeopleHelp", true },
            { PathHeights, "Land_PathHeights", "Land_PathHeightsOn", "Land_PathHeightsHelp", false },
            { RideHeights, "Land_RideHeights", "Land_RideHeightsOn", "Land_RideHeightsHelp", false },
            { Contours, "Land_Contours", "Land_ContoursOn", "Land_ContoursHelp", false },
            { Grid, "Land_Grid", "Land_GridOn", "Land_GridHelp", false },
        } };
    } // namespace

    std::span<const ToolMenuItem> tool_menu(ParkTool tool)
    {
        switch (tool)
        {
            case ParkTool::Delete:
                return kDelete;
            case ParkTool::Land:
                return kLand;
            case ParkTool::Scenery:
                return kScenery;
            case ParkTool::Shops:
                return kShops;
            case ParkTool::Rides:
                return kRides;
            case ParkTool::Management:
                return kManagement;
            case ParkTool::Files:
                return kFiles;
            case ParkTool::View:
                return kView;
            case ParkTool::Paths:
            case ParkTool::Leave:
            case ParkTool::Map:
                break;
        }
        return { };
    }

    bool is_switch(ToolItem item)
    {
        return item >= AnimalTiles;
    }

} // namespace rct3::ui
