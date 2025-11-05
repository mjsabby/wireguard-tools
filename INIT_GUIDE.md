# Using WireGuard Tools as PID 1 Init

This guide shows how to use the example init program for a minimal WireGuard-based system.

## Quick Start

### 1. Build the Static Init Binary

```bash
cd examples
make wireguard-init-static
```

This creates a fully static binary (~1.1MB) with zero runtime dependencies.

### 2. Create Your WireGuard Config

Create `wireguard.conf`:

```ini
[Interface]
PrivateKey = <your-private-key>
ListenPort = 51820

[Peer]
PublicKey = <peer-public-key>
AllowedIPs = 10.100.0.2/32
Endpoint = 203.0.113.42:51820
```

Generate keys:
```bash
wg genkey | tee privatekey | wg pubkey > publickey
```

### 3. Build Minimal Initramfs

```bash
#!/bin/bash

INITRAMFS_DIR="initramfs"
rm -rf $INITRAMFS_DIR
mkdir -p $INITRAMFS_DIR/{bin,dev,proc,sys}

# Copy the init binary
cp examples/wireguard-init-static $INITRAMFS_DIR/init

# Copy config
cp wireguard.conf $INITRAMFS_DIR/

# Create device nodes
cd $INITRAMFS_DIR/dev
sudo mknod -m 622 console c 5 1
sudo mknod -m 666 null c 1 3
sudo mknod -m 666 zero c 1 5
sudo mknod -m 666 tty c 5 0
cd ../..

# Pack it up
cd $INITRAMFS_DIR
find . | cpio -H newc -o | gzip > ../initramfs.cpio.gz
cd ..
```

### 4. Boot Your System

Add to kernel command line:
```
init=/init console=ttyS0
```

Or configure your bootloader:

**GRUB (`/boot/grub/grub.cfg`):**
```
menuentry 'WireGuard System' {
    linux /vmlinuz init=/init console=ttyS0
    initrd /initramfs.cpio.gz
}
```

**Syslinux (`syslinux.cfg`):**
```
LABEL wireguard
    KERNEL vmlinuz
    APPEND init=/init console=ttyS0
    INITRD initramfs.cpio.gz
```

### 5. Access Statistics

Once booted:
```bash
curl http://<your-server-ip>:8080
```

## Architecture Overview

```
┌─────────────────────────────────────┐
│   Kernel (with WireGuard support)   │
│         CONFIG_WIREGUARD=y          │
└──────────────┬──────────────────────┘
               │
               ├─ Netlink/IPC
               │
┌──────────────▼──────────────────────┐
│     wireguard-init (PID 1)          │
│                                     │
│  ┌───────────────────────────────┐ │
│  │  1. Network Setup             │ │
│  │     - eth0 up                 │ │
│  │     - Assign static IP        │ │
│  └───────────────────────────────┘ │
│                                     │
│  ┌───────────────────────────────┐ │
│  │  2. WireGuard Setup           │ │
│  │     - Create wg0 interface    │ │
│  │     - Parse config file       │ │
│  │     - Apply via ipc_set_device│ │
│  └───────────────────────────────┘ │
│                                     │
│  ┌───────────────────────────────┐ │
│  │  3. HTTP Stats Server         │ │
│  │     - Listen on port 8080     │ │
│  │     - Query via ipc_get_device│ │
│  │     - Return formatted stats  │ │
│  └───────────────────────────────┘ │
│                                     │
│  ┌───────────────────────────────┐ │
│  │  4. Zombie Reaper             │ │
│  │     - Handle SIGCHLD          │ │
│  │     - waitpid(-1, ..., WNOHANG)│ │
│  └───────────────────────────────┘ │
└─────────────────────────────────────┘
```

## Configuration Customization

Edit `examples/wireguard-init.c` to customize:

### Change Network Settings

```c
static int setup_network(void)
{
    // Change interface
    system("ip link set ens33 up");

    // Change IP/netmask
    system("ip addr add 192.168.1.100/24 dev ens33");

    // Add default gateway
    system("ip route add default via 192.168.1.1");

    return 0;
}
```

### Change WireGuard Tunnel IP

```c
static int setup_wireguard(const char *config_path)
{
    // ... existing code ...

    // Change tunnel IP
    system("ip addr add 10.200.0.1/24 dev wg0");

    return ret;
}
```

### Change HTTP Port or Listen Address

```c
int main(int argc, char *argv[])
{
    int http_port = 9090;  // Custom port

    // ... or modify run_http_server() to bind specific IP ...
}
```

### Use Different Config Path

```c
int main(int argc, char *argv[])
{
    const char *wg_config = "/etc/wireguard/wg0.conf";
    // ...
}
```

## API Reference

The init program uses these wireguard-tools APIs:

### Configuration API

```c
#include "config.h"

struct config_ctx ctx;
config_read_init(&ctx, false);  // Initialize parser
config_read_line(&ctx, line);   // Parse each line
device = config_read_finish(&ctx); // Finalize
```

### IPC API

```c
#include "ipc.h"

// Apply configuration
ipc_set_device(device);

// Get current stats
struct wgdevice *dev;
ipc_get_device(&dev, "wg0");

// Access peer stats
for_each_wgpeer(dev, peer) {
    peer->rx_bytes;
    peer->tx_bytes;
    peer->last_handshake_time;
}

free_wgdevice(dev);
```

### Data Structures

```c
#include "containers.h"

struct wgdevice {
    char name[IFNAMSIZ];
    uint32_t flags;
    uint8_t public_key[WG_KEY_LEN];
    uint8_t private_key[WG_KEY_LEN];
    uint16_t listen_port;
    uint32_t fwmark;
    struct wgpeer *first_peer;
};

struct wgpeer {
    uint32_t flags;
    uint8_t public_key[WG_KEY_LEN];
    struct sockaddr endpoint;
    struct timespec64 last_handshake_time;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    struct wgallowedip *first_allowedip;
    struct wgpeer *next_peer;
};
```

## Building Without External Commands

The example uses `system("ip link ...")` for simplicity. For truly minimal systems without busybox/iproute2, use direct syscalls:

### Bring Up Interface (No `ip` command)

```c
#include <sys/ioctl.h>
#include <net/if.h>

int bring_up_interface(const char *ifname)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    // Get current flags
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        perror("SIOCGIFFLAGS");
        close(sock);
        return -1;
    }

    // Set UP flag
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        perror("SIOCSIFFLAGS");
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}
```

### Assign IP Address (No `ip` command)

```c
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>

int assign_ip_address(const char *ifname, const char *ip, const char *netmask)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    struct sockaddr_in *addr;

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    // Set IP address
    addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr->sin_addr);

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        perror("SIOCSIFADDR");
        close(sock);
        return -1;
    }

    // Set netmask
    addr = (struct sockaddr_in *)&ifr.ifr_netmask;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, netmask, &addr->sin_addr);

    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) {
        perror("SIOCSIFNETMASK");
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}
```

## Minimal Kernel Config

Required kernel options:

```
CONFIG_WIREGUARD=y
CONFIG_NET=y
CONFIG_INET=y
CONFIG_NETDEVICES=y
CONFIG_ETHERNET=y
CONFIG_E1000=y  # Or your ethernet driver
CONFIG_CRYPTO=y
CONFIG_CRYPTO_CHACHA20POLY1305=y
CONFIG_CRYPTO_CURVE25519=y
```

## Testing in QEMU

```bash
#!/bin/bash

# Build everything
cd examples
make clean
make wireguard-init-static

# Create minimal initramfs
./build-initramfs.sh

# Run in QEMU
qemu-system-x86_64 \
    -kernel /boot/vmlinuz-$(uname -r) \
    -initrd initramfs.cpio.gz \
    -append "console=ttyS0 init=/init" \
    -nographic \
    -m 512M \
    -netdev user,id=net0,hostfwd=tcp::8080-:8080 \
    -device e1000,netdev=net0

# Test from host
curl http://localhost:8080
```

## Production Deployment

### 1. Security Hardening

- Run HTTP server on localhost only (bind to 127.0.0.1)
- Add iptables rules to restrict access
- Consider adding authentication

### 2. Logging

Add to init:
```c
// Open kernel log
int kmsg = open("/dev/kmsg", O_WRONLY);
dprintf(kmsg, "<6>wireguard-init: Starting\n");
```

### 3. Health Monitoring

Add endpoint:
```c
if (strstr(request, "GET /health")) {
    response = "HTTP/1.0 200 OK\r\n\r\nOK\n";
    write(client_fd, response, strlen(response));
}
```

### 4. Graceful Shutdown

```c
static volatile sig_atomic_t shutdown_requested = 0;

void sigterm_handler(int sig) {
    shutdown_requested = 1;
}

// In main:
signal(SIGTERM, sigterm_handler);

// In server loop:
if (shutdown_requested) {
    // Clean shutdown
    system("ip link del wg0");
    break;
}
```

## Troubleshooting

### Init panics immediately

- Check kernel supports `init=` parameter
- Verify init binary is executable
- Check for missing `/dev/console`

### Network doesn't come up

- Check ethernet driver is compiled in (not module)
- Verify interface name (might be `eth1`, `ens33`, etc.)
- Check dmesg: `dmesg | grep eth`

### WireGuard fails to start

- Verify kernel has `CONFIG_WIREGUARD=y`
- Check config file syntax
- Look for errors in kernel log

### Can't reach HTTP server

- Verify IP address with `ip addr`
- Check firewall rules
- Test locally: `wget -O- http://127.0.0.1:8080`

## Additional Resources

- WireGuard kernel documentation: https://www.wireguard.com/
- Linux netdevice(7) for ioctl interface
- initramfs documentation: Documentation/filesystems/ramfs-rootfs-initramfs.txt
- PID 1 requirements: man 1 init

## License

GPL-2.0 OR MIT (matching wireguard-tools)
