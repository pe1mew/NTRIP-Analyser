# Test-only TLS fixtures — committed on purpose

Certificates and **private keys** for the TLS loopback caster in
`test/test_tls.c`. The keys are worthless by construction: the CA
exists only in this directory, no real host trusts it, nothing outside
the test suite ever presents these certificates, and the client trusts
this CA only through a test-only override. Committing them is what
makes the negative tests runnable on every machine and in CI without a
generation step — a secret scanner flagging them has found a test
fixture, which this file exists to say.

| file | what it proves |
|---|---|
| `ca.crt` / `ca.key` | the toy authority, valid 2020–2050 |
| `good.*` | SAN `localhost`/`127.0.0.1`, in validity — the positive case |
| `expired.*` | same, but validity ended 2024 — `NS_FAIL_TLS_CERT` (expired) |
| `wronghost.*` | valid, but for `otherhost.invalid` — `NS_FAIL_TLS_CERT` (name) |
| `selfsigned.*` | not signed by the CA at all — `NS_FAIL_TLS_CERT` (untrusted) |

The downgrade case (plaintext where TLS was demanded) needs no
certificate: any answer that is not a TLS record fails the handshake.

Generated once, 2026-08-25, with OpenSSL 3.5 (P-256, SHA-256; the
explicit `-not_before`/`-not_after` need OpenSSL ≥ 3.4):

```sh
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -x509 -new -key ca.key -sha256 \
  -subj "/CN=NTRIP-Analyser test CA" \
  -not_before 20200101000000Z -not_after 20500101000000Z \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign" -out ca.crt

# each leaf: key, CSR, sign against the CA with the case's dates/SAN
openssl ecparam -name prime256v1 -genkey -noout -out good.key
openssl req -new -key good.key -subj "/CN=localhost" -out good.csr
printf "subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=CA:FALSE\n" > good.ext
openssl x509 -req -in good.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -sha256 -not_before 20200101000000Z -not_after 20500101000000Z \
  -extfile good.ext -out good.crt
# expired: -not_after 20240101000000Z; wronghost: CN/SAN otherhost.invalid
# selfsigned: req -x509 directly from its own key
```

There should never be a reason to regenerate before 2050.
