# Gotcha Log

Resolved entries from before the current month are in
[gotcha-log-archive.md](gotcha-log-archive.md) (moved 2026-10-14,
framework v1.49.2's archive rule). Grep both for a mechanism.

<!-- Structured problem/solution journal. Append-only.
     Part of the self-learning loop: Capture → Surface → Promote → Retire.

     PROMOTION LIFECYCLE:
     - New entries start here (Capture)
     - At end-of-session, review for patterns (Surface, via /curate)
     - When an entry recurs 2-3 times, promote it to a topic file or the
       memory index as an "if X, then Y" pattern (Promote)
     - When the root cause is fixed, mark [RESOLVED] IN THE HEADING (Retire) —
       curation reads headings, not bodies. Same for a recurrence count: [x3].
       "Fixed" means code or procedure changed. An entry retired on advice
       alone is not retired: the v3.4.0 tag gotcha was marked [RESOLVED] with
       "check the tag first" as its fix, and recurred identically in three days.
     - Record promotions in the "Promoted" table below.

     Template for new entries:

### [Short description] (YYYY-MM-DD)
**Problem**: What went wrong or was confusing.
**Root cause**: Why it happened.
**Fix**: What solved it.

     2-3 lines. Write the lesson, not the narrative of the session that
     found it. If it needs a page, it belongs in a topic file or a design
     doc, not here. -->

<!-- Seeded 2026-08-13 from the sessions that produced the current unreleased
     work. These are real defects with measured root causes, not examples. -->

### Windows filesystem is case-insensitive (2026-08-12)
**Problem**: Resizing `SHOT.png` into `shot.png` destroyed the original; later reads returned the resized copy.
**Fix**: Distinct names, not distinct case.

### A backup taken after the damage restored the damage (2026-08-14)
**Problem**: Deliberately breaking a source file to prove a test catches it left the break in place; the "restore" wrote it back.
**Root cause**: The first attempt crashed *after* writing the broken file and before restoring. The next script read that file as its baseline and saved it as `.bak` — the backup captured the break.
**Fix**: Take the baseline from `git`, not from the working file, and assert the restored text differs from the broken one. A restore that cannot fail is not a restore.

### Keystrokes went to whichever window had focus (2026-08-14)
**Problem**: Automating the Windows GUI with SendKeys typed a file path into something else; the target instance never received it.
**Root cause**: SendKeys is delivered to the focused window, and `AppActivate` does not reliably win focus on a busy desktop.
**Fix**: Drive a Win32 app by messages to its own handles — `WM_SETTEXT` to the control id, `WM_COMMAND` with the menu id. It reaches that window and nothing else, and needs no focus.

### A fetch cache made a fixed page look unfixed (2026-08-14)
**Problem**: The published privacy policy still showed the old app names minutes after the fix was deployed and the build reported success.
**Root cause**: Fetched responses are cached for fifteen minutes per URL. The build was fine; the reader was stale.
**Fix**: Add a distinct query string when re-checking a page you have just fetched. Also: `pages/builds/latest` lags behind the deployment — the site is the evidence, not the API.

### Play's crawler inferred a login from a password field (2026-08-14) [x2]
**Problem**: The submission was blocked by "missing credentials": Play had screenshotted the caster settings dialog and taken *Username* and *Password* for an account.
**Root cause**: Those fields configure access to a third party's NTRIP caster. The app has no accounts at all.
**Fix**: App access declared as "no restricted parts", with the reasoning and a public anonymous caster in the release notes, where a human reviewer reads.
**Lesson**: Expect automation to read the screen literally, and answer the human behind it.
**Recurred 2026-09-16, and the fix did not hold.** Free's first *production* release (30800) was rejected the day production access was granted -- "Violation of Play Console Requirements", decided by automated systems, citing `LoginWall.png` of the same dialog. Pro's closed-track pre-launch report had flagged it too. The release-note hint had also been removed that week (next entry). **Sturdier fix**: a real credential set in *App-toegang → Inloggegevens toevoegen* -- a dedicated reviewer account on the author's own caster (always up, reachable from anywhere, unguessable ASCII username, rate limiting kept on but loose, never rotated during a review), a backup mountpoint, and English instructions quoting the app's own labels (*Caster settings…*, *Save*, *Run the check*). Answer the automation where it looks, and the human in the same place. Tested from mobile data before sending -- a caster reachable from the home LAN proves nothing about a reviewer's reach.

### Device tooling is not standard equipment (2026-08-14) [x2]
**Problem**: `screenrecord` is absent on the EMUI handset, and every `adb shell` path was rewritten — `/sdcard/f.mp4` reached the phone as `C:/Program Files/Git/sdcard/f.mp4`.
**Fix**: Check `ls /system/bin/<tool>` before building a plan around it, and prefix `adb shell` commands carrying Unix paths with `MSYS_NO_PATHCONV=1`.
*Recurred 2026-08-20*: `adb shell uiautomator dump /sdcard/ui.xml` answered "dumped to /Files/Git/sdcard/ui.xml" and the file was unreadable. The fix was known and written down; it was not applied because the command looked like a read, not a path.

### `gh run watch | tail` reports success for a failed run (2026-08-15)
**Problem**: A failed CI run was nearly reported as passing: `gh run watch --exit-status ... | tail` printed `watch-exit=0`.
**Root cause**: `$?` after a pipeline is the **last** command's status — `tail`'s — not `gh`'s. `--exit-status` was set and correct; the pipe discarded it.
**Fix**: Redirect instead of piping (`gh run watch ... > /dev/null; echo $?`), or read the verdict from `gh run view` rather than an exit code that has passed through a pipe.

### A green verdict is not a correct registration (2026-08-16) [x2]
**Problem**: `RFSEE01` advertised a position 3.3 km from its antenna — a longitude with two digits rotated. Correcting it put HANESE's coordinates into RFSEE01's entry, moving it 25 km. **Both states returned STATION OK on all eight KPIs.**
**Root cause**: No KPI compares the sourcetable's declared position against the broadcast 1005/1006 ARP. `sourcetable_offset_m` exists in the snapshot for exactly this and nothing computes it; the GUI does the comparison in its own code, so the CLI and Android never see it.
**Fix**: Read the sourcetable directly — `-m | cut -d';' -f2,10,11` — after any registration change; a passing check says nothing about it. Specified as phase 0 of `design/work-items/measurement-tiers.md`, and it is the strongest tier-1 candidate precisely because it earned its place twice in one afternoon.

### A binary pulled through `adb shell` came back corrupted (2026-08-20)
**Problem**: The shared plot PNG, pulled with `adb shell run-as … cat > plot.png`, would not open. Its signature read `89 50 4E 47 0D 0D 0A 1A` — the PNG magic with an extra `0D` — and the local file was 949 bytes larger than the one on the phone.
**Root cause**: `adb shell` allocates a PTY, which translates `LF` to `CRLF`. Every `0x0A` in the image gained a `0x0D`. Nothing reports this: `cat` succeeds, the file arrives, only its content is wrong.
**Fix**: `adb exec-out` for anything that is not text. Same family as the path-rewriting entry above — **the shell between you and the device edits what passes through it**, in both directions.

### A tap aimed from an old screenshot hits whatever moved into the spot (2026-08-20) [x2]
**Problem**: Driving the app over `adb`, a tap meant for the share control opened the project wiki in Chrome. Earlier in the project the same habit silently changed the active mountpoint.
**Root cause**: Coordinates read off a screenshot taken before the layout changed. On the analysis bar, share sits at x≈603 and the orbit badge at x≈996; a verdict card growing by two lines moves everything below it.
**Fix**: `MSYS_NO_PATHCONV=1 adb shell uiautomator dump /sdcard/ui.xml`, read the `bounds` of the node with the right `text` or `content-desc`, tap its centre — and screenshot after every tap that was supposed to change the screen.

### A claim's own check broke the build that checks claims (2026-08-20)
**Problem**: Correcting a stale sentence in `MEMORY.md` -- the v3.5.0 release is published, not a draft -- came with a verify command that used `gh`. CI went red on a docs-only commit: *"To use GitHub CLI in a GitHub Actions workflow, set the GH_TOKEN environment variable."*
**Root cause**: `tools/verify_memory.py` has two tiers, and its own header says so: `verify:` runs on every push under `--offline`, `verify-net:` is deferred to the Monday job, which sets `GH_TOKEN`. The check was written as `verify:` without reading how checks are classified, so a network call ran in the tier that has no network credentials.
**Fix**: `verify-net:`. Offline is now 14 pass / 0 fail with 2 network claims deferred; the full run is 16 / 0. **When adding evidence to a claim, run the harness the way CI runs it** -- `python tools/verify_memory.py --offline` -- not just the way that suits the desk you are sitting at.

### A script truncated a file before deciding what to write (2026-08-22)
**Problem**: `MainActivity.kt` -- 627 lines -- became a zero-byte file in the middle of a step.
**Root cause**: an edit helper written as `io.open(p, 'w').write(f(s))`. Python opens the file, which truncates it, *before* evaluating `f(s)`; `f` raised on a failed assertion and the write never happened. The assertion was right -- the anchor matched three places -- so the guard fired and destroyed the file it was guarding.
**Fix**: compute the new text first, then open for writing. Recovered with `git show HEAD:path` rather than `git checkout`, line endings converted back to CRLF by hand and confirmed byte-identical with `git diff`. Fifth instance of the promoted *scripted edits corrupt what they rewrite* pattern, and the first where the corruption was total.

### The phone is somebody's phone, not a test fixture (2026-08-22)
**Problem**: `adb shell pm clear` on the pro edition, run to get a clean "nothing measured yet" screen for a screenshot, wiped the author's caster, mountpoint, username and password. No backup exists: the store is encrypted per install.
**Root cause**: treating the handset as scratch space. The command was the shortest route to the state I wanted, and its other effect was somebody else's configuration.
**Fix**: nothing could restore it; the author retyped it. **Reach for the state, not the reset** -- force-stop clears a run without clearing settings, a spare profile gives pro a blank hub, and a release-signed build upgrades in place where a debug build demands an uninstall. The rule held the second time the same day: free on the S23 is Play's copy, so installing over it would have taken its configuration with it, and that one was handed to the author instead.

### A failed build, then a successful install (2026-08-23)
**Problem**: `assembleProRelease` failed on a locked `classes.dex`, and the `adb install` chained after it reported *Success*. The phone then ran the **previous** APK while I was about to verify a change that was not in it.
**Root cause**: `A && B` guards on the exit status of the command, not on the freshness of the file it leaves behind; a build that fails leaves the last good artefact exactly where the installer looks. Caught only because the APK's timestamp was checked before trusting it -- the same day, and the same shape, as Play receiving a bundle built four and a half hours before the fix it claimed to carry.
**Fix**: `./gradlew --stop`, rebuild, and **read the artefact's timestamp before installing it**. Fifth instance of *what is installed is not what was built*, and the second in one day.

### A line that fits in the file wraps on the phone (2026-08-23)
**Problem**: the new Watch-card line read *"2 reconnect(s) — the stream dropped and came back"*. In the strings file it looked like every other line; on a 1080-wide phone it wrapped, leaving "back" alone on a line while every other line in the card fitted.
**Root cause**: judging a rendered string by reading its source. Nothing in the file says how wide it is.
**Fix**: shortened to the shape of the line above it and measured from the accessibility tree -- 48 px, one line, the same height as a KPI row's evidence. Text is a rendered artefact like any other; measure it where it lands.

### touch is not enough when write and build share a second (2026-08-25) [x3]
**Problem**: three times in two days a test stayed red (or green) after a source change because make skipped recompiling -- the restore landed in the same clock second as the previous build and mtime comparison saw nothing.
**Root cause**: one-second mtime resolution on this MinGW/make setup; `touch` alone raced the same wall.
**Fix**: delete the object file (`rm build/CMakeFiles/<target>.dir/.../file.obj`) before rebuilding when a just-edited file's test result looks impossible. Candidate for promotion at next recurrence.

### A recommendation dressed as research (2026-08-26)
**Problem**: the agent recommended a €12.99 price "mid-upper of the comparison set -- NTRIP clients and GNSS field tools sit at €5-20", and the author was one click from attaching that number to a real listing.
**Root cause**: the band was an impression, not a search. Nothing had been looked up; the phrase "comparison set" implied evidence that did not exist. The project has a verified-claims discipline for *state* ("shipped", "16 tests") and none for *recommendations*, which decay the same way and cost more.
**Fix**: the author asked for the real table, the search found there is **no paid NTRIP app market on Play at all** (clients free because they sell hardware; the nearest analogue, NtripChecker, free with 10,000+ installs; the next rung $100-300 survey suites), and the price became €5 on evidence. Promoted below: cite the basis or say plainly there isn't one.

### The device is not the same device twice (2026-08-26) [x2]
**Problem**: `adb shell screenrecord` -- "inaccessible or not found" on the Huawei; and the same command wrote nothing because Git Bash rewrote `/sdcard/fsvc.mp4` into `C:/Program Files/Git/sdcard/fsvc.mp4`.
**Root cause**: EMUI ships no `screenrecord` binary (Samsung does), and MSYS path conversion mangles absolute device paths that look like POSIX paths.
**Fix**: record on the S23; `export MSYS_NO_PATHCONV=1` for every adb command naming a device path.

### A screen recording carries whatever the shade holds (2026-08-26)
**Problem**: a video destined for Google -- and an unlisted YouTube upload -- pulled down the notification shade twice, capturing the author's Google Payments mail about verifying a bank account, and on the retake the phone's own charging and USB notifications.
**Root cause**: demonstrating an ongoing notification means showing the shade, and the shade is not ours.
**Fix**: inspect the shade with a screenshot **before** recording, never after; and record **unplugged over Wi-Fi adb** (`adb tcpip 5555`), which removes the charging notifications the cable itself creates. Screen timeout and battery saver are raised for the take and restored afterwards, both values read first.

### Play Console mechanics that cost a step each (2026-08-26)
**Problem**: four wrong assumptions in one sitting -- that the package name binds at first upload, that the price could wait until production, that a bundle uploaded once is available to every track, and that a version code can be uploaded twice.
**Root cause**: none of it is guessable; all of it is stated by the forms.
**Fix**: package binds **at app creation** and is permanent; the price is padlocked behind a **seller/payments account** and blocks rollout to *any* track including closed; **tracks do not share releases** (an empty internal release reads as three separate errors); a version code exists once per app, so a second track attaches it with **"Toevoegen vanuit bibliotheek"**. Recorded in `design/work-items/pro-to-play.md` S3.
**Added 2026-09-16**: the console's menu moved -- *Beleid en programma's* now sits under *Controleren en verbeteren*, and a completed declaration shows only on App-content's *Actie ondernomen* tab while the default tab reads "Je bent helemaal bij". A remembered route was out of date; the route that works is in `pro-to-play.md` S5. Ask for a screenshot of the menu rather than recite one.

### The console's error banner threw away the reason (2026-09-09)
**Problem**: pro's price refused to save for two weeks behind `Je wijzigingen kunnen niet worden opgeslagen`, with nothing marked on the page. Three sittings went into guessing -- the payments profile, per-country minima, the free/paid flag -- and the closed test stayed blocked throughout.
**Root cause**: the console discards what its own API returns. `UpdateAppPrice` answers HTTP 400 with a localised, actionable sentence: `Verwijder Rest van de wereld als je je app betaald wilt maken`, and then the same for China, Cuba, Iran and Soedan.
**Fix**: read the failed request's response before theorising -- one HAR ended two weeks of it. The substance is worth keeping too: **a paid app cannot target "Rest of the world" or the embargoed countries**, and they must come out of the **tracks'** country lists as well as the price table; clearing the price page alone does not satisfy it. €5 saved 2026-09-09 once all five were gone.

### Support answered a question about someone else's app (2026-09-09)
**Problem**: Google support explained that an app saved as free can never become paid and that pro needed a new package name. The agent accepted it, told the author the package was burned, and sized a rename across six files.
**Root cause**: a canned reply matched to the phrase "cannot set a price", not to this app. The console's own pricing page said `Je app is: Betaald`, with `Je app gratis maken` beside it and `wijzigen van betaald in kosteloos totdat je publiceert` above -- which also proved pro had never been published. The author doubted the diagnosis; the agent had not looked.
**Fix**: the console page is the state, a support reply is only a claim about it. Read the page that holds the setting before acting on any answer about it.

### A purchase flow is not a charge (2026-09-09)
**Problem**: the first real tester of pro's paid closed test was reported as charged €5. The agent recorded the plan's licence-testing assumption as falsified, rewrote three sections of the work item and told the author not to invite the 14. The payment method had been **"Testkaart, wordt altijd goedgekeurd"** -- Google's test instrument. No money moved; licence testing had worked all along.
**Root cause**: the agent escalated to falsification on one word of a report without asking what the checkout actually showed. Both of the day's earlier reversals had the same shape: acting on a report *about* an artefact instead of on the artefact.
**Fix**: read the instrument, not the flow. Two things are worth keeping from the false alarm -- the author's own install can never fail this check (a developer is not charged for their own app), and **licence testing runs a real-looking checkout at the real price**, so the tester invitation must say so or an unwarned tester stops at it. The reverse arrived a day later: a Google AI Overview said flatly that closed-test testers must buy a paid app, against an order at `Totaal EUR 0,00` already read in the console. **A generated summary does not outrank an artefact.** Settled by the tester's own screen: "Dit is een testbestelling. Er worden geen kosten in rekening gebracht."

### A deliberate fix removed because it looked out of place (2026-09-16)
**Problem**: rewriting free's first production release note, the agent struck the line *"Try: use caster.centipede.fr, port 2101, mountpoint NEAR…"* as a quick-start that did not belong in a release note and as load on a volunteer network. The same day, the production release was rejected for missing login credentials.
**Root cause**: the line was the 2026-08-14 fix for exactly that rejection -- a public anonymous caster placed where a human reviewer reads -- and this log said so. The agent judged the line by how it read, not by why it was there, and never searched the log. Whether its removal decided the rejection is unproven (the email cites an automated system and a screenshot); that it was a fix removed unread is not.
**Fix**: before recommending the removal of anything that looks odd -- a line, a flag, a workaround -- search `memory/gotcha-log.md` and the history for why it exists. Something strange that survives in a carefully kept project usually has a reason written down.

### Read the wiring, missed the gate (2026-09-13)
**Problem**: assessing GH#5, the agent told the author free could already export its settings -- *Save config* was wired in `MainActivity.kt` and passed through `Shell.kt` with no `Features` check -- and built its interim recommendation ("document the manual route") on that. The author knew better: free has no such row.
**Root cause**: the gate is one file further on, `AppMenu` in `Dialogs.kt`, `if (Features.IS_PRO)` around both config rows. `Shell.kt`'s own comment names it (*"AppMenu in Dialogs.kt, which gates the configuration rows on the edition"*); the agent read the comment and not the gate.
**Fix**: a capability claim about an edition is settled where the row is *drawn*, not where its action is *wired*. Grep `Features.` at the composable that renders the control before saying an edition has it.

### MSYS_NO_PATHCONV fixes the device path and breaks the local one (2026-09-13) [x2]
**Recurrence 2026-10-14**: `uiautomator dump /sdcard/ui.xml` from Git Bash wrote to `/Files/Git/sdcard/ui.xml` on the phone, and the pull then found nothing. Same cure, set inside the helper script so no call can forget it.
**Problem**: pulling both editions' APKs for the signing-certificate check, `adb pull /data/app/...` first failed because Git Bash rewrote the device path; with `MSYS_NO_PATHCONV=1` it failed again because the *local* destination `/c/Users/...` now reached `adb.exe` unconverted. Then `apksigner` given the same `/c/` path printed nothing at all -- its error swallowed by the `grep` it was piped into, which read as "no certificate lines".
**Root cause**: the variable switches conversion off for every argument, and Windows tools cannot resolve MSYS paths. A filter after a failing tool turns an error into an empty match.
**Fix**: with `MSYS_NO_PATHCONV=1`, pass local paths in Windows form (`C:/Users/...`). Run a tool unfiltered once before trusting a filtered empty result.

### A character count that fit was rejected (2026-09-16)
**Problem**: free's Dutch production release note measured 500 characters with `wc -m` against Play's 500 cap; Play refused it as too long.
**Root cause**: Play counts the field differently from `wc -m` -- line breaks are the likely difference.
**Fix**: keep real margin (the note shipped at 474), and write capped Play fields as one paragraph where the form allows it; the reviewer-instructions field went in that way at 443 and 464.

### The session's worktree was reassigned mid-session (2026-09-09)
**Problem**: the session's worktree was recycled by the pool onto a detached commit from August (`e7a8757`), while the session kept working for days.
**Root cause**: worktrees are pooled; nothing about the session's own state changes when one is reassigned.
**Fix**: before any edit, check `git rev-parse --abbrev-ref HEAD` and the log of the tree being edited. Here every edit went to the main checkout, confirmed on `main` each time.

### A public document advertised an account that never locks out (2026-09-17)
**Problem**: the reviewer-credential procedure, committed and pushed, told anyone reading the repository that the author's caster had an account with "no geo-IP restriction or rate banning". The author asked whether the procedure exposed anything; it did not expose the credentials, but it did expose the account's weakest property.
**Root cause**: the advice optimised for the reviewer's convenience and was written for a private reader, into a public file.
**Fix**: rate limiting stays on, set loose; the account gets an unguessable name, a long random password and access to the review mountpoints only; all three documents reworded. Before writing operational detail into this repository, ask what it tells an attacker -- the repo is public, and "how our accounts are configured" is part of the attack surface even with no secret in it.

### A handed-over command written for the wrong shell (2026-10-09)
**Problem**: the agent offered a commit message as a Bash heredoc (`git commit -F- <<'MSG' ... MSG`) to an author working at a PowerShell prompt, with the PowerShell alternative mentioned underneath. PowerShell has no heredoc of that form, so the wrapper line became the commit's subject: `git commit -F- <<'MSG' Plan: desktop first, ...`, with `MSG` as the last body line. The body survived; the subject is shell syntax, and fixing it needed an amend and a force-push of an already-pushed commit.
**Root cause**: the agent writes its own commands for the Bash tool and handed one over in the same dialect. The environment says PowerShell is the author's primary shell; the caveat was written but placed after the broken version, which is the same as not writing it.
**Fix**: a command handed to the author is written for **their** shell, not the agent's. For commit messages that means `git commit` with the editor, or `-m` subject and `-m` body — and then the text must survive PowerShell quoting: no apostrophes or double quotes inside a `-m` string, or they bite next. Heredocs only when the author is demonstrably in Git Bash. Same family as the shell-in-the-middle entries below, one layer further out: there the shell edited what reached the device, here it edited what reached git.

### [RESOLVED] Numbers only a person reads are numbers nothing checks (2026-10-09)
**Problem**: the message-detail decoder has printed MSM7 pseudoranges **5.6x** too large and phases **1.4x** too large since it was written, by applying the phase rate's `0.0001` scale to all three fields where RTCM gives 2^-29 ms and 2^-31 ms. The MSM4 path was ~12% out on both for the same reason, and QZSS 1124 -- an MSM4 -- was read with MSM6's 20- and 24-bit widths, so those cells came from the wrong bits entirely. Found while reading the layout to build `msm_extract_obs`, not by any check.
**Root cause**: the columns feed the GUI's detail view and the CLI's verbose output and nothing else. Every tested path reads C/N0, whose scale is right. A field no measurement consumes has no guard, and the three wrong constants sat beside a correct one for months.
**Fix**: the scales are now named constants used by both decoders, and `test_msm_cnr.c` renders a built frame through `rtcm_set_output_buffer` -- the GUI's own capture path -- and asserts the printed strings against values it computes from RTCM's scales, including a check that the old scale does *not* appear. Falsified by restoring `0.0001`: two checks red. **If a number is only ever read by a human, write the test that reads it like one.**

### [RESOLVED] The reference implementation accused the wrong code (2026-10-09)
**Problem**: the first run of `test/manual/sv_state_vs_rtklib.c` -- P2's cross-check against RTKLIB -- reported our propagator 1 800 km out across the whole fit interval, while the clock agreed to 0.04 ns. A real 1 800 km error would mean the sky plot has pointed at the wrong satellites since it was written.
**Root cause**: the harness, not the propagator. RTKLIB's `eph_t` stores toe twice -- an absolute `gtime_t` and `toes` in seconds of week -- and the longitude-of-node term reads `toes`. The harness set only the first, so RTKLIB dropped `-omge * toe` and rotated its *own* orbit by 25 radians. With `toes` set: 0.0000 m.
**Fix**: the shape of the disagreement was the clue, and is the lesson. The clock agreed, and a rotation about Z preserves both `|r|` and `r.v` -- so the pattern said "rotated frame", not "wrong orbit". **Read what the shape of a disagreement rules out before deciding which side is wrong**: a comparison tool is code too, and it is the code that was written five minutes ago.

### A politely written test defended nothing (2026-10-09) [RESOLVED]
**Retired 2026-10-14**: `test/test_station_report.c` now sets `has_vel = false` beside a stale `speed_mms = 7.0`, so the flag is what the test proves.
**Problem**: P5's falsification removed the `has_vel` gate from the report's velocity accumulation -- the gate that stops a stream carrying no phase-range rates from publishing a speed -- and the case named "but a stream without rates reports no speed" stayed green. The check had passed every run since it was written, and was protecting nothing.
**Root cause**: the test built its snapshot the way a well-behaved session does. `has_vel = false` was set and `selfpos_speed_mms` was left at its `NS_UNSET` initialiser, so the comparison rejected the sample on the sentinel and never consulted the flag. The test proved that *the caller blanks the field*, which is the caller's manners, not the report's defence.
**Fix**: make the input hostile. The snapshot now carries 7 mm/s *beside* `has_vel = false` -- a number it has no right to -- and the gate is the only thing that can stop it; the falsification then reddens the case by name. **A test must feed the input the defence exists for, not the input a cooperating caller would send** -- the same shape as P4's too-uniform ephemerides, one layer up: there the inputs were identical where they should have differed, here the input was correct where it should have been wrong.

### [RESOLVED] The fields were filled and never published (2026-10-09)
**Problem**: P5 claimed nine `selfpos_*` snapshot fields "serialised under `selfpos_*`". The report's serialiser carried them; `ns_stats_to_json()` and the CSV row carried none of them, so the daemon's snapshot, `File > Export Statistics` and every CSV reader described a stream without ever mentioning where it put itself. Found a step later, while wiring the GUI window that needed them.
**Root cause**: two serialisers, one claim. The work went into `station_report.c` and the sentence that was written covered both, because both emit keys of that name. The field block's own Doxygen said "consumers read by key, so the schema version stays" -- a promise about a serialiser nobody had touched.
**Fix**: both serialisers, same null-not-zero rule, four new cases covering nulls *and* values. The lesson is narrower than "a field nothing fills": **a field that one surface publishes is not a field that is published**, and a commit message that names the measurement rather than the surface cannot be checked against either. Name the surface in the claim, and the reader of the next step will notice when one is missing.

### [RESOLVED] The test harness was looking at another desktop (2026-10-09)
**Problem**: verifying the new GUI window from a launcher with no console, `FindWindow("NtripSelfPosClass")` returned 0 after the menu command -- and so did `FindWindow` for the *main* window's class, while `GetClassNameA` read that very class off the live handle. It read exactly like "the window was never created".
**Root cause**: two independent traps, each enough on its own. `FindWindow` and `EnumWindows` search the **calling thread's desktop**, and this host runs on a different one from the process it launched; a `SendMessage` to a handle already in hand crosses regardless, which is why the app still obeyed `WM_CLOSE`. And in the enumeration that replaced them, PowerShell's `Write-Output` inside a scriptblock used as a delegate becomes the delegate's **return value**: the lines never printed, and the callback's non-false return stopped the walk at the first window.
**Fix**: `EnumThreadWindows` (thread-scoped, not desktop-scoped) and `[Console]::WriteLine` inside the callback. Then the window appeared: `class='NtripSelfPosClass' title='Self-position' visible=True`. **When a check says the thing does not exist, prove the check can see a thing that does** -- here, the main window, which was provably there and equally invisible to the same call.

### [RESOLVED] A verdict on a station, from thirty-two seconds (2026-10-09)
**Problem**: the new Self-position window, opened on a stream 32 s old, filled its banner with **NOT COMPUTABLE**. Nothing had solved because nothing had had time to. The author's one-word reply to the screenshot was "not good", and it was right: that is a finding about the station, stated from a window far too short to carry one.
**Root cause**: the window had two states where it needed four. Anything that was not "solved" was presented as "cannot be solved", so `SPP_NO_EPOCH` -- the ordinary condition for the first seconds of every run -- wore the same words as `SPP_SINGLE_FREQ`, which is the one status that really is a property of the stream. **This is `gui-design.md` §14.5's second entry again**: the Stability window said "no dual-frequency pair to measure with" at twenty-five seconds, and the fix there was to wait until the window was judgeable. Same author, same mistake, a new window, a month later -- and the lesson had been written down in the very document the new section was added to.
**Fix**: four states -- solved, gathering, not computable, not solved -- with "gathering" naming what is being waited for. The general rule, which is the one that keeps being relearned: **a screen may only say what its evidence supports, and "not yet" is evidence of nothing.** Writing that down in a design document did not stop it happening; the thing that caught it was a person looking at the window.

### [RESOLVED] Nothing a build can check is wrong on screen (2026-10-09)
**Problem**: the same window shipped a heading cut to "What it mea...", notes under a horizontal scrollbar, a caption severed mid-word, and ring labels reading "0.125 m" on a plot with no points in it. The build was warning-free and 19/19 tests passed throughout.
**Root cause**: three column widths guessed at `190 / 90 / 210` px -- against `gui-design.md` §14.6, *Column widths are measured, not guessed*, which the same session had just read -- a plot allowed half the window regardless of what the figures needed, a one-line `TextOut` for a sentence longer than the plot, and a radius chooser returning its smallest step for an empty set.
**Fix**: widths measured from the longest string each column can hold, the plot capped at what is left after the figures, `DrawText` with `DT_WORDBREAK | DT_END_ELLIPSIS`, and no ring labels until points set the scale. Then **look at it**: `PrintWindow` into a bitmap, saved as a PNG and read back, which is how the remaining three were found after the first was fixed. Two traps in that harness: call `SetProcessDPIAware()` first, or a 1000x620 window is reported and captured as 800x496 at 125 % scaling and the clipping looks like the bug; and the empty state is not the solved state, which still needs a live station.

### [RESOLVED] The feature was never called (2026-10-09)
**Problem**: connected to a real station, the Self-position window sat at `GATHERING` -- 170 s of stream, 157 snapshots, 0 epochs solved, reason `no observations in this epoch`. That status is the field's *initial value*: the solve had never run. `selfpos_solve()` was defined in `ntrip_session.c` and called from nowhere, because the call site discarded the return value of `obs_feed()` -- the one thing that says an epoch has closed. The whole feature was dead through P5 **and** P6, two commits and two days of work on top of it.
**Root cause**: three failures that lined up.
  1. **Every test starts below the session.** `test_spp` builds its own epoch, `test_observables` its own payload, `test_station_report` and `test_ns_stats` their own snapshots. Nothing drove `ntrip_session` and asked whether a closed epoch reaches the solver. The suite was 19 green tests around a hole in the middle.
  2. **Every surface handled "nothing solved" correctly** -- null in the JSON, "nothing solved" in the CLI, a reason in the window. The feature's own care about absence is what made its absence look like a station's property.
  3. **The build asks the compiler almost nothing.** Flags were `-O3 -DNDEBUG -std=gnu99`: no `-Wall`, so `-Wunused-function` never fired on a static function nobody called. Proved after the fact: the committed file compiled with `-Wall` names it in one line.
**Fix**: the call site, plus the two defences that were missing. `-Wall` on our own targets (the codebase turned out clean under it, bar four real `strncpy` truncations in `gui_thread.c` and a tray tooltip, all fixed), and `test/test_selfpos_session.c`, which replays RTCM through `ns_open_file()` and asserts a closed epoch reaches the solve -- red on the shipped defect, with a one-bit control (DF393 set, epoch never closes) that must stay `SPP_NO_EPOCH`. **A test that starts below the wiring cannot see the wiring**, and "the build is warning-free" means nothing until you know what the build asks.

### Measured the way the build measures -- and then did it again (2026-10-09)
**Problem**: before adding `-Wall`, the codebase was swept for warnings with `gcc -Wall -fsyntax-only` and reported clean. The real `-O3` build then produced ten: six `-Wformat-truncation` and four `-Wstringop-truncation`.
**Root cause**: those warnings come from the optimiser's value range propagation, which `-fsyntax-only` never runs. **This is verbatim the 2026-08-15 entry** -- "`-fsyntax-only` is blind to the truncation warnings" -- promoted that day to `CLAUDE.md` as a hard constraint, and repeated here by the agent that had read it. (Earlier in the same sweep, `gcc` was not even on PATH in that shell: the failing command piped through `grep -c` printed `0` and read as "no warnings". The compiler the build uses is in `build/CMakeCache.txt`.)
**Fix**: measure with the flags, optimisation level and compiler the build actually uses -- `grep C_FLAGS build/CMakeFiles/<target>.dir/flags.make` says what those are -- and check the tool ran at all before believing a zero.

### [RESOLVED] The rule was written down and applied to one case (2026-10-09)
**Problem**: with the solve finally running against a real six-system station, it placed it **1.8 km** from its broadcast position: worst code residual 2.6 km, apparent motion 829 m/s, clock drift 393 m/s, 25 satellites at PDOP 0.9.
**Root cause**: the solver accepted every constellation but GLONASS and SBAS under **one** receiver-clock unknown. Its own header says why GLONASS is out -- *"FDMA inter-frequency biases will not fit under one receiver-clock unknown"* -- which is not a fact about FDMA: every system keeps its own time and its own hardware path, and multi-GNSS SPP therefore carries a clock unknown per system. BeiDou adds a second fault, BDT being 14 s behind GPS while a 1042 ephemeris dates its `toe` in BDT: ~55 km of satellite position. The rule that excluded one case was the rule that excluded all of them, written out in full, in the file, and generalised to nothing.
**Fix**: GPS only until the solve has a clock unknown per system, with the measured numbers at the filter so the next reader sees the evidence rather than the preference. **When a refusal is justified by a general principle, check which other cases the principle covers** -- the sentence naming the exclusion is the place to do it. And the test that missed it is the shape to remember: `test_spp.c` verified by closure against *one* synthetic constellation, building its measurements from the same single-clock model the solver inverts. A closure test cannot discover that the world has more clocks than the model.

### [RESOLVED] The linker could not write, and the check could not tell (2026-10-09)
**Problem**: the author was about to re-test the GPS-only fix against a live station on a GUI binary that did not contain it. `bin/ntrip-analyser-gui.exe` was fifteen minutes older than `src/core/spp.c`, while `bin/ntrip-analyser.exe` was current.
**Root cause**: two failures, one on each side.
  1. **Windows refuses to overwrite a running `.exe`**: `ld.exe: cannot open output file ...: Permission denied`. The author had the GUI open -- as anyone testing a GUI does -- so every build since relinked the CLI and left the GUI alone. Nothing in the normal workflow says so.
  2. **The check was blind to it.** Verification had been `cmake --build build 2>&1 | grep -cE "warning:"`, reported as "0 warnings". A link error prints `error`, and a pipe discards make's exit status in favour of grep's. The test suite does not build the GUI, so `20/20` stayed green over a GUI that had failed to link. Both halves of "0 warnings, 20/20 tests" were true and the conclusion was wrong.
**Fix**: take the build's **exit status** first and read warnings second -- `cmake --build build > log 2>&1; echo $?` -- and, when a GUI change is to be tested, compare the binary's timestamp against its sources before handing it over (`find src gui -newer bin/ntrip-analyser-gui.exe`). **Close the GUI before building it.** This is `What is installed is not what was built` in its sixth shape, and the third time in one day that a failing tool has read as a clean result -- `gcc` absent from PATH printing `0` through `grep -c`, `-fsyntax-only` missing ten warnings, and now a linker that could not write at all.

### [RESOLVED] The guard fired on the thing the protocol exists to express (2026-10-10)
**Problem**: a station streaming ten GPS satellites reported *fewer than four usable satellites*, 0 epochs solved in 106 s. `SPP_TOO_FEW_SATS` with both refusal counters at zero does not mean "few satellites": it means **not one cell was examined**.
**Root cause**: the base sends BeiDou in two frames -- 48 cells, then 5 -- which is what MSM's multiple-message bit is for. The epoch-boundary rule read the second frame as "this system has contributed before, so a new bundle has begun" and reset the set, throwing away GPS, GLONASS, Galileo and BeiDou's own first frame. Six cells of 128 reached the solver. The guard existed for a base whose multiple-message bit never clears, and it could not tell that case from a legitimate continuation.
**Fix**: compare where comparison is sound. The code already said epoch fields are not comparable *across* constellations and stopped one step short of the consequence: they are comparable **within** one. Same system and same epoch is a continuation; same system and a different epoch is the stuck-bit case. **A guard against a malformed stream must be written so it cannot fire on a well-formed one** -- and the thing it nearly always mistakes is the protocol feature it was never told about. Found only by walking a real capture frame by frame; every test in the suite sent one frame per system per epoch, which is the one shape that cannot show it.

### [RESOLVED] The epoch was dated by whichever frame happened to close it (2026-10-10)
**Problem**: a second live station solved every epoch and placed itself **7.8 km** away -- 3888 m worst code residual, 779 mm/s of apparent motion, 8 satellites at PDOP 1.1. Good geometry, impossible answer.
**Root cause**: the solve took its time from the frame that cleared DF393, and that frame is whatever the base chooses to send last. This one ends every bundle with BeiDou, whose epoch field is **BDT -- 14 s behind GPS**. Fourteen seconds moves a GPS satellite 55 km along its orbit, and what survives the receiver-clock unknown is kilometres of position and residual. The *other* station tested that day ends its bundles with NavIC, which is GPS-aligned, so identical code was correct there **by luck** -- which is exactly why one station looked plausible and the other did not.
**Fix**: convert in core where it can be tested -- `msm_epoch_to_gps_tow_ms()`: GPS, Galileo, QZSS, NavIC and SBAS as they are, BeiDou +14 s, GLONASS refused because day-of-week plus milliseconds of *day* cannot be placed in a GPS week without the date and the leap seconds. The epoch carries `tow_gps_ms`, taken from the first frame that can say rather than the last frame to arrive. **A value that arrives on several scales must be converted at the boundary, not consumed wherever it lands** -- and the clue was already in the code: the stream clock had been locking to one constellation's scale for months, with a comment explaining why the scales are not comparable. The solve two files away did not read it.

### [RESOLVED] The station stopped sending the thing the code was waiting for (2026-10-10)
**Problem**: an hour after the epoch-boundary rule was fixed and verified against RFSEE01, the same station stopped solving entirely: 41 snapshots, 0 epochs, *"waiting for the first observation epoch"*. Nothing had changed in the code since the run that worked.
**Root cause**: NavIC had dropped out of view. It had been the last frame of every bundle, and the only one clearing DF393 -- so with it went the signal the code waited on to close a set. Every remaining frame said "more follows" and no set ever closed. A 60 s capture confirmed it: **zero** DF393 clears in the whole file. The irony is that this is the exact case the original guard was written for ("a base whose multiple-message bit never clears") -- it detected the condition correctly and still never closed anything, because detection reset the set instead of completing it.
**Fix**: close the set when the next one demonstrably begins, not only when the protocol says so -- a bundle beginning is proof the previous one ended, and for some stations it is the only proof there will be. **A stream's shape is not a property of the station; it is a property of the sky at that moment.** Two runs an hour apart, same caster, same mountpoint, same code, different message set. Anything that waits for a particular frame must also have a way to proceed without it, and the third test case written against a live capture should be assumed to describe that hour rather than that station.

### [RESOLVED] A station sent a standard field with the sign reversed (2026-10-10)
**Problem**: RFSEE01's position solved cleanly while its velocity read **1 666 m/s**, drift 932 m/s, rate residual 830 m/s. The same code and orbits gave 0.03 m/s on APEL00NLD0.
**Root cause**: the receiver at RFSEE01 writes RTCM's phase-range rate (DF399/DF404, positive while the range grows) with the opposite sign -- every satellite the exact negative of its orbit's prediction, to the millimetre per second. Our decoder was right; the station was not. Separately, the rate RMS summed **pre**-fit residuals beneath a comment claiming post-fit, so it reported the clock drift on every station and made the fault look like noise of the drift's size rather than a model mismatch.
**Fix**: let the data decide -- fit both conventions, keep the one that fits (cm/s against hundreds of m/s, never close), claim nothing with only four rates, and **report** a reversed station rather than silently correcting it. **A field defined by a standard is still a claim made by somebody's firmware**; where the data can say which way round it is, ask the data. The diagnostic that settled it was printing measured against predicted per satellite across two stations -- one line made "decoder, solver or station?" a question with one answer. And the residual bug is the comment-says-one-thing shape again: read the code beneath a comment that describes what the code should do.

### A capability claim written from the design, twice in a day (2026-10-10)
**Problem**: the feature matrix's Self-position row (P8) said the daemon and the CLI's `--report` "solve only stations that broadcast their own orbits". Neither solved even those. The daemon session found its half by measuring -- 0 of 262 samples solved on DELF00NLD0, which broadcasts its own 1019. Checking the CLI half before writing its matrix cell found the same: `docs/cli.md`, merged an hour earlier, said such a station "needs nothing more", and `-t --report` on a replay carrying a station's own ephemerides still answered `observations without orbits`.
**Root cause**: the session layer frames RTCM and decodes none of it; each frontend decodes what it needs. The GUI's frame handler decodes ephemerides, `-d` does as a side effect of printing, and `-t`, `-s` and the daemon did not. Both claims assumed a shared layer did the work, and that assumption was even copied into the daemon session's brief -- which survived only because the brief also said "verify".
**Fix**: both frontends now decode the ephemeris types they see, each with a before/after measurement. **A claim about what a frontend does is tested on that frontend before it is written** -- here a two-minute replay of observations and their station's ephemerides concatenated into one capture, needing no credentials. Same family as *Read the wiring, missed the gate* (2026-09-13): a capability inferred from part of the code instead of observed in the product.

### A handed-over commit landed in the wrong checkout (2026-10-10)
**Problem**: `self-position` gained a commit titled *"Record the daemon's scatter against the GUI's, side by side"* that contains none of that: it holds the CLI's own-orbit fix, the feature-matrix row and two changelog sections. Two minutes later the real commit of that name was made in the daemon session's worktree and had to be merged separately. The mislabelled one was already pushed, so its message stays wrong unless a force-push rewrites it.
**Root cause**: three sessions were handing the author commit commands at once, each meaning a different checkout. This session had staged its work in the main checkout; the daemon session said "run this from the worktree" and handed over a bare `git commit -m ...`. The author ran it where they stood -- the main checkout -- and git committed what was staged *there*, under the daemon's message. Nothing in the command said where it belonged; the location lived in a sentence above it. The CLI session's command had carried `git -C '<its worktree>'` and could not have gone astray.
**Fix**: **a git command handed to the author names its checkout in the command itself** -- `git -C <path> commit ...`, `git -C <path> merge ...` -- never in prose beside it. With more than one session staging work, a bare `git commit` commits whatever is staged in whichever folder the prompt happens to be in, and the message no longer describes the content. The same family as the wrong-shell entry above: there the command depended on which shell it met, here on which folder; both times the context the command needed was written beside it instead of in it, which is the same as not writing it.

### A working check replaced on a toolchain's reputation (2026-10-10)
**Problem**: after #8 merged, the agent gave MEMORY's Current State the verify command `git merge-base --is-ancestor 38a1e0e HEAD`, then replaced it with a file-based check before committing -- reasoning that CI "checks out shallow" and would not have the commit. It does have it: both jobs that run `verify_memory.py` set `fetch-depth: 0`, and have since 2026-08-26, put there in answer to *A verify command that only works on a desk* -- the very failure the agent thought it was preventing.
**Root cause**: the premise was `actions/checkout`'s *default* depth, not this repository's workflow. A correct, documented default -- and the wrong evidence, because the workflow overrides it two lines from where it would have been read.
**Fix**: none needed in the code -- the file-based check that replaced it passes and proves the same thing on `main`, so it stays. The lesson is the promoted one, in its purest form: **a toolchain's reputation is not evidence, even when the reputation is accurate**; read the workflow the check will actually run in. Caught by the next curation reading the old entry, not by any check.

### [RESOLVED] [x2] A new function's name filled a snapshot field it never touched (2026-10-12)
**Recurred the same day, 2026-10-12**: a note added inside `play-listing.md`'s *Full description* section was measured by `check_release.py` as listing text, 4181 characters against Play's 4000, and CI failed on `1fd9814`. The fix the first entry wrote down (*run `check_release.py`*) had been written for `src/core` changes, and this was a document. **`check_release.py` reads documents too** (the listing, the feature matrix, the notices), so run it before handing over *any* commit, not only code ones. The note now has a section of its own outside the measured one.

**Problem**: CI failed on `e978ec8`, but the cause was M1 (`38e32f0`), pushed with it: `check_release.py` reported "the known-gap list has no entries that are now filled -- sourcetable_pos_valid". Nothing had filled the field. M1 had added a core function of exactly that name, `sourcetable_pos_valid(lat, lon)`.
**Root cause**: the unfilled-field check is a whole-word search for each `NsStatsSnapshot` field name outside `ns_stats.c`, so any identifier spelled like a field counts as a write. The local run before committing M1 was `test_all` and `verify_memory.py`, not `check_release.py`. The guard that would have caught it locally was never run.
**Fix**: the function became `sourcetable_pos_usable`, so one name stops meaning two things, and the field stays on `SNAPSHOT_KNOWN_GAPS` where it belongs. Before naming a core function, grep `ns_stats.h` for the name. Run `python tools/check_release.py` after any `src/core` change; locally, its only expected failures are the stale-release-artefact lines.

### [RESOLVED] A string handed to a UI library ran as HTML (2026-10-12)
**Problem**: a test sourcetable entry named `<img src=x onerror="window.pwned=1">` ran its script when the mountpoint map's marker was hovered. The page source was clean: the core had escaped every `<`.
**Root cause**: `bindTooltip(p.mountpoint)` passes a string, and Leaflet sets a string tooltip as innerHTML (attribution too). The core's escaping protects the *page source*; once JavaScript decodes the JSON, a name is raw text again. Escaping once at the boundary is necessary but not sufficient, because the library is a second boundary.
**Fix**: every caster-supplied string reaches Leaflet as an element built with `textContent`, and the user's attribution goes through `textContent` → `innerHTML` before Leaflet. Caught only because the page was opened with a hostile sourcetable and a `window.pwned` canary, not by reading the code.

### [RESOLVED] A page said "Copied" when nothing was copied (2026-10-12)
**Problem**: the location picker said "Copied to clipboard!" in a browser that had refused the write (`NotAllowedError`), so **<<** pasted whatever was there before. This had been the case since the picker was written.
**Root cause**: the copy used `navigator.clipboard` whenever it existed and fell back only when it did not. The result was never consulted; the success message was printed unconditionally after the call.
**Fix**: one `copyText(t, done)` for both map pages, which tries `execCommand` and then the API, and reports `done(false)` with a selected box to copy by hand. A message announcing success must be the *callback* of the success, never the line after the call. Same family as "a screen may only say what its evidence supports".

### [RESOLVED] A library option did half of what its name says (2026-10-12)
**Problem**: Leaflet's `noWrap: true` stopped the repeated worlds, but the page still requested 16 tiles off the world's edge (x = −2…5 at zoom 2), all wasted.
**Root cause**: the name was read as the behaviour. Leaflet's own documentation says `noWrap` "can be used in combination with the `bounds` option to prevent requesting tiles outside the CRS limits".
**Fix**: `bounds` added. Found by counting the tile requests the page made, not by reading the option. A recurrence of *read the artefact, not the toolchain's promise*.

### [RESOLVED] A shipped feature broke when the service under it changed (2026-10-12)
**Problem**: the location picker (**Map**) had shown nothing but *API KEY REQUIRED* watermarks since CARTO began requiring a key from everyone (about 2026-09-23). It was found three weeks later, and only because a new feature reused its tile source.
**Root cause**: the feature depended on a third party's free terms, and nothing ever looked at the page it produces. The OSM Foundation's tile server, the obvious alternative, refuses a page opened from a file (no `Referer`, 403); a comment in the code had said so, and it was verified again.
**Fix**: no default tile source. Natural Earth outlines are built in, and the user may set their own key. Nothing shipped depends on a third party's goodwill for the map underneath any more. Lesson: **a dependency on someone else's free service is a claim about their terms, and terms change** — prefer what the program carries.

### [RESOLVED] The GUI's ANSI text went into a UTF-8 page (2026-10-12)
**Problem**: CARTO's attribution `© OpenStreetMap contributors © CARTO` would have shown as `� … �`.
**Root cause**: the GUI is an ANSI program, so `©` is the single byte 0xA9; the page is UTF-8, and the core rightly replaces invalid UTF-8 with U+FFFD. Each half was right on its own.
**Fix**: `AnsiToUtf8` (CP_ACP → wide → CP_UTF8) before quoting. Any GUI text bound for a page, a JSON file or the network crosses an encoding boundary. The sourcetable's bytes do not, because they never were ANSI.

### [RESOLVED] A Leaflet map measured its container before it had a size (2026-10-12)
**Problem**: the mountpoint map opened at maximum zoom on empty sea.
**Root cause**: the map's top edge was set by script *after* `L.map()`. Leaflet measures the container once, at creation, so `fitBounds` saw 0 px of height.
**Fix**: a flex layout, with the header filled in before the map is made. Found by `getBoundingClientRect` and the pane transform, not by the screenshot, which in the browser pane is sometimes blank while the canvas holds 845 894 painted pixels. When a screenshot shows nothing, count the canvas's pixels before believing it.

### A handed-over commit aborted on an empty message (2026-10-12)
**Problem**: the author ran the suggested `git commit`, the editor closed without a message, and git aborted. The author reported "committed" and asked for the next session to start from a commit that did not exist.
**Root cause**: the commit message lived in the conversation, not in the command. That is the third face of the promoted rule *a command carries all its context in itself*.
**Fix**: since then every commit is handed over as `git -C <checkout> commit -F <file>`, with the message written to the scratchpad first; it has worked four times. The claimed commit was checked (`git log`, worktrees, every branch) before anything was started on it, which is what caught this.

### A blind tap script walked out of the app (2026-10-13) [x3]
**Recurrence 2026-10-14 (Huawei)**: after `adb install -r` the first `am start` was ignored, the launcher's app drawer had focus, and the swipes meant for the hub scrolled the drawer. Relaunch until `mCurrentFocus` names the app, then act.
**Recurrence 2026-10-14**: reading before each tap was not enough. A Back press meant to close a dialog that had already closed left the app, and the swipe that followed scrolled the launcher's store front; another Back landed on the S23's launcher. Check the focused window (`dumpsys window | grep mCurrentFocus`) before every Back and every swipe, not only before taps. And the reader itself (`uiautomator dump` parsed with a regex) died on a node without `content-desc`; read attributes as optional.
**Problem**: a script meant to open the phone's mountpoint map ten times reported "nothing after 30 s" every time. The phone was on the launcher's app drawer, and the dev app's active connection and one connection's mountpoint had changed meanwhile.
**Root cause**: on the Huawei the first tap after the app comes to the front is often ignored; the map never opened, and the script's Back -- meant to leave the map -- left the app. Nine more rounds tapped blind coordinates on the home screen. Separately, a button's position was parsed with a regex that took a stray digit from the node before it, so a tap meant for *Select mountpoint* landed on the map.
**Fix**: every round now brings the app to the front (`am start`), reads the screen (`uiautomator dump`) before each tap, taps an element by its own `bounds`, retries a tap that did not open what it should, and presses Back only from a screen it has confirmed. Bounds are parsed with an anchored `sed` on `bounds="[x1,y1][x2,y2]"`. The changed connections were reported to the author, not repaired: whether a tap or the author made them could not be told.

### An expected value worked by hand used the wrong Earth (2026-10-13)
**Problem**: a new test said one degree of latitude is 111194.93 m and failed against the core's 111195.08.
**Root cause**: the hand figure was for a 6371 km sphere; the core uses the mean radius, 6371008.8 m (`geo.h`). The code was right.
**Fix**: a probe compiled with the build's flags printed the core's own value before the test was changed. An expected value comes from the constant the code uses, computed, not recalled.

### [RESOLVED] A new field would have refused the biggest caster on the phone (2026-10-13)
**Problem**: M6a added the core's kind, distance and bearing to each sourcetable entry crossing to the app. The JNI glue allocated a fixed 1 MB and the bridge refuses rather than cuts, so onocoy -- 1.26 MB with the new fields -- would have reached the phone as "Could not reach the caster". Underneath, the bridge read at most 4096 entries, so the phone's list of onocoy had always lacked about a third.
**Root cause**: a size chosen from the casters in hand (Centipede's 150 kB), never re-measured when the document grew.
**Fix**: the bridge sizes its own output (256 KB doubling to 32 MB, written again from the same table), and the entry ceiling is 16384. Found only because the change was run against the largest caster available through the bridge's own fetch, compiled on the desktop -- not against the two-entry caster on the phone.

### [RESOLVED] Twenty seconds of silence was taken for the end of a list (2026-10-13)
**Problem**: the phone's map drew 12 of onocoy's ~5900 mountpoints as though that were the caster.
**Root cause**: the sourcetable fetch ends on `ENDSOURCETABLE` or on two silent ten-second waits, and returned whatever it had either way. onocoy goes silent part-way now and then -- curl from the desktop then got 1315 entries without the closing line, then 9, then none.
**Fix**: `receive_mount_table_ex()` reports an answer that stops before `ENDSOURCETABLE` as stalled or dropped; `receive_mount_table_whole()` asks once more and refuses a list still cut short, in every frontend. Six casters checked all end a whole list with the line. A timeout is not an end-of-data marker; where the protocol has one, require it.

### [RESOLVED] A loopback test died of SIGPIPE on Linux only (2026-10-13)
**Problem**: CI failed `test_bridge_vrs` with SIGPIPE after new cases were added at its end; Windows passed.
**Root cause**: the test's caster thread kept writing to a stream the bridge had closed. Before, the process ended at the close; the new cases kept it alive long enough for a write to hit the closed socket, and Linux kills the writer. Windows has no such signal.
**Fix**: `signal(SIGPIPE, SIG_IGN)` in the test, as `test_tls.c` already did. Closing the socket on a failed send was rejected: the socket is non-blocking, and a full buffer fails the same way. A loopback harness on POSIX ignores SIGPIPE from its first line.

### [RESOLVED] Frame times taken where they could not mean anything (2026-10-13)
**Problem**: the map's frame times were reported from debug builds, and one set was read from counters never reset before the author's pinch, so it held two map loads and a selection besides.
**Root cause**: a debug build runs Compose unoptimised -- the same pan was a median 24 ms on debug and 16 ms on a release-configured build -- and `gfxinfo` accumulates since the process started.
**Fix**: a `bench` build type (release settings, debug key, `….bench`) and `dumpsys gfxinfo <pkg> reset` immediately before the action being measured (`docs/RUNBOOK.md`). The same rule as *measure the way the build measures*, for frames.

### [RESOLVED] Compose state read in composition redrew the whole screen per gesture step (2026-10-13)
**Problem**: panning the map ran at a median 73 ms a frame even with its outlines pre-rendered.
**Root cause**: the view's scale and pan were keys of a `LaunchedEffect`, so every gesture step recomposed the screen; the marker loop then read those states twice per mark, 5900 times.
**Fix**: the view is watched through `snapshotFlow` and read once per frame in the draw; empty-map panning went to 7 ms. Read state where it is used -- the draw phase -- and once.

### A toolchain move left the old Gradle daemon holding lint's cache (2026-10-14) [RESOLVED]
**Problem**: after AGP 8.11 → 8.13 and again 8.13 → 9.4, `lintVitalAnalyzeProRelease` failed with "being used by another process" on a jar in `lint-cache`; `clean` failed the same way.
**Root cause**: a daemon of the previous Gradle version still had the file open, and the new wrapper's `--stop` reaches only daemons of its own version.
**Fix**: stop it with that version's own `gradle --stop` (from `~/.gradle/wrapper/dists`). In `docs/RUNBOOK.md`'s toolchain note.

### PowerShell split a Gradle property at its dots (2026-10-14)
**Problem**: `gradlew bundleProRelease -Pandroid.r8.optimizedResourceShrinking=false` failed: "Task '.r8.optimizedResourceShrinking=false' not found".
**Root cause**: PowerShell parses an unquoted `-P…` argument with dots in it, and handed Gradle only part of it.
**Fix**: quote it: `'-Pandroid.r8.optimizedResourceShrinking=false'`.

### An XML comment may not contain `--` (2026-10-14)
**Problem**: a new `AndroidManifest.xml` failed the manifest merger with "Error parsing", and nothing more.
**Root cause**: its header comment used ` -- ` as a dash; `--` is illegal inside an XML comment.
**Fix**: a colon instead. Write XML comments without double hyphens; the house style's ` -- ` belongs in code comments only.

### The phone locked between the steps of a test (2026-10-14) [x4]
**Problem**: four times in one afternoon a step on the S23 or the Huawei found the lock screen (`NotificationShade` or the clock in the dump) instead of the app, and waited for the author.
**Root cause**: the screen timeout is shorter than the gap between steps that wait on a build, a watch or the author.
**Fix**: read `mCurrentFocus` before each step and ask for an unlock instead of acting on the lock screen; a longer timeout during a session is the author's setting to change.

### A corrupted NDK download failed CI on a docs-only commit (2026-10-14)
**Problem**: "Both editions" failed on a commit that changed two Markdown files: "Error reading Zip content from a SeekableByteChannel" in *Install the pinned NDK*, before Gradle ran.
**Root cause**: a truncated download on the runner; nothing in the repository.
**Fix**: `gh run rerun <id> --failed` passed. Read the failing step before the code; if it recurs, cache the NDK in `ci.yml`.

### A merge of a subset erased what only the superset holds (2026-10-14) [RESOLVED]
**Problem**: importing free's connection into pro blanked pro's ephemeris stream; self-position on a station that sends no orbits sat at "waiting for orbits".
**Root cause**: the merge replaced a matching connection outright, and free's empty ephemeris fields meant "I cannot hold this", not "remove it".
**Fix**: `mergeConnections(fromFree = true)` keeps the fields free cannot hold. A merge from a narrower source must say which fields it owns.

### The wiki went six weeks unpublished behind a broken clone (2026-10-15) [RESOLVED]
**Problem**: `tools/publish_wiki.sh` failed with "fatal: not a git repository" and copied nothing; the live wiki was still the 25 August version, so the Pro page said "closed testing" a day after pro reached Play. The CI "pages deployment" kept passing, which is the project website, not the wiki.
**Root cause**: an interrupted clone on 2026-09-02 left `$TMPDIR/ntrip-wiki/.git` holding only `hooks/`; the script reused any folder with a `.git` in it, and git refused that one.
**Fix**: the script reuses its copy only when `git -C "$WORK" rev-parse --git-dir` prints exactly `.git`, and stops with a message when a fetch fails. A published page is checked by fetching it — the wiki's `_history` date — not by any build status.

## Promoted

<!-- Track what has been promoted, so it is not promoted twice and so the loop
     is visibly working. Keep incrementing occurrences AFTER promotion: a
     promoted pattern that recurs means the promotion did not take. -->

| Date | Gotcha | Occurrences | Promoted to |
|------|--------|-------------|-------------|
| 2026-08-13 | A remembered value must not satisfy the KPI that asks for it | 1 | project file, hard constraint |
| 2026-08-13 | Judge constellations by NavSys, never the 1005/1006 bits | **3** — 2026-08-12 three times in one session | project file, domain facts; `memory/MEMORY.md` active decisions |
| 2026-08-14 | Scripted file edits corrupt what they rewrite — escapes, then line endings, then the whole file | **13** — heredoc 2026-08-12, doubled CRs and a literal newline 2026-08-14, `sed -i` deleting a table row 2026-08-16, four eaten backslashes and one truncated file 2026-08-22, **four halved-backslash heredocs 2026-08-25** (the tool halves `\\` before the shell sees it — one anchor became a literal NUL); **2026-10-09 the constraint itself broken without damage** — a `python - <<'PY'` heredoc used to edit a verify comment in `CLAUDE.md`. The file came through intact, which is luck rather than compliance, and is not counted as a corruption | project file, hard constraint — scripts now go through Write to a file, never a heredoc |
| 2026-08-14 | A data property appears in every renderer, so fix it in all of them | **2** — Android 2026-08-13, GUI 2026-08-14 | `memory/MEMORY.md` active decisions |
| 2026-08-14 | Read the artefact; a toolchain's reputation is not evidence | **12** — 16 KB alignment, bundle ABIs, signing key (2026-08-14); wrapped strings on-device 2026-08-23 (reconnect line) and 2026-08-25 twice (export filename template, banner evidence line); **then four in a new shape, 2026-09-09 to -13: acting on a *report about* an artefact** — a support reply against the console's pricing page, "charged" against the checkout's test card, an AI Overview against a €0,00 order, and wiring against the gate in `Dialogs.kt`. The promotion names toolchains; the recurrences are second-hand accounts; **2026-10-10 back to the original shape** — a working verify command replaced because `actions/checkout` defaults to a shallow clone, while this repository's workflow sets `fetch-depth: 0`; **2026-10-12 a library option read by its name** — Leaflet's `noWrap` still requested 16 tiles off the world's edge until paired with `bounds`, found by counting requests; **2026-10-13 an inference from the app's state** — "NRBY_ADV sits at a nominal 52, 6" was written into the plan from what the phone showed after selecting it; the raw record (`STR;NRBY_ADV;GLOBAL;…;INT;;;1;0`) has no position at all, and 52, 6 was the app's default kept because there was none to take. Corrected in the plan the same day | project file, hard constraint — **widened 2026-09-17 to cover reports about an artefact** |
| 2026-08-15 | Measure the way the build measures, or report no number | **4** — `-fsyntax-only` blind to truncation warnings, `-std=c99` hiding `M_PI`, both 2026-08-15; **2026-10-09 the identical `-fsyntax-only` sweep, by an agent that had read the constraint, missed the same ten truncation warnings — and in the same sweep `gcc` was absent from PATH, so a failed command piped through `grep -c` reported `0` and read as clean** | project file, hard constraint |
| 2026-08-15 | Two build systems over one source set: CI must run both | **2, then closed** — `build-gui.bat` retired 2026-08-25 (TLS L2; it had been broken since L1), `service/Makefile` builds by wildcard | `memory/MEMORY.md` active decisions |
| 2026-08-25 | Gradle cannot see out-of-tree C sources — clean + `--no-build-cache` after any `src/` or `lib/` C change | **3** — three stale-`.so` rounds in one afternoon; deleting `.cxx` alone re-served the cache | `docs/RUNBOOK.md` Common Problems |
| 2026-08-16 | A snapshot field nothing fills is worse than a missing one | **3** — ARP fields (until a live run tripped over them), `latency_s` and `sourcetable_offset_m` (both found 2026-08-16) | `memory/MEMORY.md` active decisions |
| 2026-08-16 | A green verdict is not a correct registration — read the sourcetable | **2** — 3.3 km transposition and a 25 km paste, both STATION OK, 2026-08-16 | `memory/MEMORY.md` active decisions; specified as measurement-tiers phase 0 |
| 2026-08-16 | What is installed is not what was built — on any platform | **6** — pro's APK 2026-08-14, the VPS binary 2026-08-16, `test_all` green beside a stale `bin/` 2026-08-18, **Play's 3.7.0 bundle built before the fix it claimed to carry, and an install that succeeded from a failed build, both 2026-08-23**; **2026-10-09 the GUI could not relink because it was running, and "0 warnings, 20/20" was reported over the failure** | `memory/MEMORY.md` active decisions (generalised from Android); `tools/check_release.py` artefact checks |
| 2026-08-18 | A tag points at a commit, not at your working tree | **2** — v3.4.0 2026-08-15, v3.5.0 2026-08-18, both staged-not-committed, both caught by the packaging guard | `docs/RUNBOOK.md` release sequence, with the `git show <tag>:` check |
| 2026-08-18 | An entry is only `[RESOLVED]` when code or procedure changed | **1** — the v3.4.0 tag gotcha was retired on advice alone and recurred in three days | this log's header, promotion lifecycle |
| 2026-08-20 | The shell between you and the device edits what passes through it — and so does the one between you and the author | **4** — paths rewritten by Git Bash 2026-08-14 and again 2026-08-20, a PNG CRLF-mangled by the PTY 2026-08-20; 2026-09-13 the cure for the first broke the local path instead; **2026-10-09 a Bash heredoc handed to a PowerShell prompt wrote its own wrapper into a commit subject** — and its sibling of 2026-10-10, below, where the context the command lacked was a folder rather than a shell | `memory/MEMORY.md` active decisions; `MSYS_NO_PATHCONV=1` for paths, `adb exec-out` for bytes |
| 2026-08-22 | The device under test holds the author's data | **2** — `pm clear` wiped pro's caster and credentials; free on the S23 was left alone for the same reason | `memory/MEMORY.md` active decisions |
| 2026-08-25 | A pause that lives in prose is not a checklist | **2** — 3.7.2 and 3.7.3 both hit `no matches found` at the upload because packaging lived in a sentence between the commands | `docs/RUNBOOK.md` release sequence; hand over commands only when everything they name exists |
| 2026-08-26 | Cite the basis, or say there isn't one | **1** — a €12.99 price "band" the agent had never looked up, nearly attached to a live listing; the real search found no paid NTRIP market at all | `CLAUDE.md` hard constraint |
| 2026-08-25 | A flag's lifetime must match the thing it describes | **2** — `usedEphStream` reset per run over a process-lived cache (2026-08-25); the same family as the matrix's stale-file-as-full-cache | `memory/MEMORY.md` active decisions |
| 2026-10-09 | A screen may only say what its evidence supports — "not yet" is evidence of nothing | **4** — "fewest held: 9" from a partial first epoch and "no dual-frequency pair" at 25 s (both 2026-08-18, fixed with `SR_WARMUP_S` and a judgeability gate); **NOT COMPUTABLE at 32 s in the new Self-position window, and ring labels of 0.125 m on a plot with no points, both 2026-10-09** — the second pair happened in a window added to the same design document that records the first; **2026-10-12 a web page**: the location picker said "Copied to clipboard!" after a write the browser had refused, its success message printed after the call rather than from its result | `design/gui-design.md` §14.5, §16.5 and §17.4; `memory/MEMORY.md` active decisions |
| 2026-10-09 | A falsification that stays green accuses the test, not the code | **3** — synthetic ephemerides sharing one `af1` hid a deleted satellite clock-drift term (P4); a snapshot that blanked `selfpos_speed_mms` beside `has_vel = false` hid a deleted `has_vel` gate (P5); **and again in P6, hours after the promotion**, a snapshot left at its `NS_UNSET` initialisers hid a deleted status gate in the *snapshot* serialiser — the promotion did not stop the test being written politely; the falsification caught it; **2026-10-12, done systematically**: a mutation run of 13 broken copies of `sourcetable_map.c` found three cases passing for the wrong reason (a NUL check the decoder made redundant, a length limit whose test was malformed anyway, every test entry a fixed base) | `memory/MEMORY.md` active decisions and Recently Promoted |
| 2026-10-10 | A claim about what a frontend does is tested on that frontend before it is written | **3** — *Read the wiring, missed the gate* 2026-09-13; then twice on 2026-10-10: the feature matrix said the daemon solved stations broadcasting their own orbits (it solved none), and `docs/cli.md` said the same of `-t --report` (it did not either) — both assuming a shared layer decoded ephemerides, which none does | `memory/MEMORY.md` active decisions and Recently Promoted |
| 2026-10-10 | A command handed to the author carries all its context in the command itself — shell and checkout | **4** — a Bash heredoc handed to a PowerShell prompt (2026-10-09); a bare `git commit` run in the wrong checkout, which committed another session's staged work under this one's message (2026-10-10); **a bare `git commit` whose editor closed empty, reported as committed (2026-10-12)** — the message too is context, so commits now go over as `git -C <checkout> commit -F <file>`; **2026-10-13 a `git cherry-pick` handed over while changes were staged**, which git refuses — the order of the commands, and the state of the index, are context too: hand over the commit first, then what depends on it. Each time the context was written beside the command instead of in it | `memory/MEMORY.md` active decisions and Recently Promoted |
| 2026-10-14 | Driving a phone from a script: read the focus before every tap, Back and swipe | **3** — the blind tap script (Huawei 2026-10-13; Back and swipe on the S23 and an ignored `am start` after install on the Huawei, 2026-10-14), Git Bash rewriting `/sdcard` (2026-09-13, 2026-10-14), the lock screen four times in one afternoon (2026-10-14) | `docs/RUNBOOK.md`, Android: build, install, observe |
