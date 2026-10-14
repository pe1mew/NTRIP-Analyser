package nl.pe1mew.ntripanalyser

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.security.MessageDigest

/**
 * Pro picks up where free left off (GH#5): free hands its saved
 * connections to pro, on the user's say-so.
 *
 * Pro starts free's [action] for a result; free checks who is asking,
 * asks the user, and returns the connections in the exchange format
 * (`docs/jsonConfigs.md`) inside the result -- a Binder transaction,
 * never a file. Option C of the issue: the two apps carry different Play
 * signing keys (read from both Play-installed APKs, 2026-09-13), so no
 * signature-level permission can join them, and **free's consent screen
 * is the security control**, not a courtesy.
 *
 * Each side checks the other's signer. A release build pins the other
 * edition's Play app signing certificate. A dev or bench build accepts
 * the key it was signed with itself, because both editions built on one
 * machine carry that machine's debug key -- and a dev build must never
 * hand anything to, or take anything from, a copy signed by someone
 * else.
 */
object Handover {

    /** What pro starts in free. */
    const val ACTION = "nl.pe1mew.ntripanalyser.action.SHARE_CONNECTIONS"

    /** The contract's version, so a later one can tell an older peer. */
    const val VERSION = 1

    const val EXTRA_VERSION = "nl.pe1mew.ntripanalyser.extra.VERSION"
    /** The connections, as the exchange format's text. */
    const val EXTRA_CONNECTIONS = "nl.pe1mew.ntripanalyser.extra.CONNECTIONS"
    /** Why nothing came back: one of the REFUSED_* values. */
    const val EXTRA_REFUSED = "nl.pe1mew.ntripanalyser.extra.REFUSED"

    const val REFUSED_BY_USER = "user"
    const val REFUSED_CALLER = "caller"
    const val REFUSED_NOTHING = "nothing"

    /** Play app signing certificates, SHA-256, read with `apksigner`
     *  from the Play-installed APKs (GH#5, 2026-09-13). */
    private const val PLAY_CERT_FREE =
        "3ef1c272fd319307389a33694aafc4928388e02654f29350174ef625dbd88090"
    private const val PLAY_CERT_PRO =
        "3f8a94b4a8b9e907166c3cc49a61a33a6b222ea564e3d4a4f6a8c643fffadf9b"

    /**
     * The other edition's package: this one's with the edition swapped,
     * keeping the build type's suffix -- `….pro.dev` pairs with
     * `….free.dev`, never with the Play app.
     */
    fun peerPackage(ownPackage: String): String =
        if (".pro" in ownPackage) ownPackage.replace(".pro", ".free")
        else ownPackage.replace(".free", ".pro")

    private fun sha256Hex(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }

    private fun hexBytes(hex: String): ByteArray =
        ByteArray(hex.length / 2) { hex.substring(2 * it, 2 * it + 2).toInt(16).toByte() }

    /** The certificates a package is signed with now, SHA-256, hex. */
    @Suppress("DEPRECATION")
    private fun signers(pm: PackageManager, pkg: String): List<String> = runCatching {
        if (Build.VERSION.SDK_INT >= 28) {
            val info = pm.getPackageInfo(pkg, PackageManager.GET_SIGNING_CERTIFICATES)
            val si = info.signingInfo ?: return emptyList()
            val certs = if (si.hasMultipleSigners()) si.apkContentsSigners
                        else si.signingCertificateHistory.takeLast(1).toTypedArray()
            certs.map { sha256Hex(it.toByteArray()) }
        } else {
            pm.getPackageInfo(pkg, PackageManager.GET_SIGNATURES)
                .signatures.orEmpty().map { sha256Hex(it.toByteArray()) }
        }
    }.getOrDefault(emptyList())

    /** What pro finds when it looks for free, before asking it anything. */
    enum class Peer { ABSENT, FOREIGN, GENUINE }

    /** Whether the other edition is installed here, and is the real one. */
    fun findPeer(context: Context): Peer {
        val pkg = peerPackage(context.packageName)
        val installed = runCatching {
            context.packageManager.getPackageInfo(pkg, 0); true
        }.getOrDefault(false)
        return when {
            !installed -> Peer.ABSENT
            isGenuinePeer(context, pkg) -> Peer.GENUINE
            else -> Peer.FOREIGN
        }
    }

    /**
     * Whether [pkg] is the genuine other edition: the right name, and
     * signed by the key this build trusts for it.
     */
    fun isGenuinePeer(context: Context, pkg: String?): Boolean {
        if (pkg == null || pkg != peerPackage(context.packageName)) return false
        val pm = context.packageManager
        val trusted = if (BuildConfig.BUILD_TYPE == "release") {
            if (Features.IS_PRO) PLAY_CERT_FREE else PLAY_CERT_PRO
        } else {
            signers(pm, context.packageName).singleOrNull() ?: return false
        }
        if (Build.VERSION.SDK_INT >= 28) {
            return runCatching {
                pm.hasSigningCertificate(pkg, hexBytes(trusted), PackageManager.CERT_INPUT_SHA256)
            }.getOrDefault(false)
        }
        val theirs = signers(pm, pkg)
        return theirs.size == 1 && theirs[0] == trusted
    }
}
