#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $(basename "$0") <new-server-ip>"
  exit 1
fi

IP="$1"
echo "→ Regenerating server cert for IP: $IP"

# 1) Remove old leaf cert files
rm -f server.key server.crt server.csr extfile.cnf

# 2) Generate a new private key
openssl genrsa -out server.key 4096

# 3) Create a CSR with CN=<IP>
openssl req -new \
  -key server.key \
  -subj "/CN=${IP}" \
  -out server.csr

# 4) Create a tiny openssl extfile for the SAN
cat > extfile.cnf <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
subjectKeyIdentifier=hash
subjectAltName=IP:${IP}
EOF

# 5) Sign the CSR with your existing CA files
#    Adjust paths if your CA is elsewhere.
openssl x509 -req \
  -in server.csr \
  -CA ca.cert.pem \
  -CAkey ../ca.key.pem \
  -CAcreateserial \
  -out server.crt \
  -days 365 \
  -sha256 \
  -extfile extfile.cnf

# 6) Done
echo "✓ server.key and server.crt regenerated (SAN=IP:${IP})"
