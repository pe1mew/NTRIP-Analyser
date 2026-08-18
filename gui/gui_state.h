/**
 * @file gui_state.h
 * @brief Shared application state and constants for NTRIP-Analyser GUI
 *
 * Defines the AppState structure that holds all GUI window handles and
 * runtime state. A single instance is allocated in WinMain and attached
 * to the main window via SetWindowLongPtr(GWLP_USERDATA).
 */

#ifndef GUI_STATE_H
#define GUI_STATE_H

#define _WIN32_WINNT 0x0601  /* Windows 7+ */

#include <winsock2.h>   /* MUST come before windows.h */
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <stdbool.h>
#include <stdio.h>
#include "net/ntrip_handler.h"
#include "core/sv_ephemeris.h"
#include "core/ns_stats.h"
#include "core/kpi.h"
#include "core/station_report.h"
#include "core/thresholds.h"
#include "core/vrs_check.h"
#include "core/iono.h"
#include "net/ntrip_proto.h"
#include "resource.h"    /* IDI_APP_ICON; pure #defines, also fed to windres */

/**
 * @brief Load the application icon at the size Windows asks for.
 *
 * `LoadIcon` always returns the system large size, which Windows then
 * squashes for title bars and the notification area -- and a 32x32 arc
 * pattern scaled to 16 turns to mush.  `LoadImage` with the metric size
 * picks the matching image out of the multi-size .ico instead, so each
 * one is the render drawn for that size.
 *
 * @param small_icon TRUE for title-bar and notification-area size,
 *                   FALSE for the Alt-Tab and shell size.
 * @return The icon, or the system default if the resource is missing --
 *         a window with no icon at all looks like a fault.
 */
static __inline HICON GuiLoadAppIcon(BOOL small_icon)
{
    int cx = GetSystemMetrics(small_icon ? SM_CXSMICON : SM_CXICON);
    int cy = GetSystemMetrics(small_icon ? SM_CYSMICON : SM_CYICON);
    HICON h = (HICON)LoadImage(GetModuleHandle(NULL),
                               MAKEINTRESOURCE(IDI_APP_ICON),
                               IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    return h ? h : LoadIcon(NULL, IDI_APPLICATION);
}

/* ── Application constants ────────────────────────────────── */
#define APP_TITLE       "NTRIP-Analyser"
#define APP_CLASS_NAME  "NtripAnalyserGuiClass"
#define APP_MIN_WIDTH   800
#define APP_MIN_HEIGHT  690
#define APP_INIT_WIDTH  1024
#define APP_INIT_HEIGHT 850

/* ── Margins and spacing ──────────────────────────────────── */
#define GUI_MARGIN      8
#define GUI_SPACING     4
#define GUI_LABEL_W     80
#define GUI_EDIT_H      22
#define GUI_BTN_H       26
#define GUI_BTN_W       120
#define GUI_GROUP_PAD   16

/* ── Stats constants ─────────────────────────────────────── */
#define GUI_MAX_MSG_TYPES  4096
#define GUI_BUFFER_SIZE    4096

/* ── Detail window class ────────────────────────────────── */
#define DETAIL_CLASS_NAME       "NtripDetailClass"

/* ── Sky-SV detail window class ─────────────────────────── */
#define SV_DETAIL_CLASS_NAME    "NtripSkySvDetailClass"

/**
 * @struct RtcmRawMsg
 * @brief Heap-allocated copy of a raw RTCM frame, posted from worker to UI.
 */
typedef struct {
    int msg_type;
    int length;
    unsigned char data[GUI_BUFFER_SIZE];
} RtcmRawMsg;

/**
 * @struct GuiMsgStat
 * @brief Per-message-type statistics collected during stream reception.
 */
typedef struct {
    int    count;         /**< frames received */
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool   seen;

    /* Epoch accounting.  MSM splits one epoch across several frames of
     * the same type when the observations do not fit in one frame, so
     * frame count over-states the message rate for large constellations.
     * The dt statistics above are sampled per EPOCH, not per frame, and
     * epochs is what should be compared against the advertised interval.
     * For non-MSM types there is no epoch field and epochs == count. */
    int      epochs;      /**< distinct epochs seen */
    uint32_t last_epoch;  /**< previous MSM epoch field value */
    bool     has_epoch;   /**< TRUE once last_epoch is meaningful */
} GuiMsgStat;

/**
 * @brief High-resolution monotonic timer (seconds).
 */
static inline double gui_get_time_seconds(void) {
    static LARGE_INTEGER freq;
    static int freq_init = 0;
    LARGE_INTEGER now;
    if (!freq_init) {
        QueryPerformanceFrequency(&freq);
        freq_init = 1;
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / freq.QuadPart;
}

/** @brief A single past (az, el, C/N0) sample for a satellite's track trail.
 *
 * @c ts_rel is seconds since `SkyPlotState::sessionT0` rather than an
 * absolute gui_get_time_seconds() value.  That keeps the struct at four
 * floats -- 16 bytes -- so carrying C/N0 alongside position costs no extra
 * memory.  A float holds a 24-hour session offset to better than 0.01 s,
 * far finer than the 60 s sampling interval, and every consumer compares
 * differences rather than absolute times.
 */
typedef struct {
    float az_deg;
    float el_deg;
    float cnr_dbhz;       /**< C/N0 at this sample (0 = unknown) */
    float roti;           /**< ROTI at this sample, TECU/min; -1 = unknown */
    float ts_rel;         /**< seconds since SkyPlotState.sessionT0 */
} SkyTrackPoint;

/* Compile-time guard on the size above.  The trail buffer holds
 * SKY_TRACK_CAP * 8 * 64 of these, so a silent widening -- a double slips
 * in, say -- costs megabytes without anyone noticing.   20 bytes was a
 * deliberate 16->20 widening (2026-08): carrying ROTI per point is what
 * makes the trail a timelapse of ionospheric structure, ~2.9 MB for the
 * whole buffer.  C99 has no _Static_assert, hence the negative-array-size
 * idiom. */
typedef char sky_track_point_is_20_bytes[(sizeof(SkyTrackPoint) == 20) ? 1 : -1];

/** @brief Sampling interval (seconds) between track-buffer points.
 *
 * Tighter = denser dots / smoother line; coarser = longer history per SV.
 * Window per SV = SKY_TRACK_CAP * SKY_TRACK_INTERVAL_S seconds.
 * Default 60 s × 1440 = 86400 s = 24 hours of trail per SV -- enough
 * for a typical long-running session to keep the trail for the whole
 * capture.  The polyline renderer in gui_sky_window.c connects
 * consecutive samples into a smooth arc regardless of the dot
 * spacing; at GLONASS orbital speed (~4 km/s) 60-s dots land ~7
 * pixels apart on an 800-px plot, which reads as continuous.  Runs
 * are split where consecutive samples are more than
 * SKY_TRACK_GAP_BREAK_S apart in time, so a satellite that sets and
 * later rises again is drawn as two separate arcs rather than a
 * straight chord across the plot.
 *
 * Memory cost: SKY_TRACK_CAP * sizeof(SkyTrackPoint) per SV slot
 *            = SKY_TRACK_CAP * 16 B * 8 GNSS * 64 PRN
 *            = 11.3 MB at the default 1440 entries.  Lower
 * SKY_TRACK_CAP if you want a tighter memory footprint at the cost
 * of shorter trails.  (Keeping SkyTrackPoint at four floats is what
 * holds this figure -- widening ts_rel back to a double would push
 * the struct to 24 B and the buffer to 16.9 MB.)
 */
#define SKY_TRACK_INTERVAL_S    60.0

/** @brief Break the polyline if two consecutive samples are this many
 * seconds apart -- protects against drawing a straight chord across
 * the plot when an SV sets and rises hours later. */
#define SKY_TRACK_GAP_BREAK_S   300.0

/* ── Session history ─────────────────────────────────────────────────────
 * The Msg Stats min/max/avg figures actively hide the faults that matter:
 * a 45-second dropout of one message type and a steady stream can produce
 * similar averages.  Sampling the same numbers over time makes gaps,
 * bursts and reconnects self-evident.
 *
 * Sampled once per second rather than the 30 s the ESP32 History.cpp uses,
 * because the point is to see short dropouts -- a 30 s bucket would average
 * a 5 s gap away, which is the very failure this is meant to expose.
 */
#define HIST_INTERVAL_S   1.0
#define HIST_CAP          14400   /* 4 hours at 1 s; 345 KB */

/**
 * @struct HistSample
 * @brief One second of session history.
 *
 * Rates are per second over the sampling interval, not cumulative, so a
 * dropout reads as a visible trough rather than a flat spot in a rising
 * total.
 */
typedef struct {
    float    ts_rel;       /**< seconds since session start */
    float    bytes_per_s;  /**< throughput this interval */
    float    frames_per_s; /**< RTCM frames decoded this interval */
    float    cnr_mean;     /**< mean C/N0 over tracked SVs, 0 = unknown */
    float    arp_delta_m;  /**< metres from the session's first ARP, -1 = unknown */
    float    roti;         /**< median ROTI, TECU/min; -1 = unknown */
    uint16_t crc_errors;   /**< CRC failures in this interval */
    uint8_t  sats;         /**< satellites tracked */
    uint8_t  reserved;
} HistSample;

/** @brief Session-history ring plus the deltas needed to fill it. */
typedef struct {
    HistSample pts[HIST_CAP];
    int    head;            /**< next write index */
    int    count;           /**< 0..HIST_CAP */
    double t0;              /**< gui_get_time_seconds() at session start */
    double lastSampleTime;  /**< when the previous sample was taken */

    /* Previous cumulative readings, for per-interval differencing. */
    LONG   lastBytes;
    LONG   lastFrames;
    LONG   lastCrc;

    /* Reference ARP, latched on the first 1005/1006 and never moved --
     * self-centring would hide exactly the drift we want to see. */
    BOOL   refValid;
    double refLat, refLon;
} HistState;

/** @brief Severity of a Stream Health row, driving its row colour.
 *
 * Stored in the ListView item's lParam, same technique as the Msg Stats
 * verdicts. */
enum {
    HEALTH_OK   = 0,  /* normal; keeps system colours */
    HEALTH_INFO = 1,  /* noteworthy but not wrong */
    HEALTH_WARN = 2,  /* worth attention */
    HEALTH_BAD  = 3,  /* a real fault */
};

/* The caster handshake is the session layer's NsHandshake
 * (src/net/ntrip_proto.h).  The GUI's own duplicate of it was deleted
 * when the obs worker moved onto the session layer -- see
 * design/architecture.md par. 9, step 4. */

/** @brief How the connected mountpoint serves corrections.
 *
 * Distinguishing these matters because a fixed base and a virtual station
 * behave oppositely: a fixed base that moves is broken, while a virtual
 * station is *expected* to move, since the network places it at the rover.
 * Applying the fixed-base checks to a VRS produces nothing but false
 * alarms. */
enum {
    STATION_UNKNOWN = 0,  /* not enough evidence yet */
    STATION_FIXED   = 1,  /* single physical base at a fixed ARP */
    STATION_VRS     = 2,  /* VRS / MAC / nearest-base network service */
};

/** @brief Advertised-vs-observed verdict for one message type.
 *
 * Stored in each Msg Stats ListView item's lParam so the custom-draw
 * handler can colour rows without re-parsing the Status text. */
enum {
    MSGSTAT_UNKNOWN = 0,  /* no sourcetable entry, no comparison possible */
    MSGSTAT_OK      = 1,  /* advertised and arriving at roughly the rate  */
    MSGSTAT_MISSING = 2,  /* advertised but never received                */
    MSGSTAT_RATE    = 3,  /* arriving, but not near the advertised rate   */
    MSGSTAT_EXTRA   = 4,  /* received but not advertised                  */
};

/** @brief Ring buffer of past positions for one satellite (since stream open). */
#define SKY_TRACK_CAP   1440    /* 24 hours at SKY_TRACK_INTERVAL_S = 60 s/point */
typedef struct {
    SkyTrackPoint pts[SKY_TRACK_CAP];
    int           head;   /**< next write index */
    int           count;  /**< 0..SKY_TRACK_CAP */
} SkyTrackBuffer;

/**
 * @struct SkySat
 * @brief Last-known sky position of a single satellite + its track trail.
 *
 * One slot per (gnss_id, prn) in @ref SkyPlotState.  Updated each MSM
 * epoch by WM_APP_SKY_UPDATE from the worker thread.  Stale entries
 * (last_seen_ts older than ~30 s) are filtered out at paint time.
 */
typedef struct {
    double az_deg;        /**< 0..360, 0 = north, clockwise */
    double el_deg;        /**< -90..+90, +90 = zenith */
    double last_seen_ts;  /**< gui_get_time_seconds() at last update */
    float  cnr_dbhz;      /**< best CNR this epoch (0 = unknown) */
    float  roti;          /**< ROTI, TECU/min; -1 = unknown */
    bool   valid;
    SkyTrackBuffer track; /**< history of observed positions since stream open */
} SkySat;

/* ── Sky-plot sector grid (Onocoy-style observed-vs-expected heatmap) ─────
 * 9 elevation bands of 10° each.  Per-band azimuth bin counts roughly
 * proportional to cos(mean elevation) so each sector covers a comparable
 * solid angle.  Total: 1+5+8+11+16+21+25+30+33 = 150 sectors (close to
 * Onocoy's documented 149). */
#define SKY_N_EL_BANDS   9
#define SKY_MAX_AZ_BINS  33   /* widest band */

/**
 * @struct SkySector
 * @brief Per-sector observation accumulator.
 *
 * observed: count of (epoch, SV) pairs where the SV was actually tracked
 *           by the obs receiver in this sector.
 * expected: count of (epoch, SV) pairs where any cached ephemeris placed
 *           an SV in this sector, regardless of whether the receiver
 *           tracked it.  Ratio observed/expected drives the colour ramp.
 */
typedef struct {
    int observed;
    int expected;
} SkySector;

/** Sky-plot rendering mode (toggle via 'M' key on the sky window). */
typedef enum {
    SKY_MODE_MARKERS = 0,   /* live SV dots (default) */
    SKY_MODE_HEATMAP = 1,   /* sector observed/expected heatmap */
} SkyPlotMode;

/**
 * @struct SkyPlotState
 * @brief Sky-plot model held by the UI thread and rendered by gui_sky_window.c.
 */
typedef struct {
    SkySat sats[SV_EPH_MAX_GNSS][SV_EPH_MAX_SATS_PER_GNSS];

    /* Sector grid for heatmap mode.  Filled by WM_APP_SKY_UPDATE handler
     * as SkySatUpdate entries arrive (observed_flag picks observed++ vs
     * expected++).  Reset to zero when a new stream is opened. */
    SkySector sectors[SKY_N_EL_BANDS][SKY_MAX_AZ_BINS];

    SkyPlotMode mode;

    /* Reference epoch for SkyTrackPoint.ts_rel.  Stamped from
     * gui_get_time_seconds() when a stream is opened, at the same point
     * the sector grid is reset. */
    double sessionT0;

    /* Per-GNSS filter for marker mode.  0 = show all constellations;
     * 1..7 = show only that gnss_id (G=1, R=2, E=3, J=4, C=5, S=6, I=7).
     * Toggled by clicking a legend chip in gui_sky_window.c.  Tracks for
     * hidden SVs stay in memory and reappear when the filter is cleared. */
    int filter_gnss_id;
} SkyPlotState;

/* ── C/N0 vs elevation accumulator (Signal Quality window) ───────────────
 * Fed on EVERY MSM epoch, deliberately not gated by SKY_TRACK_INTERVAL_S.
 * The 60 s trail sampling is right for sky trails but far too coarse for a
 * scatter: at one point per SV per minute the cloud takes hours to become
 * readable.  At a typical 1 Hz epoch with ~38 SVs this fills in seconds.
 *
 * Two parts with different jobs:
 *   - a **cell grid**, counting how often each (elevation, C/N0) square
 *     was hit, which draws the cloud;
 *   - unbounded per-constellation elevation-bin sums, which drive the mean
 *     overlay in coarser bins, where a mean is worth taking.
 *
 * The grid replaced a 32768-sample ring of raw points, for two reasons the
 * Android app met first (`android/design/views.md`):
 *
 *   - **A ring forgets.** At ~38 SV/s it held about fourteen minutes, so a
 *     plot labelled "whole session" showed the last quarter-hour while the
 *     mean line beneath it spoke for the whole run. The grid counts every
 *     sample for the life of the session in a *fixed* 202 kB -- half what
 *     the ring cost -- because a cell hit a million times is still a cell.
 *   - **A point cloud cannot show density.** Ten thousand samples in one
 *     square looked exactly like one, which flattered a station that sits
 *     at a single elevation. A count per cell can be shaded.
 */
#define SIG_EL_STEP       1.0     /* degrees per cell */
#define SIG_EL_CELLS      91      /* 0..90 degrees    */
/*
 * One whole decibel per cell, and not finer.
 *
 * C/N0 arrives quantised, and how coarsely depends on the message: MSM4
 * and MSM5 carry six bits -- whole decibels, nothing between them. A
 * half-decibel cell can then only ever fill every second row, and the
 * plot draws a blank row between every filled one, which reads as
 * horizontal white lines through the cloud. The station is doing that,
 * not the renderer. A whole decibel is the coarsest any stream delivers,
 * so at this size MSM6/MSM7 at a sixteenth and the legacy messages at a
 * quarter simply land in the same row.
 */
#define SIG_CN0_STEP      1.0     /* dB-Hz per cell   */
#define SIG_CN0_CELLS     71      /* 0..70 dB-Hz      */

/* Coarser bins for the mean overlay: a mean over a 1-degree slice is as
 * noisy as the samples in it, and the line is there to be readable. */
#define SIG_EL_BIN_DEG    5.0
#define SIG_EL_BINS       18      /* 18 * 5 = 90 degrees */

/** @brief Occupancy grid + binned means for the Signal Quality window. */
typedef struct {
    /** Hits per (constellation, elevation cell, C/N0 cell). */
    unsigned int cell[SV_EPH_MAX_GNSS][SIG_EL_CELLS][SIG_CN0_CELLS];
    double binSum[SV_EPH_MAX_GNSS][SIG_EL_BINS];  /**< sum of C/N0 per bin */
    long   binCnt[SV_EPH_MAX_GNSS][SIG_EL_BINS];  /**< samples per bin */
    long   total;                                 /**< lifetime sample count */
} SigCnrState;

/** @brief Azimuth-bin count per elevation band.  Defined in gui_sky_window.c. */
extern const int sky_az_bins_per_band[SKY_N_EL_BANDS];

/**
 * @struct SkySatUpdate
 * @brief One-shot update payload posted from the worker thread per MSM epoch.
 *
 * Allocated with HeapAlloc(GetProcessHeap(), 0, count*sizeof(SkySatUpdate)).
 * Worker passes pointer + count via WM_APP_SKY_UPDATE; UI handler frees it.
 */
typedef struct {
    int   gnss_id;
    int   prn;
    float az_deg;
    float el_deg;
    float cnr_dbhz;
    float roti;            /* median-window ROTI, TECU/min; -1 = unknown */
    int   observed_flag;   /* 1 = was in this MSM frame's sat-mask; 0 = expected via eph only */
} SkySatUpdate;

/**
 * @struct AppState
 * @brief Holds all window handles and runtime state for the GUI.
 */
typedef struct {
    /* ── Main window ──────────────────────────────────────── */
    HWND hMain;

    /* ── Connection settings: Edit controls ───────────────── */
    HWND hEditCaster;
    HWND hEditPort;
    HWND hEditMountpoint;
    HWND hEditUsername;
    HWND hEditPassword;
    HWND hEditLatitude;
    HWND hEditLongitude;

    /* ── Action buttons ───────────────────────────────────── */
    HWND hBtnLoadConfig;
    HWND hBtnSaveConfig;
    HWND hBtnGenerate;
    HWND hBtnGetMounts;
    HWND hBtnOpenStream;
    HWND hBtnCloseStream;
    HWND hBtnMapPick;       /* "Map" button: open browser map */
    HWND hBtnMapPaste;      /* "<<" button: paste coords from clipboard */

    /* ── Mountpoint ListView ──────────────────────────────── */
    HWND hLvMountpoints;

    /* ── Tab control + child panels ───────────────────────── */
    HWND hTabOutput;
    HWND hEditLog;
    HWND hLvMsgStats;
    HWND hLvSatellites;
    HWND hLvStreamHealth;

    /* ── Status bar ───────────────────────────────────────── */
    HWND hStatusBar;

    /* ── Group boxes ──────────────────────────────────────── */
    HWND hGroupConnection;
    HWND hGroupActions;
    HWND hGroupEph;

    /* ── Labels (STATIC controls) ─────────────────────────── */
    HWND hLblCaster;
    HWND hLblPort;
    HWND hLblMountpoint;
    HWND hLblUsername;
    HWND hLblPassword;
    HWND hLblLatitude;
    HWND hLblLongitude;

    /* ── Ephemeris stream controls ───────────────────────── */
    HWND hLblEphCaster, hLblEphPort, hLblEphMountpoint;
    HWND hLblEphUsername, hLblEphPassword;
    HWND hEditEphCaster, hEditEphPort, hEditEphMountpoint;
    HWND hEditEphUsername, hEditEphPassword;

    /* ── Worker thread state ──────────────────────────────── */
    HANDLE hWorkerThread;
    volatile BOOL bStopRequested;
    volatile BOOL bWorkerRunning;

    /* ── Ephemeris worker thread (optional secondary stream) */
    HANDLE hWorkerThreadEph;
    volatile BOOL bStopRequestedEph;
    volatile BOOL bWorkerRunningEph;

    /* ── Configuration snapshot (used by worker) ──────────── */
    NTRIP_Config config;

    /* ── Log pipe handles ─────────────────────────────────── */
    int pipeFds[2];
    int savedStdout;
    int savedStderr;

    /* ── Real-time message statistics ─────────────────────── */
    GuiMsgStat msgStats[GUI_MAX_MSG_TYPES];

    /* ── Real-time satellite statistics ───────────────────── */
    SatStatsSummary satStats;

    /* Per-constellation satellites-in-view and C/N0, copied from the
     * session's snapshot by the worker and read by the UI thread.
     *
     * Distinct from satStats above, which counts every satellite seen
     * since the session opened and never forgets one.  This is the
     * current view over a five-second window, so a setting satellite
     * leaves it.  Both are shown: "40 seen, 38 in view" is the useful
     * reading, and either number alone invites the wrong one.
     *
     * Written by the worker and read by the UI without a lock, matching
     * how satStats is already handled -- a torn read costs one stale
     * repaint, which is not worth a critical section on the frame path. */
    NsGnssStats gnssStats[NS_MAX_GNSS];
    int         nGnssStats;

    /* Per-satellite ionospheric view, copied from the session by the
     * worker each frame; read by the Ionosphere window and the sky
     * overlay on the UI thread.  Same lock-free single-writer contract
     * as gnssStats above.  The sig_a/sig_b labels point at static tables
     * in the parser, so they are safe to read from any thread. */
    IonoSatView ionoView[64];
    int         nIonoView;

    /* Ionosphere sky window: heatmap vs per-dot track presentation,
     * toggled with the space bar in that window. */
    BOOL ionoSkyHeatmap;

    /* The session's full statistics snapshot, refreshed once a second
     * from NS_EV_STATS.  Kept so File > Export Statistics writes exactly
     * what the monitoring daemon publishes, through the same serialisers
     * -- an exported file and a Munin sample describe the stream the
     * same way rather than in two dialects. */
    NsStatsSnapshot lastStats;
    BOOL            haveStats;

    /* ── Stability (View > Stability) ──────────────────────────────
     * Tier 2: has this station *been* fit, over as much of the stream
     * as has been open. Not a run and not startable -- it accumulates
     * for the life of the session, which is the shape commissioning
     * wants: connect, work, and the verdict is there when you look.
     *
     * Here rather than in the window for the same reason as the check
     * below: an hour of evidence must survive its window being closed,
     * and SrState cannot be rebuilt from a repaint.
     *
     * reportFromCapture is kept so Restart cannot quietly turn a replay
     * into a live run and begin inventing availability figures for it. */
    /* ── Thresholds ────────────────────────────────────────────────
     * The standard every verdict in this process is judged by.
     * Built-in until File > Load Thresholds says otherwise, and
     * remembered across restarts so an installer who works to one
     * standard does not have to reload it every morning. */
    Thresholds    thresholds;
    char          thresholdsPath[MAX_PATH];
    char          thresholdsFp[16];

    SrState       reportRun;
    StationReport reportOut;
    BOOL          reportHave;
    BOOL          reportFromCapture;
    double        reportLastSample;   /* stream time of the last sample */

    /* ── Station check (View > Station Check) ──────────────────────
     * A bounded acceptance run: the same engine the CLI's --check and
     * the Android station mode use, over the stream that is already
     * open.
     *
     * The run state lives here rather than in the window because it
     * must outlive it.  Closing the window mid-run would otherwise
     * abandon a ninety-second test silently, and the sustain clocks
     * inside KpiRun cannot be rebuilt from a repaint.
     *
     * checkSettled freezes checkReport: a verdict that keeps moving is
     * not a sign-off, and this is what makes the result quotable. */
    KpiRun     checkRun;
    KpiReport  checkReport;
    BOOL       checkActive;
    BOOL       checkSettled;
    BOOL       checkHaveReport;
    /* Ended before a verdict settled.  Without this the header kept
     * reading "35 s elapsed, verdict held 4 of 60 s" over a run that had
     * been abandoned -- indistinguishable from one still counting.
     * checkEndWhy names which of the three ways it ended. */
    BOOL       checkAbandoned;
    char       checkEndWhy[64];
    double     checkStartedAt;        /* seconds, same clock as checkNow() */
    double     checkElapsedS;         /* frozen at settle, else live       */

    /* VRS assertions, advanced only when the station is a VRS.  The
     * gate test moves the rover and so is opt-in; see checkGateWanted. */
    VrsRun     checkVrs;
    VrsReport  checkVrsReport;
    BOOL       checkVrsActive;
    BOOL       checkGateWanted;       /* user ticked "include gate test"   */
    BOOL       checkGateStarted;

    /* Reconnect automatically after a drop, using the session layer's
     * backoff.  Off by default: the GUI has always required a manual
     * reconnect, and silently re-establishing a stream would change what
     * an unattended run means. */
    BOOL autoReconnect;

    /* The checkbox mirroring autoReconnect in the Actions row; kept in
     * sync with the Tools-menu item, whichever one is used. */
    HWND hChkReconnect;

    /* Notification-area ("tray") state.  minimiseToTray is the user's
     * preference; trayIconShown tracks whether the icon is currently
     * registered, so it is added and removed exactly once -- a stale
     * icon left behind after exit is the classic failure here. */
    BOOL minimiseToTray;
    BOOL trayIconShown;

    /* ── Stream info (set by worker, read by UI) ─────────── */
    volatile LONG  streamBytes;       /* total data bytes received */
    volatile LONG  streamFormat;      /* 0=none, 1=RTCM3, 2=UBX, 3=SBF, 4=RT27, 5=LB2, 6=Unknown */
    char           sourceFormat[32];  /* Format string from sourcetable (e.g. "RTCM 3.2", "RT27") */
    char           sourceDetails[256]; /* Details string from sourcetable */
    /* The STR record's nav-system field ("GPS+GLO+GAL"), which is the
     * station's actual claim about constellations.  The 1005/1006
     * indicator bits cannot express BeiDou, so this is what KPI 8
     * compares against. */
    char           sourceNav[64];

    /* ── Advertised message types (parsed from sourceDetails) ──────
     * The sourcetable STR format-details field lists what the mountpoint
     * claims to send, as "1077(1),1087(1),1005(10)" -- type with its
     * interval in seconds.  advInterval[t] > 0 means type t is advertised
     * at that interval; 0 means it is not advertised at all.
     *
     * advValid is FALSE when no sourcetable entry could be obtained for
     * this mountpoint, in which case no advertised-vs-observed comparison
     * is possible and the Status column reads "unknown". */
    float advInterval[GUI_MAX_MSG_TYPES];
    BOOL  advValid;
    int   advCount;                   /* number of advertised types */
    BOOL  advAutoFetched;             /* TRUE if fetched implicitly on connect */

    /* ── Sourcetable position + station classification ─────────────
     * The STR line's declared position, used to cross-check the ARP the
     * station actually broadcasts in RTCM 1005/1006.  A large mismatch
     * usually means the base was registered with the caster using wrong
     * coordinates.
     *
     * That check is only meaningful for a FIXED base.  On a VRS or
     * nearest-base service the reference point legitimately follows the
     * rover, so classification has to happen first or every network
     * stream reads as a fault.  See stationType. */
    double sourceLat, sourceLon;      /* from the sourcetable STR line */
    BOOL   sourcePosValid;
    char   sourceNetwork[64];         /* Network field, also carries VRS hints */

    int    stationType;               /* STATION_* below */
    char   stationWhy[160];           /* which signal decided it, for display */

    /* Caster handshake, parsed from the response header the worker
     * already reads before the RTCM bytes begin. */
    NsHandshake handshake;
    LONG           streamBytesLast;   /* snapshot for rate calc (UI side) */
    double         streamRateTime;    /* timestamp of last rate calc */

    /* ── Stream health counters (written by workers, read by UI) ──
     * Frame-level integrity accounting.  These are deliberately kept
     * stream-level rather than per-message-type: analyze_rtcm_message()
     * reads the type field *before* validating the CRC, so on a corrupt
     * frame the type itself is untrustworthy and attributing the error
     * to it would be misleading.
     *
     * Written from worker threads via Interlocked*, read by the UI
     * timer -- same discipline as streamBytes above. */
    volatile LONG  healthFramesOk;     /* frames decoded with a valid CRC */
    volatile LONG  healthCrcErrors;    /* complete frames, CRC mismatch */
    volatile LONG  healthResyncs;      /* preamble re-syncs after bogus length */

    /* ── Splitter between mountpoint list and tab control ── */
    int  splitterLvH;         /* current mountpoint ListView height (pixels) */
    BOOL splitterDragging;    /* TRUE while the user is dragging */
    int  splitterDragStartY;  /* mouse Y at start of drag */
    int  splitterDragStartH;  /* lvH at start of drag */

    /* ── Detail windows (one per open message type) ──────── */
    HWND hDetailWnds[GUI_MAX_MSG_TYPES]; /* NULL if not open */

    /* ── Last decoded text per message type ──────────────── */
    /* HeapAlloc'd string with \r\n line endings, ready for the
     * EDIT control.  Replaced on every new frame; freed when a
     * new stream is started or the application exits.
     * Only ever touched on the UI thread (message handlers),
     * so no locking is needed. */
    char *lastDecodedText[GUI_MAX_MSG_TYPES];

    /* ── Sky-plot window (floating, optional) ────────────── */
    /* hSkyWnd is NULL when closed; cleared by the sky window's
     * WM_DESTROY.  When the sky window is destroyed it also stashes
     * its un-minimised screen rect here so the next open restores
     * the same size and position. */
    HWND hSkyWnd;
    RECT skyWndRect;
    BOOL skyWndRectValid;
    RECT ionoSkyWndRect;        /* Ionosphere Sky remembers its own size */
    BOOL ionoSkyWndRectValid;

    /* Live sky-plot model.  Written by WM_APP_SKY_UPDATE on the UI
     * thread; read on the UI thread during WM_PAINT of hSkyWnd. */
    SkyPlotState skyState;

    /* Open SV-detail popups, indexed by (gnss_id, prn-1).  Slot is set
     * when a window opens, cleared by the window's WM_CLOSE handler. */
    HWND hSvDetailWnds[SV_EPH_MAX_GNSS][SV_EPH_MAX_SATS_PER_GNSS];

    /* RTCM stream capture.  The **session layer** writes the file --
     * ns_capture_start / ns_capture_stop -- and the session belongs to
     * the worker thread, so the menu cannot call them directly.  These
     * carry the request across and the answer back, the same way the
     * GGA uplink is driven from worker-read state.
     *
     * Proven interchangeable with the GUI's own former fwrite: one
     * stream written by both produced byte-identical files
     * (SHA-256 81c065e6..., 121,467 bytes) -- cli-track.md, V6. */
    CRITICAL_SECTION  csRtcmDump;
    BOOL              csRtcmDumpInit;       /* TRUE after InitializeCS */
    int               captureReq;           /* 0 idle, 1 start, 2 stop  */
    char              captureReqPath[MAX_PATH];  /* the path to start   */
    BOOL              captureActive;        /* the worker's answer      */
    char              capturePath[MAX_PATH];
    unsigned long     captureBytes;

    /* RTCM file replay.  Set by the File menu before launching
     * WorkerReplayRtcm; the worker reads frames from this path. */
    char              replayPath[MAX_PATH];

    /* ── VRS / nearby-service analysis ──────────────────────────────
     * Distance is updated by the status-bar timer (haversine between
     * the rover GGA position currently in use and the broadcast
     * 1005/1006 ARP).  vrsDistanceValid stays FALSE until both ends
     * are known. */
    double vrsDistanceKm;
    BOOL   vrsDistanceValid;

    /* Rolling 5-minute distance history for the VRS Monitor strip
     * chart.  Written by the status-bar timer once per second; read
     * by gui_vrs_window.c during paint.  Simple ring buffer. */
#define VRS_DIST_BUFFER_N 300
    float  vrsDistHistKm[VRS_DIST_BUFFER_N];   /* km; NaN = no sample */
    int    vrsDistHistHead;                    /* next write index */
    int    vrsDistHistCount;                   /* 0..N */

    /* Unique ARP positions seen this session.  Each entry is a (lat,
     * lon) snapshot taken whenever 1005/1006 reports an ARP that
     * differs from the previous one by more than VRS_ARP_DELTA_M.
     * Drawn as small dots on the VRS Monitor polar plot to surface
     * VRS hand-overs / nearest-station swaps. */
#define VRS_ARP_HIST_N    32
    double vrsArpHistLat[VRS_ARP_HIST_N];
    double vrsArpHistLon[VRS_ARP_HIST_N];
    int    vrsArpHistCount;

    /* GGA send-control state.
     *   ggaSendEnabled  -- master switch (Tools menu toggle).  When
     *                      FALSE the obs worker suppresses periodic
     *                      GGA, used to verify GGA-gated behaviour.
     *   ggaOverrideValid -- set TRUE when the user has injected a
     *                      test GGA position (Position-shift tests);
     *                      the worker uses ggaOverrideLat/Lon
     *                      instead of config.LATITUDE/LONGITUDE. */
    volatile BOOL    ggaSendEnabled;
    volatile BOOL    ggaOverrideValid;
    volatile double  ggaOverrideLat;
    volatile double  ggaOverrideLon;
    volatile double  ggaCurrentLat;   /* lat actually being sent this tick */
    volatile double  ggaCurrentLon;   /* lon actually being sent this tick */
    volatile LONG    ggaSendCount;    /* total GGAs sent since stream open */
    volatile LONG    ggaLastSendUnix; /* unix-time of last successful GGA send */

    /* Throttle for the VRS position-shift buttons.  When the user
     * clicks N/E/S/W in the VRS Monitor we stamp the current
     * ggaSendCount here; the buttons stay disabled until ggaSendCount
     * exceeds this value -- i.e. until the worker has actually
     * transmitted a GGA with the new position to the caster.  -1
     * means "no shift pending" (buttons free).  Reset button is
     * always enabled. */
    volatile LONG    ggaShiftRequestedAtCount;

    /* Unix-time of the most recent shift-button click.  Used by
     * OnStreamDone to decide whether the disconnect should be
     * attributed to the GGA shift -- if it happened within 60 s of a
     * shift the log line names the likely cause (single-station
     * caster rejecting an out-of-coverage position).  0 = never. */
    volatile LONG    ggaLastShiftUnix;

    /* VRS Monitor window (floating, optional) -- same lifecycle
     * convention as the Sky Plot window. */
    HWND hVrsWnd;
    RECT vrsWndRect;
    BOOL vrsWndRectValid;

    /* Signal Quality window (floating, optional) -- same lifecycle
     * convention as the Sky Plot and VRS Monitor windows. */
    HWND hSignalWnd;
    RECT signalWndRect;
    BOOL signalWndRectValid;
    SigCnrState sigCnr;   /**< C/N0 vs elevation accumulator */

    /* Session History window (floating, optional) -- same lifecycle
     * convention as the other floating windows. */
    HWND hHistWnd;
    HWND hIonoWnd;
    HWND hIonoSkyWnd;
    RECT histWndRect;
    BOOL histWndRectValid;
    HistState hist;       /**< session-history ring buffer */

    /* Station Check window (floating, optional) -- same lifecycle
     * convention as the other floating windows.  The run it displays
     * lives in the check* fields above and outlives this handle. */
    HWND hCheckWnd;
    RECT checkWndRect;
    BOOL checkWndRectValid;

    HWND hReportWnd;
    RECT reportWndRect;
    BOOL reportWndRectValid;

} AppState;

/**
 * @brief Retrieve AppState pointer from a window handle.
 */
static inline AppState* GetAppState(HWND hwnd) {
    return (AppState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

/* ── Function declarations (implemented across gui_*.c files) ────────
 *
 * @note **Threading.**  Everything here runs on the UI thread except the
 *       four `Worker*` entry points, which run on their own thread and
 *       communicate only by `PostMessage`.  The rule that keeps this
 *       safe: a worker never touches a control handle, and the UI thread
 *       never blocks on a worker.  The few AppState fields written by a
 *       worker and read by the UI are marked where they are declared.
 */

/* ── gui_layout.c ──────────────────────────────────────────────────── */

/**
 * @brief Create every child control of the main window.
 *
 * Called once from `WM_CREATE`.  Stores each control's handle in
 * @p state, which is how the rest of the program reaches them.
 *
 * @param hwnd  Main window.
 * @param state Application state; receives the control handles.
 */
void CreateControls(HWND hwnd, AppState *state);

/**
 * @brief Reposition the child controls after a resize.
 *
 * Called from `WM_SIZE`.  Layout is computed rather than stored, so the
 * window has no minimum-size assumptions beyond `WM_GETMINMAXINFO`.
 *
 * @param hwnd   Main window.
 * @param state  Application state holding the control handles.
 * @param width  New client-area width, pixels.
 * @param height New client-area height, pixels.
 */
void ResizeControls(HWND hwnd, AppState *state, int width, int height);

/* ── gui_events.c ──────────────────────────────────────────────────── */

/**
 * @brief Main window procedure.
 *
 * Handles the window lifecycle, the menu and buttons, the 1 Hz status
 * timer, and every `WM_APP_*` message the worker threads post.
 *
 * @return Result for the handled message, or `DefWindowProc` otherwise.
 */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief Built-in thresholds, overlaid by whichever policy was loaded
 *        last time this program ran.
 *
 * Called before any window can judge anything.  A remembered file that
 * has since been deleted or edited badly is reported once and then
 * ignored — it must not stop the program starting, and it must not
 * silently become a different standard either.
 */
void GuiThresholdsInit(AppState *state);

/**
 * @brief Apply a policy file, reporting any refusal to the user.
 *
 * Applied over the *built-in* values rather than over whatever was
 * loaded before, so two policies cannot half-merge into a standard
 * nobody wrote.
 *
 * @param quiet TRUE when restoring at startup rather than at the user's
 *              request.
 */
BOOL GuiThresholdsLoad(AppState *state, const char *path, BOOL quiet);

/* ── gui_thread.c ──────────────────────────────────────────────────────
 *
 * Each worker takes the AppState pointer as its `LPVOID param`, returns
 * 0, and reports progress by posting to `state->hMain`.  None of them
 * touches a control directly.
 */

/**
 * @brief Fetch the caster's sourcetable and hand it to the UI.
 *
 * Posts `WM_APP_MOUNT_RESULT` with the raw table as a heap string, which
 * the UI thread parses and frees.
 *
 * @param param AppState pointer.
 * @return 0 always; failure is reported through the posted message.
 */
DWORD WINAPI WorkerGetMountpoints(LPVOID param);

/**
 * @brief Run the observation stream until stopped.
 *
 * Drives an @ref NtripSession and posts `WM_APP_STREAM_INFO` as the
 * stream is characterised, `WM_APP_SOURCETABLE` if it fetched one to
 * resolve a typed-in mountpoint, and `WM_APP_STREAM_DONE` on exit.
 *
 * @param param AppState pointer.
 * @return 0 always.
 */
DWORD WINAPI WorkerOpenStream(LPVOID param);

/**
 * @brief Run the optional secondary ephemeris stream.
 *
 * Feeds only the `sv_ephemeris` cache, which the sky plot needs when the
 * observation mountpoint broadcasts no ephemeris messages.  Reports
 * through the log rather than the stream messages, since it has no
 * bearing on the observation stream's state.
 *
 * @param param AppState pointer.
 * @return 0 always.
 */
DWORD WINAPI WorkerOpenEphStream(LPVOID param);

/**
 * @brief Replay a captured `.rtcm3` file through the live code path.
 *
 * Uses the same session layer and the same event handler as
 * @ref WorkerOpenStream, so a capture is analysed exactly as a live
 * stream is, and posts the same messages.
 *
 * @param param AppState pointer; the path is `state->replayPath`.
 * @return 0 always.
 */
DWORD WINAPI WorkerReplayRtcm(LPVOID param);

/* ── gui_log.c ─────────────────────────────────────────────────────────
 *
 * The decoders write to stdout with printf.  Rather than rewrite them
 * for the GUI, stdout and stderr are redirected into a pipe and drained
 * into the Log tab, so the CLI and the GUI show identical decoder text.
 */

/**
 * @brief Redirect stdout and stderr into a pipe for the Log tab.
 *
 * Silently does nothing if the pipe cannot be created; the program still
 * runs, it just shows no decoder output.
 *
 * @param state Application state; receives the pipe descriptors.
 */
void LogRedirectStart(AppState *state);

/**
 * @brief Restore stdout and stderr and close the pipe.
 *
 * Safe to call when redirection is not active.
 *
 * @param state Application state.
 */
void LogRedirectStop(AppState *state);

/**
 * @brief Drain whatever is waiting in the pipe into the Log control.
 *
 * Called from the log timer and again after bursts of message activity:
 * `WM_TIMER` is low priority, so at high message rates Windows may not
 * deliver it for long stretches and output would otherwise sit in the
 * pipe. Reads only what is already available, so it never blocks the UI.
 *
 * @param state Application state.
 */
void LogPumpTimer(AppState *state);

/* ── gui_parsers.c ─────────────────────────────────────────────────── */

/**
 * @brief Parse a raw sourcetable into the mountpoint ListView.
 *
 * Fills one row per STR entry and computes each mountpoint's distance
 * from the supplied position, which is what makes the list sortable by
 * proximity — the usual way of choosing a base.
 *
 * @param raw      Raw sourcetable text from the caster.
 * @param listview Destination ListView; cleared first.
 * @param userLat  Reference latitude, degrees, for the distance column.
 * @param userLon  Reference longitude, degrees.
 */
void ParseMountTable(const char *raw, HWND listview, double userLat, double userLon);

/**
 * @brief Find one mountpoint's STR line in a raw sourcetable.
 *
 * Extracts the Format (field 3) and format-details (field 4) columns for
 * @p mountpoint.  A leading '/' on either name is ignored, and the match
 * is case-insensitive, matching the caster conventions users hit in
 * practice.
 *
 * @return TRUE if the mountpoint was found and the outputs were written.
 */
BOOL SourcetableFindMountpoint(const char *raw, const char *mountpoint,
                               char *fmt_out, size_t fmt_sz,
                               char *det_out, size_t det_sz,
                               char *nav_out, size_t nav_sz);

/**
 * @brief Parse a sourcetable format-details string into advertised intervals.
 *
 * Accepts the usual "1077(1),1087(1),1005(10)" form, tolerates whitespace,
 * and treats a bare type with no parenthesised interval as advertised with
 * an unknown interval (recorded as -1).
 *
 * @param details   Format-details string; may be NULL or empty.
 * @param out       Array of GUI_MAX_MSG_TYPES floats, zeroed by this call.
 * @return number of advertised message types found.
 */
int ParseAdvertisedTypes(const char *details, float *out);

/**
 * @brief Case-insensitive substring search (strstr that ignores case).
 *
 * @return Pointer into @p haystack at the first match, or NULL.
 */
const char *stristr(const char *haystack, const char *needle);


/* ── gui_events.c — config helpers ─────────────────────────────────────
 *
 * The edit fields are the authority while the user is typing, and
 * `state->config` is the authority once anything acts on it.  These two
 * move the values across, and are called at the boundaries: read the
 * fields before connecting, write them after loading a file.
 */

/**
 * @brief Copy the connection edit fields into `state->config`.
 *
 * Call before anything reads the config -- connecting, saving, or
 * launching a worker -- so edits made since the last sync are included.
 *
 * @param state Application state; `config` is overwritten from the fields.
 */
void GuiToConfig(AppState *state);

/**
 * @brief Fill the connection edit fields from `state->config`.
 *
 * The reverse of @ref GuiToConfig, used after loading a configuration
 * file or generating a template.
 *
 * @param state Application state; the fields are overwritten.
 */
void ConfigToGui(AppState *state);

/* ── gui_events.c — RTCM message description lookup ────────────────── */

/**
 * @brief Human-readable description of an RTCM message type.
 *
 * For example 1077 gives "MSM7 GPS".  Used for the Msg Stats
 * Description column and for detail-window titles.
 *
 * @param msg_type RTCM message number.
 * @return A static string, never NULL; an empty string for an unknown
 *         type, so callers can print it unconditionally.
 */
const char* RtcmMsgDescription(int msg_type);

/* ── gui_detail.c ──────────────────────────────────────────────────── */

/**
 * @brief Open a live detail window for one RTCM message type.
 *
 * The window shows each frame of that type as it arrives, decoded.  It
 * registers its class on first use and titles itself from
 * @ref RtcmMsgDescription, e.g. "RTCM 1077 - MSM7 GPS".
 *
 * The caller records the handle in `state->hDetailWnds[msg_type]`; the
 * window posts `WM_APP_DETAIL_CLOSED` when it closes so that slot can be
 * cleared.
 *
 * @param hInst    Module instance.
 * @param hParent  Owner window.
 * @param msg_type RTCM message number to follow.
 * @return The new window, or NULL if creation failed.
 */
HWND CreateDetailWindow(HINSTANCE hInst, HWND hParent, int msg_type);

#endif /* GUI_STATE_H */
