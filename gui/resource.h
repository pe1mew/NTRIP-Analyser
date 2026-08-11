/**
 * @file resource.h
 * @brief Resource IDs for NTRIP-Analyser Windows GUI
 */

#ifndef RESOURCE_H
#define RESOURCE_H

/* ── Application icon ─────────────────────────────────────── */
#define IDI_APP_ICON            100

/* ── Menu bar ─────────────────────────────────────────────── */
#define IDR_MAIN_MENU           200

/* ── Menu items: File ─────────────────────────────────────── */
#define IDM_FILE_LOAD_CONFIG    9001
#define IDM_FILE_SAVE_CONFIG    9002
#define IDM_FILE_GENERATE       9003
#define IDM_FILE_EXIT           9004
#define IDM_FILE_SAVE_SKYPLOT   9005
#define IDM_FILE_LOAD_EPH       9006
#define IDM_FILE_RTCM_START     9007
#define IDM_FILE_RTCM_STOP      9008
#define IDM_FILE_RTCM_REPLAY    9009
/* 9010-9012 belong to the Connection block below, so this File item
 * takes the next free id rather than colliding with Get Mountpoints. */
#define IDM_FILE_EXPORT_STATS   9013

/* ── Menu items: Connection ───────────────────────────────── */
#define IDM_CONN_MOUNTPOINTS    9010
#define IDM_CONN_OPEN_STREAM    9011
#define IDM_CONN_CLOSE_STREAM   9012

/* ── Menu items: View ─────────────────────────────────────── */
#define IDM_VIEW_SKY_PLOT       9020
#define IDM_VIEW_VRS_MONITOR    9021
#define IDM_VIEW_SIGNAL_QUALITY 9022
#define IDM_VIEW_HISTORY        9023

/* ── Menu items: Tools ────────────────────────────────────── */
#define IDM_TOOLS_VRS_GGA_TOGGLE   9040  /* toggle auto-send GGA on/off */
#define IDM_TOOLS_AUTO_RECONNECT   9041  /* reconnect with backoff on drop */
#define IDM_TOOLS_TRAY_MINIMIZE    9042  /* minimise to the notification area */

/* ── Tray context menu ────────────────────────────────────── */
#define IDM_TRAY_RESTORE        3010
#define IDM_TRAY_EXIT           3011
/* The VRS position-shift / reset controls were moved into the VRS
 * Monitor window itself (in-plot N/E/S/W buttons + Reset top-left),
 * see resource IDs IDC_VRS_BTN_* below. */

/* ── Menu items: Help ─────────────────────────────────────── */
#define IDM_HELP_ABOUT          9030
#define IDM_HELP_GITHUB         9031

/* ── Connection settings: Edit controls ───────────────────── */
#define IDC_EDIT_CASTER         1001
#define IDC_EDIT_PORT           1002
#define IDC_EDIT_MOUNTPOINT     1003
#define IDC_EDIT_USERNAME       1004
#define IDC_EDIT_PASSWORD       1005
#define IDC_EDIT_LATITUDE       1006
#define IDC_EDIT_LONGITUDE      1007

/* ── Connection settings: Labels (static) ─────────────────── */
#define IDC_LBL_CASTER          1050
#define IDC_LBL_PORT            1051
#define IDC_LBL_MOUNTPOINT      1052
#define IDC_LBL_USERNAME        1053
#define IDC_LBL_PASSWORD        1054
#define IDC_LBL_LATITUDE        1055
#define IDC_LBL_LONGITUDE       1056

/* ── Action buttons ───────────────────────────────────────── */
#define IDC_BTN_LOAD_CONFIG     1101
#define IDC_BTN_SAVE_CONFIG     1102
#define IDC_BTN_GENERATE        1103
#define IDC_BTN_GET_MOUNTS      1110
#define IDC_BTN_OPEN_STREAM     1111
#define IDC_BTN_CLOSE_STREAM    1120
#define IDC_BTN_MAP_PICK        1130    /* "Map" button: open browser map */
#define IDC_BTN_MAP_PASTE       1131    /* "<<" button: paste coords from clipboard */

/* ── Mountpoint ListView ──────────────────────────────────── */
#define IDC_LV_MOUNTPOINTS      1301

/* ── Tab control + child panels ───────────────────────────── */
#define IDC_TAB_OUTPUT          1401
#define IDC_EDIT_LOG            1402
#define IDC_LV_MSG_STATS        1403
#define IDC_LV_SATELLITES       1404
#define IDC_LV_STREAM_HEALTH    1405

/* ── Status bar ───────────────────────────────────────────── */
#define IDC_STATUSBAR           1500

/* ── Group boxes ──────────────────────────────────────────── */
#define IDC_GROUP_CONNECTION    1601
#define IDC_GROUP_ACTIONS       1602
#define IDC_GROUP_EPH           1603

/* ── Ephemeris stream: Edit controls ──────────────────────── */
#define IDC_EDIT_EPH_CASTER     1201
#define IDC_EDIT_EPH_PORT       1202
#define IDC_EDIT_EPH_MOUNTPOINT 1203
#define IDC_EDIT_EPH_USERNAME   1204
#define IDC_EDIT_EPH_PASSWORD   1205

/* ── Ephemeris stream: Labels (static) ────────────────────── */
#define IDC_LBL_EPH_CASTER      1250
#define IDC_LBL_EPH_PORT        1251
#define IDC_LBL_EPH_MOUNTPOINT  1252
#define IDC_LBL_EPH_USERNAME    1253
#define IDC_LBL_EPH_PASSWORD    1254

/* ── Custom window messages ───────────────────────────────── */
#define WM_APP_MOUNT_RESULT     (WM_APP + 1)
#define WM_APP_STREAM_DONE      (WM_APP + 2)
#define WM_APP_LOG_LINE         (WM_APP + 5)
#define WM_APP_STATUS_UPDATE    (WM_APP + 6)
#define WM_APP_STAT_UPDATE      (WM_APP + 7)
#define WM_APP_SAT_UPDATE       (WM_APP + 8)
#define WM_APP_STREAM_INFO      (WM_APP + 9)
#define WM_APP_MSG_RAW          (WM_APP + 10)   /* raw RTCM frame for detail window */
#define WM_APP_DETAIL_CLOSED    (WM_APP + 11)   /* detail window closed: wParam=msg_type */
#define WM_APP_SKY_UPDATE       (WM_APP + 12)   /* sky-plot SV update: wParam=count, lParam=SkySatUpdate* */
#define WM_APP_SOURCETABLE      (WM_APP + 13)   /* sourcetable fetched on connect: lParam=heap string, UI frees */
#define WM_APP_TRAY             (WM_APP + 14)   /* notification-area callback: lParam=mouse message */

/* ── Detail window ───────────────────────────────────────── */
#define IDC_DETAIL_EDIT         1700
#define IDC_DETAIL_COPY         1701

/* ── VRS Monitor window: in-plot shift buttons ───────────── */
#define IDC_VRS_BTN_N           1800
#define IDC_VRS_BTN_E           1801
#define IDC_VRS_BTN_S           1802
#define IDC_VRS_BTN_W           1803
#define IDC_VRS_BTN_RESET       1804

/* ── Context menu IDs ────────────────────────────────────── */
#define IDM_CTX_SELECT_ALL      3001
#define IDM_CTX_COPY            3002

/* ── Timer IDs ────────────────────────────────────────────── */
#define IDT_LOG_PUMP            2001
#define IDT_STATUS_UPDATE       2002
#define IDT_SKY_CLOCK           2003

#endif /* RESOURCE_H */
