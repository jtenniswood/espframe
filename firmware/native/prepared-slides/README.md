# Prepared Immich slides

This optional service prepares one JPEG for a single photo or a portrait pair. It uses the Immich preview API and applies EXIF orientation, the frame's Fit or Fill geometry, and the mounting rotation. It returns an 800 × 1280 baseline JPEG in Guition panel order, with 4:2:0 chroma sampling. [Immich preview endpoint](https://github.com/immich-app/immich/blob/main/server/src/controllers/asset-media.controller.ts) · [Sharp JPEG options](https://sharp.pixelplumbing.com/api-output/#jpeg)

The native firmware uses direct Immich previews by default. To opt in, open **Photo preparation service** in the browser installer or in the frame’s web settings and enter the service URL and its separate access token. The frame connects to Immich for photo selection and uses the service for prepared images. It continues to draw captions and the clock locally. During optional portrait pairing, photos with unknown orientation use direct previews so the frame can inspect their dimensions. Clear the service URL in the frame’s web settings to remove its token and return to direct previews. A repeat USB setup with the same Immich connection keeps the saved service if both service fields are left blank.

The frame falls back to direct previews if a prepared image fails. An HTTPS preparation service becomes available after the frame synchronizes its clock. Connection, authentication and service failures defer further service requests for 30 seconds. Set a new token when changing the service URL. The frame’s settings response and backup omit the token.

## Run on a test host

Set `IMMICH_URL` to the base URL of one Immich instance and `IMMICH_API_KEY` to a key with `asset.view` permission. Set `SLIDE_SERVICE_TOKEN` to a separate random token of at least 32 characters. Anyone with that token can request slides for the assets the Immich key can view. Bind the service to a private interface and use HTTPS before sending credentials across a network.

From this directory, with Node.js 24 or later and those environment variables set:

```sh
npm ci
npm run typecheck
npm test
npm start
```

The local process listens on `127.0.0.1:8080` unless `HOST` or `PORT` is set. To run the container with the same environment variables in a local env file:

```sh
docker build -t espframe-prepared-slides .
docker run --rm --env-file /path/to/service.env -p 127.0.0.1:8080:8080 espframe-prepared-slides
```

## Request a slide

Send `GET /v1/slides/<asset-uuid>` with the `Authorization: Bearer <service-token>` header. Add `companion=<asset-uuid>` for a portrait pair or `rotation=180` for an upside-down mount. `fit` and `0` are the defaults. `mode=fill` crops landscape photos and portrait pairs and retains the full image of a single portrait. `mode=fill-frame` also crops single portraits to fill the screen. A pair uses the primary photo on the left, the companion on the right and an eight-pixel gap.

The frame uses direct Immich previews for full-frame crops when an older
preparation service rejects `mode=fill-frame`.

Add `orientation=portrait` or `orientation=landscape` to filter the primary photo by its dimensions after EXIF correction. A mismatch returns HTTP 409, and the frame moves on to the next candidate. Square photos count as landscape. Frames with a photo orientation filter use direct previews when an older service rejects the option.

The in-memory cache holds up to 128 slides and 24 MiB. It serves a hit for up to one minute, then fetches previews again and reuses the finished JPEG if their content hashes match. `X-Espframe-Cache` distinguishes `miss`, `hit`, and `revalidated` responses. The cache starts empty after a restart. The endpoint returns an error when a preview is missing, unsupported, or too large, or when both preparation slots are busy.

Recent firmware sends `POST` with the same path and layout options to warm the next slide when its photo IDs are already known. The service replies with an empty `202` for queued or active preparation, `204` when the JPEG is ready, or `429` when the hint queue is full. Up to two hints can wait behind one speculative job, leaving capacity for a foreground request. The frame requests `GET` with `wait=0` to take a ready JPEG or start preparing it in the background. A `202` or `429` lets the frame start direct playback immediately. The original blocking `GET` remains available, including when a frame connects to an older service.

## Verify the V1 hardware decoder

Request five test slides with `rotation=0` and save them as `slide-0.jpg` through `slide-4.jpg` in the ignored `firmware/native/photos` directory. Use the [V1 fixture benchmark](../README.md#measure-swipe-rendering-over-the-local-network). The frame applies its own mounting rotation for that trial. On `/api/trial/prepare`, `hardware_jpeg` reports successful hardware calls and `rejected_preview` reports JPEGs that fell back to the built-in patterns. Verify all 48 scripted gestures settle and that no embedded JPEG is rejected.

## Compare direct and prepared downloads on V1

The replay server generates a fixed sequence of portrait pairs from synthetic JPEG, progressive JPEG, and WebP previews. It serves the Immich search and preview responses used by the direct player, and the prepared-slide endpoint uses those same previews. It requires no real Immich server or photo credentials.

1. On the test host, use `REPLAY_HOST=<private-IPv4-address> npm run replay` and wait for `Replay ready`. The port defaults to 9116. Restart the process before each run so search starts at the same position. Start with an empty service cache. Set `REPLAY_WARM_ROTATION=0` or `REPLAY_WARM_ROTATION=180` to prewarm the pairs for that mounting rotation.
2. Build the [V1 rapid swipe trial](../README.md#measure-swipe-rendering-over-the-local-network) in two separate build directories. For both builds, set `-D SWIPE_TRIAL_REPLAY_ORIGIN=http://<private-IPv4-address>:9116`. Set `-D SWIPE_TRIAL_REPLAY_PREPARED=OFF` for direct previews and `-D SWIPE_TRIAL_REPLAY_PREPARED=ON` for prepared slides. These options are only valid for the timed rapid app. It uses the saved Wi‑Fi and mount settings and sets the synthetic photos and Fit layout in memory.
3. Run the same rollback readiness check before each upload. Save `/api/trial/benchmark` and `/api/trial/prepare` before the two-minute rollback. Compare bytes, cold returns, refill times and drag gaps after all 48 gestures. The prepared build reports `prepared_transfer` and `prepared_decode`. Direct `transfer` activity in that build indicates a fallback to ordinary previews. The server prints cache hits and misses for each prepared request. Set `REPLAY_PREPARED_UNAVAILABLE=1` without prewarming to exercise the fallback while Immich preview responses stay available.

To test browsing previously seen slides during an outage, start a fresh server with `REPLAY_OFFLINE_HISTORY=1` and build the prepared replay app with `-D SWIPE_TRIAL_REPLAY_HISTORY=ON`. Set `REPLAY_WARM_ROTATION` to match the frame if you want to isolate the history cache from cold server preparation. The server returns HTTP 503 on both photo endpoints after serving ten distinct prepared slides to the frame. This replay holds autoplay after the first photo. The timed app should reach slide ten and rewind to slide one, with 18 settled gestures and `settled_slide` equal to `1`. `prepared_cache_decode` reports local JPEG decodes when an older slide leaves the ready-to-draw window. Its byte count measures JPEG input from the frame’s memory.

To measure direct Immich with smaller previews, use the same direct V1 trial image and restart the replay server with `REPLAY_PREVIEW_SIZE=720`. The synthetic 960 × 1440 portraits become 720 × 1080 while retaining the same JPEG, progressive JPEG and WebP mix. Also set `REPLAY_BASELINE_JPEG_QUALITY=80` to test baseline JPEG at Immich’s default quality, using the current encoder’s chroma sampling rule. The replay retains the same originals, order, pairing and mounting orientation. Both options apply to synthetic previews. Immich’s actual preview settings apply across the instance. [Immich image settings](https://docs.immich.app/administration/system-settings/#image-settings-thumbnails-and-previews) · [Immich encoder](https://raw.githubusercontent.com/immich-app/immich/main/server/src/repositories/media.repository.ts)

### Measure direct preview lookahead

Build the direct replay twice from the same revision, using the same replay origin and separate build directories. Set `-D SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD=ON` for the control and set it to `OFF` for lookahead. For each run, restart the synthetic server with the same settings, then follow the [V1 rollback readiness and app-only trial instructions](../README.md#verify-the-rollback-and-gestures).

From the repository root, capture each run as soon as the trial boots. The frame freezes timing counters after the final swipe.

```sh
python3 firmware/native/tools/compare_preview_replays.py capture \
  --frame "http://$FRAME_IP" --replay "http://$REPLAY_IP:9116" control.json
# Repeat after the lookahead trial boots.
python3 firmware/native/tools/compare_preview_replays.py capture \
  --frame "http://$FRAME_IP" --replay "http://$REPLAY_IP:9116" lookahead.json
python3 firmware/native/tools/compare_preview_replays.py compare control.json lookahead.json
```

The comparison checks that both runs completed all 48 swipes with matching replay inputs, cache slots, and timing thresholds. It reports photos ready at the start of a swipe, cold returns, slow first motions, drag gaps and refill times. The lookahead columns also show how many staged previews the frame used and how long it waited for them.

### Compare firmware when Immich previews stall

Start the same replay server before each run with `REPLAY_PREVIEW_STALL_MS=2500`.
It sends half of every eighth preview, pauses for 2.5 seconds, then sends
the rest. Search responses and the other previews arrive normally. This
reproduces a slow response after headers and data have already arrived.
Leave the preparation service off and use the same lookahead and companion
prefetch options in both direct trial images. Capture the two versions as
above, then compare them with:

```sh
python3 firmware/native/tools/compare_preview_replays.py compare --slow-previews control.json candidate.json
```

The comparison checks the pause duration and reports how many paused
downloads started, completed or were interrupted by capture time, alongside
swipe wait times and cache hits. The server starts with fresh counters when
restarted. Leave `REPLAY_PREVIEW_STALL_MS` unset for ordinary replay runs.

Add `-D SWIPE_TRIAL_REPLAY_PAIR_FILL=ON` to both direct trial builds to
measure **Fill each half**. The frame samples each new pair's outer edges,
pane boundaries and centre divider before adding captions or tone. The
comparison requires each new synthetic slide to render as a pair and rejects
a failed edge check or a mismatch between Fit and Fill. Use the physical panel
to check the visible crop.

To measure a **prewarmed preparation service** under the same conditions,
use `SWIPE_TRIAL_REPLAY_PAIR_FILL=ON` on both builds and enable
`SWIPE_TRIAL_REPLAY_PREPARED` on the second one. For that run, start the server
with `REPLAY_WARM_PAIR_FILL=1` and `REPLAY_WARM_ROTATION=0` or `180` to match
the frame. The service builds the pairs before the timed run and before the
preview pauses begin. Leave both warm options unset for the direct run.

```sh
python3 firmware/native/tools/compare_preview_replays.py compare --prepared-service direct.json prepared.json
```

The report includes photo bytes downloaded by the frame and whether the
service was prewarmed. It requires the prepared run to use the service
throughout. Prepared JPEGs pass the Fill check when the pane edges contain
photo pixels and the inner four divider pixels are dark.

To exercise the frame's lookahead, leave the replay's warm options unset
and use the prepared Fill build. Check `prepared_warm` for acknowledged
lookahead requests, `prepared_decode` for JPEGs actually used, and the
ordinary transfer counters for direct fallback. Keep the server logs to tie
cache activity to each run. The comparator above expects zero fallbacks.

### Measure portrait companion prefetch

For both builds, leave `SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD=OFF` so the worker can
still download the next primary photo during decoding. Build the control with
`SWIPE_TRIAL_REPLAY_DISABLE_COMPANION_PREFETCH=ON` and the candidate with it
set to `OFF`. Use the same replay origin, dataset and timed trials as above.
Capture each result after restarting the replay server, then compare them.

```sh
python3 firmware/native/tools/compare_preview_replays.py compare --companion-prefetch control.json candidate.json
```

This comparison verifies both firmware modes and reports the share of newly
prepared slides that render both portraits. The firmware freezes these counts
with the timing report after the final swipe. Reloading a slide from history
does not count as a new slide.

When both photo IDs are known, direct playback can start a companion while
the primary transfers. For a required pair with a primary still in flight
on the background worker, the foreground worker can prepare the companion
first. `overlapped_companion` counts both paths in the preparation report.
Use the stalled-preview comparison with companion prefetch enabled on both
builds to measure the effect on Fill, refills and gestures.
