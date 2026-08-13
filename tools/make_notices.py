"""Generate the third-party notices, from the versions actually built.

Two obligations, one text:

* **cJSON** is MIT, which requires its copyright notice and permission
  notice to be shipped with the software. It is compiled into every
  artefact this project produces -- the CLI, the GUI, the daemon and
  both Android editions -- so the notice ships with all of them.
* **Everything else is Apache 2.0**, which requires attribution and any
  NOTICE the library carries. The AndroidX, Compose and kotlinx
  libraries carry no NOTICE file of their own, so attribution is what is
  owed.

    python tools/make_notices.py

Written by a script rather than by hand for one reason: **the versions
have to be true**. They are read from `android/gradle/libs.versions.toml`
-- the file the build actually resolves -- so a bumped dependency cannot
leave a legal notice quoting last year's version. `docs/licences.md` had
drifted to an `androidx.security-crypto` version the build stopped using
weeks ago, which is exactly the failure this prevents.

Outputs:

* `android/app/src/main/res/raw/notices.txt` -- shown by the app's
  About -> Open-source notices screen, wrapped for a phone dialog
* `packaging/THIRD-PARTY-NOTICES.txt` -- copied into the desktop
  release archive by the `release` target
"""
import os
import re
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Android dependencies, in the order a reader would meet them. The
# second field is the key in libs.versions.toml.
ANDROID = [
    ("AndroidX Core KTX", "coreKtx"),
    ("AndroidX Lifecycle (runtime-ktx, runtime-compose)", "lifecycle"),
    ("AndroidX Activity Compose", "activityCompose"),
    ("Jetpack Compose (UI, Material 3), via BOM", "composeBom"),
    ("kotlinx.serialization", "serialization"),
    ("AndroidX Security Crypto (with Google Tink)", "securityCrypto"),
    ("Kotlin standard library", "kotlin"),
]

CJSON_NOTICE = """Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."""

APACHE_SUMMARY = """Licensed under the Apache License, Version 2.0 (the "License"); you may
not use these files except in compliance with the License. You may
obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing
permissions and limitations under the License."""

INTRO = ("NTRIP-Analyser is Apache 2.0 with the Commons Clause; see LICENSE. "
         "This file covers the third-party software it includes, and nothing "
         "here restricts your rights under those licences.")

CLOSING = ("The GNSS data this software reads belongs to whoever operates the "
           "caster you connect to, under their terms, not ours.")


def versions():
    """The versions the Android build actually resolves."""
    path = os.path.join(ROOT, "android", "gradle", "libs.versions.toml")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    block = text.split("[versions]", 1)[1].split("[", 1)[0]
    return dict(re.findall(r'^\s*([A-Za-z0-9_]+)\s*=\s*"([^"]+)"',
                           block, re.M))


def reflow(text, width):
    """Re-wrap to `width`, keeping paragraph breaks.

    Only whitespace changes. The words of a licence are what must be
    reproduced; where the lines break is not part of it, and a phone
    dialog cannot show the columns a text file assumes.
    """
    blank = chr(10) + chr(10)
    return blank.join(
        textwrap.fill(" ".join(p.split()), width=width)
        for p in text.split(blank)
    )


def build(android):
    """The notice text. `android` adds the app's own dependencies."""
    # A phone dialog is about forty monospace columns; a text file beside
    # a binary can assume the usual seventy-odd.
    width = 42 if android else 68
    rule = "=" * width

    out = ["THIRD-PARTY NOTICES", "", reflow(INTRO, width), "", rule, "",
           "cJSON -- MIT License", "", reflow(CJSON_NOTICE, width), ""]

    if android:
        v = versions()
        out += [rule, "",
                reflow("The following are used by the Android application "
                       "and are licensed under the Apache License, "
                       "Version 2.0.", width), ""]
        for name, key in ANDROID:
            out += textwrap.wrap(name, width=width, subsequent_indent="  ")
            ver = v.get(key)
            if ver:
                out.append("  " + ver)
        out += ["", reflow(APACHE_SUMMARY, width), ""]

    out += [rule, "", reflow(CLOSING, width), ""]
    return chr(10).join(out)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline=chr(10)) as f:
        f.write(text)
    print("wrote", os.path.relpath(path, ROOT),
          "(%d lines)" % text.count(chr(10)))


def main():
    write(os.path.join(ROOT, "android", "app", "src", "main", "res",
                       "raw", "notices.txt"), build(android=True))
    write(os.path.join(ROOT, "packaging", "THIRD-PARTY-NOTICES.txt"),
          build(android=False))


if __name__ == "__main__":
    main()
