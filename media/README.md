# Media provenance

All media here comes from the saved native studies, not image synthesis. GIFs are downsampled exports of the corresponding movies and do not interpolate new simulation states.

- `baseline-4spp.mp4` and `baseline-4spp-contact.png`: original baseline source-and-render bundle, 320×180, 240 frames at 24 fps, 10 seconds. The delivery records 4 spp/8 bounces and dx=0.14 m.
- `baseline-32spp.mp4` and `baseline-32spp-contact.png`: separate prior baseline export labeled 32 spp, 320×180, 240 frames at 24 fps, 10 seconds. This is not the latest transport experiment. A sample count cannot be independently recovered from the encoded video. `baseline-32spp-still.png` is extracted at 3 seconds; `baseline-32spp.gif` keeps the full duration at 12 fps.
- `transport-diagnostic.mp4`: delivered 960×540 MPEG-4 particle-state visualization, 240 frames at 24 fps, 10 seconds, dx=0.10 m. It is explicitly not a surface/beauty render. `transport-midpoint.png` is extracted at 5 seconds. `transport-diagnostic.gif` is reduced to 640 pixels wide and 12 fps, preserving the full duration.
- `reconstruction-2fps.mp4`: earlier mesher-profile checkpoint, 384×216, 20 frames at 2 fps over ten seconds, 4 spp/5 bounces.
- `reconstruction-detail.png`: separately rendered 640×360, 64-spp/12-bounce diagnostic from the earlier checkpoint. Not a hero replacement for that low-rate movie.

All five MP4s passed strict full decoding during packaging. This establishes media readability and frame counts, not numerical or physical validity. Native source was not executed during packaging.

## Latest retained-trick addition

- `retained-trick-10s.mp4`: original retained-trick H.264 movie, 960×540, 240 frames at 24 fps, ten seconds. Deterministic direct-light/Whitted preview, four fixed subpixel AA samples and 12-bounce cap; not converged full-GI path tracing. SHA-256: aec7ceb35023d91fd40ade9cd1daef51eec34566ec23c5fb8d2cd8904198e8d0.
- `retained-trick.gif`: full-duration 480-pixel-wide, 12-fps derivative of that movie. No interpolated frames.
- `retained-trick-sequence.jpg`, `retained-trick-splashdown.jpg`, `retained-trick-splash-issues.jpg`, `retained-trick-splash-9_75s.png`: original delivered sequence and splash-review stills, inspected for correspondence and visible defects. The latter audit supplies no replacement render or source fix.

The original movie and stills are unmodified. The GIF is a display derivative. Earlier failed transport media remains intact and is deliberately labeled as an older particle-state diagnostic.
