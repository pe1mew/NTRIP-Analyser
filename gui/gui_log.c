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

/* Append text to the log control at the end, leaving the caret there. */
static void EditAppend(HWND hEdit, const char *text)
{
    int len = GetWindowTextLength(hEdit);
    SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

/**
 * @brief Append pipe text to the log control, giving it line breaks.
 *
 * An EDIT control breaks a line on CR LF and on nothing else, while
 * everything behind the pipe writes plain "\n" as C has always done.
 * The pipe does not bridge that: both of its ends are text-mode, so the
 * expansion the write side performs the read side folds straight back,
 * and the control receives bare LF -- which it renders as no break at
 * all, running a whole session's log into one line.
 *
 * Expanding here, at the single point where pipe text enters the
 * control, is what keeps the ~200 printf calls behind it free of it.
 */
static void EditAppendLf(HWND hEdit, const char *text)
{
    char   out[4096];
    size_t o = 0;

    for (const char *p = text; *p; p++) {
        if (*p == '\n' && (o == 0 || out[o - 1] != '\r'))
            out[o++] = '\r';
        out[o++] = *p;

        if (o >= sizeof(out) - 2) {          /* room for CR LF and NUL */
            out[o] = '\0';
            EditAppend(hEdit, out);
            o = 0;
        }
    }
    if (o) {
        out[o] = '\0';
        EditAppend(hEdit, out);
    }
}

/* Documented in gui_state.h -- the contract lives with the declaration.
 *
 * Creates a pipe, saves the original stdout/stderr descriptors,
 * and redirects them to the pipe's write end.
 */
void LogRedirectStart(AppState *state)
{
    /* Started from Explorer -- which is how the program is normally
     * started -- there is no console, so stdout and stderr have no
     * descriptor at all: _fileno returns a negative number and every
     * _dup2 below fails without saying so.  The pipe is then created
     * and pumped faithfully for the whole session with nothing ever
     * written into it, which is how a session's entire worker output
     * once went missing while the posted [EPH] lines came through.
     *
     * Attaching the streams to the null device first gives them a real
     * descriptor to redirect.  With a console present the streams
     * already have one and this costs nothing. */
    if (_fileno(stdout) < 0 && !freopen("NUL", "w", stdout)) return;
    if (_fileno(stderr) < 0 && !freopen("NUL", "w", stderr)) return;

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

/* Documented in gui_state.h -- the contract lives with the declaration.
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
            EditAppendLf(state->hEditLog, buf);
        }

        _close(state->pipeFds[0]);
        state->pipeFds[0] = -1;
    }
}

/* Documented in gui_state.h -- the contract lives with the declaration.
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
        EditAppendLf(state->hEditLog, buf);

        /* Re-check for more data */
        avail = 0;
    }
}
