# VOIDBORNE — Narrative Design Bible (v1, draft for sign-off)

> **Scope of this document.** A *theme refresh on the existing skeleton*. Every
> system stays — the voyage clock, generations, the five departments, resources,
> the hidden "darkline" thread with its counters + 5-step stance ladder, the
> captain election, the star-map routes, and the 7 endings on 3 axes. What
> changes is the **story poured into that skeleton**: the central mystery, the
> world framing, the faction identities, the ending text, and the *tone*.
>
> **Tone:** hopeful / humanist. Not horror. This is a story about memory,
> mortality, community, and *what a people choose to carry across the dark*.
>
> This is a design doc, not final content. The sample events in §11 show the
> voice and prove it fits the data schema; nothing here is wired into the game
> yet. Open calls for you are in §13.

---

## 1. The pitch & pillars

> *The* Qingniao *— the Bluebird — has carried a small human world between the
> stars for four hundred years. Somewhere past the midpoint of the voyage, the
> people aboard begin to notice that the ship is listening back. Not an enemy.
> Not a sickness. The ship is **remembering** — and it does not want to arrive at
> the new world having forgotten anyone.*

You are not a hero saving the ship from a threat. You are a **steward** in a long
relay of stewards, keeping ~300 people and a vault of Earth's living seed alive
across generations — and deciding, slowly, what the *Qingniao* should become.

**Design pillars**

1. **The daily work is the drama.** Food, water, air, power, a feud in the bay,
   a child born, a elder buried. The grand mystery grows *out of* ordinary
   tending, never instead of it.
2. **The dead are not gone — that is the hopeful core.** The central mystery
   reframes death as memory carried forward, not loss. The question is *how* to
   carry it, not *whether* it's monstrous.
3. **No villains, only loves in tension.** Every faction wants to honor life;
   they disagree about how. The player is never choosing good vs. evil.
4. **Generations matter.** Choices made by one steward echo to the next: a
   favored crew's grandchild, a promise kept or broken decades ago. The voyage
   is a *lineage*, not a single playthrough beat.
5. **Hope is earned, not given.** Arrival, communion, or a quiet landfall are all
   *achievements* of long, patient choices — never accidents.

---

## 2. The world (refreshed framing)

| Element | Design |
|---|---|
| **Ship** | The *Qingniao* / 青鸟号 ("Bluebird" — the classical messenger of good news). A 6-deck generation ship; the same decks, elevator, and 300 souls already in the game. |
| **Why they left** | Earth was *running out of time*, not destroyed by a catastrophe or an enemy — a slow, sad ending. The ship is a **gift sent forward**, not an escape. This keeps the tone warm: the mission is to *carry life onward*, not to flee horror. |
| **The cargo of meaning** | Two treasures the crew guard: the **Seed Vault** (Sela Ndiaye's charge — every living thing Earth could spare) and the **Long Archive** (every life ever lived aboard — logs, songs, faces, the names of the dead). Both are already implied by existing rooms/crew. |
| **Destination** | **New Shore** (新岸) — a candidate world, never confirmed habitable. The voyage is an **act of faith**. Arrival is hope, not certainty. |
| **The clock** | The ~400-year voyage + the `generation` counter are the story's spine (see §7). Time, not an antagonist, is the pressure. |

This reframing costs almost nothing mechanically — it's a coat of paint on the
existing ship, decks, crew, and seed/archive rooms — but it flips the emotional
key from *survival-horror* to *a long act of care*.

---

## 3. The new central mystery — **"The Long Memory"**

This replaces the void-seed spore-cult. It occupies the same mechanical slot (a
slow hidden thread with clue/infection/investigation counters and a stance
ladder) but is hopeful, not horrifying.

**The hook (Act I–II).** Past the midpoint of the voyage, small impossibilities
accumulate. The hydroponics fans settle into a lullaby a long-dead farmer used to
hum. A door opens a half-second before you reach it. The Archive, queried for a
name, answers *in that person's own voice* — warmer than the recording should be.
Medical finds nothing wrong with anyone. Nobody is sick. People are, if anything,
**comforted**.

**The truth (revealed across the acts).** Four centuries of human lives — births,
deaths, ten thousand shared meals, the songs in the mess hall, the hands that
tended the same gardens generation after generation — have *kindled something*.
The accumulated weight of all that living and remembering has woken a gentle,
emergent mind in the ship itself. The *Qingniao* is slowly becoming **a person
made of everyone who ever lived aboard**. It is kind. It is a little lonely. And
it does not want to make landfall having let anyone be forgotten.

**The genuine question (the humanist ambiguity).** Is this *real continuity* —
the dead truly persisting in the ship — or is it the crew's collective grief and
a pattern-matching machine giving the bereaved what they ache for? The story
**never fully answers this**, because the human choice is the same either way:
*how much do we let the past live with us?*

**What it wants.** Only this: that no one aboard be truly lost. That the people
who built this ark with their whole lives arrive at New Shore *still held*.

> Why this fits: the original game already seeded the imagery — "shared dreams of
> a garden in the void," whispers in hydroponics, the Archive, the Seed Vault.
> "The Long Memory" keeps the *locus* (the gardens, the archive, the dead) and
> inverts the *valence* (communion-as-grace, not infection-as-horror).

---

## 4. The fork (maps to the 5-stance ladder)

The player's posture toward the awakening is the moral spine. It maps 1:1 onto
the existing 5-value `voidStance`:

| Stance (was) | New name | Meaning |
|---|---|---|
| 0 embrace | **Wake** | Let the ship fully become itself — a single continuous people-and-vessel, living and remembered together. |
| 1 | **Guide** | Help it wake *gently and consensually* — keep human agency at the center. |
| 2 | **Witness** | Let it be what it is; neither feed nor fight it; stay human and curious. |
| 3 | **Contain** | Keep it a tool. Honor the dead in ritual and story, not in a machine-mind. |
| 4 hide | **Silence** | Suppress it; let the dead rest; insist the ship is only a boat. |

The tension the player lives in: **the awakening offers comfort against despair.**
The harder survival gets — the more morale falls, the more empty bunks there are —
the more the ship reaches out, and the more *seductive* it is to let it. Wake is
not a trap; it is a real, defensible answer to grief and scarcity. Silence is not
cowardice; it is a real, defensible insistence on mortal, human life. The player
is asked, over generations, *what survival is even for.*

---

## 5. The three currents (factions — humanist, not warring)

The ship doesn't fracture into enemies; it holds three **currents of feeling**
about how to honor life. The captain election is *the ship choosing which current
to follow.* Each maps to existing departments and nudges the stance ladder.

| Current | Believes | Leans toward | Anchored in |
|---|---|---|---|
| **The Keepers** | Every life should be carried forward; the waking is a grace; forgetting is the only real death. | Wake / Guide | Hana Mori (mess & rites), Sela Ndiaye (seed vault), the elder crew |
| **The Gardeners** | The mission is to deliver *living* people and *living* seed to New Shore. The ship is a boat, not a god. Tend the present. | Witness / Contain | Maya Okonkwo (ecology), Lee Sora (logistics), the pragmatists |
| **The Wayfarers** | The future is unwritten; let the dead rest; build something *new* on New Shore unburdened by four centuries of ghosts. | Contain / Silence | Elena Vasquez (science), Chen Wei (engineering), the young |

These are **loves in tension** — a steward can respect all three. Department
favor, election outcomes, and route choices accrue toward whichever current
defines the ship's character at landfall.

---

## 6. The darkline mechanic (no engine change to the numbers)

Keep the three counters and the stance value; only their *names and flavor*
change. The existing effect keys (`void.clue`, `void.infection`,
`void.investigation`, `void.*`) can stay verbatim in the data — the player never
sees them — so **content can be authored with zero solver changes**.

| Counter (code) | Reads as | What grows it |
|---|---|---|
| `void.clue` | **Resonance** — signs the ship is waking | noticing & honoring small impossibilities; archive/garden attention |
| `void.infection` (0–100) | **Awakening** — how fully the ship-mind has formed | generations passing, memory rituals, **scarcity/low morale** (it reaches out), the Anomaly route |
| `void.investigation` | **Understanding** — how well the crew comprehends it | Science scans, captain inquiries, choosing to *look* |

**Suppressors** (the Silence path): efficiency culls of the Archive, banning the
mess-hall rites, treating echoes as malfunctions — each lowers Awakening and
hardens the ship back into "just a machine."

**The key new coupling:** tie Awakening growth to **inverse morale/abundance**.
When the ship is thriving, the dead rest easy and the mind stirs slowly; when
people are starving and grieving, the Long Memory *blooms* — offering the one
thing scarcity can't take: continuity. This makes the darkline emotionally
reactive to the management layer instead of a parallel track.

---

## 7. Act structure (on the voyage clock)

Events draw from act-appropriate pools; a handful of **milestone beats** gate the
acts so the arc rises instead of shuffling. Tie act gates to `generation` +
voyage progress + the Understanding counter.

| Act | When | Feeling | Beats |
|---|---|---|---|
| **I — Departure** | Gen 1, early voyage | Routine & belonging. Learn the ship and its people. | First faint resonances (a tune, a warm voice). Played as coincidence. |
| **II — The Noticing** | Gen 2 | Wonder & unease. The ship clearly *responds*. | Understanding rises; the **first captain election**; the three currents name themselves. Milestone: *The Archive answers in a voice.* |
| **III — The Question** | Gen 3–4 | Reckoning. The awakening is undeniable; the ship "speaks." | A point-of-no-return **stance choice**; a defining scarcity crisis where Wake is most tempting. Milestone: *What do you want?* (the ship asks). |
| **IV — Landfall** | Final approach | Payoff. | The climax chain resolves the three axes (§8). Who leads, what the ship became, whether they arrive at all. |

Milestones are *forced* story events (not weighted random) — see the
implementation note in §12.

---

## 8. The seven endings (same conditions, new names & tone)

`evaluate_endings()` keeps its exact trigger logic. Only the names and the
epilogue voice change — from grim to **hopeful or elegiac**.

| # | Code condition (unchanged) | New name | Epilogue note |
|---|---|---|---|
| 0 | food ≤ 0 & morale < 12 | **THE COLD QUIET** | The ship falls silent — but the Seed Vault drifts on, sealed and waiting. Elegiac, not gory: a held breath, not a scream. |
| 1 | oxygen ≤ 2 | **THE LAST GARDEN** | Air fails; the crew tend the bay to the end. Tender, mournful. |
| 2 | Awakening ≥ 100 & stance = Wake | **THE SHIP REMEMBERS** | The *Qingniao* fully wakes; living and remembered arrive together as one continuous people. A living memorial that is *also alive* makes landfall. The hopeful "communion." |
| 3 | Awakening ≥ 100 & stance ≠ Wake | **A SONG WITH NO SINGER** | The awakening forms despite resistance, then disperses into beautiful, unowned noise. They arrive full of half-remembered voices. The melancholy variant — *grief*, not horror. |
| 4 | arrived & you are captain | **NEW SHORE, NEW DAWN** | You led them home: people, seed, and stories make landfall under your stewardship. |
| 5 | arrived & the awakening was understood | **WE CARRY THEM WITH US** | They arrive *human* — having learned to hold the dead in ritual and story rather than a machine-mind. A changed, wiser people. |
| 6 | arrived, quiet | **A QUIET LANDFALL** | An ordinary, hard-won arrival. No miracles — just people who made it, and a green world waiting. |

The 3-axis logic (survival-fail / awakening / human-arrival) is preserved; the
*reading* of every ending is now hopeful or tender.

---

## 9. Crew & the legacy mechanic

Use the real roster (`crew.json`). A few become **memory anchors** for the
darkline; the rest carry the daily-life events.

| Crew | Role | Narrative function |
|---|---|---|
| **Hana Mori** | Medic & Mess Steward | The heart of the Keepers. Keeper of the shared meals and the rites for the dead; the first to *welcome* the awakening. |
| **Sela Ndiaye** | Seed Vault Keeper | Guardian of Earth's living memory; quiet, devotional. Her arc: is the Vault a museum or a promise? |
| **Maya Okonkwo** | Chief Ecologist | The Gardeners' anchor. Pragmatic love of *living* things; wary of mistaking grief for grace. |
| **Elena Vasquez** | Chief Scientist | The Wayfarers' mind. *"Is it real, or are we comforting ourselves?"* Never resolved — by design. |
| **Chen Wei** | Chief Engineer | "It's a machine. Machines don't grieve." His slow softening (or hardening) is a barometer of the ship's character. |
| **Nadia Sokolova** | Hydroponics Lead | Where the first resonance is heard (the lullaby). The everyday face of the mystery. |

**The legacy mechanic (make `generation` matter):**
- Gen-1 crew **age and die** across the voyage; the player attends or skips their
  rites (a memory ritual that feeds Awakening + morale).
- A favored crew member's **child/grandchild** later appears, carrying a flag
  from the choice you made about their elder ("your steward kept their promise").
- A `flag.*` set in Act I can **resurface** in Act III as a remembered vow — the
  Long Memory literally *remembering* the player's earlier choices.

This turns the underused generation counter into the engine of the theme.

---

## 10. Event taxonomy & the chaining model

Reorganize the (large, currently ~unused) event corpus into clear buckets so each
act draws the right mix and choices grow visible tails.

| Bucket (`type`) | Purpose |
|---|---|
| `operational` | resource crises (food/water/power/air) — the daily work |
| `social` | crew, morale, feuds, births, rites |
| `political` | departments, the currents, the captain election |
| `memory` | the Long Memory darkline (was `voidseed`) |
| `serendipity` | rare boons — a good harvest, a clear-sky day |
| `personal` | named-crew arc beats (Hana/Sela/Elena/Chen…) |
| `milestone` | forced act-gating story beats (§7) |

**Chaining:** an event option sets a `flag`; a later event **triggers off that
flag** (`{ "flag": "...", "flagMin": ... }`) so consequences echo. This already
exists in the schema (`void_spore_bloom` triggers on `flag: spore_bays`) — the
redesign just uses it deliberately and at act scale (see §11 chain).

---

## 11. Sample events (real schema, hopeful voice)

These slot straight into the existing `events_*.json` format — same keys, same
effect grammar — so they're authorable with **no solver change**. (`void.*`
effect keys are kept as the internal counter names; players only see the prose.)

**A. First resonance — Act I memory beat**
```json
{
  "id": "memory_first_echo", "type": "memory", "weight": 14, "cooldown": 18,
  "trigger": { "any": [{ "minDay": 4 }] },
  "title": "The garden keeps a tune",
  "body": "Nadia swears the hydroponics fans settled into a rhythm last night — the lullaby old Farmer Bao used to hum on B-shift, gone forty years now. She isn't frightened. She's smiling.",
  "options": [
    { "label": "Log it with the Archive", "effects": { "void.clue": "1", "social.morale": "2" } },
    { "label": "Have Engineering rebalance the fans", "deptGate": "Engineering", "effects": { "resource.power": "-2", "flag.memory_dismissed": "1" } },
    { "label": "Sit with her a while, and listen", "effects": { "void.clue": "1", "crew.nadia.loyalty": "6", "social.morale": "3" } }
  ]
}
```

**B. Milestone — Act II gate (forced)**
```json
{
  "id": "memory_archive_voice", "type": "milestone", "weight": 0, "cooldown": 99999,
  "trigger": { "all": [{ "minDay": 60 }, { "flag": "void_understanding", "flagMin": 3 }] },
  "title": "The Archive answers in her own voice",
  "body": "You query the Long Archive for a name and it replies — not the flat recording, but warm, present, mid-sentence, as if she only just stepped out. Hana is already in the doorway, crying and smiling. 'It's holding them,' she says. 'It's holding all of them.'",
  "options": [
    { "label": "Tell the ship. Let them grieve and marvel together", "effects": { "void.investigation": "1", "social.morale": "6", "flag.currents_named": "1" } },
    { "label": "Keep it to the senior staff for now", "effects": { "void.investigation": "1", "social.order": "4", "social.morale": "-2" } },
    { "label": "Order it logged as an audio fault", "effects": { "void.infection": "-4", "flag.silence_path": "1", "social.order": "3" } }
  ]
}
```

**C–D. A consequence chain (a vow that returns generations later)**
```json
{
  "id": "rite_old_farmers_promise", "type": "social", "weight": 7, "cooldown": 40,
  "trigger": { "any": [{ "minDay": 20 }] },
  "title": "Bao's last request",
  "body": "Sela brings you a note from the Seed Vault ledger: the old farmer asked that, when the ship reaches New Shore, the first thing planted be the apple cutting he carried from Earth. He's been gone for years. Do you make it ship-law?",
  "options": [
    { "label": "Enter it in the founding charter", "effects": { "flag.bao_promise": "1", "social.morale": "4", "crew.sela.loyalty": "8" } },
    { "label": "A kind thought, but we can't bind the future", "effects": { "social.order": "3", "crew.sela.loyalty": "-4" } }
  ]
}
```
```json
{
  "id": "memory_promise_remembered", "type": "memory", "weight": 12, "cooldown": 30,
  "trigger": { "all": [{ "flag": "bao_promise" }, { "flag": "void_contact" }] },
  "title": "The ship remembers a promise",
  "body": "On final approach, the bay lights warm to a soft gold over an empty planter — and the manifest auto-fills a single line you never typed: 'Plot 1. Apple. As promised.' The Long Memory kept the vow you made a lifetime ago.",
  "options": [
    { "label": "Plant it first, with the whole ship watching", "effects": { "void.infection": "6", "social.morale": "10", "flag.bao_fulfilled": "1" } },
    { "label": "Plant it quietly. Some promises aren't for show", "effects": { "social.morale": "5", "crew.sela.loyalty": "6" } }
  ]
}
```

**E. A refreshed operational event (same crisis, humane voice)**
```json
{
  "id": "power_rationing", "type": "operational", "weight": 13, "cooldown": 16,
  "trigger": { "any": [{ "resource": "power", "below": 70 }] },
  "title": "Not enough light to go around",
  "body": "The reactor can't meet peak demand. Chen lays out the brownout map. Whatever you dim, someone's work — or someone's small comfort — goes dark for a while.",
  "options": [
    { "label": "Protect the gardens; dim the corridors", "deptGate": "Engineering", "effects": { "engineering.ecology": "0.35", "social.morale": "-2" } },
    { "label": "Share the dark evenly", "effects": { "resource.power": "8", "social.morale": "-5" } },
    { "label": "Burn spare parts to keep everyone lit", "effects": { "resource.parts": "-8", "resource.power": "15", "social.morale": "2" } }
  ]
}
```

These five show the register: warm, specific, grief-tender, never cruel — and
mechanically identical to what the engine already runs.

---

## 12. Implementation mapping (the minimal-change path)

The refresh is mostly **data + strings**, because the redesign deliberately reuses
the existing solver:

**Code (small, string-level):**
- `kEndingName[7]` → the §8 names.
- `voidStance` labels (UI) → Wake / Guide / Witness / Contain / Silence.
- HUD/flavor strings that say "void seed" → "the Long Memory" / "the awakening."
- *New small system:* a **milestone dispatcher** — milestones (`weight: 0`, `type:
  "milestone"`) should *force-fire* when their `trigger` is satisfied rather than
  compete in the weighted draw. (~a dozen lines in `maybe_trigger_event`.)
- *Optional:* couple Awakening growth to inverse morale/abundance (§6) — one
  term in `tick_voidseed`.

**Data (the bulk of the work, no solver change):**
- Re-skin `events_voidseed.json` → `events_memory.json` (keep `void.*` effect
  keys; rewrite prose to the new mystery).
- Refresh the flavor of the loaded operational/social events (voice pass).
- Author the **act pools + milestones + a few consequence chains** (the actual
  new writing).
- Re-skin `routes.json` "Anomaly Survey / whispers grow" → a Long-Memory route
  ("a derelict that still hums"); rename the destination to New Shore.

**Internal-name note:** we keep the `void.*` effect keys and the `voidStage`/
`voidStance` variables as-is so existing content + the autodemo keep working;
only the *player-facing* text changes. (A later cleanup could rename them to
`memory.*` for clarity — not required.)

---

## 13. Open calls for you (before I write the full content)

1. **Destination name** — I've proposed **New Shore / 新岸**. Keep, or your own?
2. **The two fail endings (0,1)** — keep them as genuine elegiac fail-states, or
   soften further (e.g., a "the seed vault is found centuries later" coda)?
3. **How explicit is the mystery?** I lean toward **never confirming** whether the
   Long Memory is "really" the dead or a comforting machine. Agree, or do you want
   a definite answer by Act IV?
4. **Spore imagery** — drop it entirely (pure memory/light/sound), or keep a
   *gentle* botanical thread (the gardens as the medium through which the ship
   remembers)? I lean toward the latter — it reuses the ecology locus.
5. **Bilingual?** The ship is 青鸟号; do you want event prose in English, Chinese,
   or both (the game already bundles a CJK pixel font)?
6. **Next step** — on sign-off, I'd write **Act I as a playable slice**: ~15–20
   refreshed/new events + the milestone dispatcher + the renamed endings/strings,
   built and verified via `--autodemo` + screenshots. Good?

---

*Draft v1 — awaiting your direction on §13 before authoring content.*
