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
#include "ntrip_handler.h"

/* ── Application constants ────────────────────────────────── */
#define APP_TITLE       "NTRIP-Analyser"
#define APP_CLASS_NAME  "NtripAnalyserGuiClass"
#define APP_MIN_WIDTH   800
#define APP_MIN_HEIGHT  600
#define APP_INIT_WIDTH  1024
#define APP_INIT_HEIGHT 768

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
#define DETAIL_CLASS_NAME  "NtripDetailClass"

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

    /* ── Labels (STATIC controls) ─────────────────────────── */
    HWND hLblCaster;
    HWND hLblPort;
    HWND hLblMountpoint;
    HWND hLblUsername;
    HWND hLblPassword;
    HWND hLblLatitude;
    HWND hLblLongitude;

    /* ── Worker thread state ──────────────────────────────── */
    HANDLE hWorkerThread;
    volatile BOOL bStopRequested;
    volatile BOOL bWorkerRunning;

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
    volatile LONG  streamFormat;      /* 0=unknown, 1=RTCM3, 2=UBX, 3=other */
    LONG           streamBytesLast;   /* snapshot for rate calc (UI side) */
    double         streamRateTime;    /* timestamp of last rate calc */

    /* ── Splitter between mountpoint list and tab control ── */
    int  splitterLvH;         /* current mountpoint ListView height (pixels) */
    BOOL splitterDragging;    /* TRUE while the user is dragging */
    int  splitterDragStartY;  /* mouse Y at start of drag */
    int  splitterDragStartH;  /* lvH at start of drag */

    /* ── Detail windows (one per open message type) ──────── */
    HWND hDetailWnds[GUI_MAX_MSG_TYPES]; /* NULL if not open */

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
