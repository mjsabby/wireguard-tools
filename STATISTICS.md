# WireGuard Statistics and Monitoring

## Overview

WireGuard provides real-time per-peer statistics that can be queried on demand. All statistics are maintained by the kernel and retrieved via the `wg show` command.

## Available Statistics Per Peer

- **Public Key**: Unique identifier for each peer
- **Endpoint**: Current IP address and port (auto-updated when peer sends packets)
- **Last Handshake**: Timestamp of most recent successful handshake
- **Transfer**: Bytes received (rx_bytes) and transmitted (tx_bytes)
- **Allowed IPs**: Configured routing table for this peer
- **Persistent Keepalive**: Keepalive interval if configured

## Command Line Usage

### 1. Pretty Print (Human-Readable)

```bash
wg show wg0
```

Example output:
```
interface: wg0
  public key: AbCd1234...
  private key: (hidden)
  listening port: 51820

peer: XyZ9876...
  endpoint: 192.168.1.100:51820
  allowed ips: 10.0.0.2/32
  latest handshake: 1 minute, 23 seconds ago
  transfer: 15.32 MiB received, 8.91 MiB sent
```

### 2. Transfer Statistics Only

```bash
wg show wg0 transfer
```

Output format (tab-separated):
```
<peer_public_key>    <rx_bytes>    <tx_bytes>
```

Example:
```
XyZ9876abcdef...    16058368    9346048
AbC1234zyxwvu...    42949672    85899345
```

### 3. Latest Handshakes Only

```bash
wg show wg0 latest-handshakes
```

Output format (tab-separated):
```
<peer_public_key>    <unix_timestamp>
```

Example:
```
XyZ9876abcdef...    1699123456
AbC1234zyxwvu...    1699123789
```

### 4. Full Dump (All Data, Machine-Readable)

```bash
wg show wg0 dump
```

Output format (tab-separated, one line per peer):
```
<interface>  <private_key>  <public_key>  <listen_port>  <fwmark>
<interface>  <peer_pubkey>  <psk>  <endpoint>  <allowed_ips>  <last_handshake>  <rx_bytes>  <tx_bytes>  <keepalive>
```

Example:
```
wg0    (private_key)    (public_key)    51820    off
wg0    XyZ9876...    (none)    192.168.1.100:51820    10.0.0.2/32    1699123456    16058368    9346048    off
```

### 5. Other Specific Queries

```bash
# Show all peer public keys
wg show wg0 peers

# Show current endpoints
wg show wg0 endpoints

# Show allowed IPs
wg show wg0 allowed-ips

# Show all interfaces
wg show interfaces
```

## Programmatic Access (Parsing)

### Shell Script Example

```bash
#!/bin/bash

# Get transfer stats for all peers
wg show wg0 transfer | while IFS=$'\t' read -r pubkey rx tx; do
    echo "Peer: $pubkey"
    echo "  Downloaded: $rx bytes"
    echo "  Uploaded: $tx bytes"
    echo "  Total: $((rx + tx)) bytes"
    echo ""
done
```

### Monitoring Script Example

```bash
#!/bin/bash

# Monitor handshakes - detect disconnected peers
STALE_THRESHOLD=180  # 3 minutes
NOW=$(date +%s)

wg show wg0 latest-handshakes | while IFS=$'\t' read -r pubkey timestamp; do
    age=$((NOW - timestamp))
    if [ $age -gt $STALE_THRESHOLD ]; then
        echo "WARNING: Peer $pubkey is stale (last seen: ${age}s ago)"
    fi
done
```

### Bandwidth Monitoring

```bash
#!/bin/bash

# Calculate bandwidth per peer over time
INTERVAL=5  # seconds

# Get initial values
wg show wg0 transfer > /tmp/wg_stats_old

sleep $INTERVAL

# Get new values
wg show wg0 transfer > /tmp/wg_stats_new

# Calculate rates
paste /tmp/wg_stats_old /tmp/wg_stats_new | while read old_key old_rx old_tx new_key new_rx new_tx; do
    rx_rate=$(( (new_rx - old_rx) / INTERVAL ))
    tx_rate=$(( (new_tx - old_tx) / INTERVAL ))
    echo "Peer: $old_key"
    echo "  Download rate: $rx_rate bytes/sec"
    echo "  Upload rate: $tx_rate bytes/sec"
done
```

## C/Direct API Access

The `wg show` command uses the `ipc_get_device()` function from `ipc.h`:

```c
#include "ipc.h"
#include "containers.h"

struct wgdevice *device = NULL;
if (ipc_get_device(&device, "wg0") < 0) {
    perror("Unable to access interface");
    return 1;
}

// Iterate through peers
struct wgpeer *peer;
for_each_wgpeer(device, peer) {
    printf("Peer: ");
    // peer->public_key
    // peer->endpoint.addr
    // peer->last_handshake_time.tv_sec
    // peer->rx_bytes
    // peer->tx_bytes
}

free_wgdevice(device);
```

## Statistics Location

The statistics are maintained by:
- **Linux**: Kernel module via netlink
- **OpenBSD/FreeBSD**: Kernel via ioctl
- **Userspace**: Via socket to userspace daemon

Data is **real-time** and can be queried as frequently as needed without performance impact.

## Notes

- **Counter Wrapping**: The `rx_bytes` and `tx_bytes` are 64-bit counters (uint64_t), so they won't wrap for practical purposes
- **Handshake Timing**: Normal handshakes occur every 2-3 minutes during active communication
- **Endpoint Updates**: The endpoint field auto-updates when receiving authenticated packets (roaming support)
- **Zero Values**: If a peer has never connected, `last_handshake_time` will be 0
- **Atomic Reads**: Statistics are read atomically from the kernel, so they're always consistent

## Performance Considerations

- `wg show` queries are lightweight - safe to run frequently
- For high-frequency monitoring, use specific queries (`transfer`, `latest-handshakes`) rather than full dumps
- The kernel maintains these counters with minimal overhead
- No need to poll faster than once per second for most use cases
