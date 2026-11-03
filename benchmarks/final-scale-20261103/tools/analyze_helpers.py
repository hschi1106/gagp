def strip(x):
    """Drop timing fields (*_ms); everything else must match exactly."""
    if isinstance(x, dict): return {k: strip(v) for k, v in x.items() if not k.endswith("_ms")}
    if isinstance(x, list): return [strip(v) for v in x]
    return x
