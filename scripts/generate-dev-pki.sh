#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <output-directory>" >&2
    exit 2
fi

output_directory=$1
mkdir -p "$output_directory"
chmod 700 "$output_directory"
umask 077

openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 30 \
    -keyout "$output_directory/ca.key" \
    -out "$output_directory/ca.crt" \
    -subj "/CN=Personal-VPN Development CA" \
    -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "subjectKeyIdentifier=hash"

openssl req -new -newkey rsa:3072 -sha256 -nodes \
    -keyout "$output_directory/server.key" \
    -out "$output_directory/server.csr" \
    -subj "/CN=localhost" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=serverAuth" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

openssl x509 -req -sha256 -days 30 \
    -in "$output_directory/server.csr" \
    -CA "$output_directory/ca.crt" \
    -CAkey "$output_directory/ca.key" \
    -CAcreateserial \
    -copy_extensions copy \
    -out "$output_directory/server.crt"

openssl req -new -newkey rsa:3072 -sha256 -nodes \
    -keyout "$output_directory/client.key" \
    -out "$output_directory/client.csr" \
    -subj "/CN=integration-test-client" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=clientAuth"

openssl x509 -req -sha256 -days 30 \
    -in "$output_directory/client.csr" \
    -CA "$output_directory/ca.crt" \
    -CAkey "$output_directory/ca.key" \
    -CAserial "$output_directory/ca.srl" \
    -copy_extensions copy \
    -out "$output_directory/client.crt"

openssl req -new -newkey rsa:3072 -sha256 -nodes \
    -keyout "$output_directory/client-2.key" \
    -out "$output_directory/client-2.csr" \
    -subj "/CN=integration-test-client-2" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=clientAuth"

openssl x509 -req -sha256 -days 30 \
    -in "$output_directory/client-2.csr" \
    -CA "$output_directory/ca.crt" \
    -CAkey "$output_directory/ca.key" \
    -CAserial "$output_directory/ca.srl" \
    -copy_extensions copy \
    -out "$output_directory/client-2.crt"

chmod 600 "$output_directory"/*.key
verification_time=$(($(date +%s) + 300))
openssl verify -CAfile "$output_directory/ca.crt" \
    -attime "$verification_time" \
    -purpose sslserver "$output_directory/server.crt"
openssl verify -CAfile "$output_directory/ca.crt" \
    -attime "$verification_time" \
    -purpose sslclient "$output_directory/client.crt"
openssl verify -CAfile "$output_directory/ca.crt" \
    -attime "$verification_time" \
    -purpose sslclient "$output_directory/client-2.crt"
