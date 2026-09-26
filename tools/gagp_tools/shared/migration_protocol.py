"""Explicit protocol identities keep historical and revised evidence separate."""

STRICT_PROTOCOL = "frozen-v1"
REPRESENTATIVE_PROTOCOL = "representative-2026-09-23"
SPEEDUP_PROTOCOL = "representative-speedup-2026-09-23"


def representative(protocol: str = STRICT_PROTOCOL) -> bool:
    if protocol not in (STRICT_PROTOCOL, REPRESENTATIVE_PROTOCOL, SPEEDUP_PROTOCOL):
        raise ValueError(f"unknown migration acceptance protocol: {protocol}")
    return protocol in (REPRESENTATIVE_PROTOCOL, SPEEDUP_PROTOCOL)
