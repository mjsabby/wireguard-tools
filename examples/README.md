# WireGuard Init Example

This directory contains an example custom init program (PID 1) that:

1. Brings up network interfaces
2. Configures WireGuard from a config file
3. Runs an HTTP server to expose statistics
4. Handles PID 1 responsibilities (zombie reaping)

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
- Basic network utilities (`ip` command) - or modify code to use ioctl directly

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

### Network Configuration

Edit `setup_network()` in `wireguard-init.c`:

```c
/* Change interface name */
ret = system("ip link set eth1 up");

/* Change IP address */
ret = system("ip addr add 192.168.1.100/24 dev eth1");

/* Add default route */
ret = system("ip route add default via 192.168.1.1");
```

### WireGuard Interface IP

Edit `setup_wireguard()`:

```c
/* Change WireGuard tunnel IP */
system("ip addr add 10.100.0.1/24 dev wg0");
```

### HTTP Port

Change the port in `main()`:

```c
int http_port = 8080;  /* Change to your preferred port */
```

### Config File Location

```c
const char *wg_config = "/wireguard.conf";  /* Change path */
```

## Architecture

### Components

1. **Network Setup** (`setup_network()`):
   - Brings up loopback
   - Configures Ethernet interface
   - Assigns static IP address

2. **WireGuard Setup** (`setup_wireguard()`):
   - Creates WireGuard interface
   - Parses config file using wireguard-tools config parser
   - Applies configuration via IPC
   - Brings interface up

3. **HTTP Server** (`run_http_server()`):
   - Simple HTTP/1.0 server
   - Queries WireGuard stats via `ipc_get_device()`
   - Returns text/plain response with peer statistics

4. **Signal Handling** (`setup_signals()`):
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

## Removing External Dependencies

The example uses `system()` to call `ip` commands. For a truly minimal system, replace these with direct ioctl/netlink calls:

```c
/* Instead of: system("ip link set eth0 up"); */
/* Use ioctl directly: */
int sock = socket(AF_INET, SOCK_DGRAM, 0);
struct ifreq ifr;
strcpy(ifr.ifr_name, "eth0");
ioctl(sock, SIOCGIFFLAGS, &ifr);
ifr.ifr_flags |= IFF_UP;
ioctl(sock, SIOCSIFFLAGS, &ifr);
close(sock);
```

See Linux netdevice(7) man page for details.

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
