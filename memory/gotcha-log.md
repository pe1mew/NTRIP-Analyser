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

### Measured the way the build measures -- and then did it again (2026-10-09)
**Problem**: before adding `-Wall`, the codebase was swept for warnings with `gcc -Wall -fsyntax-only` and reported clean. The real `-O3` build then produced ten: six `-Wformat-truncation` and four `-Wstringop-truncation`.
**Root cause**: those warnings come from the optimiser's value range propagation, which `-fsyntax-only` never runs. **This is verbatim the 2026-08-15 entry** -- "`-fsyntax-only` is blind to the truncation warnings" -- promoted that day to `CLAUDE.md` as a hard constraint, and repeated here by the agent that had read it. (Earlier in the same sweep, `gcc` was not even on PATH in that shell: the failing command piped through `grep -c` printed `0` and read as "no warnings". The compiler the build uses is in `build/CMakeCache.txt`.)
**Fix**: measure with the flags, optimisation level and compiler the build actually uses -- `grep C_FLAGS build/CMakeFiles/<target>.dir/flags.make` says what those are -- and check the tool ran at all before believing a zero.

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

### PowerShell split a Gradle property at its dots (2026-10-14)
**Problem**: `gradlew bundleProRelease -Pandroid.r8.optimizedResourceShrinking=false` failed: "Task '.r8.optimizedResourceShrinking=false' not found".
**Root cause**: PowerShell parses an unquoted `-P…` argument with dots in it, and handed Gradle only part of it.
**Fix**: quote it: `'-Pandroid.r8.optimizedResourceShrinking=false'`.

### An XML comment may not contain `--` (2026-10-14)
**Problem**: a new `AndroidManifest.xml` failed the manifest merger with "Error parsing", and nothing more.
**Root cause**: its header comment used ` -- ` as a dash; `--` is illegal inside an XML comment.
**Fix**: a colon instead. Write XML comments without double hyphens; the house style's ` -- ` belongs in code comments only.

### The phone locked between the steps of a test (2026-10-14) [x7]
**Recurrence 2026-10-20 and -21 (Huawei)**: during the GUI review's long samples (the 60 s sustain, the 600 s watch floor), and once more before the track bench, which the script caught (it checks the focus before each size and stops with `LOCKED`). Same fix; the timeout stays the author's setting.
**Problem**: four times in one afternoon a step on the S23 or the Huawei found the lock screen (`NotificationShade` or the clock in the dump) instead of the app, and waited for the author.
**Root cause**: the screen timeout is shorter than the gap between steps that wait on a build, a watch or the author.
**Fix**: read `mCurrentFocus` before each step and ask for an unlock instead of acting on the lock screen; a longer timeout during a session is the author's setting to change.

### A corrupted NDK download failed CI on a docs-only commit (2026-10-14)
**Problem**: "Both editions" failed on a commit that changed two Markdown files: "Error reading Zip content from a SeekableByteChannel" in *Install the pinned NDK*, before Gradle ran.
**Root cause**: a truncated download on the runner; nothing in the repository.
**Fix**: `gh run rerun <id> --failed` passed. Read the failing step before the code; if it recurs, cache the NDK in `ci.yml`.

### A Bash idiom handed to a PowerShell 5.1 prompt (2026-10-15)
**Problem**: the wiki publish command went over as `cd … && bash tools/publish_wiki.sh --push`; the author's PowerShell 5.1 refused `&&` with a parser error. A `/tmp` path failed the same way.
**Root cause**: the command was written in the agent's shell, not the author's. PowerShell 5.1 has no `&&`, `||` or `/tmp`.
**Fix**: hand over one command per block, written for PowerShell: `& "C:\Program Files\Git\bin\bash.exe" tools/publish_wiki.sh --push`, `git -C <checkout>`. A recurrence of the promoted *command carries its context* pattern.

### Play's description still said "First closed test" a month after going public (2026-10-16)
**Problem**: free's live store page opened with the closed-test text ("First closed test of NTRIP Analyser… What would help most: a station where the verdict looks wrong") from 2026-09-16 to 2026-10-16, in English and in Google's Dutch translation, and sat in *Maps & Navigation*. `play-listing.md` said its full description section was "what is live", and it said *Tools*.
**Root cause**: the listing doc was taken as the record of the console; nobody read the live page whole. Release-note checks looked only at *What's new*.
**Fix**: read the store page itself (`get_page_text`, `hl=en` and `hl=nl`) after every listing or release change; both listings rewritten 2026-10-16 and free moved to Tools. A recurrence of *read the artefact, not a report about it*.

### "Done" covered handed-over commands that never ran (2026-10-16) [x2]
**Problem**: after a batch of cleanup commands the author reported "done"; every branch was still there. The first command (`git worktree remove`) had failed on *Filename too long* halfway — the worktree unregistered, its 241 MB of files left behind — and nothing after it took effect. A second "done" left `origin/mountmap-android` and `origin/tls` in place; earlier in the session two pushes reported as made had not happened either.
**Root cause**: a batch is reported by its intent, not its output. On Windows, Gradle output in a worktree passes the 260-character path limit (269 here), and `worktree remove` stops after unregistering.
**Fix**: after every reported action read the state it was meant to change (`git ls-remote --heads`, `git branch -a`, `git worktree list`, `git status -sb`) before building on it. Remove a worktree with `git -c core.longpaths=true worktree remove …`, or delete a leftover folder with `cmd /c rd /s /q "\\?\C:\…"`. Remove worktrees before `branch -d`.

### Play installed a minSdk the build never declared (2026-10-16)
**Problem**: the Huawei's Play install of 3.9.0 reported `minSdk=29`; `build.gradle.kts` says 26, and 3.8.0 from Play had reported 26. It looks like a toolchain regression.
**Root cause**: most likely none in the build: Play generates APKs from the bundle per SDK range, and this Android 10 phone (SDK 29) would get the 29+ one. **Not checked** that an Android 8 or 9 phone is still offered the app; `bundletool build-apks` on the `.aab` would show the variants.
**Fix**: judge `minSdk` from the bundle or a locally built APK, not from one device's install.

### `git stash` used to compare against HEAD set aside the whole session (2026-10-20)
**Problem**: mid-check, a `git stash` meant to see the committed state took every uncommitted change of the step, and the author's untracked `guiTemplate.pptx` with them.
**Root cause**: stash is a git write command on the working tree, used as if it were a read.
**Fix**: `git stash pop --index` restored everything, staged state included. Compare against HEAD with `git show HEAD:<path>` or `git diff`, never by moving the work aside; stash is not used here.

### `Select-Object -First` on a Gradle pipe killed the build (2026-10-20)
**Problem**: `.\gradlew.bat … | Select-Object -First 5` ended Gradle with exit 255 and no error.
**Root cause**: PowerShell stops the upstream pipeline once `-First` has its lines; Gradle is killed mid-build.
**Fix**: capture first, then filter: `$o = .\gradlew.bat … ; $o | Select-Object -Last 20`.

### A plan assumed a layer could do what it had never been asked to (2026-10-21)
**Problem**: the GUI review plan said the network-RTK check could keep monitoring after its gate "with the existing session calls". It could not: once the gate's disconnect ended the session, nothing in `src/session` restarted it.
**Root cause**: the plan was written from what the bridge seemed to need, not from `ntrip_session.h`. A recurrence of the promoted *a claim about what a layer does is tested before it is written*, in a plan rather than a document.
**Fix**: found when step 3 started; the author chose an additive call, `ns_set_auto_reconnect()`, pinned by `test/test_bridge_vrs.c`. Grep the header for the call before a plan names a capability.

### A colour judged a distance other than the one beside it (2026-10-21)
**Problem**: the reference-position drive coloured its distance by A3, and showed 155,7 km in green.
**Root cause**: A3 judges the distance to the GGA position sent; the number shown was the phone's. They agree only when the phone's fix is what is sent.
**Fix**: the colour stays neutral unless both measure the same distance (`HandoverDetail`'s `a3` gate). A recurrence of the promoted *a screen may only say what its evidence supports*: a colour is a claim about the number it sits on.

### The bridge test left the test caster's slots taken (2026-10-21)
**Problem**: after the new reconnect block in `test_bridge_vrs.c`, the following cases could not connect: both of the stub caster's slots were held.
**Root cause**: the reconnecting session re-opened a slot after `bridge_close` freed the first; the stub had no way to drop it.
**Fix**: `g_drop = 1` and a short sleep after `bridge_close`, so every case starts with free slots. A new case that reconnects must free what it takes.

### Indented lines in the listing doc read as store text (2026-10-21)
**Problem**: `tools/check_release.py` failed on `play-listing.md`: the pending bullet change, indented four spaces, was measured as a short description.
**Root cause**: the checker takes indented blocks as the literal store text.
**Fix**: proposals and pending wording go in blockquotes, never indented.

### A Wi-Fi toggle is not a clean stimulus on the Huawei (2026-10-20)
**Problem**: to see the bar's restart reason, Wi-Fi was toggled twice. The phone did not rejoin by itself the first time, and when it did, no restart showed: the drop failed KPI 1 instead.
**Root cause**: the drop is a real outage, and KPI 1 judging it is correct; a restart needs the candidate verdict to change without a failure. The Huawei also writes no lines for the app to logcat, so nothing there shows what happened.
**Fix**: recorded as *restart reason not observed* in `design/appGuiReview.md`. Rejoining Wi-Fi is the author's; observe through the screen (`uiautomator dump`), not logcat.

## Promoted

<!-- Track what has been promoted, so it is not promoted twice and so the loop
     is visibly working. Keep incrementing occurrences AFTER promotion: a
     promoted pattern that recurs means the promotion did not take. -->

| Date | Gotcha | Occurrences | Promoted to |
|------|--------|-------------|-------------|
| 2026-08-13 | A remembered value must not satisfy the KPI that asks for it | 1 | project file, hard constraint |
| 2026-08-13 | Judge constellations by NavSys, never the 1005/1006 bits | **3** — 2026-08-12 three times in one session | project file, domain facts (the index's copy removed 2026-10-18 as a duplicate) |
| 2026-08-14 | Scripted file edits corrupt what they rewrite — escapes, then line endings, then the whole file | **13** — heredoc 2026-08-12, doubled CRs and a literal newline 2026-08-14, `sed -i` deleting a table row 2026-08-16, four eaten backslashes and one truncated file 2026-08-22, **four halved-backslash heredocs 2026-08-25** (the tool halves `\\` before the shell sees it — one anchor became a literal NUL); **2026-10-09 the constraint itself broken without damage** — a `python - <<'PY'` heredoc used to edit a verify comment in `CLAUDE.md`. The file came through intact, which is luck rather than compliance, and is not counted as a corruption | project file, hard constraint — scripts now go through Write to a file, never a heredoc |
| 2026-08-14 | A data property appears in every renderer, so fix it in all of them | **2** — Android 2026-08-13, GUI 2026-08-14 | `memory/MEMORY.md` active decisions |
| 2026-08-14 | Read the artefact; a toolchain's reputation is not evidence | **13** — **2026-10-16 the listing doc against the store page**: `play-listing.md` called its text "what is live" while free's page had shown the closed-test description and sat in another category for a month. Earlier, 12 — 16 KB alignment, bundle ABIs, signing key (2026-08-14); wrapped strings on-device 2026-08-23 (reconnect line) and 2026-08-25 twice (export filename template, banner evidence line); **then four in a new shape, 2026-09-09 to -13: acting on a *report about* an artefact** — a support reply against the console's pricing page, "charged" against the checkout's test card, an AI Overview against a €0,00 order, and wiring against the gate in `Dialogs.kt`. The promotion names toolchains; the recurrences are second-hand accounts; **2026-10-10 back to the original shape** — a working verify command replaced because `actions/checkout` defaults to a shallow clone, while this repository's workflow sets `fetch-depth: 0`; **2026-10-12 a library option read by its name** — Leaflet's `noWrap` still requested 16 tiles off the world's edge until paired with `bounds`, found by counting requests; **2026-10-13 an inference from the app's state** — "NRBY_ADV sits at a nominal 52, 6" was written into the plan from what the phone showed after selecting it; the raw record (`STR;NRBY_ADV;GLOBAL;…;INT;;;1;0`) has no position at all, and 52, 6 was the app's default kept because there was none to take. Corrected in the plan the same day | project file, hard constraint — **widened 2026-09-17 to cover reports about an artefact** |
| 2026-08-15 | Measure the way the build measures, or report no number | **4** — `-fsyntax-only` blind to truncation warnings, `-std=c99` hiding `M_PI`, both 2026-08-15; **2026-10-09 the identical `-fsyntax-only` sweep, by an agent that had read the constraint, missed the same ten truncation warnings — and in the same sweep `gcc` was absent from PATH, so a failed command piped through `grep -c` reported `0` and read as clean** | project file, hard constraint |
| 2026-08-15 | Two build systems over one source set: CI must run both | **2, then closed** — `build-gui.bat` retired 2026-08-25 (TLS L2; it had been broken since L1), `service/Makefile` builds by wildcard | `memory/MEMORY.md` active decisions; checked 2026-10-16, `build-gui.bat` absent and the Makefile still globs |
| 2026-08-25 | Gradle cannot see out-of-tree C sources — clean + `--no-build-cache` after any `src/` or `lib/` C change | **3** — three stale-`.so` rounds in one afternoon; deleting `.cxx` alone re-served the cache | `docs/RUNBOOK.md` Common Problems |
| 2026-08-16 | A snapshot field nothing fills is worse than a missing one | **3** — ARP fields (until a live run tripped over them), `latency_s` and `sourcetable_offset_m` (both found 2026-08-16) | `memory/MEMORY.md` active decisions |
| 2026-08-16 | A green verdict is not a correct registration — read the sourcetable | **2** — 3.3 km transposition and a 25 km paste, both STATION OK, 2026-08-16 | `memory/MEMORY.md` active decisions; specified as measurement-tiers phase 0 |
| 2026-08-16 | What is installed is not what was built — on any platform | **6** — pro's APK 2026-08-14, the VPS binary 2026-08-16, `test_all` green beside a stale `bin/` 2026-08-18, **Play's 3.7.0 bundle built before the fix it claimed to carry, and an install that succeeded from a failed build, both 2026-08-23**; **2026-10-09 the GUI could not relink because it was running, and "0 warnings, 20/20" was reported over the failure** | `memory/MEMORY.md` active decisions (generalised from Android); `tools/check_release.py` artefact checks |
| 2026-08-18 | A tag points at a commit, not at your working tree | **2** — v3.4.0 2026-08-15, v3.5.0 2026-08-18, both staged-not-committed, both caught by the packaging guard | `docs/RUNBOOK.md` release sequence, with the `git show <tag>:` check |
| 2026-08-18 | An entry is only `[RESOLVED]` when code or procedure changed | **1** — the v3.4.0 tag gotcha was retired on advice alone and recurred in three days | this log's header, promotion lifecycle |
| 2026-08-20 | The shell between you and the device edits what passes through it — and so does the one between you and the author | **4** — paths rewritten by Git Bash 2026-08-14 and again 2026-08-20, a PNG CRLF-mangled by the PTY 2026-08-20; 2026-09-13 the cure for the first broke the local path instead; **2026-10-09 a Bash heredoc handed to a PowerShell prompt wrote its own wrapper into a commit subject** — and its sibling of 2026-10-10, below, where the context the command lacked was a folder rather than a shell | `memory/MEMORY.md` active decisions; `MSYS_NO_PATHCONV=1` for paths, `adb exec-out` for bytes; checked 2026-10-16, still needed by the device helpers |
| 2026-08-22 | The device under test holds the author's data | **2** — `pm clear` wiped pro's caster and credentials; free on the S23 was left alone for the same reason | `memory/MEMORY.md` active decisions |
| 2026-08-25 | A pause that lives in prose is not a checklist | **2** — 3.7.2 and 3.7.3 both hit `no matches found` at the upload because packaging lived in a sentence between the commands | `docs/RUNBOOK.md` release sequence; hand over commands only when everything they name exists |
| 2026-08-26 | Cite the basis, or say there isn't one | **1** — a €12.99 price "band" the agent had never looked up, nearly attached to a live listing; the real search found no paid NTRIP market at all | `CLAUDE.md` hard constraint |
| 2026-08-25 | A flag's lifetime must match the thing it describes | **2** — `usedEphStream` reset per run over a process-lived cache (2026-08-25); the same family as the matrix's stale-file-as-full-cache | `memory/MEMORY.md` active decisions |
| 2026-10-09 | A screen may only say what its evidence supports — "not yet" is evidence of nothing | **5** — **2026-10-21 a colour**: the reference-position drive coloured the phone's 155,7 km green by A3, which judges the GGA position sent, not the phone. Earlier, 4 — "fewest held: 9" from a partial first epoch and "no dual-frequency pair" at 25 s (both 2026-08-18, fixed with `SR_WARMUP_S` and a judgeability gate); **NOT COMPUTABLE at 32 s in the new Self-position window, and ring labels of 0.125 m on a plot with no points, both 2026-10-09** — the second pair happened in a window added to the same design document that records the first; **2026-10-12 a web page**: the location picker said "Copied to clipboard!" after a write the browser had refused, its success message printed after the call rather than from its result | `design/gui-design.md` §14.5, §16.5 and §17.4; `memory/MEMORY.md` active decisions |
| 2026-10-09 | A falsification that stays green accuses the test, not the code | **3** — synthetic ephemerides sharing one `af1` hid a deleted satellite clock-drift term (P4); a snapshot that blanked `selfpos_speed_mms` beside `has_vel = false` hid a deleted `has_vel` gate (P5); **and again in P6, hours after the promotion**, a snapshot left at its `NS_UNSET` initialisers hid a deleted status gate in the *snapshot* serialiser — the promotion did not stop the test being written politely; the falsification caught it; **2026-10-12, done systematically**: a mutation run of 13 broken copies of `sourcetable_map.c` found three cases passing for the wrong reason (a NUL check the decoder made redundant, a length limit whose test was malformed anyway, every test entry a fixed base) | `memory/MEMORY.md` active decisions and Recently Promoted |
| 2026-10-10 | A claim about what a frontend does is tested on that frontend before it is written | **4** — **2026-10-21 in a plan**: the GUI review plan had the network-RTK check reconnect "with the existing session calls", which had no such call. Earlier, 3 — *Read the wiring, missed the gate* 2026-09-13; then twice on 2026-10-10: the feature matrix said the daemon solved stations broadcasting their own orbits (it solved none), and `docs/cli.md` said the same of `-t --report` (it did not either) — both assuming a shared layer decoded ephemerides, which none does | `memory/MEMORY.md` active decisions and Recently Promoted |
| 2026-10-10 | A command handed to the author carries all its context in the command itself — shell and checkout | **5** — **2026-10-15 `&&` and `/tmp` handed to PowerShell 5.1**, which has neither. Earlier, 4 — a Bash heredoc handed to a PowerShell prompt (2026-10-09); a bare `git commit` run in the wrong checkout, which committed another session's staged work under this one's message (2026-10-10); **a bare `git commit` whose editor closed empty, reported as committed (2026-10-12)** — the message too is context, so commits now go over as `git -C <checkout> commit -F <file>`; **2026-10-13 a `git cherry-pick` handed over while changes were staged**, which git refuses — the order of the commands, and the state of the index, are context too: hand over the commit first, then what depends on it. Each time the context was written beside the command instead of in it | `memory/MEMORY.md` active decisions and Recently Promoted |
| 2026-10-14 | Driving a phone from a script: read the focus before every tap, Back and swipe | **3** — the blind tap script (Huawei 2026-10-13; Back and swipe on the S23 and an ignored `am start` after install on the Huawei, 2026-10-14), Git Bash rewriting `/sdcard` (2026-09-13, 2026-10-14), the lock screen four times in one afternoon (2026-10-14), and three more in the GUI review (2026-10-20, -21) | `docs/RUNBOOK.md`, Android: build, install, observe |
