// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parks opened one after another give back what the one before held on the device: the blank landscape, opened
// again after other parks, leaves as many buffers, pipelines and textures alive as it did the first time.
//
//   ./nob script scripts/tests/park_reload.ts -- --no-audio

export {};

const kBlank = "Campaigns/Base/BlankLandscape.dat";
const kOthers = ["Campaigns/Base/VanillaHills.dat", "Campaigns/Base/IslandHopping.dat", "Campaigns/Base/GhostTown.dat"];

async function open(path: string) {
    await park.open(path);
    await frames(10);
    const live = render.live();
    log(path, JSON.stringify(live));
    return live;
}

const first = await open(kBlank);
for (const path of kOthers)
    await open(path);
const again = await open(kBlank);
for (const key of ["buffers", "pipelines", "textures"] as const)
    if (again[key] !== first[key])
        throw new Error(`expected the blank landscape's ${key} alive again: ${first[key]} the first time, ${again[key]} after other parks`);
