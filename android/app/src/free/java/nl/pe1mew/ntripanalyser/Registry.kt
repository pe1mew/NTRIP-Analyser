/**
 * @file Registry.kt (free)
 * @brief What the free edition's hub contains, in order.
 *
 * The list *is* the layout: the hub renders these top to bottom, and the
 * shared report comes out in the same order. This is the only UI file
 * that differs between the editions -- everything that arranges panels
 * lives in `src/main/`, so a navigation fix or a change to the share
 * format is made once and both editions have it
 * (`android/design/editions.md`).
 *
 * A paid panel is absent here *and* its file is absent from this source
 * set, so the free APK contains neither its screen nor its strings.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

/**
 * The free edition's hub.
 *
 * The paid capabilities are named once, by the More in Pro card at the
 * bottom (P1.5) -- not as greyed rows, because a disabled control is
 * indistinguishable from a broken one to somebody who has not paid.
 */
internal val hubPanels: List<Panel> = listOf(
    VerdictPanel,
    ConnectionPanel,
    BrowsePanel,
    ChipsPanel,
    ErrorPanel,
    WatchPanel,
    KpiPanel,
    RunControlsPanel,
    EphemerisPanel,
    SetupHintPanel,
    MoreInProPanel,
)
