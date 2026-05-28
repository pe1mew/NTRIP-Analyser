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
#include "ntrip_handler.h"
#include "sv_ephemeris.h"

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
    int    count;
    double min_dt;
    double max_dt;
    double sum_dt;
    double last_time;
    bool   seen;
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

/** @brief A single past (az, el) sample for a satellite's track trail. */
typedef struct {
    float  az_deg;
    float  el_deg;
    double ts;            /**< gui_get_time_seconds() when the point was added */
} SkyTrackPoint;

/** @brief Ring buffer of past positions for one satellite (since stream open). */
#define SKY_TRACK_CAP   120     /* ~10 minutes at 5 s/point */
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
} SkyPlotState;

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

    /* ── Stream info (set by worker, read by UI) ─────────── */
    volatile LONG  streamBytes;       /* total data bytes received */
    volatile LONG  streamFormat;      /* 0=none, 1=RTCM3, 2=UBX, 3=SBF, 4=RT27, 5=LB2, 6=Unknown */
    char           sourceFormat[32];  /* Format string from sourcetable (e.g. "RTCM 3.2", "RT27") */
    char           sourceDetails[128]; /* Details string from sourcetable */
    LONG           streamBytesLast;   /* snapshot for rate calc (UI side) */
    double         streamRateTime;    /* timestamp of last rate calc */

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

    /* Live sky-plot model.  Written by WM_APP_SKY_UPDATE on the UI
     * thread; read on the UI thread during WM_PAINT of hSkyWnd. */
    SkyPlotState skyState;

    /* Open SV-detail popups, indexed by (gnss_id, prn-1).  Slot is set
     * when a window opens, cleared by the window's WM_CLOSE handler. */
    HWND hSvDetailWnds[SV_EPH_MAX_GNSS][SV_EPH_MAX_SATS_PER_GNSS];

    /* RTCM stream capture.  When @ref hRtcmDump is non-NULL the worker
     * thread writes each raw frame to the file.  Access serialised
     * through @ref csRtcmDump so the UI thread can fclose() safely
     * even while a write is in flight. */
    FILE             *hRtcmDump;
    CRITICAL_SECTION  csRtcmDump;
    BOOL              csRtcmDumpInit;       /* TRUE after InitializeCS */
    char              rtcmDumpPath[MAX_PATH];
    LONG              rtcmDumpBytes;        /* updated under the CS */

    /* RTCM file replay.  Set by the File menu before launching
     * WorkerReplayRtcm; the worker reads frames from this path. */
    char              replayPath[MAX_PATH];

} AppState;

/**
 * @brief Retrieve AppState pointer from a window handle.
 */
static inline AppState* GetAppState(HWND hwnd) {
    return (AppState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

/* ── Function declarations (implemented across gui_*.c files) */

/* gui_layout.c */
void CreateControls(HWND hwnd, AppState *state);
void ResizeControls(HWND hwnd, AppState *state, int width, int height);

/* gui_events.c */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* gui_thread.c */
DWORD WINAPI WorkerGetMountpoints(LPVOID param);
DWORD WINAPI WorkerOpenStream(LPVOID param);
DWORD WINAPI WorkerOpenEphStream(LPVOID param);
DWORD WINAPI WorkerReplayRtcm(LPVOID param);

/* gui_log.c */
void LogRedirectStart(AppState *state);
void LogRedirectStop(AppState *state);
void LogPumpTimer(AppState *state);

/* gui_parsers.c */
void ParseMountTable(const char *raw, HWND listview, double userLat, double userLon);

/* gui_events.c — config helpers */
void GuiToConfig(AppState *state);
void ConfigToGui(AppState *state);

/* gui_events.c — RTCM message description lookup */
const char* RtcmMsgDescription(int msg_type);

/* gui_detail.c */
HWND CreateDetailWindow(HINSTANCE hInst, HWND hParent, int msg_type);

#endif /* GUI_STATE_H */
