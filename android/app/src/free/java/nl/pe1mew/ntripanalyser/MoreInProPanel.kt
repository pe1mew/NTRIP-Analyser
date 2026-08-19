/**
 * @file MoreInProPanel.kt (free)
 * @brief The one place the free edition names what the paid one adds.
 *
 * A single card at the bottom of the hub, and the *only* mention of a
 * paid capability anywhere in this edition. Deliberately not greyed
 * rows in place of the real cards: a disabled control is
 * indistinguishable from a broken one to somebody who has not paid, and
 * five of them would turn the free app into a demo of the paid one
 * (`design/guiV2rollout.md`, `android/design/editions.md`).
 *
 * It lists what pro does **today**, not what is planned. Advertising a
 * capability that does not exist yet is how a listing stops being
 * trustworthy, and this text is the one the wiki already publishes at
 * *What the paid edition adds* -- the same claims, so the two cannot
 * drift apart.
 *
 * It sits at the bottom, below the run controls: someone using the free
 * app is here to grade a station, and the advert should be what they
 * find after that is done rather than what greets them.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * The page this card leads to.
 *
 * Here rather than in `Links.kt` because it is the free edition's only,
 * and the paid build should not carry an address for advertising
 * itself. `tools/check_release.py` checks that the page exists, like
 * every other wiki link the app can open — a page name is a thing that
 * gets renamed, and landing a curious user on a 404 is worse than
 * offering no link at all.
 */
internal const val PRO_URL =
    "https://github.com/pe1mew/NTRIP-Analyser/wiki/What-the-paid-edition-adds"

/**
 * What the paid edition adds, said once.
 *
 * No [Panel.destination] and no [Panel.shareSection]: it is an
 * advertisement, not a measurement, and a report about a station should
 * never carry one.
 */
object MoreInProPanel : Panel {
    override val key = "more-in-pro"

    @Composable
    override fun Content(state: HubState, actions: HubActions) {
        val uriHandler = LocalUriHandler.current
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text(
                    stringResource(R.string.pro_title),
                    style = MaterialTheme.typography.titleSmall,
                )
                Text(
                    stringResource(R.string.pro_body),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                TextButton(onClick = { uriHandler.openUri(PRO_URL) }) {
                    Text(stringResource(R.string.pro_more))
                }
            }
        }
    }
}
