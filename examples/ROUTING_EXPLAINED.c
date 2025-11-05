/* SPDX-License-Identifier: GPL-2.0 OR MIT
 * WireGuard Routing and Packet Flow Guide
 */

/*
 * ============================================================================
 * PACKET FLOW EXPLANATION
 * ============================================================================
 *
 * Setup:
 *   - eth0: 10.0.0.1/24 (LAN interface)
 *   - wg0:  10.100.0.1/24 (WireGuard tunnel)
 *   - ListenPort: 51820
 *   - Peer: 203.0.113.42:51820 (remote), AllowedIPs = 10.100.0.2/32
 *
 * ============================================================================
 * INBOUND FLOW (Peer connects to your server)
 * ============================================================================
 *
 * 1. Peer sends encrypted UDP packet:
 *    - Source: 203.0.113.42:51820
 *    - Destination: 10.0.0.1:51820 (your eth0 IP)
 *    - Payload: Encrypted WireGuard packet
 *
 * 2. Packet arrives at your eth0 → kernel UDP stack
 *    - Kernel sees destination port 51820
 *    - Delivers to WireGuard module
 *
 * 3. WireGuard processes packet:
 *    - Authenticates peer by public key
 *    - Decrypts payload
 *    - Reveals inner packet: 10.100.0.2 → 10.100.0.1:80 (for example)
 *    - Checks: Is 10.100.0.2 in peer's AllowedIPs? YES
 *    - Updates peer endpoint to 203.0.113.42:51820 (roaming support)
 *
 * 4. WireGuard injects decrypted packet into wg0:
 *    - Acts like packet arrived on wg0 interface
 *    - Now kernel sees: 10.100.0.2 → 10.100.0.1:80
 *
 * 5. Kernel routing delivers to your HTTP server on port 80
 *
 * ============================================================================
 * OUTBOUND FLOW (Your server responds to peer)
 * ============================================================================
 *
 * 1. Your app sends response packet:
 *    - Source: 10.100.0.1:80
 *    - Destination: 10.100.0.2
 *
 * 2. Kernel consults routing table:
 *    Route: 10.100.0.0/24 dev wg0
 *    Decision: Send packet to wg0 interface
 *
 * 3. Packet arrives at wg0 → WireGuard
 *    - WireGuard checks: Which peer has 10.100.0.2 in AllowedIPs?
 *    - Finds peer with AllowedIPs = 10.100.0.2/32
 *    - Encrypts packet
 *
 * 4. WireGuard sends encrypted UDP packet out eth0:
 *    - Source: 10.0.0.1:51820 (your eth0)
 *    - Destination: 203.0.113.42:51820 (peer's endpoint)
 *    - Payload: Encrypted packet
 *
 * 5. Packet leaves via eth0, goes to LAN router, to internet, to peer
 *
 * ============================================================================
 * ROUTING TABLE REQUIREMENTS
 * ============================================================================
 *
 * Automatic Routes (created when you assign IP to interface):
 *
 *   10.0.0.0/24 dev eth0 scope link
 *   10.100.0.0/24 dev wg0 scope link
 *
 * These are sufficient for basic WireGuard operation!
 *
 * ============================================================================
 * WHEN DO YOU NEED ADDITIONAL ROUTES?
 * ============================================================================
 *
 * Scenario 1: Peer wants to access your LAN (10.0.0.0/24)
 *   - Peer needs: AllowedIPs = 10.0.0.0/24
 *   - You need: IP forwarding enabled
 *   - You DON'T need: NAT (if LAN router knows about 10.100.0.0/24)
 *
 * Scenario 2: Route internet through WireGuard
 *   - Peer needs: AllowedIPs = 0.0.0.0/0
 *   - You need: Default route on wg0 (ip route add default dev wg0)
 *   - You need: NAT (unless peer is on public IP)
 *
 * Scenario 3: Multiple subnets
 *   - Add explicit routes for each subnet
 *
 * ============================================================================
 * NAT vs ROUTING
 * ============================================================================
 *
 * WITHOUT NAT (Pure Routing):
 *   Requirements:
 *   1. IP forwarding enabled on WireGuard server
 *   2. LAN router knows: packets for 10.100.0.0/24 go to 10.0.0.1
 *   3. Peer AllowedIPs includes LAN subnet
 *
 *   Packet flow (peer → LAN host):
 *   Peer → WireGuard → [10.100.0.2 → 10.0.0.5] → eth0 → LAN host
 *   LAN host sees source: 10.100.0.2
 *
 *   Return packet:
 *   LAN host → router → [10.0.0.5 → 10.100.0.2] → eth0 → WireGuard → Peer
 *
 * WITH NAT (Masquerading):
 *   Requirements:
 *   1. IP forwarding enabled
 *   2. NAT rule: POSTROUTING on eth0, MASQUERADE
 *
 *   Packet flow (peer → LAN host):
 *   Peer → WireGuard → [10.100.0.2 → 10.0.0.5] → NAT → [10.0.0.1 → 10.0.0.5] → eth0 → LAN host
 *   LAN host sees source: 10.0.0.1 (your server)
 *
 *   Return packet:
 *   LAN host → [10.0.0.5 → 10.0.0.1] → NAT → [10.0.0.5 → 10.100.0.2] → WireGuard → Peer
 *
 * YOUR CASE (from question):
 *   "wireguard ip subnet gets routed to the ethernet nic"
 *   This means: Pure routing, NO NAT needed!
 *   Your LAN router has: route add 10.100.0.0/24 via 10.0.0.1
 *
 * ============================================================================
 * IP FORWARDING
 * ============================================================================
 *
 * CRITICAL: You MUST enable IP forwarding if packets need to cross interfaces
 *
 * Via sysctl:
 *   echo 1 > /proc/sys/net/ipv4/ip_forward
 *
 * Via syscall (in init program):
 *   int fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY);
 *   write(fd, "1\n", 2);
 *   close(fd);
 *
 * ============================================================================
 * EXAMPLE SCENARIOS
 * ============================================================================
 *
 * Scenario A: Simple VPN (peer-to-server only)
 *   Server config:
 *     [Interface]
 *     Address = 10.100.0.1/24
 *     ListenPort = 51820
 *
 *     [Peer]
 *     PublicKey = <peer-key>
 *     AllowedIPs = 10.100.0.2/32
 *
 *   Routing: AUTOMATIC (no extra routes needed)
 *   IP Forwarding: NOT NEEDED
 *   NAT: NOT NEEDED
 *
 *   Use case: Peer connects to services on 10.100.0.1
 *
 * Scenario B: Peer accesses entire LAN (no NAT)
 *   Server config:
 *     [Interface]
 *     Address = 10.100.0.1/24
 *     ListenPort = 51820
 *
 *     [Peer]
 *     PublicKey = <peer-key>
 *     AllowedIPs = 10.100.0.2/32, 10.0.0.0/24
 *
 *   Routing: AUTOMATIC (kernel knows eth0 → 10.0.0.0/24)
 *   IP Forwarding: REQUIRED
 *   NAT: NOT NEEDED (if LAN router has route)
 *   LAN Router: route add 10.100.0.0/24 via 10.0.0.1
 *
 *   Use case: Peer can access any host on 10.0.0.0/24
 *
 * Scenario C: Peer accesses LAN (with NAT)
 *   Server config:
 *     [Interface]
 *     Address = 10.100.0.1/24
 *     ListenPort = 51820
 *     PostUp = iptables -A FORWARD -i wg0 -j ACCEPT
 *     PostUp = iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
 *
 *     [Peer]
 *     PublicKey = <peer-key>
 *     AllowedIPs = 10.100.0.2/32
 *
 *   Routing: AUTOMATIC
 *   IP Forwarding: REQUIRED
 *   NAT: REQUIRED (via iptables)
 *   LAN Router: No changes needed!
 *
 *   Use case: Peer can access LAN, LAN thinks traffic comes from server
 *
 * ============================================================================
 * VERIFICATION COMMANDS (if you had tools)
 * ============================================================================
 *
 * Check routing table:
 *   ip route show
 *
 * Expected output:
 *   10.0.0.0/24 dev eth0 proto kernel scope link src 10.0.0.1
 *   10.100.0.0/24 dev wg0 proto kernel scope link src 10.100.0.1
 *
 * Check IP forwarding:
 *   cat /proc/sys/net/ipv4/ip_forward
 *   (should show: 1)
 *
 * Check WireGuard peers:
 *   wg show wg0
 *
 * Trace packet path:
 *   tcpdump -i eth0 udp port 51820  (see encrypted WireGuard packets)
 *   tcpdump -i wg0                   (see decrypted inner packets)
 *
 * ============================================================================
 * KERNEL ROUTING ALGORITHM
 * ============================================================================
 *
 * When kernel needs to send packet with destination X:
 *
 * 1. Check routing table (longest prefix match)
 * 2. Find most specific route that matches X
 * 3. Send packet to interface specified by route
 * 4. If no route found, send to default gateway
 *
 * Example:
 *   Destination: 10.100.0.2
 *   Routes:
 *     10.100.0.0/24 dev wg0
 *     10.0.0.0/24 dev eth0
 *   Match: 10.100.0.0/24 (longest prefix)
 *   Decision: Send to wg0
 *
 * ============================================================================
 * WHY WIREGUARD DOESN'T NEED ROUTES FOR INBOUND
 * ============================================================================
 *
 * INBOUND: Encrypted packet comes in on eth0:51820
 *   - WireGuard is a kernel module listening on port 51820
 *   - Kernel UDP stack delivers packet to WireGuard
 *   - WireGuard decrypts and injects into wg0
 *   - No routing needed for THIS step
 *
 * OUTBOUND: Response packet created by your app
 *   - Destination is peer's tunnel IP (10.100.0.2)
 *   - Kernel checks routing table
 *   - Finds: 10.100.0.0/24 dev wg0
 *   - Sends packet to wg0
 *   - WireGuard encrypts and sends out eth0
 *   - Routing IS needed for THIS step
 *
 * ============================================================================
 */
