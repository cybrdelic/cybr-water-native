# V11 redirect revision

[Watch the extended review](https://drive.google.com/file/d/1bOUq7p2UpH8TkAKaKyZ9nsbxHkHt3bnL/view)

[Open the raw extended render](https://drive.google.com/file/d/1v5Mj0EgXpsg7DfppsuFBVBm5yYM5C7_W/view)

The water now survives the whip-to-redirect transition. In a matched 3 cm run at 3.5 seconds, the previous controller has no primary liquid above 80 cm. The revision retains 15.851 litres above that height and reaches 1.372 m.

[Watch the revised motion](https://drive.google.com/file/d/1qoOCOMb2Q7Y56D3g9pfcG8FznRPMbJPu/view)

[View the raw native render](https://drive.google.com/file/d/13n7aLVhxW3VlQWCGpuL-9opvuefphrLx/view)

[View the matched comparison](https://drive.google.com/file/d/1v5Y27Z2iD2LZar20875mGC9Fs78Wnq51/view)

## What changed

The previous redirect acted inside a stationary annulus ending at radius 0.74 m. The outgoing whip streams reached beyond it and fell.

Two moving catch regions now meet the streams while they remain elevated. They gather briefly, turn around the basin, and overlap the following slice. A first trial held the liquid in compact blobs. The second trial adds tangential and vertical strain with radial confinement. This produces flatter, stretched sheets and ligaments during the turn.

The feedback remains bounded and enters the existing MAC solve before pressure projection. The recorded states come from the APIC solver. The V6 source, common solver, surface reconstruction and optical implementation retain their source bytes.

The render runner can now carry fragment history across a chronological selection of frames. The movie uses that option.

The longer trial exposed a second failure at the wave-to-vortex handoff. The unbraked wave carried 58% of the primary liquid beyond the controller's lateral bounds by 5.83 seconds. A smooth braking and return field now overlaps the vortex. At 5.67 seconds, the braked run has 77.506 L above 80 cm, compared with 17.356 L in the unbraked trial. At 6.83 seconds it retains 270.224 L above that height and reaches 1.888 m.

This later trial imports the revised 4.5-second state. Primary positions, velocities, IDs and marker volumes match the source state exactly. APIC gradients are reconstructed and the pressure warm start is reset at this boundary. The extended review is a sequence of native phase experiments. A single uninterrupted run from the stationary pool remains to be qualified.

## Matched result

Both controllers start from the same saved 1.5-second state, use six solver threads, a 3 cm pressure grid, 1 cm marker spacing, surface tension 0.072 N/m and the same seed. Import preserves positions, velocities, IDs and per-marker volumes. APIC gradients are reconstructed and the pressure warm start is reset for both runs.

| At 3.5 seconds | Previous controller | Revised controller |
|---|---:|---:|
| Primary liquid above 80 cm | 0 L | 15.851 L |
| Maximum primary height | 0.588 m | 1.372 m |
| Total primary volume | 439.624 L | 439.624 L |

The new regression checks that an outbound stream at 1 m lateral distance receives inward redirect feedback. Native checks, finish-checks, continuation-checks and choreography-checks pass. All 28 Python tests pass.

## Render details

The redirect movie contains 37 actual frames from simulation time 1.5 to 4.5 seconds. Native output is 640×360 with 24 Monte Carlo samples per pixel and a maximum depth of 32. Playback is 12 fps. The 960×540 comparison uses 64 samples per pixel and a maximum depth of 48.

Both comparison images share the same camera and studio illumination. Compared with the previous delivery, this review uses a closer camera and studio lighting. The review MP4 applies FFmpeg spatial filtering with nlmeans=s=2:p=7:r=15. The raw MP4 and original render receipts are included alongside it. The filter adds no temporal interpolation.

These remain review renders. Visible noise, rough breakup and limited sheet resolution remain. The input is a saved 1.5-second state. A fresh simulation from the stationary pool has yet to qualify the entire revised choreography.

The extended sequence uses 12 fps through 4.5 seconds and 6 fps afterward, with actual native frames at both cadences. It cuts to a wider camera at 6.167 seconds to keep the rising vortex in frame. The wide camera is [1.55,1.9,4.8], target [0,1.1,-.25], vertical FOV 36 degrees. Fragment history carries within each shot and restarts at the staged render boundaries.

## Source and reproduction

The controller, regression and render-runner changes are published in commit 8477b2c660747b7753f6cd017b844438fbcb5004.

[Review the source changes](https://github.com/cybrdelic/cybr-water-native/commit/8477b2c660747b7753f6cd017b844438fbcb5004)

The wave braking revision is commit 12607cf8fa29ebb75e6dc3b6bf20b2e5a53b69c8. All 28 Python tests and the native checks pass on this revision.

[Review the wave braking change](https://github.com/cybrdelic/cybr-water-native/commit/12607cf8fa29ebb75e6dc3b6bf20b2e5a53b69c8)

Build the native tools from this revision.

    make -C choreography-v7/engine -j2 build/gallery_sim build/gallery_export build/surface build/trace_native

Use the previously delivered states/after/cache/frame_000036 quartet as the shared input. Run each controller in its own checkout and a fresh output directory.

    OMP_WAIT_POLICY=PASSIVE choreography-v7/engine/build/gallery_sim \
      --scene waterbending_martial_v11 --dx .03 --duration 10 --fps 24 --threads 6 \
      --out runs/redirect/cache --apic --particles-per-axis 3 --reseed-interval 0 \
      --whitewater-rate 24000 --max-whitewater 180000 --surface-tension .072 \
      --auto-domain --domain-padding .48 --domain-lower -1.98,-.12,-1.50 \
      --domain-upper 1.98,2.82,1.50 \
      --import-frame states/after/cache/frame_000036.bin --stop-after-frame 108

The matched previous controller is commit fe76a809b14c37e61449f7e640e5632935898893. Source identities, binary identities, state hashes, numerical ledgers and exact native rendering commands are in the accompanying evidence. The camera is [1.48,1.53,3.8], target [0,.83,-.13], vertical FOV 28 degrees, with studio illumination.

For the later phase, run the wave braking revision with the same physical options, import the frame_000108 quartet, use domain lower [-3.42,-.12,-2.94] and upper [3.42,2.82,2.22], omit --stop-after-frame, and set --save-every 4. The duration remains 10 seconds. The evidence includes the exact input hash and the complete ledgers for each staged run.

Run audit_handoff.py with --repo, --take, --input, --end and --out to validate the saved experiment. Its optional component-shape diagnostic uses SciPy. It reports RMS marker spans and occupied-cell connectivity. Mesh thickness requires a separate geometric measurement.

## Extended outcome

The solver completed the staged experiment through simulation time 10 s. The extended movie contains 69 unique native rendered frames spanning 1.5–9.833 s. The source frame clock determines playback timing. The raw and spatially filtered MP4s both fully decode, and their presentation timestamps match the recorded simulation samples.

Every saved state in the two audited windows retains 439,624 primary IDs and the same per-marker volumes. Total primary volume is 439.624 L and the native volume ledger error is zero. The source states, audit results, render receipts and video provenance are included.

The final timing remains unresolved. By 8.5 s, no primary water remains above 80 cm. The slam therefore lands most liquid before the controller releases fully at 8.9 s. The tail shows residual guidance-free motion, with a weak final splash. This sequence remains visually unaccepted.

The retained optical receipts report up to 5 medium underflows and 597 unclosed-medium escapes in an individual frame. The maximum recorded dense-surface volume error is 0.1965%. These results leave optical closure, local sheet thickness and sample convergence unresolved.
