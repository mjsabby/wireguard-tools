# Testing WireGuard Server with QEMU

This guide shows how to test your WireGuard server using a QEMU VM as a peer.

## Test Setup

```
Host Machine (10.0.0.1)
  ├── WireGuard Server (wg0: 10.100.0.1)
  └── QEMU VM (NAT network)
      └── WireGuard Client (wg0: 10.100.0.2)
          └── Can reach LAN 10.0.0.0/24 through tunnel
```

## Prerequisites

```bash
# Install QEMU (if not already installed)
sudo apt-get install qemu-system-x86_64 qemu-utils

# Download a minimal Linux image (e.g., Alpine Linux - small and fast)
wget https://dl-cdn.alpinelinux.org/alpine/v3.18/releases/x86_64/alpine-virt-3.18.4-x86_64.iso

# Or use any Linux ISO you prefer (Ubuntu, Debian, etc.)
```

## Step 1: Create VM Disk

```bash
# Create a 2GB disk for the VM
qemu-img create -f qcow2 wireguard-test-vm.qcow2 2G
```

## Step 2: Install OS in VM

```bash
# Boot VM with ISO for installation
qemu-system-x86_64 \
  -m 512 \
  -smp 2 \
  -hda wireguard-test-vm.qcow2 \
  -cdrom alpine-virt-3.18.4-x86_64.iso \
  -boot d \
  -net nic \
  -net user,hostfwd=tcp::2222-:22 \
  -nographic

# Inside VM, install Alpine (or your chosen OS)
# For Alpine:
# - Login as root (no password)
# - Run: setup-alpine
# - Follow prompts (use defaults mostly)
# - When done: poweroff
```

## Step 3: Boot VM with Network Access

```bash
# Start VM with NAT networking
# Port forwarding: Host 2222 → VM 22 (SSH)
qemu-system-x86_64 \
  -m 512 \
  -smp 2 \
  -hda wireguard-test-vm.qcow2 \
  -net nic \
  -net user,hostfwd=tcp::2222-:22 \
  -nographic

# Or with more explicit networking:
qemu-system-x86_64 \
  -m 512 \
  -smp 2 \
  -hda wireguard-test-vm.qcow2 \
  -device e1000,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -nographic
```

## Step 4: Install WireGuard in VM

```bash
# SSH into VM from host
ssh -p 2222 root@localhost

# Inside VM - Install WireGuard
# For Alpine:
apk add wireguard-tools

# For Ubuntu/Debian:
apt-get update && apt-get install -y wireguard-tools

# For other distros, adjust accordingly
```

## Step 5: Generate WireGuard Keys

### On VM (client):

```bash
# Generate client keys
wg genkey | tee client-private.key | wg pubkey > client-public.key

# Show keys
echo "Client Private Key:"
cat client-private.key
echo "Client Public Key:"
cat client-public.key
```

### On Host (server):

```bash
# Generate server keys (if not already done)
wg genkey | tee server-private.key | wg pubkey > server-public.key

echo "Server Private Key:"
cat server-private.key
echo "Server Public Key:"
cat server-public.key
```

## Step 6: Configure WireGuard Server

On your host machine, create `/wireguard.conf`:

```ini
[Interface]
PrivateKey = <server-private-key-from-above>
ListenPort = 51820

[Peer]
# QEMU VM Client
PublicKey = <client-public-key-from-above>
AllowedIPs = 10.100.0.2/32
# Optional: Add more networks the client can access
# AllowedIPs = 10.100.0.2/32, 10.0.0.0/24
```

**Important**: The server's `AllowedIPs` defines what traffic FROM the peer is allowed.
- `10.100.0.2/32` - Only allows peer's WireGuard IP
- Add `10.0.0.0/24` if you want peer to send traffic with LAN IPs (for routing)

## Step 7: Configure WireGuard Client (VM)

In the VM, create `/etc/wireguard/wg0.conf`:

```ini
[Interface]
PrivateKey = <client-private-key-from-step-5>
Address = 10.100.0.2/32

[Peer]
# Your WireGuard Server
PublicKey = <server-public-key-from-step-5>
# Use gateway IP from QEMU NAT (usually 10.0.2.2)
Endpoint = 10.0.2.2:51820
# Route LAN traffic through tunnel
AllowedIPs = 10.0.0.0/24
PersistentKeepalive = 25
```

**Note**: In QEMU user networking (NAT), the host appears as `10.0.2.2` from the VM's perspective.

## Step 8: Start WireGuard

### On Host (Server):

```bash
# Using your init program:
sudo ./wireguard-init-static

# Or manually:
sudo ip link add dev wg0 type wireguard
sudo ip addr add 10.100.0.1/24 dev wg0
sudo wg setconf wg0 /wireguard.conf
sudo ip link set wg0 up
```

### On VM (Client):

```bash
# Start WireGuard interface
sudo wg-quick up wg0

# Check status
sudo wg show
```

## Step 9: Test Connectivity

### From VM, test WireGuard tunnel:

```bash
# Ping WireGuard server
ping 10.100.0.1
# Should work!

# Check WireGuard status
wg show
# Should show handshake and data transfer

# Ping LAN gateway (your router)
ping 10.0.0.1
# Should work if routing is correct!

# Try to reach other LAN devices
ping 10.0.0.5
# (if you have other devices on LAN)
```

### From Host, verify WireGuard:

```bash
# Check WireGuard status
sudo wg show wg0

# Should show:
# - peer's public key
# - endpoint (VM's NAT IP)
# - latest handshake
# - transfer stats

# Ping the VM through tunnel
ping 10.100.0.2
```

## Step 10: Verify Routing

### On VM, check routing table:

```bash
ip route show

# Should see:
# 10.0.0.0/24 dev wg0 scope link
# (added by AllowedIPs)
```

### On Host, check routes:

```bash
ip route show

# Should see:
# 10.100.0.2 dev wg0 scope link
# (added by your init program's add_routes_for_peers)
```

## Troubleshooting

### VM can't reach server:

```bash
# On VM, check if WireGuard is running
wg show

# Check if packets are going out
sudo tcpdump -i eth0 port 51820

# On host, check if server is listening
sudo ss -ulnp | grep 51820
```

### Tunnel works but can't reach LAN:

```bash
# On host, verify IP forwarding is enabled
cat /proc/sys/net/ipv4/ip_forward
# Should be: 1

# Check if routes exist
ip route | grep wg0

# On VM, verify AllowedIPs includes LAN
wg show wg0 allowed-ips
```

### Can't ping from LAN back to VM:

This is expected! Your LAN router (ER605) doesn't know about 10.100.0.0/24 yet.
You need to add the static route on ER605 (as discussed earlier).

## Test Matrix

| From | To | Should Work? | Why |
|------|-----|--------------|-----|
| VM | 10.100.0.1 (WG server) | ✅ Yes | Direct tunnel |
| VM | 10.0.0.1 (LAN gateway) | ✅ Yes | Routed through tunnel |
| Host | 10.100.0.2 (VM) | ✅ Yes | Direct tunnel |
| LAN device | 10.100.0.2 (VM) | ⚠️ Need ER605 route | Requires static route |

## QEMU Networking Notes

### QEMU User Mode (NAT):
- VM gets IP in `10.0.2.0/24` range (usually `10.0.2.15`)
- Host appears as `10.0.2.2` to VM
- Gateway: `10.0.2.2`
- DNS: `10.0.2.3`

### Port Forwarding:
```bash
# Forward host port to VM port
-net user,hostfwd=tcp::2222-:22  # SSH
-net user,hostfwd=tcp::8080-:80  # HTTP
```

## Cleanup

```bash
# On VM:
sudo wg-quick down wg0

# On Host:
sudo ip link del wg0

# Stop QEMU:
# Inside VM:
poweroff
```

## Advanced: TAP Networking (Bridge Mode)

If you want the VM on the actual LAN (not NAT):

```bash
# Create TAP interface
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
sudo ip link set tap0 master br0  # if you have a bridge

# Start QEMU with TAP
sudo qemu-system-x86_64 \
  -m 512 \
  -smp 2 \
  -hda wireguard-test-vm.qcow2 \
  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
  -device e1000,netdev=net0 \
  -nographic
```

This puts the VM directly on your LAN (gets IP from your router), but requires more setup.

## Summary

This QEMU setup lets you test:
1. ✅ WireGuard tunnel establishment
2. ✅ Peer-to-server communication
3. ✅ Routing through tunnel to LAN
4. ✅ Your init program's route creation
5. ✅ IP forwarding functionality

All without needing a real remote peer!
