/**
 * @file Registry.kt (pro)
 * @brief What the paid edition's hub contains, in order.
 *
 * The list *is* the layout: the hub renders these top to bottom, and the
 * shared report comes out in the same order. This is the only UI file
 * that differs between the editions -- everything that arranges panels
 * lives in `src/main/`, so a navigation fix or a change to the share
 * format is made once and both editions have it
 * (`android/design/editions.md`).
 *
 * Identical to free's list today, and that is expected: the paid panels
 * arrive in phase 2, each as a file in this source set plus one line
 * here. Where they go is already decided -- after the eight checks,
 * under the rule that marks *beyond the eight checks* -- so nothing
 * above them moves when they land.
 *
 * Project: NTRIP-Analyser
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause
 */
package nl.pe1mew.ntripanalyser

/**
 * The paid edition's hub.
 *
 * Phase 2 adds HandoverPanel and Tier2Panel between [KpiPanel] and
 * [RunControlsPanel], where [VrsPanel] now sits; tracks and TLS add
 * nothing here, because one draws inside the sky canvas and the other
 * has no screen at all.
 */
internal val hubPanels: List<Panel> = listOf(
    VerdictPanel,
    ConnectionPanel,
    BrowsePanel,
    ChipsPanel,
    ErrorPanel,
    WatchPanel,
    KpiPanel,
    VrsPanel,
    HandoverPanel,
    RunControlsPanel,
    EphemerisPanel,
    SetupHintPanel,
)
