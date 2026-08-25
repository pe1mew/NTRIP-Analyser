/**
 * @file Failure.kt
 * @brief What went wrong, in this app's own words, and what to tap next.
 *
 * GUI v3, P5.2 (`design/guiV3spec.md` §5). The core classifies the fault
 * and writes an English sentence for it; the CLI, the GUI and the daemon
 * print that sentence. This app does not: it maps the **code** to its
 * own `strings.xml`, which is what leaves room for a Dutch build later.
 * The core's sentence still arrives in `stats.failureDetail` and is what
 * the log carries, so the two are never far apart when one is wrong.
 *
 * The numbers mirror `NsFailure` in `src/core/ns_failure.h`, and a
 * mismatch would put the wrong sentence under a fault -- worse than no
 * sentence, because it sends the reader to the wrong field. They are
 * checked against the C enum by `tools/check_release.py`.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import android.content.Context

/** The `NsFailure` values, in the order the C enum declares them. */
object Failure {
    const val NONE = 0
    const val DNS = 1
    const val REFUSED = 2
    const val UNREACHABLE = 3
    const val TIMEOUT = 4
    const val NOT_NTRIP = 5
    const val AUTH = 6
    const val FORBIDDEN = 7
    const val NO_MOUNTPOINT = 8
    const val BUSY = 9
    const val REJECTED = 10
    const val DROPPED = 11
    const val STALLED = 12
    const val TLS_HANDSHAKE = 13
    const val TLS_CERT = 14
}

/**
 * Which field the reader should go and look at.
 *
 * A message that names the fault and does not offer the fix is half a
 * message, and on a phone the fix is a text field two taps away.
 */
enum class FailureFix { NONE, HOST, CREDENTIALS, MOUNTPOINT }

/** Where the fault points, for the connection dialog to open on. */
fun failureFix(code: Int): FailureFix = when (code) {
    Failure.DNS, Failure.REFUSED, Failure.UNREACHABLE,
    Failure.TIMEOUT, Failure.NOT_NTRIP,
    // A handshake failure points at the port/TLS pair the user typed;
    // a certificate failure points at the caster, which no field fixes.
    Failure.TLS_HANDSHAKE -> FailureFix.HOST
    Failure.AUTH, Failure.FORBIDDEN -> FailureFix.CREDENTIALS
    Failure.NO_MOUNTPOINT -> FailureFix.MOUNTPOINT
    else -> FailureFix.NONE
}

/**
 * The sentence this app shows for a failure, or null if there is none.
 *
 * @param settings supplies the host, port and mountpoint the sentences
 *        name. A message that says *"cannot find the caster"* without
 *        saying which one is a message about somebody else's problem.
 */
fun failureSentence(context: Context, code: Int, settings: CasterSettings): String? =
    when (code) {
        Failure.DNS -> context.getString(R.string.fail_dns, settings.caster)
        Failure.REFUSED ->
            context.getString(R.string.fail_refused, settings.caster, settings.port)
        Failure.UNREACHABLE -> context.getString(R.string.fail_unreachable, settings.caster)
        Failure.TIMEOUT ->
            context.getString(R.string.fail_timeout, settings.caster, settings.port)
        Failure.NOT_NTRIP ->
            context.getString(R.string.fail_not_ntrip, settings.caster, settings.port)
        Failure.AUTH -> context.getString(R.string.fail_auth)
        Failure.FORBIDDEN ->
            context.getString(R.string.fail_forbidden, settings.mountpoint)
        Failure.NO_MOUNTPOINT ->
            context.getString(R.string.fail_no_mountpoint, settings.mountpoint)
        Failure.BUSY -> context.getString(R.string.fail_busy)
        Failure.REJECTED -> context.getString(R.string.fail_rejected)
        Failure.DROPPED -> context.getString(R.string.fail_dropped)
        Failure.STALLED -> context.getString(R.string.fail_stalled)
        Failure.TLS_HANDSHAKE ->
            context.getString(R.string.fail_tls_handshake,
                settings.caster, settings.port)
        Failure.TLS_CERT -> context.getString(R.string.fail_tls_cert)
        else -> null
    }
