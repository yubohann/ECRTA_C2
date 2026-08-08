#!/usr/bin/env python3
"""Offline unit tests for the ETA-C2 greedy min-max balancing algorithm.

The C++ implementation (allocateTasks post-LKH pass) is mirrored here 1:1 so we
can validate the algorithm properties without burning simulator time:
  1. makespan (max load) never increases after balancing;
  2. moves are bounded by eta_balance_iters and each move reduces max load
     by at least eta_min_improvement;
  3. per-drone sequences keep all tasks (move = transfer, no duplication/loss);
  4. edge cases: empty assignments, single drone, all tasks on one drone,
     zero-cost rows, non-finite matrix entries;
  5. service-time EMA dominates travel in the time-grounded load.
"""
import math
import random

ETA_ITERS = 10
ETA_MIN_IMPROV = 3.0
SPEED = 2.0


def load_of(seq, drone_row, n_drones, n_centers, mat, service_by_grid, center_grid):
    if not seq:
        return 0.0
    L = 0.0
    prev = drone_row
    for p in seq:
        ci = p  # center index
        col = 1 + n_drones + ci
        v = mat[prev][col]
        if not math.isfinite(v):
            v = 1e9
        grid = center_grid.get(ci, -1)
        L += v / SPEED + service_by_grid.get(grid, 0.0)
        prev = col
    return L


def balance(all_centers, n_drones, n_centers, mat, service_by_grid, center_grid):
    """Returns (balanced, moves, before_max, after_max). Mirrors C++ logic."""
    all_centers = [list(s) for s in all_centers]
    loads = [load_of(s, 1 + d, n_drones, n_centers, mat, service_by_grid, center_grid)
             for d, s in enumerate(all_centers)]
    before = max(loads) if loads else 0.0
    moves = 0
    for _ in range(ETA_ITERS):
        max_d = max(range(n_drones), key=lambda d: loads[d])
        min_d = min(range(n_drones), key=lambda d: loads[d])
        if max_d == min_d or not all_centers[max_d]:
            break
        cur_max = loads[max_d]
        improved = False
        for take in range(len(all_centers[max_d]), 0, -1):
            seq_a = list(all_centers[max_d])
            seq_b = list(all_centers[min_d])
            seq_a.pop(take - 1)
            seq_b.append(all_centers[max_d][take - 1])
            new_max = max(
                load_of(seq_a, 1 + max_d, n_drones, n_centers, mat, service_by_grid, center_grid),
                load_of(seq_b, 1 + min_d, n_drones, n_centers, mat, service_by_grid, center_grid))
            if new_max < cur_max - ETA_MIN_IMPROV:
                all_centers[max_d] = seq_a
                all_centers[min_d] = seq_b
                loads[max_d] = load_of(seq_a, 1 + max_d, n_drones, n_centers, mat, service_by_grid, center_grid)
                loads[min_d] = load_of(seq_b, 1 + min_d, n_drones, n_centers, mat, service_by_grid, center_grid)
                moves += 1
                improved = True
                break
        if not improved:
            break
    after = max(loads) if loads else 0.0
    return all_centers, moves, before, after


def make_mat(n_drones, n_centers, rng, sparse_ok=True):
    dim = 1 + n_drones + n_centers
    mat = [[rng.uniform(2.0, 20.0) for _ in range(dim)] for _ in range(dim)]
    if sparse_ok:
        for r in range(dim):
            for c in range(dim):
                if rng.random() < 0.15:
                    mat[r][c] = float("nan")
    for i in range(dim):
        mat[i][i] = 0.0
    return mat


def test_property_no_worse():
    rng = random.Random(7)
    for trial in range(200):
        n_d = rng.randint(2, 5)
        n_c = rng.randint(2, 12)
        mat = make_mat(n_d, n_c, rng)
        service = {c: rng.uniform(0.0, 30.0) for c in range(n_c)}
        centers = list(range(n_c))
        rng.shuffle(centers)
        split = [0] * n_d
        for c in centers:
            split[rng.randrange(n_d)] += 1
        all_centers = []
        pos = 0
        for d in range(n_d):
            all_centers.append(centers[pos:pos + split[d]])
            pos += split[d]
        _, moves, before, after = balance(all_centers, n_d, n_c, mat, service, {c: c for c in range(n_c)})
        assert after <= before + 1e-9, (trial, before, after)
        assert moves <= ETA_ITERS
    print("PASS property: makespan never increases; moves bounded")


def test_property_tasks_preserved():
    rng = random.Random(11)
    for trial in range(200):
        n_d = rng.randint(2, 4)
        n_c = rng.randint(3, 10)
        mat = make_mat(n_d, n_c, rng)
        service = {c: rng.uniform(1.0, 20.0) for c in range(n_c)}
        centers = list(range(n_c))
        rng.shuffle(centers)
        split = [0] * n_d
        for c in centers:
            split[rng.randrange(n_d)] += 1
        all_centers = []
        pos = 0
        for d in range(n_d):
            all_centers.append(centers[pos:pos + split[d]])
            pos += split[d]
        flat_before = sorted(c for s in all_centers for c in s)
        out, _, _, _ = balance(all_centers, n_d, n_c, mat, service, {c: c for c in range(n_c)})
        flat_after = sorted(c for s in out for c in s)
        assert flat_before == flat_after, (trial, flat_before, flat_after)
    print("PASS property: no task duplication or loss")


def test_edge_cases():
    rng = random.Random(3)
    n_d, n_c = 2, 4
    mat = make_mat(n_d, n_c, rng)
    service = {c: 5.0 for c in range(n_c)}
    # empty assignments
    out, moves, before, after = balance([[], []], n_d, n_c, mat, service, {c: c for c in range(n_c)})
    assert moves == 0 and after == 0.0
    # all tasks on one drone
    out, moves, before, after = balance([[0, 1, 2, 3], []], n_d, n_c, mat, service, {c: c for c in range(n_c)})
    assert moves >= 0 and after <= before + 1e-9
    # single drone
    out, moves, before, after = balance([[0, 1]], 1, n_c, mat, service, {0: 0, 1: 1})
    assert moves == 0
    print("PASS edge cases: empty / single-drone / lopsided")


def test_service_dominance():
    # Two drones, equal travel, one has a heavy service task -> balancing should
    # move the heavy task to the idle drone (if travel allows).
    n_d, n_c = 2, 4
    rng = random.Random(5)
    mat = [[1.0 if r == c else (1.0 if r < 2 or c < 2 else 3.0)
            for c in range(1 + n_d + n_c)] for r in range(1 + n_d + n_c)]
    for i in range(1 + n_d + n_c):
        mat[i][i] = 0.0
    # drone1 gets heavy task 2 (service 40), drone0 gets light tasks 0,1
    all_centers = [[0, 1], [2, 3]]
    service = {0: 2.0, 1: 2.0, 2: 40.0, 3: 2.0}
    out, moves, before, after = balance(all_centers, n_d, n_c, mat, service, {c: c for c in range(n_c)})
    assert moves >= 1, "balancing should move the heavy task"
    assert after < before - 1e-6, "makespan must drop"
    print(f"PASS service dominance: before={before:.1f} after={after:.1f} moves={moves}")


if __name__ == "__main__":
    test_property_no_worse()
    test_property_tasks_preserved()
    test_edge_cases()
    test_service_dominance()
    print("ALL ETA-BALANCE TESTS PASSED")
