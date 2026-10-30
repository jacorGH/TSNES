#!/usr/bin/env bash
# Touch-SNES relay on your own server (Ubuntu or Debian), using coturn.
# Usage:  sudo bash setup-coturn.sh https://YOU.github.io/YOUR-REPO/
# Prints a link that saves the relay in Touch-SNES when you open it on your phone.
set -e
APP_URL="${1:-}"
apt-get update -y
apt-get install -y coturn curl
PUBLIC_IP=$(curl -fsS https://api.ipify.org)
PRIVATE_IP=$(hostname -I | awk '{print $1}')
TURN_USER=tsnes
TURN_PASS=$(head -c 32 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 24)
cat > /etc/turnserver.conf <<CONF
listening-port=3478
fingerprint
lt-cred-mech
user=${TURN_USER}:${TURN_PASS}
realm=tsnes
external-ip=${PUBLIC_IP}/${PRIVATE_IP}
min-port=49160
max-port=49260
no-cli
no-multicast-peers
denied-peer-ip=10.0.0.0-10.255.255.255
denied-peer-ip=172.16.0.0-172.31.255.255
denied-peer-ip=192.168.0.0-192.168.255.255
denied-peer-ip=169.254.0.0-169.254.255.255
CONF
[ -f /etc/default/coturn ] && sed -i 's/^#\?TURNSERVER_ENABLED=.*/TURNSERVER_ENABLED=1/' /etc/default/coturn
# Open the relay's ports on this machine (cloud images often ship with strict iptables rules).
iptables -I INPUT -p udp --dport 3478 -j ACCEPT
iptables -I INPUT -p tcp --dport 3478 -j ACCEPT
iptables -I INPUT -p udp --dport 49160:49260 -j ACCEPT
command -v netfilter-persistent >/dev/null && netfilter-persistent save || true
systemctl enable coturn
systemctl restart coturn
CFG=$(printf '{"mode":"own","turn":"turn:%s:3478","user":"%s","pass":"%s"}' "$PUBLIC_IP" "$TURN_USER" "$TURN_PASS" | base64 | tr -d '\n=' | tr '+/' '-_')
echo
echo "Relay ready:  turn:${PUBLIC_IP}:3478   user: ${TURN_USER}   password: ${TURN_PASS}"
echo "Also allow UDP 3478, TCP 3478 and UDP 49160-49260 in your cloud provider's firewall (Oracle: the subnet's security list)."
if [ -n "$APP_URL" ]; then echo "Open this on your phone to save it in Touch-SNES:"; echo "${APP_URL%/}/#relay=${CFG}"; fi
