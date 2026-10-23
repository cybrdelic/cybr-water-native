# Current native water work

The accepted V6 result is preserved in [native-v6](native-v6/README.md). The new opt-in choreography is in [choreography-v7](choreography-v7/README.md). Historical retained-trick and open-splash files are not overwritten.

This branch first installs a compact hash-checked migration against the repository's existing V4 source. The native-source workflow materializes readable V6 and V7 source trees, compiles both, runs their regression suites, and commits the readable sources back to this branch. On a bootstrap checkout before that job completes, run `python3 scripts/materialize_native.py`.

The new controller implements separated lifts, a figure-eight braid, staggered whip sweeps, an orbital catch, a shield hold and a spiral return. It drives the actual APIC/MAC state; it is not animated render geometry. The V6 renderer and reconstruction are retained.

Local regression results are included separately from CI results. A numerical test or guide-curve test is not proof that the full fluid choreography looks correct. Completed native review media and exact simulation/export checks will be recorded separately; an incomplete or previous movie must not be advertised as the new take.

Large simulation caches and movies are excluded from Git; reproduction scripts and source identity manifests are included. No license change is made by this publication.
