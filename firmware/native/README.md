# Native photo swipe prototype

This standalone ESP-IDF app exercises the photo gesture on the Guition
JC8012P4A1 **V3** (ESP32-P4 v3.x silicon). It starts on the middle of five
locally prepared slides. Drag either way, release a short drag, flick, or
press while a slide is settling to take over from its visible position.

The prototype works offline. It uses the V3 panel initialization sequence
from the maintained Espframe device profile and the GSL3680 firmware and
point identification sources already in the repository. See
[the touchscreen component's attribution](../../components/gsl3680/LICENSE.md).

## Build

Use ESP-IDF 6.1.1. From the repository root:

```sh
idf.py -C firmware/native build
idf.py -C firmware/native merge-bin
```

The merged binary is for a V3 test unit. Use USB to install it on a dedicated
test frame after exporting its settings. Have the regular Espframe V3
factory image available to restore the device after the test. The prototype
does not participate in Espframe's OTA or browser installer flow.

Host regression checks run without the ESP-IDF toolchain:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/native/main \
  firmware/native/tests/swipe_test.cpp -o /tmp/espframe-swipe-test
/tmp/espframe-swipe-test
```

## Test on the frame

1. Boot to the middle scene and drag toward each adjacent scene slowly. The
   image boundary should follow your finger with no visible tearing.
2. Drag only a short distance and pause before lifting. The view should
   settle back. Repeat with a quick flick and check that it advances.
3. Start a transition, then grab it again and pull back. Repeat rapid
   forward and backward swipes, including the split scene.
4. Check the first and last scenes. Dragging beyond those boundaries should
   keep the displayed pixels filled.
5. Record high-frame-rate video for touch-to-visible-motion and pacing.
   USB logs report recognition-to-DMA-scanout as `gesture_to_scanout_us`.
   This is an estimate before the new pixels are physically visible.

To probe with real photos, place baseline JPEGs in the ignored
`firmware/native/photos/` directory as `slide-0.jpg` through `slide-4.jpg`.
Prepare the landscape image as 1280 × 800, rotate it 90 degrees clockwise
into 800 × 1280, and bake in the rotation. Compose a portrait pair into the
landscape canvas before rotating. Run `idf.py -C firmware/native reconfigure`
after adding files. Missing or rejected JPEGs use the built-in test scenes.
