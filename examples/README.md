# WireGuard Init Example - Pure Syscalls, No External Tools

This directory contains an example custom init program (PID 1) that uses **ONLY syscalls and ioctl**.

**Zero external tool dependencies:**
- ✅ No busybox
- ✅ No iproute2 (`ip` command)
- ✅ No other userspace tools
- ✅ Just this single static binary as PID 1

**Features:**
1. Brings up network interfaces (pure ioctl)
2. Assigns IP addresses (pure ioctl)
3. Creates WireGuard interface (netlink)
4. Configures WireGuard from config file
5. Runs HTTP server to expose statistics
6. Handles PID 1 responsibilities (zombie reaping)

## Building

### Static Build (Recommended for Minimal Systems)

```bash
cd examples
make wireguard-init-static
```

This creates a fully static binary with no runtime dependencies.

### Dynamic Build

```bash
cd examples
make wireguard-init
```

## Usage

### 1. Prepare Your System

The init program expects:
- A WireGuard configuration file at `/wireguard.conf` (in initramfs)
- The kernel to support WireGuard (`CONFIG_WIREGUARD=y`)
- **That's it!** No other userspace tools needed

### 2. Configuration File Format

Create `/wireguard.conf` in your initramfs:

```ini
[Interface]
PrivateKey = YourPrivateKeyHere==
ListenPort = 51820

[Peer]
PublicKey = PeerPublicKeyHere==
AllowedIPs = 10.100.0.2/32
Endpoint = 203.0.113.42:51820
```

### 3. Boot Configuration

Set your init parameter in bootloader (e.g., GRUB):

```
linux /vmlinuz ... init=/wireguard-init
```

Or in your kernel command line:
```
init=/wireguard-init
```

### 4. Accessing Statistics

Once booted, access stats via HTTP:

```bash
curl http://10.0.0.1:8080
```

Output:
```
WireGuard Statistics (wg0)
==========================

Public Key: YourPublicKey==
Listen Port: 51820

Peers:

  Peer: PeerPublicKey==
    Endpoint: 203.0.113.42:51820
    Last Handshake: 45 seconds ago
    Transfer: 15.32 MiB received, 8.91 MiB sent
    Raw Transfer: 16058368 bytes received, 9346048 bytes sent
```

## Customization

All configuration is at the top of `wireguard-init.c` via #define directives:

```c
#define ETH_INTERFACE      "eth0"         /* Your ethernet interface */
#define ETH_IP_ADDRESS     "10.0.0.1"     /* Static IP for ethernet */
#define ETH_NETMASK        "255.255.255.0"
#define WG_INTERFACE       "wg0"          /* WireGuard interface name */
#define WG_IP_ADDRESS      "10.100.0.1"   /* WireGuard tunnel IP */
#define WG_NETMASK         "255.255.255.0"
#define WG_CONFIG_PATH     "/wireguard.conf"
#define HTTP_PORT          8080
```

Simply edit these values and recompile. No need to modify function code.

## Architecture

### Components

1. **Low-Level Network Functions** (pure syscalls):
   - `bring_interface_up()` - Uses ioctl SIOCGIFFLAGS/SIOCSIFFLAGS
   - `set_interface_address()` - Uses ioctl SIOCSIFADDR/SIOCSIFNETMASK
   - `create_wireguard_interface()` - Uses netlink RTM_NEWLINK

2. **Network Setup** (`setup_network()`):
   - Brings up loopback via ioctl
   - Brings up ethernet via ioctl
   - Assigns static IP via ioctl
   - **No external tools** - pure kernel syscalls

3. **WireGuard Setup** (`setup_wireguard()`):
   - Creates WireGuard interface via netlink
   - Parses config file using wireguard-tools config parser
   - Applies configuration via wireguard-tools IPC
   - Brings interface up via ioctl
   - Assigns IP address via ioctl

4. **HTTP Server** (`run_http_server()`):
   - Simple HTTP/1.0 server
   - Queries WireGuard stats via `ipc_get_device()`
   - Returns text/plain response with peer statistics

5. **Signal Handling** (`setup_signals()`):
   - Handles SIGCHLD to reap zombie processes (critical for PID 1)
   - Ignores SIGPIPE to prevent crashes

### PID 1 Responsibilities

As init (PID 1), this program:
- **Must never exit** (would cause kernel panic)
- **Must reap zombie children** (via SIGCHLD handler)
- **Must be robust** (handles errors gracefully)

### API Usage

The program uses wireguard-tools internal APIs:

```c
/* Configuration parsing */
#include "config.h"
config_read_init(&ctx, false);
config_read_line(&ctx, line);
device = config_read_finish(&ctx);

/* Device configuration */
#include "ipc.h"
ipc_set_device(device);
ipc_get_device(&device, "wg0");

/* Key encoding */
#include "encoding.h"
key_to_base64(buf, key);
```

## Testing

### In a VM

```bash
# Build initramfs with your init
mkdir -p initramfs/bin
cp wireguard-init initramfs/init
chmod +x initramfs/init
cp wireguard.conf initramfs/

# Create initramfs
cd initramfs
find . | cpio -H newc -o | gzip > ../initramfs.cpio.gz

# Boot with QEMU
qemu-system-x86_64 \
  -kernel /boot/vmlinuz \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0 init=/init" \
  -nographic
```

### Checking Stats

From another machine on the network:

```bash
curl http://<your-vm-ip>:8080
```

## Production Considerations

1. **Security**:
   - The HTTP server has no authentication
   - Consider adding basic auth or restricting to localhost
   - Consider HTTPS if exposing externally

2. **Logging**:
   - Add syslog support
   - Log to `/dev/kmsg` for kernel log integration

3. **Error Handling**:
   - More graceful degradation
   - Retry logic for transient failures

4. **Monitoring**:
   - Health check endpoint (`/health`)
   - Prometheus metrics format option

5. **Shutdown**:
   - Handle SIGTERM/SIGINT for clean shutdown
   - Properly tear down WireGuard interface

## Troubleshooting

### Interface doesn't come up

Check kernel messages:
```bash
dmesg | grep -i wireguard
```

### Can't access HTTP server

Check if listening:
```bash
netstat -tlnp | grep 8080
```

### No network connectivity

Verify interface is up:
```bash
ip link show
ip addr show
```

## License

GPL-2.0 OR MIT (matching wireguard-tools)
