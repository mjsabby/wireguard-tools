#!/bin/bash
# Quick setup script for QEMU WireGuard testing

set -e

echo "=========================================="
echo "QEMU WireGuard Test Environment Setup"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (needed for WireGuard setup)"
    exit 1
fi

WORK_DIR="$HOME/wg-qemu-test"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

echo "Working directory: $WORK_DIR"
echo ""

# Step 1: Generate keys if they don't exist
if [ ! -f server-private.key ]; then
    echo "Generating server keys..."
    wg genkey | tee server-private.key | wg pubkey > server-public.key
    chmod 600 server-private.key
fi

if [ ! -f client-private.key ]; then
    echo "Generating client keys..."
    wg genkey | tee client-private.key | wg pubkey > client-public.key
    chmod 600 client-private.key
fi

SERVER_PRIVATE=$(cat server-private.key)
SERVER_PUBLIC=$(cat server-public.key)
CLIENT_PRIVATE=$(cat client-private.key)
CLIENT_PUBLIC=$(cat client-public.key)

echo ""
echo "=========================================="
echo "Generated Keys"
echo "=========================================="
echo "Server Public Key: $SERVER_PUBLIC"
echo "Client Public Key: $CLIENT_PUBLIC"
echo ""

# Step 2: Create server config
echo "Creating server config: /wireguard.conf"
cat > /wireguard.conf << EOF
[Interface]
PrivateKey = $SERVER_PRIVATE
ListenPort = 51820

[Peer]
# QEMU VM Client
PublicKey = $CLIENT_PUBLIC
AllowedIPs = 10.100.0.2/32
EOF

echo "Server config created."
echo ""

# Step 3: Create client config for VM
echo "Creating client config: $WORK_DIR/client-wg0.conf"
cat > "$WORK_DIR/client-wg0.conf" << EOF
[Interface]
PrivateKey = $CLIENT_PRIVATE
Address = 10.100.0.2/32

[Peer]
# WireGuard Server (host machine)
PublicKey = $SERVER_PUBLIC
# QEMU NAT: host appears as 10.0.2.2 from VM
Endpoint = 10.0.2.2:51820
# Route LAN traffic through tunnel
AllowedIPs = 10.0.0.0/24
PersistentKeepalive = 25
EOF

echo "Client config created."
echo ""

# Step 4: Create helper script for VM
cat > "$WORK_DIR/vm-setup-wireguard.sh" << 'VMSCRIPT'
#!/bin/sh
# Run this inside the QEMU VM

echo "Installing WireGuard tools..."
apk add wireguard-tools || apt-get install -y wireguard-tools

echo "Creating WireGuard config..."
mkdir -p /etc/wireguard
cat > /etc/wireguard/wg0.conf
chmod 600 /etc/wireguard/wg0.conf

echo "Starting WireGuard..."
wg-quick up wg0

echo ""
echo "WireGuard Status:"
wg show

echo ""
echo "Testing connectivity..."
echo "Ping WireGuard server (10.100.0.1):"
ping -c 3 10.100.0.1

echo ""
echo "Ping LAN gateway (10.0.0.1):"
ping -c 3 10.0.0.1

echo ""
echo "Done! WireGuard is configured and running."
VMSCRIPT

chmod +x "$WORK_DIR/vm-setup-wireguard.sh"

echo "=========================================="
echo "Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo ""
echo "1. Download Alpine Linux ISO:"
echo "   wget https://dl-cdn.alpinelinux.org/alpine/v3.18/releases/x86_64/alpine-virt-3.18.4-x86_64.iso"
echo ""
echo "2. Create VM disk:"
echo "   qemu-img create -f qcow2 wireguard-test-vm.qcow2 2G"
echo ""
echo "3. Install Alpine in VM:"
echo "   qemu-system-x86_64 -m 512 -smp 2 -hda wireguard-test-vm.qcow2 \\"
echo "     -cdrom alpine-virt-3.18.4-x86_64.iso -boot d \\"
echo "     -net nic -net user,hostfwd=tcp::2222-:22 -nographic"
echo ""
echo "4. Boot VM after installation:"
echo "   qemu-system-x86_64 -m 512 -smp 2 -hda wireguard-test-vm.qcow2 \\"
echo "     -net nic -net user,hostfwd=tcp::2222-:22 -nographic"
echo ""
echo "5. Copy client config to VM:"
echo "   scp -P 2222 $WORK_DIR/client-wg0.conf root@localhost:/etc/wireguard/wg0.conf"
echo ""
echo "6. In VM, start WireGuard:"
echo "   wg-quick up wg0"
echo ""
echo "7. On host, start WireGuard server:"
echo "   # Use your wireguard-init program, or manually:"
echo "   ip link add dev wg0 type wireguard"
echo "   ip addr add 10.100.0.1/24 dev wg0"
echo "   wg setconf wg0 /wireguard.conf"
echo "   ip link set wg0 up"
echo "   # Add routes"
echo "   ip route add 10.100.0.2/32 dev wg0"
echo ""
echo "=========================================="
echo "Files created in: $WORK_DIR"
echo "  - server-private.key, server-public.key"
echo "  - client-private.key, client-public.key"
echo "  - client-wg0.conf (for VM)"
echo "  - /wireguard.conf (server config)"
echo "=========================================="
