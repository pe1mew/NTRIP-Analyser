package nl.pe1mew.ntripanalyser

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/**
 * Free's half of [Handover]: asked by pro for the saved connection, it
 * checks who is asking and then asks the user.
 *
 * **This screen is the security control** (GH#5, option C). Nothing
 * reaches the caller without an explicit *Share*: the caller must have
 * started this for a result -- otherwise there is nobody to answer and
 * no name to check -- must be the genuine pro edition by name and
 * signer ([Handover.isGenuinePeer]), and then the user must agree.
 * Touches are dropped while another window covers this one, so an
 * overlay cannot press *Share* for them.
 *
 * Free only: pro has nothing to hand back. The class is shared because
 * an edition carries no code of its own (`checkEditionParity`,
 * `android/design/editions.md`); only free's manifest declares it, and
 * a pro build that somehow reached it refuses anyway.
 */
class ShareConnectionsActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.decorView.filterTouchesWhenObscured = true
        setResult(Activity.RESULT_CANCELED)

        val caller = callingPackage
        val store = Settings.loadProfiles(this)
        val payload = ConfigFile.encodeConnections(store)

        when {
            Features.IS_PRO || !Handover.isGenuinePeer(this, caller) -> {
                refuse(Handover.REFUSED_CALLER)
                setContent { AppTheme { Refused(caller) } }
            }
            payload == null -> {
                refuse(Handover.REFUSED_NOTHING)
                setContent { AppTheme { NothingToShare() } }
            }
            else -> setContent {
                AppTheme {
                    Consent(
                        connections = store.profiles.filter { it.isComplete },
                        onShare = {
                            setResult(Activity.RESULT_OK, Intent()
                                .putExtra(Handover.EXTRA_VERSION, Handover.VERSION)
                                .putExtra(Handover.EXTRA_CONNECTIONS, payload))
                            finish()
                        },
                        onDecline = { refuse(Handover.REFUSED_BY_USER); finish() },
                    )
                }
            }
        }
    }

    private fun refuse(why: String) {
        setResult(Activity.RESULT_CANCELED, Intent()
            .putExtra(Handover.EXTRA_VERSION, Handover.VERSION)
            .putExtra(Handover.EXTRA_REFUSED, why))
    }

    @Composable
    private fun Page(content: @Composable () -> Unit) {
        Surface(Modifier.fillMaxSize()) {
            Column(
                Modifier.safeDrawingPadding().padding(24.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) { content() }
        }
    }

    @Composable
    private fun Consent(
        connections: List<CasterSettings>,
        onShare: () -> Unit,
        onDecline: () -> Unit,
    ) = Page {
        Text(stringResource(R.string.handover_title),
             style = MaterialTheme.typography.headlineSmall)
        Text(stringResource(R.string.handover_asks))
        for (c in connections) {
            Text(
                "${c.caster}:${c.port} / ${c.mountpoint}" +
                    if (c.user.isNotBlank()) "\n" +
                        stringResource(R.string.handover_as_user, c.user) else "",
                fontWeight = FontWeight.Medium,
            )
        }
        Text(stringResource(R.string.handover_what_goes))
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(onClick = onDecline, modifier = Modifier.weight(1f)) {
                Text(stringResource(R.string.handover_decline))
            }
            Button(onClick = onShare, modifier = Modifier.weight(1f)) {
                Text(stringResource(R.string.handover_share))
            }
        }
    }

    @Composable
    private fun Refused(caller: String?) = Page {
        Text(stringResource(R.string.handover_refused_title),
             style = MaterialTheme.typography.headlineSmall)
        Text(stringResource(R.string.handover_refused_body,
                            caller ?: stringResource(R.string.handover_unknown_caller)))
        Button(onClick = { finish() }) { Text(stringResource(R.string.handover_close)) }
    }

    @Composable
    private fun NothingToShare() = Page {
        Text(stringResource(R.string.handover_nothing))
        Button(onClick = { finish() }) { Text(stringResource(R.string.handover_close)) }
    }
}
