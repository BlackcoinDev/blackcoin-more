# Plan: Optional P2P Service-Bit Filtering for Blackcoin More 28.4.0

## Context

During live network monitoring of the Blackcoin mainnet, a peer was observed advertising a non-standard service bit while running a forked client:

```json
{
    "addr": "158.173.67.91:45354",
    "subver": "/Blackcoin:30.1.1/",
    "services": "0000000001000809",
    "servicesnames": [
        "NETWORK",
        "WITNESS",
        "P2P_V2",
        "UNKNOWN[2^24]"
    ]
}
```

This peer is running the **QuantumQuasar/QQfork v30.x** client. That project has publicly stated its intent to remove the `bad-unknown-witness-version` defense introduced in Blackcoin More 28.4.0, in order to allow witness-version scripts (> 1) on the network.

The official Blackcoin More 28.4.0 node already has a **consensus-level** defense: `ContextualCheckBlock` rejects any block containing an unknown witness program version. This is the primary protection.

This plan proposes an **optional P2P hygiene layer** that lets node operators reject peers advertising unknown service bits. It is a secondary, opt-in measure.

## Goals

1. Provide an opt-in startup flag `-blockunknownservices` for node operators who want stricter peer hygiene.
2. Disconnect inbound and outbound peers that advertise any service bit in the reserved **bits 24-31** range.
3. Filter `addr`/`addrv2` relay so that address records carrying bits 24-31 are not stored or propagated.
4. Avoid breaking future honest protocol upgrades by making the behavior **optional and off by default**.
5. Keep the change minimal and isolated to P2P connection handling.

## Non-Goals

1. This is **not a consensus change**. It does not alter block validation.
2. It does not remove or invalidate blocks already accepted from fork peers.
3. It does not guarantee fork nodes cannot connect — they can simply stop advertising bits 24-31.
4. It does not prevent fork nodes from appearing in `getpeerinfo` or `getnetworkinfo` under different service-bit configurations.

## Known Service Bits in Blackcoin More 28.4.0

Defined in `src/protocol.h`:

| Bit | Symbol | Description |
|---|---|---|
| 0 | `NODE_NETWORK` | Full block chain available |
| 2 | `NODE_BLOOM` | Bloom filter support |
| 3 | `NODE_WITNESS` | SegWit witness data support |
| 6 | `NODE_COMPACT_FILTERS` | BIP157/158 block filters |
| 10 | `NODE_NETWORK_LIMITED` | Last ~2 days of blocks only (BIP159) |
| 11 | `NODE_P2P_V2` | BIP324 v2 transport |

The observed fork peer sets bit 24, which is outside this known set.

## Target Service-Bit Range

The option must filter the reserved experimental range **bits 24-31**:

```cpp
static constexpr ServiceFlags FORBIDDEN_SERVICE_BITS = ServiceFlags(
    (1ULL << 24) | (1ULL << 25) | (1ULL << 26) | (1ULL << 27) |
    (1ULL << 28) | (1ULL << 29) | (1ULL << 30) | (1ULL << 31)
);
```

These bits are reserved in Bitcoin for temporary experiments and are the most likely range a fork project would use to avoid colliding with assigned BIP service bits. Filtering this entire range catches the current QQfork signal and any nearby variants.

## Proposed Change

### 1. Add command-line option

In `src/init.cpp`, under connection options, add:

```cpp
argsman.AddArg("-blockunknownservices",
    "Disconnect inbound and outbound peers that advertise reserved experimental service bits (bits 24-31). "
    "Also drop address records from addr/addrv2 messages that carry those bits. "
    "This is an opt-in hygiene measure and is off by default. "
    "Warning: enabling this may cause disconnection from future honest nodes that advertise bits 24-31 before this node is upgraded.",
    ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
```

### 2. Define the forbidden bit mask

Add a `constexpr` as a local helper in `src/net_processing.cpp`:

```cpp
// Bits 24-31 are reserved in Bitcoin for temporary experiments.
// We treat them as forbidden when -blockunknownservices is enabled.
static constexpr ServiceFlags FORBIDDEN_SERVICE_BITS = ServiceFlags(
    (1ULL << 24) | (1ULL << 25) | (1ULL << 26) | (1ULL << 27) |
    (1ULL << 28) | (1ULL << 29) | (1ULL << 30) | (1ULL << 31)
);
```

### 3. Add the disconnect check for inbound and outbound peers

In `src/net_processing.cpp`, inside the version-message handler (around line 3980). The check must be placed **before** `m_addrman.SetServices()` to prevent forbidden service bits from being recorded in addrman:

```cpp
        vRecv >> nVersion >> Using<CustomUintFormatter<8>>(nServices) >> nTime;
        if (nTime < 0) {
            nTime = 0;
        }

        // Must check before SetServices below to avoid recording forbidden service bits in addrman.
        if (m_opts.block_unknown_services && !pfrom.HasPermission(NetPermissionFlags::NoBan) && HasForbiddenServiceBits(nServices))
        {
            LogPrint(BCLog::NET, "peer=%d advertised forbidden service bits (%08x); disconnecting\n", pfrom.GetId(), nServices);
            pfrom.fDisconnect = true;
            return;
        }

        vRecv.ignore(8); // Ignore the addrMe service bits sent by the peer
        vRecv >> CNetAddr::V1(addrMe);
        if (!pfrom.IsInboundConn())
        {
            // Overwrites potentially existing services. In contrast to this,
            // unvalidated services received via gossip relay in ADDR/ADDRV2
            // messages are only ever added but cannot replace existing ones.
            m_addrman.SetServices(pfrom.addr, nServices);
        }
        if (pfrom.ExpectServicesFromConn() && !HasAllDesirableServiceFlags(nServices))
        {
            LogPrint(BCLog::NET, "peer=%d does not offer the expected services (%08x offered, %08x expected); disconnecting\n", pfrom.GetId(), nServices, GetDesirableServiceFlags(nServices));
            pfrom.fDisconnect = true;
            return;
        }
```

**Ordering is critical here.** If the forbidden-bit check runs after `SetServices`, the address is already recorded in addrman with the bad services before the peer is disconnected. On shutdown, it gets written to peers.dat.

The check does **not** use `ExpectServicesFromConn()` because that predicate returns `false` for inbound connections, and we want to prevent inbound fork connections. It explicitly checks `!pfrom.HasPermission(NetPermissionFlags::NoBan)` to ensure whitelisted peers are exempt.

> **Note on Existing Connections:** Since `-blockunknownservices` is a startup-only configuration flag, enabling or disabling it requires a node restart. This restart naturally terminates all existing peer connections, ensuring that every peer is checked against the version-handshake filter upon reconnecting. No runtime rechecking is needed.

### 4. Plumb the option through to PeerManager

`PeerManagerImpl` is constructed with an options struct. Add a `bool block_unknown_services` field to that struct and populate it from `init.cpp` based on the `-blockunknownservices` argument.

The exact struct name and path can be determined by grepping for `PeerManagerImpl` construction in `init.cpp` and `net_processing.cpp`.

### 5. Filter `addr`/`addrv2` relay

The goal is to prevent the node from storing or propagating address records that carry forbidden bits. In `src/net_processing.cpp`, locate the `addr`/`addrv2` message handlers. Before adding an address to `addrman`, check whether it advertises forbidden bits and skip it if so.

A convenient helper to add:

```cpp
static bool HasForbiddenServiceBits(ServiceFlags services)
{
    return (services & FORBIDDEN_SERVICE_BITS) != 0;
}
```

In the `addr`/`addrv2` processing path, for each `CAddress` received:

```cpp
if (m_options.block_unknown_services && HasForbiddenServiceBits(addr.nServices)) {
    LogPrint(BCLog::NET, "ignoring address %s from peer=%d: contains forbidden service bits (%08x)\n", addr.ToStringAddrPort(), pfrom.GetId(), addr.nServices);
    continue;
}
```

This prevents:
- The node from storing the address in its `addrman` database.
- The node from gossiping the address to other peers.
- Future outbound connection attempts to that address from this node.

If `addrman` already contains addresses with forbidden bits from before the option was enabled, the existing outbound connection logic will disconnect them during the version handshake when their service bits are discovered.

### 6. Add a test

Add functional or unit tests that:
1. Start a node with `-blockunknownservices`.
2. Connect a mininode that advertises bit 24 (or any bit 24-31).
3. Assert that the connection is dropped immediately after the version handshake.
4. Connect a second, honest mininode. Send an `addr`/`addrv2` message from the second mininode containing a third-party address with bit 24 set.
5. Assert that this third-party address does **not** appear in `getnodeaddresses` RPC results (confirming `addrman` exclusion) and is **not** relayed to the second mininode (confirming gossip suppression).
6. Connect a whitelisted/noban peer advertising bit 24 and assert that the connection is **not** dropped (whitelist bypass confirmation).

Also add a test that:
- Starts a node **without** the option (default behavior).
- Connects a mininode advertising bit 24.
- Asserts that the connection is **not** dropped, to ensure the default behavior is unchanged.

This ensures the option works as intended, respects whitelists, and does not regress.

## Files Likely to Change

| File | Change |
|---|---|
| `src/init.cpp` | Register `-blockunknownservices` argument; pass value into `PeerManager` options |
| `src/net_processing.h` / `src/net_processing.cpp` | Add option field, disconnect logic, and addr-relay filtering |
| `src/test/...` or `test/functional/...` | Add test coverage for connection drop and addr filtering |

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Honest nodes using bits 24-31 in the future are disconnected | The option is **off by default**. Operators who enable it accept the risk. The flag name and help text should make this clear. |
| Fork nodes can simply stop advertising bits 24-31 and reconnect | This is expected. The service-bit check is a hygiene layer, not a consensus fix. The real protection remains `bad-unknown-witness-version`. |
| Unknown service bits are allowed by Bitcoin Core design | We are not changing the protocol. We are offering an opt-in local policy that targets a specific reserved bit range. |
| Test failures on unusual service-bit combinations | The mask must exactly cover bits 24-31. Tests should cover bits 0-23 allowed, bits 24-31 rejected. |

## Suggested Rollout

1. Implement the plan as a single focused PR.
2. Run existing P2P functional tests.
3. Add functional tests for: connection drop, addr relay filtering, and default behavior unchanged.
4. Document the flag in release notes.
5. Do **not** change defaults; keep it opt-in.

## Relationship to Existing Defenses

This plan complements, but does not replace, the existing 28.4.0 defenses:

- **`bad-unknown-witness-version`** in `ContextualCheckBlock`: the primary consensus-level defense.
- **Mandatory script flags** in mempool policy: prevents relay of non-standard scripts.
- **`-blockunknownservices` (proposed)**: an optional P2P hygiene filter.

## Notes

- The observed fork peer (`/Blackcoin:30.1.1/`) also supports `P2P_V2`, which is a normal feature. Only the `UNKNOWN[2^24]` bit is suspicious.
- Bits 24-31 are reserved in Bitcoin for temporary experiments. The QQfork project appears to be using bit 24 as a private coordination signal.
- Because service bits are unauthenticated advertisements, any peer can set or clear them. Therefore, P2P-level filtering cannot provide strong security guarantees.
- Inbound and outbound filtering are both required: a fork node can initiate a connection to us, or we can discover and initiate a connection to a fork node via addr relay.

## Design Decisions

| Decision | Resolution |
|---|---|
| Where to define `FORBIDDEN_SERVICE_BITS` | Local to `net_processing.cpp` (not `protocol.h`) |
| Connection types to filter | All types — inbound, outbound, full-relay, and block-relay |
| `getpeerinfo` disconnect reason | Not needed; log is sufficient |
| Evict existing `addrman` entries on startup | No — only filter new `addr`/`addrv2` messages |
| Log level | `LogPrint(BCLog::NET, ...)` — gated behind `-debug=net` |
| Forbidden bit range | Hardcoded to bits 24-31 |
