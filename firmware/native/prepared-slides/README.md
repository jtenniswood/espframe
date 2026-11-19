# Prepared Immich slides

This optional service prepares one JPEG for a single photo or a portrait pair. It uses the Immich preview API and applies EXIF orientation, the frame's Fit or Fill geometry, and the mounting rotation. It returns an 800 × 1280 baseline JPEG in Guition panel order, with 4:2:0 chroma sampling. [Immich preview endpoint](https://github.com/immich-app/immich/blob/main/server/src/controllers/asset-media.controller.ts) · [Sharp JPEG options](https://sharp.pixelplumbing.com/api-output/#jpeg)

The prototype serves a standalone endpoint for benchmarking. The firmware continues to connect directly to Immich. The frame can keep selecting photos, choosing portrait pairs, and drawing captions and clock overlays when a client is added.

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

Send `GET /v1/slides/<asset-uuid>` with the `Authorization: Bearer <service-token>` header. Add `companion=<asset-uuid>` for a portrait pair, `mode=fill` to crop, or `rotation=180` for an upside-down mount. `fit` and `0` are the defaults. A pair uses the primary photo on the left, the companion on the right and an eight-pixel gap.

The in-memory cache holds up to 128 slides and 24 MiB. It serves a hit for up to one minute, then fetches previews again and reuses the finished JPEG if their content hashes match. `X-Espframe-Cache` distinguishes `miss`, `hit`, and `revalidated` responses. The cache starts empty after a restart. The endpoint returns an error when a preview is missing, unsupported, or too large, or when both preparation slots are busy.

## Verify the V1 hardware decoder

Request five test slides with `rotation=0` and save them as `slide-0.jpg` through `slide-4.jpg` in the ignored `firmware/native/photos` directory. Use the [V1 fixture benchmark](../README.md#measure-swipe-rendering-over-the-local-network). The frame applies its own mounting rotation for that trial. On `/api/trial/prepare`, `hardware_jpeg` reports successful hardware calls and `rejected_preview` reports JPEGs that fell back to the built-in patterns. Verify all 48 scripted gestures settle and that no embedded JPEG is rejected.

## Compare direct and prepared downloads on V1

The replay server generates a fixed sequence of portrait pairs from synthetic JPEG, progressive JPEG, and WebP previews. It serves the Immich search and preview responses used by the direct player, and the prepared-slide endpoint uses those same previews. It requires no real Immich server or photo credentials.

1. On the test host, use `REPLAY_HOST=<private-IPv4-address> npm run replay` and wait for `Replay ready`. The port defaults to 9116. Restart the process before each run so search starts at the same position. Start with an empty service cache. Set `REPLAY_WARM_ROTATION=0` or `REPLAY_WARM_ROTATION=180` to prewarm the pairs for that mounting rotation.
2. Build the [V1 rapid swipe trial](../README.md#measure-swipe-rendering-over-the-local-network) in two separate build directories. For both builds, set `-D SWIPE_TRIAL_REPLAY_ORIGIN=http://<private-IPv4-address>:9116`. Set `-D SWIPE_TRIAL_REPLAY_PREPARED=OFF` for direct previews and `-D SWIPE_TRIAL_REPLAY_PREPARED=ON` for prepared slides. These options are only valid for the timed rapid app. It uses the saved Wi‑Fi and mount settings and sets the synthetic photos and Fit layout in memory.
3. Run the same rollback readiness check before each upload. Save `/api/trial/benchmark` and `/api/trial/prepare` before rollback. Compare bytes, cold returns, refill times and drag gaps after all 48 gestures. The prepared build reports `prepared_transfer` and `prepared_decode`. Direct `transfer` activity in that build indicates a fallback to ordinary previews. The server prints cache hits and misses for each prepared request.
