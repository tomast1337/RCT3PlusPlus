#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fits the ride rating model's weights to rides in saved parks: an approximation of the ratings saved parks
hold, not a reproduction of the original game's.

    ride-tests PARK.dat... > rides.csv          (build/debug/ride-tests, over any saved parks)
    tools/ratings-fit/fit.py rides.csv [--model assets/simulation/ride_ratings.json] [--write]

Each family of ride types gets weights for excitement, intensity and nausea over the features the model
names (src/simulation/ratings.hpp computes them). All rides together give shared weights; each family's
weights are drawn towards those, harder the fewer rides it has. Excitement is fitted before the intensity
penalty, so the penalty in the model stays as it is. Prints each family's error, left-one-out, beside the
shared weights' error. With --write the weights go into the model file.
"""
import argparse
import csv
import json
import sys

import numpy as np

GRAVITY = 9.81
RATINGS = ("excitement", "intensity", "nausea")
SHRINK = 4.0  # Rides' worth of pull towards the shared weights; our own value
RIDGE = 0.5   # Pull of the shared weights towards nothing


def features(row, model):
    """The same features, capped, as the model in src/simulation/ratings.cpp."""
    f = lambda k: float(row[k])
    raw = {
        "one": 1.0,
        "max_speed": f("max_speed"),
        "average_speed": f("average_speed"),
        "ride_time": f("ride_time"),
        "ride_length": f("ride_length"),
        "up_force": max(f("max_positive_vertical") / GRAVITY - 1, 0),
        "down_force": abs(f("max_negative_vertical")) / GRAVITY,
        "side_force": abs(f("max_lateral")) / GRAVITY,
        "air_time": f("air_time"),
        "drops": f("drop_count"),
        "highest_drop": f("highest_drop"),
        "inversions": f("inversion_count"),
        "lift_hills": f("lift_hill_count"),
    }
    return np.array([min(raw[name], cap) for name, cap in model["features"].items()])


def kept_share(intensity, penalty):
    share = 1.0
    for bound in penalty["bounds"]:
        if intensity >= bound:
            share *= penalty["keep"]
    return share


def solve(x, y, scale, prior, pull):
    """Least squares with each weight drawn towards `prior` by `pull`, in units of the feature's spread."""
    reg = np.diag(pull * scale**2)
    return np.linalg.solve(x.T @ x + reg, x.T @ y + reg @ prior)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--model", default="assets/simulation/ride_ratings.json")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    model = json.load(open(args.model))
    family_of = {t.lower(): name for name, fam in model["families"].items() for t in fam.get("types", [])}
    rows = [r for path in args.csv for r in csv.DictReader(open(path))]
    if not rows:
        sys.exit("no rides")
    x = np.array([features(r, model) for r in rows])
    targets = {k: np.array([float(r[k]) for r in rows]) for k in RATINGS}
    # Before the penalty, so the fitted weights and the penalty together give the saved excitement.
    targets["excitement"] = targets["excitement"] / np.array([kept_share(i, model["intensity_penalty"]) for i in targets["intensity"]])
    fam = np.array([family_of.get(r["type"].lower(), "default") for r in rows])
    scale = x.std(axis=0) + 1e-6
    scale[0] = 1.0
    zero = np.zeros(x.shape[1])

    shared = {k: solve(x, targets[k], scale, zero, RIDGE) for k in RATINGS}
    print(f"{'family':<12}{'rides':>6}  " + "  ".join(f"{k[:5]:>11}" for k in RATINGS) + "   (mean miss: family / shared)")
    total = {k: [0.0, 0.0] for k in RATINGS}
    for name in model["families"]:
        mask = fam == name
        n = int(mask.sum())
        weights = {}
        misses = []
        for k in RATINGS:
            weights[k] = solve(x[mask], targets[k][mask], scale, shared[k], SHRINK) if n else shared[k]
            own = shared_miss = 0.0
            for i in np.flatnonzero(mask):
                keep = mask.copy()
                keep[i] = False
                w = solve(x[keep], targets[k][keep], scale, shared[k], SHRINK) if keep.any() else shared[k]
                own += abs(x[i] @ w - targets[k][i])
                shared_miss += abs(x[i] @ shared[k] - targets[k][i])
            total[k][0] += own
            total[k][1] += shared_miss
            misses.append(f"{own / max(n, 1):5.2f}/{shared_miss / max(n, 1):5.2f}")
        print(f"{name:<12}{n:>6}  " + "  ".join(misses))
        for k in RATINGS:
            model["families"][name][k] = {f: round(float(w), 5) for f, w in zip(model["features"], weights[k])}
    print(f"{'all':<12}{len(rows):>6}  " + "  ".join(f"{total[k][0] / len(rows):5.2f}/{total[k][1] / len(rows):5.2f}" for k in RATINGS))
    if args.write:
        with open(args.model, "w") as out:
            json.dump(model, out, indent=2)
            out.write("\n")


if __name__ == "__main__":
    main()
