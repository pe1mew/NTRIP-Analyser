/**
 * @file gui_log.c
 * @brief stdout/stderr redirection to GUI log panel via pipe.
 *
 * Before a worker thread starts, stdout and stderr are redirected to
 * a pipe. A WM_TIMER callback reads from the pipe and appends text
 * to the log EDIT control. Restored when the worker finishes.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

#include "resource.h"
#include "gui_state.h"

#include <stdio.h>
#include <io.h>
#include <fcntl.h>

/**
 * @brief Redirect stdout and stderr to a pipe for GUI capture.
 *
 * Creates a pipe, saves the original stdout/stderr descriptors,
 * and redirects them to the pipe's write end.
 */
void LogRedirectStart(AppState *state)
{
    /* Create a pipe: pipeFds[0]=read, pipeFds[1]=write */
    if (_pipe(state->pipeFds, 8192, _O_TEXT) != 0) {
        return;  /* pipe creation failed */
    }

    /* Save original stdout/stderr file descriptors */
    state->savedStdout = _dup(_fileno(stdout));
    state->savedStderr = _dup(_fileno(stderr));

    /* Redirect stdout and stderr to the pipe write end */
    _dup2(state->pipeFds[1], _fileno(stdout));
    _dup2(state->pipeFds[1], _fileno(stderr));

    /* Make stdout/stderr line-buffered so output appears promptly */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/**
 * @brief Restore original stdout/stderr and close the pipe.
 */
void LogRedirectStop(AppState *state)
{
    /* Flush before restoring */
    fflush(stdout);
    fflush(stderr);

    /* Restore original stdout/stderr */
    if (state->savedStdout >= 0) {
        _dup2(state->savedStdout, _fileno(stdout));
        _close(state->savedStdout);
        state->savedStdout = -1;
    }
    if (state->savedStderr >= 0) {
        _dup2(state->savedStderr, _fileno(stderr));
        _close(state->savedStderr);
        state->savedStderr = -1;
    }

    /* Close the pipe write end (read end may still have data) */
    if (state->pipeFds[1] >= 0) {
        _close(state->pipeFds[1]);
        state->pipeFds[1] = -1;
    }

    /* Drain remaining data from the pipe read end */
    if (state->pipeFds[0] >= 0) {
        char buf[1024];
        HANDLE hRead = (HANDLE)_get_osfhandle(state->pipeFds[0]);
        DWORD avail = 0;
        while (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            int n = _read(state->pipeFds[0], buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';

            /* Append to log */
            int len = GetWindowTextLength(state->hEditLog);
            SendMessage(state->hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessage(state->hEditLog, EM_REPLACESEL, FALSE, (LPARAM)buf);
        }

        _close(state->pipeFds[0]);
        state->pipeFds[0] = -1;
    }
}

/**
 * @brief Timer callback: read available data from the pipe and append to log.
 *
 * Called from WM_TIMER with IDT_LOG_PUMP. Non-blocking: uses PeekNamedPipe
 * to check for available data before reading.
 */
void LogPumpTimer(AppState *state)
{
    if (state->pipeFds[0] < 0) return;

    HANDLE hRead = (HANDLE)_get_osfhandle(state->pipeFds[0]);
    DWORD avail = 0;

    while (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        char buf[2048];
        int toRead = (avail < sizeof(buf) - 1) ? (int)avail : (int)(sizeof(buf) - 1);
        int n = _read(state->pipeFds[0], buf, toRead);
        if (n <= 0) break;
        buf[n] = '\0';

        /* Append to log edit control */
        int len = GetWindowTextLength(state->hEditLog);
        SendMessage(state->hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(state->hEditLog, EM_REPLACESEL, FALSE, (LPARAM)buf);

        /* Re-check for more data */
        avail = 0;
    }
}
