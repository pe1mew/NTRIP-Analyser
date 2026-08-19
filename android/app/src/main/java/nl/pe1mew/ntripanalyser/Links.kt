/**
 * @file Links.kt
 * @brief The four URLs the app sends people to.
 *
 * Together in one file because they are referenced from the menu, the
 * About box and the orbit badge, and `internal` rather than `private`
 * for the same reason -- top-level `private` in Kotlin means "this file
 * only". Each is checked against the published pages by
 * `tools/check_release.py`, so none can be renamed out from under a
 * link.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */

package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*

internal const val REPO_URL = "https://github.com/pe1mew/NTRIP-Analyser"

/**
 * The wiki, not `docs/readme.md`.
 *
 * The readme is written for someone building this repository; a phone
 * user tapping "Documentation" wants *Getting started*. The wiki pages
 * in `docs/wiki/` are the ones written for them, and one wiki serves
 * both editions.
 */
internal const val HELP_URL = "https://github.com/pe1mew/NTRIP-Analyser/wiki"

/**
 * The published privacy policy, from `docs/privacy-policy.md`.
 *
 * Play carries this URL on the listing, but somebody who has already
 * installed the app should not have to go back to the store to read what
 * it does with their position -- especially in the edition that asks for
 * it. Verified against `docs/wiki/Privacy-and-support.md` by
 * `tools/check_release.py`.
 */
internal const val PRIVACY_URL =
    "https://pe1mew.github.io/NTRIP-Analyser/privacy-policy"

/**
 * Where the orbit badge leads.
 *
 * A badge that says the sky is drawn from the phone, or from a file too
 * old to use, has told the reader something is wrong without telling
 * them what to do about it. This page does: what orbits are for, why
 * both the sky view and the C/N0-against-elevation plot need them, and
 * where a current navigation file comes from. Checked against
 * `docs/wiki/` by `tools/check_release.py`, so the page cannot be
 * renamed out from under the link.
 */
internal const val ORBITS_URL =
    "https://github.com/pe1mew/NTRIP-Analyser/wiki/Orbits-and-the-ephemeris-stream"
