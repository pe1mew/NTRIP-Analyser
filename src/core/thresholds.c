/**
 * @file thresholds.c
 * @brief User-supplied thresholds -- the table, and what reads it.
 *
 * See thresholds.h for the shape and the reasoning. Layer rules: no
 * I/O, no printf. The caller owns the file and the error is written
 * into the caller's buffer.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "core/thresholds.h"
#include "core/iono.h"

#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define K(field) offsetof(KpiPolicy, field)
#define S(field) offsetof(SrPolicy,  field)
#define V(field) offsetof(VrsPolicy, field)

/* The table. Ranges are what a *setting* may be, not what a station may
 * be: outside them the number could not describe a measurement at all.
 *
 * The three floors -- min_window_s, min_samples, sustain_s -- are the
 * ones that protect the evidence rather than the station. Below them a
 * verdict stops meaning anything: ten minutes is what six of the six
 * tier-2 metrics need, a window with three samples is an anecdote, and
 * a sustain shorter than half a minute lets a flickering station pass on
 * a lucky moment. */
static const ThField FIELDS[] = {
  /* ── Tier 1 ─────────────────────────────────────────────────────── */
  { "sustain_s", TH_TIER1, TH_DOUBLE, K(sustain_s), 30, 600, "s", 0,
    "seconds a verdict must hold before the run reports it" },
  { "min_bytes_per_s", TH_TIER1, TH_DOUBLE, K(min_bytes_per_s), 0, 1e7,
    "B/s", 0, "throughput below which the stream is not delivering" },
  { "arp_deadline_s", TH_TIER1, TH_DOUBLE, K(arp_deadline_s), 1, 3600,
    "s", 0, "allowance for the first 1005/1006 to arrive" },
  { "msm_max_dt_s", TH_TIER1, TH_DOUBLE, K(msm_max_dt_s), 0.1, 60,
    "s", 1, "slowest acceptable epoch interval per constellation" },
  { "expect_unknown", TH_TIER1, TH_INT, K(expect_unknown), 0, 200, "", 0,
    "satellites expected when neither sourcetable nor stream says" },
  { "min_cnr_median", TH_TIER1, TH_DOUBLE, K(min_cnr_median), 0, 70,
    "dB-Hz", 1, "median C/N0 floor for a healthy antenna chain" },
  { "min_integrity_pct", TH_TIER1, TH_DOUBLE, K(min_integrity_pct), 0, 100,
    "%", 3, "share of frames that must pass CRC" },
  { "bad_integrity_pct", TH_TIER1, TH_DOUBLE, K(bad_integrity_pct), 0, 100,
    "%", 3, "share below which the link is corrupting frames" },
  { "integrity_window_s", TH_TIER1, TH_DOUBLE, K(integrity_window_s), 10, 86400,
    "s", 0, "stream each integrity reading covers" },

  /* ── Tier 2 ─────────────────────────────────────────────────────── */
  { "reconnects_warn_per_h", TH_TIER2, TH_DOUBLE, S(reconnects_warn_per_h),
    0, 1000, "/h", 2, "reconnections per hour worth investigating" },
  { "reconnects_bad_per_h", TH_TIER2, TH_DOUBLE, S(reconnects_bad_per_h),
    0, 1000, "/h", 2, "reconnections per hour nobody can survey on" },
  { "integrity_warn_pct", TH_TIER2, TH_DOUBLE, S(integrity_warn_pct), 0, 100,
    "%", 3, "share of frames passing CRC, below which is DEGRADED" },
  { "integrity_bad_pct", TH_TIER2, TH_DOUBLE, S(integrity_bad_pct), 0, 100,
    "%", 3, "share below which it is UNSTABLE" },
  { "integrity_window_s", TH_TIER2, TH_DOUBLE, S(integrity_window_s), 10, 86400,
    "s", 0, "stream each integrity reading covers" },
  { "cnr_drop_warn", TH_TIER2, TH_DOUBLE, S(cnr_drop_warn), 0, 60,
    "dB-Hz", 1, "fall in mean C/N0 worth investigating" },
  { "cnr_drop_bad", TH_TIER2, TH_DOUBLE, S(cnr_drop_bad), 0, 60,
    "dB-Hz", 1, "fall in mean C/N0 that is a fault" },
  { "sats_warn", TH_TIER2, TH_INT, S(sats_warn), 0, 200, "", 0,
    "fewest satellites held before the window is DEGRADED" },
  { "sats_bad", TH_TIER2, TH_INT, S(sats_bad), 0, 200, "", 0,
    "fewest satellites held before it is UNSTABLE" },
  { "roti_warn", TH_TIER2, TH_DOUBLE, S(roti_warn), 0, 100, "TECU/min", 2,
    "median ROTI above which the ionosphere is unsettled" },
  { "roti_bad", TH_TIER2, TH_DOUBLE, S(roti_bad), 0, 100, "TECU/min", 2,
    "median ROTI above which it is disturbed" },
  { "offrate_warn", TH_TIER2, TH_DOUBLE, S(offrate_warn), 0, 1, "", 3,
    "share of samples with a type off its advertised rate" },
  { "offrate_bad", TH_TIER2, TH_DOUBLE, S(offrate_bad), 0, 1, "", 3,
    "share at which delivery is UNSTABLE" },
  { "warmup_s", TH_TIER2, TH_DOUBLE, S(warmup_s), 0, 3600, "s", 0,
    "session start not counted as a measurement of the station" },
  { "min_window_s", TH_TIER2, TH_DOUBLE, S(min_window_s), 600, 86400, "s", 0,
    "evidence required before any verdict is offered" },
  { "min_samples", TH_TIER2, TH_INT, S(min_samples), 10, 100000, "", 0,
    "samples required before any verdict is offered" },

  /* ── The network-RTK assertions ─────────────────────────────────
   * Deadlines describing what a caster does, not what a station
   * achieves. The least likely of these thresholds to need changing,
   * and included for the same reason as the rest: "unlikely to need
   * changing" is not a reason to make something unarguable. */
  { "accept_s", TH_VRS, TH_DOUBLE, V(accept_s), 0.5, 120, "s", 1,
    "A1: a disconnect later than this is unrelated to the GGA" },
  { "rtcm_s", TH_VRS, TH_DOUBLE, V(rtcm_s), 0.5, 300, "s", 1,
    "A2: deadline for the first correction after a GGA" },
  { "arp_max_km", TH_VRS, TH_DOUBLE, V(arp_max_km), 0.1, 2000, "km", 1,
    "A3: how far the reference position may be from the rover" },
  { "hold_s", TH_VRS, TH_DOUBLE, V(hold_s), 5, 3600, "s", 0,
    "A4: window the stream must hold at the GGA cadence" },
  { "gate_s", TH_VRS, TH_DOUBLE, V(gate_s), 5, 3600, "s", 0,
    "A5: how long to wait for the drop once the GGA stops" },
};

#define N_FIELDS ((int)(sizeof(FIELDS) / sizeof(FIELDS[0])))

int thresholds_field_count(void) { return N_FIELDS; }

const ThField *thresholds_field(int i)
{
    if (i < 0 || i >= N_FIELDS) return NULL;
    return &FIELDS[i];
}

/** @brief Where a field lives in whichever policy owns it. */
static void *field_ptr(Thresholds *t, const ThField *f)
{
    char *base = (f->tier == TH_TIER1) ? (char *)&t->kpi
               : (f->tier == TH_TIER2) ? (char *)&t->sr
                                       : (char *)&t->vrs;
    return base + f->offset;
}

static const void *field_ptr_const(const Thresholds *t, const ThField *f)
{
    const char *base = (f->tier == TH_TIER1) ? (const char *)&t->kpi
                     : (f->tier == TH_TIER2) ? (const char *)&t->sr
                                             : (const char *)&t->vrs;
    return base + f->offset;
}

double thresholds_value(const Thresholds *t, int i)
{
    const ThField *f = thresholds_field(i);
    if (!t || !f) return 0.0;
    const void *p = field_ptr_const(t, f);
    return (f->type == TH_INT) ? (double)(*(const int *)p)
                               : *(const double *)p;
}

bool thresholds_is_set(const Thresholds *t, int i)
{
    if (!t || i < 0 || i >= N_FIELDS) return false;
    return t->set[i];
}

void thresholds_defaults(Thresholds *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    kpi_policy_defaults(&t->kpi);
    sr_policy_defaults(&t->sr);
    vrs_policy_defaults(&t->vrs);
    t->schema_version = TH_SCHEMA_VERSION;
}

/* The constellations `expect_sats` is indexed by, in the core's own
 * 1-based order. Named rather than numbered in the file: a user editing
 * "beidou" cannot silently mean SBAS. */
static const char *GNSS_KEYS[8] = {
    NULL, "gps", "glonass", "galileo", "qzss", "beidou", "sbas", "navic"
};

/** @brief The section a field appears under in the file. */
static const char *section_name(int tier)
{
    switch (tier) {
    case TH_TIER1: return "tier1";
    case TH_TIER2: return "tier2";
    default:       return "vrs";
    }
}

static void fail(char *err, size_t cap, const char *fmt, ...)
{
    if (!err || cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/**
 * @brief Cross-field rules: a warn level on the wrong side of its bad
 *        level is a policy that can never produce the middle verdict.
 */
static bool check_ordering(const Thresholds *t, char *err, size_t cap)
{
    struct { double warn, bad; bool higher_is_better; const char *pair; } r[] = {
        { t->sr.reconnects_warn_per_h, t->sr.reconnects_bad_per_h, false,
          "reconnects_warn_per_h must not exceed reconnects_bad_per_h" },
        { t->sr.integrity_warn_pct, t->sr.integrity_bad_pct, true,
          "integrity_warn_pct must not be below integrity_bad_pct" },
        { t->sr.cnr_drop_warn, t->sr.cnr_drop_bad, false,
          "cnr_drop_warn must not exceed cnr_drop_bad" },
        { (double)t->sr.sats_warn, (double)t->sr.sats_bad, true,
          "sats_warn must not be below sats_bad" },
        { t->sr.roti_warn, t->sr.roti_bad, false,
          "roti_warn must not exceed roti_bad" },
        { t->sr.offrate_warn, t->sr.offrate_bad, false,
          "offrate_warn must not exceed offrate_bad" },
        { t->kpi.min_integrity_pct, t->kpi.bad_integrity_pct, true,
          "min_integrity_pct must not be below bad_integrity_pct" },
    };
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++) {
        bool ok = r[i].higher_is_better ? (r[i].warn >= r[i].bad)
                                        : (r[i].warn <= r[i].bad);
        if (!ok) { fail(err, cap, "%s", r[i].pair); return false; }
    }
    return true;
}

bool thresholds_parse(Thresholds *t, const char *json_text,
                      char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (!t || !json_text) {
        fail(err, err_cap, "no policy text to read");
        return false;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        fail(err, err_cap, "not valid JSON");
        return false;
    }

    /* Applied to a copy: on any error the caller's policy is left
     * exactly as it was, because a half-applied standard is one no
     * verdict can be attributed to. */
    Thresholds work = *t;
    bool ok = true;

    const cJSON *v = cJSON_GetObjectItem(root, "schema_version");
    if (v && cJSON_IsNumber(v)) {
        work.schema_version = v->valueint;
        if (v->valueint > TH_SCHEMA_VERSION) {
            fail(err, err_cap,
                 "schema_version %d is newer than this build understands (%d)",
                 v->valueint, TH_SCHEMA_VERSION);
            ok = false;
        }
    }

    if (ok && (v = cJSON_GetObjectItem(root, "name")) && cJSON_IsString(v))
        snprintf(work.name, sizeof(work.name), "%s", v->valuestring);

    const cJSON *sect[4] = { NULL, NULL, NULL, NULL };
    if (ok) {
        sect[TH_TIER1] = cJSON_GetObjectItem(root, "tier1");
        sect[TH_TIER2] = cJSON_GetObjectItem(root, "tier2");
        sect[TH_VRS]   = cJSON_GetObjectItem(root, "vrs");
    }

    for (int i = 0; ok && i < N_FIELDS; i++) {
        const ThField *f = &FIELDS[i];
        const cJSON *s = sect[f->tier];
        if (!s || !cJSON_IsObject(s)) continue;

        const cJSON *e = cJSON_GetObjectItem(s, f->key);
        if (!e) continue;                      /* absent: keep the default */

        if (!cJSON_IsNumber(e)) {
            fail(err, err_cap, "%s.%s must be a number", section_name(f->tier),
                 f->key);
            ok = false;
            break;
        }
        double val = e->valuedouble;
        if (val < f->lo || val > f->hi) {
            fail(err, err_cap,
                 "%s.%s is %g, outside the usable range %g to %g",
                 section_name(f->tier), f->key, val, f->lo, f->hi);
            ok = false;
            break;
        }

        void *p = field_ptr(&work, f);
        if (f->type == TH_INT) *(int *)p = (int)(val + 0.5);
        else                   *(double *)p = val;
        work.set[i] = true;
    }

    /* expect_sats: an object keyed by constellation, each entry
     * optional, so a policy can raise GPS without restating the rest. */
    if (ok && sect[TH_TIER1] && cJSON_IsObject(sect[TH_TIER1])) {
        const cJSON *es = cJSON_GetObjectItem(sect[TH_TIER1], "expect_sats");
        if (es) {
            if (!cJSON_IsObject(es)) {
                fail(err, err_cap,
                     "tier1.expect_sats must be an object keyed by "
                     "constellation, e.g. { \"gps\": 8 }");
                ok = false;
            } else {
                for (int g = 1; ok && g <= 7; g++) {
                    const cJSON *e = cJSON_GetObjectItem(es, GNSS_KEYS[g]);
                    if (!e) continue;
                    if (!cJSON_IsNumber(e) || e->valuedouble < 0 ||
                        e->valuedouble > 100) {
                        fail(err, err_cap,
                             "tier1.expect_sats.%s must be a count "
                             "between 0 and 100", GNSS_KEYS[g]);
                        ok = false;
                        break;
                    }
                    work.kpi.expect_sats[g] = (int)(e->valuedouble + 0.5);
                }
            }
        }
    }

    if (ok) ok = check_ordering(&work, err, err_cap);

    cJSON_Delete(root);
    if (!ok) return false;

    work.loaded = true;
    *t = work;
    return true;
}

void thresholds_fingerprint(const Thresholds *t, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!t) return;

    /* FNV-1a over the effective values, formatted at the precision they
     * are shown at: two policies that print identically fingerprint
     * identically, which is the property a reader can check by eye. */
    unsigned long h = 2166136261UL;
    for (int i = 0; i < N_FIELDS; i++) {
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "%s=%.*f;", FIELDS[i].key,
                         FIELDS[i].decimals + 3, thresholds_value(t, i));
        for (int k = 0; k < n && k < (int)sizeof(buf); k++) {
            h ^= (unsigned char)buf[k];
            h *= 16777619UL;
        }
    }
    for (int g = 1; g <= 7; g++) {
        h ^= (unsigned char)t->kpi.expect_sats[g];
        h *= 16777619UL;
    }
    snprintf(out, cap, "%08lx", h & 0xffffffffUL);
}
