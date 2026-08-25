"""Generate the embedded CA bundle, from the PEM actually vendored.

One trust store for every product (design/work-items/tls-rollout.md,
decision 3): the NDK has no system PEM file to point at, so the
Mozilla/curl bundle in lib/ca-bundle/cacert.pem is embedded as a C
array and every build compiles the same one. Generated at *refresh*
time and committed -- not at build time -- because four build systems
would otherwise each need Python; `check_release.py` regenerates and
diffs it, exactly as it does the notices, so the array cannot drift
from the PEM.

    python tools/make_ca_bundle.py

Refreshing the bundle:

    curl -o lib/ca-bundle/cacert.pem        https://curl.se/ca/cacert.pem
    curl -o lib/ca-bundle/cacert.pem.sha256 https://curl.se/ca/cacert.pem.sha256
    sha256sum -c  (compare the two)
    python tools/make_ca_bundle.py

Output: src/session/ns_ca_bundle.c (the array, with the bundle's own
Mozilla date carried into NS_CA_BUNDLE_DATE for the age check).
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PEM = os.path.join(ROOT, "lib", "ca-bundle", "cacert.pem")
OUT = os.path.join(ROOT, "src", "session", "ns_ca_bundle.c")

NL = chr(10)


def main():
    with open(PEM, "rb") as f:
        data = f.read()

    text = data.decode("utf-8", errors="replace")
    m = re.search(r"as of: (.+?)\s*$", text, re.M)
    date = m.group(1) if m else "unknown"
    roots = text.count("BEGIN CERTIFICATE")

    lines = [
        "/**",
        " * @file ns_ca_bundle.c",
        " * @brief The embedded trust store -- GENERATED, do not edit.",
        " *",
        " * Written by tools/make_ca_bundle.py from lib/ca-bundle/cacert.pem",
        " * (the Mozilla root store as curl distributes it). Every product",
        " * trusts exactly this list -- the phone has no system PEM file, so",
        " * nobody gets a different answer about the same caster.",
        " *",
        " * %d root certificates; Mozilla data of %s." % (roots, date),
        " *",
        " * Project: NTRIP-Analyser",
        " * License: the bundle is MPL-2.0 (see lib/ca-bundle/); this",
        " * wrapping is Apache License 2.0 with Commons Clause.",
        " */",
        "",
        "#include \"session/ns_ca_bundle.h\"",
        "",
        "const char ns_ca_bundle_date[] = \"%s\";" % date,
        "",
        "/* The PEM text, byte for byte, NUL-terminated: mbedTLS parses PEM",
        " * only when the buffer length includes the terminator. */",
        "const unsigned char ns_ca_bundle_pem[] = {",
    ]

    body = data + b"\0"
    for i in range(0, len(body), 12):
        chunk = body[i:i + 12]
        lines.append("    " + "".join("0x%02x," % b for b in chunk))
    lines += [
        "};",
        "",
        "const unsigned int ns_ca_bundle_pem_len = sizeof(ns_ca_bundle_pem);",
        "",
    ]

    with open(OUT, "w", encoding="utf-8", newline=NL) as f:
        f.write(NL.join(lines))
    print("wrote", os.path.relpath(OUT, ROOT),
          "(%d roots, dated %s, %d bytes)" % (roots, date, len(body)))


if __name__ == "__main__":
    main()
