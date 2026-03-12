# Troubleshooting

## Common Issues

**Photos aren't loading / `HTTP 401` in logs**
:   Your API key is invalid or doesn't have permission. Generate a new one in Immich under *Account Settings > API Keys* and update it in the [web UI](./configuration#immich-connection).

**"No valid asset in response"**
:   Your Immich library may be empty, or only contains videos (the frame only fetches images). Make sure you have photos uploaded to Immich.

**Image doesn't change even though logs show new asset IDs**
:   Usually an SSL or auth issue with the image download endpoint. If you're using a self-signed certificate, make sure `immich_verify_ssl` is set to `false` (the default).

**WiFi setup screen appears unexpectedly**
:   The device lost its WiFi connection. Connect to the hotspot shown on screen and reconfigure, or check that your router is reachable.

**"Portrait left/right decode failed, falling back to single"**
:   One of the paired portrait images failed to download. The frame falls back to showing the single portrait — no action needed.

**"No companion portrait found for this day"**
:   There's no other portrait photo in Immich taken on the same day, so the portrait is shown on its own. This is normal.

## Known Limitations

- You can only swipe back **one photo**. There's no deep history.
- Standard landscape images are resized to 1280x960. Portrait pairs are resized to 640x1200 each.
- Very wide panoramas (wider than 2:1 aspect ratio) are shown with letterboxing rather than zoomed.
- The companion portrait search picks from up to 10 random same-day photos — it doesn't guarantee the best pairing.

## Bugs & Feature Requests

If you run into a problem not covered here, or have an idea for a new feature, please open an issue on GitHub:

**[github.com/jtenniswood/espframe/issues](https://github.com/jtenniswood/espframe/issues)**