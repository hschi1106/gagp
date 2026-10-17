"""Deterministic, input-disjoint quality fixtures from the native task pool."""
import hashlib
import json
import math

INPUT_FIELDS = ("input", "scalars", "start", "start_b", "extent", "extent_b")


def identity(row):
    return json.dumps({k: row.get(k, [] if k in ("input", "scalars") else 0)
                       for k in INPUT_FIELDS}, sort_keys=True, separators=(",", ":"))


def unique(rows):
    result = {}
    for row in rows:
        key = identity(row)
        if key in result:
            for field in ("expected", "summary1", "summary2"):
                if result[key].get(field) != row.get(field):
                    raise ValueError("task oracle is inconsistent for identical input")
        else:
            result[key] = row
    return result


def split_pool(pool, train_limit=200, test_limit=1000):
    if train_limit < 1 or test_limit < 1:
        raise ValueError("split limits must be positive")
    train_pool, test_pool = unique(pool["train"]), unique(pool["test"])
    unique(pool["train"] + pool["test"])
    train = dict(list(train_pool.items())[:train_limit])
    test = {k: v for k, v in test_pool.items() if k not in train}
    reserved = []
    if not test:
        if len(test_pool) < 2:
            raise ValueError("input domain cannot provide a nonempty disjoint split")
        keys = sorted(test_pool, key=lambda k: hashlib.sha256(
            ("day24-reserve-20261017:" + pool["task"] + ":" + k).encode()).hexdigest())
        reserved = keys[:max(1, math.ceil(len(keys) * .2))]
        train = dict((k, v) for k, v in train_pool.items() if k not in reserved)
        train = dict(list(train.items())[:train_limit])
        test = {k: v for k, v in test_pool.items() if k not in train}
    test = dict(list(test.items())[:test_limit])
    if not train or not test or train.keys() & test.keys():
        raise ValueError("quality split is empty or overlaps")
    return dict(train=list(train.values()), test=list(test.values()), metadata=dict(
        task=pool["task"], data_seed=pool["data_seed"],
        raw_train_unique=len(train_pool), raw_test_unique=len(test_pool),
        train_count=len(train), test_count=len(test), input_overlap=0,
        heldout_outside_training_support=sum(k not in train_pool for k in test),
        reserved_test_inputs=len(reserved),
        origin="ASGP BuildTask local domain, not official PSB fixtures",
        rule="first unique native train inputs up to200; unique native test inputs excluding train, up to1000; reserve20% only if otherwise no held-out input"))
