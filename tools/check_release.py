"""Check the facts that must agree before a release is submitted.

    python tools/check_release.py

Nothing here is clever. Every check is something that was, at some
point, wrong in this repository and not noticed until somebody read the
file by chance:

* the About blurb still said **seven** KPIs, months after `--check`
  started reporting eight and the CLI text was corrected;
* `docs/licences.md` named an `androidx.security-crypto` version the
  build had stopped using weeks earlier;
* About -> Documentation opened `docs/readme.md`, written for someone
  building the repository, rather than the wiki written for the person
  holding the phone.

None of those break a build or fail a test. They are *claims*, and a
claim is only checkable against the thing it claims about -- so each
check below reads both sides and compares them. Run it before every
submission, and add a check whenever drift is found by hand, so that it
is found by machine the next time.

Exit status is 0 when everything agrees, 1 otherwise.
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PROBLEMS = []
CHECKED = 0


def read(*parts):
    with io.open(os.path.join(ROOT, *parts), encoding="utf-8") as f:
        return f.read()


def check(ok, what, detail=""):
    """One comparison. `what` names the two sides being compared."""
    global CHECKED
    CHECKED += 1
    if ok:
        print("  ok   " + what)
    else:
        print("  FAIL " + what + ((" -- " + detail) if detail else ""))
        PROBLEMS.append(what)


# ── Version ───────────────────────────────────────────────────────────
# One version for every artefact, from src/core/version.h. The risk is
# not that the header is wrong; it is that something else restates it.

def check_version():
    print("version")
    h = read("src", "core", "version.h")
    part = {n: int(re.search(r"NTRIP_VERSION_" + n + r"\s+(\d+)", h).group(1))
            for n in ("MAJOR", "MINOR", "PATCH")}
    ver = "%d.%d.%d" % (part["MAJOR"], part["MINOR"], part["PATCH"])
    print("  version.h says " + ver)

    string = re.search(r'NTRIP_VERSION_STRING\s+"([^"]+)"', h).group(1)
    check(string == ver, "VERSION_STRING matches the three numbers",
          string)

    rc = re.search(r"NTRIP_VERSION_RC\s+([\d,]+)", h).group(1)
    check(rc == "%d,%d,%d,0" % (part["MAJOR"], part["MINOR"], part["PATCH"]),
          "Win32 VERSIONINFO matches", rc)

    rc_str = re.search(r'NTRIP_VERSION_RC_STR\s+"([^"]+)"', h).group(1)
    check(rc_str == ver + ".0", "Win32 version string matches", rc_str)

    # Minor and patch are capped by the versionCode scheme, and the cap
    # is silent: 3.100.0 and 4.0.0 both compute to 40000.
    check(part["MINOR"] < 100 and part["PATCH"] < 100,
          "minor and patch are below 100, as the versionCode scheme needs")

    # The versionCode is a derivation and must have exactly one
    # definition -- the Gradle file. A constant that restates it in the
    # header is what this check exists to prevent coming back.
    check("NTRIP_ANDROID_VERSION_CODE" not in h.replace("#define", "", 0)
          or "#define NTRIP_ANDROID_VERSION_CODE" not in h,
          "versionCode is not also #defined in version.h")

    gradle = read("android", "app", "build.gradle.kts")
    check("versionPart(\"MAJOR\")" in gradle and "version.h" in gradle,
          "Gradle reads the version from version.h")

    # Play has required a 64-bit binary since 2019 and rejects an upload
    # without one. The filter is a deliberate choice here; the check is
    # that it never loses arm64 by accident.
    check("arm64-v8a" in gradle, "the build produces a 64-bit ARM binary")

    # Play refuses new apps and updates below API 36 from 31 August 2026.
    # A targetSdk is easy to leave behind during a toolchain change, and
    # the rejection arrives after the upload rather than before it.
    target = re.search(r"targetSdk\s*=\s*(\d+)", gradle)
    check(bool(target) and int(target.group(1)) >= 36,
          "targetSdk meets Play's floor of 36",
          target.group(1) if target else "not found")

    # 16 KB pages: the flag, not the artefact -- check_release does not
    # build. docs/RUNBOOK.md says how to verify the .so itself.
    cml = read("android", "app", "src", "main", "cpp", "CMakeLists.txt")
    check("max-page-size=16384" in cml,
          "the native build asks for 16 KB page alignment")
    return ver


# ── URLs ──────────────────────────────────────────────────────────────
# Every address the app can open must be one this repository publishes.

def check_urls():
    print("urls the app can open")
    kt = read("android", "app", "src", "main", "java", "nl", "pe1mew",
              "ntripanalyser", "MainActivity.kt")
    urls = dict(re.findall(
        r'private const val (\w+_URL)\s*(?:=|=\s*\n\s*)\s*"([^"]+)"', kt))

    privacy = urls.get("PRIVACY_URL", "")
    wiki_privacy = read("docs", "wiki", "Privacy-and-support.md")
    check(bool(privacy) and privacy in wiki_privacy,
          "the app's privacy link is the address the wiki publishes",
          privacy)

    # docs/privacy-policy.md is what GitHub Pages serves at that address,
    # so the file has to exist for the link to resolve.
    check(os.path.exists(os.path.join(ROOT, "docs", "privacy-policy.md")),
          "docs/privacy-policy.md exists to be served there")

    help_url = urls.get("HELP_URL", "")
    check("/wiki" in help_url,
          "About -> Documentation opens the wiki, not a developer readme",
          help_url)

    # A wiki link is a page name, and page names get renamed. The orbit
    # badge exists to send somebody who has a problem to the page that
    # fixes it; landing them on a 404 would be worse than no badge.
    for name, url in urls.items():
        if "/wiki/" not in url:
            continue
        page = url.rsplit("/wiki/", 1)[1]
        check(os.path.exists(os.path.join(ROOT, "docs", "wiki",
                                          page + ".md")),
              name + " points at a wiki page that exists", page)

    listing = read("design", "work-items", "play-listing.md")
    check(privacy in listing or "privacy-policy" in listing,
          "the listing carries the privacy policy address")

    # The policy is a public document naming the products it covers, and
    # Play shows it beside them. It named "NTRIP Analyser - free" months
    # after the titles changed -- the very form that was dropped because
    # promotional words in a title are grounds for rejection.
    policy = read("docs", "privacy-policy.md")
    titles = re.findall(r"\| (?:Free|Pro) \| `([^`]+)`", listing)
    for title in titles:
        check(title in policy,
              "the privacy policy names the app as the listing does",
              title)
    check("Analyser - free" not in policy,
          "the privacy policy does not use a dropped app name")


# ── Claims about the app, made outside the app ────────────────────────

def check_claims():
    print("claims")
    kpi = read("src", "core", "kpi.h")
    expect = re.search(r"KPI_EXPECT_SATS\s*\{([^}]*)\}", kpi).group(1)
    n = len([x for x in expect.split(",") if x.strip()])
    print("  kpi.h carries %d checks" % n)

    words = {7: "seven", 8: "eight", 9: "nine"}
    wrong = [w for k, w in words.items() if k != n]

    # Only surfaces that describe the app as it is now. Design records
    # and old changelog entries state what was true when written.
    surfaces = [
        ("android/app/src/main/res/values/strings.xml", "the About blurb"),
        ("android/app/src/main/AndroidManifest.xml", "the manifest"),
        ("android/app/src/free/java/nl/pe1mew/ntripanalyser/Features.kt",
         "the free edition's feature flags"),
        ("design/work-items/play-listing.md", "the store listing"),
        ("docs/wiki/The-eight-checks.md", "the wiki"),
        ("docs/wiki/Home.md", "the wiki's home page"),
    ]
    for rel, name in surfaces:
        text = read(*rel.split("/")).lower()
        # "seven KPIs", "seven-KPI check", "seven RTK service checks".
        found = [w for w in wrong
                 if re.search(w + r"[ -](kpi|check|rtk)", text)]
        check(not found, name + " does not miscount the checks",
              ", ".join(found))


# ── Store metadata limits ─────────────────────────────────────────────
# Play rejects on these, after the upload, which is the worst moment to
# find out.

def check_listing():
    print("store listing")
    listing = read("design", "work-items", "play-listing.md")

    for title in re.findall(r"\| (?:Free|Pro) \| `([^`]+)`", listing):
        check(len(title) <= 30, "title within 30 characters: " + title,
              str(len(title)))
        # Play's metadata policy treats promotional words in a title as
        # grounds for rejection.
        low = title.lower()
        check(not any(w in low.split() for w in ("free", "sale", "new")),
              "title free of promotional words: " + title)

    block = listing.split("## Short description", 1)[1] \
                   .split("## Full description", 1)[0]
    shorts = [l.strip() for l in block.split(chr(10))
              if l.startswith("    ") and l.strip()]
    check(bool(shorts), "short descriptions found")
    for s in shorts:
        check(len(s) <= 80, "short description within 80 characters",
              "%d: %s" % (len(s), s))

    full = listing.split("## Full description", 1)[1] \
                  .split("## Category", 1)[0]
    body = chr(10).join(l[4:] if l.startswith("    ") else l
                        for l in full.split(chr(10))).strip()
    check(len(body) <= 4000, "full description within 4000 characters",
          str(len(body)))


# ── Generated files ───────────────────────────────────────────────────
# A generated file is only true if it was regenerated after its source
# moved. Cheap to prove: regenerate and see whether anything changed.

def check_generated():
    print("generated files")
    targets = [
        os.path.join("android", "app", "src", "main", "res", "raw",
                     "notices.txt"),
        os.path.join("packaging", "THIRD-PARTY-NOTICES.txt"),
    ]
    before = {t: read(*t.split(os.sep)) for t in targets}
    subprocess.run([sys.executable,
                    os.path.join(ROOT, "tools", "make_notices.py")],
                   cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
    for t in targets:
        check(read(*t.split(os.sep)) == before[t],
              t.replace(os.sep, "/") + " is up to date with its sources",
              "regenerating it changed it -- commit the new copy")

    # The notice has to name the versions the build resolves.
    toml = read("android", "gradle", "libs.versions.toml")
    notices = read("android", "app", "src", "main", "res", "raw",
                   "notices.txt")
    crypto = re.search(r'securityCrypto\s*=\s*"([^"]+)"', toml).group(1)
    check(crypto in notices,
          "the notice names the security-crypto version the build uses",
          crypto)


# ── The feature matrix ────────────────────────────────────────────────
# A matrix of what each product does is true the day it is written and
# quietly wrong a month later. Only one part of it can be checked by
# machine -- but it is the part that moves. An edition gate added to
# Features.kt with no row in the matrix means the free/paid split has
# changed and the document describing it has not.

def check_feature_matrix():
    print("feature matrix")
    matrix = read("design", "feature-matrix.md")

    flags = set()
    for edition in ("free", "pro"):
        kt = read("android", "app", "src", edition, "java", "nl", "pe1mew",
                  "ntripanalyser", "Features.kt")
        flags |= set(re.findall(r"const val (\w+)", kt))

    check(bool(flags), "Features.kt declares edition gates",
          "none found -- has the file moved?")
    for flag in sorted(flags):
        check(flag in matrix,
              "the matrix documents the " + flag + " gate",
              "add a row to design/feature-matrix.md")


# ── Links that leave the published site ───────────────────────────────
# docs/ is what GitHub Pages serves, and the site root is that folder.
# A relative link out of it -- `](../LICENSE)` -- works when browsing the
# repository and 404s on the website, silently, for the audience docs/
# exists for. Verified live: https://pe1mew.github.io/LICENSE is a 404.
#
# Links *within* docs/ are fine and should stay relative: GitHub Pages
# runs jekyll-relative-links, so `licences.md` is rewritten to
# `/NTRIP-Analyser/licences.html` and works in both places.

def check_doc_links():
    print("published links")
    bad = []
    for name in sorted(os.listdir(os.path.join(ROOT, "docs"))):
        if not name.endswith(".md"):
            continue
        for line_no, line in enumerate(read("docs", name).splitlines(), 1):
            if "](../" in line:
                bad.append("docs/%s:%d" % (name, line_no))
    check(not bad,
          "no doc links escape the published site with ../",
          "; ".join(bad[:4]) + (" …" if len(bad) > 4 else ""))


# ── Snapshot fields nothing fills ─────────────────────────────────────
# A field declared in NsStatsSnapshot is a promise: the daemon serialises
# it to JSON, the CSV export carries a column for it, and a frontend
# renders it. A field nothing writes keeps none of that promise while
# looking exactly like one that does -- worse than a missing field,
# because "0" and "null" read as answers.
#
# Three of these were found by accident in a single day (the ARP fields
# years earlier, then latency_s and sourcetable_offset_m), which is twice
# too often for a pattern that a search can find in a second.
#
# The rule: every field must be written somewhere outside ns_stats.c,
# whose job is to declare, initialise and serialise -- never to measure.

# Constants set in ns_stats_init(), which is the right place for them.
SNAPSHOT_INIT_ONLY = {"schema_version"}

# Known gaps, each with a home. This list must shrink, never grow: a new
# unfilled field fails the check, while these are debts already recorded.
SNAPSHOT_KNOWN_GAPS = {
    "latency_s":              "measurement-tiers.md phase 1",
    "sourcetable_offset_m":   "measurement-tiers.md phase 0",
    "sourcetable_pos_valid":  "measurement-tiers.md phase 0",
    "station_type":           "measurement-tiers.md, unfilled fields",
    "arp_drift_m":            "measurement-tiers.md, unfilled fields",
    "arp_moves":              "measurement-tiers.md, unfilled fields",
}
# `frames_malformed` was on this list for one commit. It is not there now
# because the field is gone: NS_BAD_MALFORMED had no producer, so the
# honest fix was to retire the concept rather than invent a number for
# it. That is what this list is for -- a gap either gets filled or gets
# removed, and either way it leaves.


# ── Thresholds: documented, and loadable ──────────────────────────────
# Two ways a threshold goes wrong that no compiler notices.
#
# It can be **undocumented**: added to a header, used to decide a
# verdict, and absent from docs/thresholds.md -- so a user is judged by a
# number the project never explains. The page exists precisely because
# these are judgements rather than facts.
#
# It can be **unloadable**: added to KpiPolicy or SrPolicy and left out
# of the table in thresholds.c, so --thresholds silently cannot set it
# and --thresholds-print silently does not show it. That is the failure
# the one-table design exists to prevent, and this is what proves the
# table stayed complete.

# Macros in the two headers that are not thresholds: sizes, counts,
# sentinels, include guards.
THRESHOLD_MACRO_SKIP = {
    "KPI_H", "KPI_COUNT", "KPI_MAX_CRC_RATE",
    "STATION_REPORT_H", "SR_METRIC_COUNT", "SR_JSON_SCHEMA_VERSION",
}


def check_thresholds():
    print("thresholds")
    doc = read("docs", "thresholds.md")

    undocumented = []
    for header, prefix in (("kpi.h", "KPI_"), ("station_report.h", "SR_")):
        text = read("src", "core", header)
        for m in re.finditer(r"^#define\s+(" + prefix + r"\w+)", text, re.M):
            name = m.group(1)
            if name in THRESHOLD_MACRO_SKIP:
                continue
            if name not in doc:
                undocumented.append(name)
    check(not undocumented,
          "every threshold in the headers is documented",
          ", ".join(undocumented) + " -- add it to docs/thresholds.md "
          "with a rationale, or it is a number nobody can argue with")

    # Every field of both policy structs must appear in the table, or it
    # cannot be set from a file nor shown by --thresholds-print.
    table = read("src", "core", "thresholds.c")
    keys = set(re.findall(r'\{\s*"(\w+)",\s*TH_TIER', table))

    missing = []
    for header, struct in (("kpi.h", "KpiPolicy"), ("station_report.h",
                                                    "SrPolicy")):
        text = read("src", "core", header)
        m = re.search(r"typedef struct \{(.*?)\} " + struct + r";", text, re.S)
        if not m:
            missing.append(struct + " not found")
            continue
        body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
        for line in body.splitlines():
            line = line.strip()
            if not line.endswith(";"):
                continue
            name = re.sub(r"\[.*", "", line[:-1].split()[-1]).strip("*")
            # expect_sats is an array, handled by its own parser.
            if name in ("expect_sats",):
                continue
            if name not in keys:
                missing.append(struct + "." + name)
    check(not missing,
          "every policy field is in the table, so it can be set and shown",
          ", ".join(missing) + " -- add a row to FIELDS in thresholds.c")


def check_snapshot_fields():
    print("snapshot fields")
    h = read("src", "core", "ns_stats.h")
    end = h.index("} NsStatsSnapshot;")
    body = re.sub(r"/\*.*?\*/", "", h[h.rindex("typedef struct {", 0, end):end],
                  flags=re.S)

    fields = []
    for line in body.splitlines():
        line = re.sub(r"//.*", "", line).strip()
        if line.endswith(";"):
            fields += re.findall(r"(\w+)\s*(?:\[[^\]]*\])?\s*(?=[,;])", line)

    check(len(fields) > 20, "the snapshot's fields were parsed",
          "%d found -- has the struct moved?" % len(fields))

    sources = []
    for sub, pat in (("src", "*.c"), ("src", "*.h"), ("gui", "*.c"),
                     ("gui", "*.h"), ("service", "*.c"),
                     (os.path.join("android", "app", "src", "main", "cpp"), "*.c")):
        for root, _, names in os.walk(os.path.join(ROOT, sub)):
            for name in names:
                if name.endswith(pat[1:]) and not name.startswith("ns_stats."):
                    sources.append(os.path.join(root, name))

    blob = ""
    for path in set(sources):
        with io.open(path, encoding="utf-8", errors="ignore") as f:
            blob += f.read()

    unfilled = [f for f in fields
                if f not in SNAPSHOT_INIT_ONLY
                and not re.search(r"\b" + re.escape(f) + r"\b", blob)]

    new = [f for f in unfilled if f not in SNAPSHOT_KNOWN_GAPS]
    check(not new, "no new snapshot field is left unfilled",
          ", ".join(new) + " -- fill it in the change that declares it, "
          "or do not declare it")

    stale = [f for f in SNAPSHOT_KNOWN_GAPS if f not in unfilled]
    check(not stale, "the known-gap list has no entries that are now filled",
          ", ".join(stale) + " -- remove from SNAPSHOT_KNOWN_GAPS")

    if unfilled:
        print("  note %d field(s) still unfilled, each tracked:" % len(unfilled))
        for f in sorted(unfilled):
            print("       %-22s %s" % (f, SNAPSHOT_KNOWN_GAPS.get(f, "?")))


def main():
    ver = check_version()
    check_urls()
    check_claims()
    check_listing()
    check_generated()
    check_feature_matrix()
    check_doc_links()
    check_thresholds()
    check_snapshot_fields()

    print("")
    if PROBLEMS:
        print("%d of %d checks failed:" % (len(PROBLEMS), CHECKED))
        for p in PROBLEMS:
            print("  - " + p)
        return 1
    print("%d checks agree; %s is consistent." % (CHECKED, ver))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
