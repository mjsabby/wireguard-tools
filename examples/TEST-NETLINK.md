# Netlink Interface Creation Test

This directory contains a standalone test program to verify that the netlink code for creating WireGuard interfaces works correctly.

## What It Does

The test program `test-netlink-create` demonstrates how to create a WireGuard interface using **pure netlink syscalls**, without relying on external tools like `ip` or `busybox`.

This is equivalent to running:
```bash
ip link add dev wg0 type wireguard
```

But implemented using direct kernel netlink communication.

## Building

```bash
cd examples/
make test-netlink-create
```

Or compile manually:
```bash
gcc -o test-netlink-create test-netlink-create.c
```

## Running

**Must be run as root** (creating network interfaces requires CAP_NET_ADMIN):

```bash
sudo ./test-netlink-create
```

You can specify a custom interface name:
```bash
sudo ./test-netlink-create wg1
```

## Expected Output

```
==============================================
Netlink WireGuard Interface Creation Test
==============================================

Creating WireGuard interface 'wg0' using netlink...
  ✓ Netlink socket created (fd=3)
  ✓ Netlink socket bound
  ✓ Netlink message header initialized
    - Type: RTM_NEWLINK (create new link)
    - Flags: REQUEST | CREATE | EXCL | ACK
  ✓ Added IFLA_IFNAME attribute: 'wg0'
  ✓ Added IFLA_LINKINFO attribute
  ✓ Added IFLA_INFO_KIND attribute: 'wireguard'
  ✓ Final netlink message size: 60 bytes

Sending netlink message to kernel...
  ✓ Message sent

Waiting for kernel acknowledgment...
  ✓ Received response (36 bytes)

✓ SUCCESS: WireGuard interface 'wg0' created!

==============================================
Verification
==============================================
To verify the interface was created, run:
  ip link show wg0

To see more details:
  ip -d link show wg0

To clean up:
  ip link del wg0
==============================================
```

## Verification

After running the test, verify the interface exists:

```bash
ip link show wg0
```

Expected output:
```
5: wg0: <POINTOPOINT,NOARP> mtu 1420 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/none
```

See detailed info:
```bash
ip -d link show wg0
```

Expected to show:
```
5: wg0: <POINTOPOINT,NOARP> mtu 1420 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/none promiscuity 0 minmtu 0 maxmtu 0
    wireguard numtxqueues 1 numrxqueues 1 gso_max_size 65536 gso_max_segs 65535
```

## Cleanup

Remove the test interface:
```bash
sudo ip link del wg0
```

## How It Works

The program demonstrates the complete netlink protocol for interface creation:

1. **Create netlink socket**: `socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)`
2. **Build RTM_NEWLINK message** with attributes:
   - `IFLA_IFNAME` = "wg0" (interface name)
   - `IFLA_LINKINFO` (nested):
     - `IFLA_INFO_KIND` = "wireguard" (interface type)
3. **Send message to kernel**: `send()` on netlink socket
4. **Receive acknowledgment**: `recv()` and parse `NLMSG_ERROR`

## Netlink Message Structure

```
┌─────────────────────────────────┐
│ nlmsghdr                         │
│   nlmsg_type = RTM_NEWLINK      │ ← "Create new link"
│   nlmsg_flags = CREATE | EXCL   │ ← "Create, fail if exists"
├─────────────────────────────────┤
│ ifinfomsg                        │
│   ifi_family = AF_UNSPEC        │
├─────────────────────────────────┤
│ rtattr                           │
│   rta_type = IFLA_IFNAME        │ ← Interface name attribute
│   rta_data = "wg0"              │
├─────────────────────────────────┤
│ rtattr (nested)                  │
│   rta_type = IFLA_LINKINFO      │ ← Link type info
│   ┌───────────────────────────┐ │
│   │ rtattr                    │ │
│   │   rta_type = INFO_KIND   │ │ ← Type of interface
│   │   rta_data = "wireguard" │ │
│   └───────────────────────────┘ │
└─────────────────────────────────┘
```

## Why This Test?

This isolates and verifies the most complex part of the PID 1 init program: the netlink interface creation code. By testing it standalone, you can:

1. **Verify it works** without needing a full init environment
2. **Understand the netlink protocol** with detailed output
3. **Debug issues** more easily than in the full init program
4. **Compare with `ip` command** to ensure equivalence

## Requirements

- Linux kernel with WireGuard support (kernel 5.6+ or wireguard module)
- Root privileges (CAP_NET_ADMIN)
- No external dependencies (pure libc)

## Related Files

- `wireguard-init.c` - Full PID 1 init program (uses this same netlink code)
- `examples/Makefile` - Build system

## Learning Resources

- `man 7 netlink` - Netlink protocol overview
- `man 7 rtnetlink` - Routing/link netlink API
- `/usr/include/linux/rtnetlink.h` - Netlink message types
- `/usr/include/linux/if_link.h` - Interface link attributes
- iproute2 source code - See how `ip link add` works internally
