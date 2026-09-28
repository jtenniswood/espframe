---
title: "Espframe FAQ: Immich Photo Frame Setup and Support"
description: "Answers to common questions about Espframe hardware compatibility, Immich setup, installation, privacy, photo sources, updates, and troubleshooting."
---

# Espframe Frequently Asked Questions

Quick answers about the Espframe Immich photo frame, supported Guition display, setup, privacy, and common issues. Follow each related guide for full instructions.

## What is Espframe?

Espframe is source-available, non-commercial ESPHome firmware that turns a supported Guition ESP32-P4 touchscreen into a digital photo frame for a self-hosted Immich library. It connects directly to your Immich server and shows photos on the display. Read the [project overview](/) and [Immich photo frame guide](/immich-photo-frame).

## Which display does Espframe support?

Espframe supports the 10-inch Guition ESP32-P4 JC8012P4A1 display. The original panel uses the firmware marked 2627 or lower on the rear case; the new panel uses 2628 or higher. See [installation and panel selection](/install#choose-the-correct-panel-firmware).

## Does Espframe work on other ESP32 displays?

The documented supported hardware is the Guition ESP32-P4 10-inch JC8012P4A1. Other displays are not listed as supported, so use the documented model when choosing hardware. See [installation requirements](/install#what-you-ll-need).

## Do I need Home Assistant to use Espframe?

No. Espframe connects directly to Immich and works without Home Assistant, a hub, or a cloud service. Home Assistant integration is optional for ESPHome controls and dashboard visibility. See the [Home Assistant integration guide](/home-assistant).

## Do I need a self-hosted Immich server?

Yes. Espframe is designed to display photos from an Immich server that you operate. The frame must be able to reach the server over your network using its configured HTTP or HTTPS URL. Read the [Immich photo frame guide](/immich-photo-frame) or [troubleshoot connection problems](/troubleshooting#immich-connection-problems).

## What do I need before installing Espframe?

You need a supported Guition display, an Immich server, an Immich API key, a USB-C data cable, and a desktop computer running Chrome or Edge for browser flashing. See the [installation guide](/install) and [API key permissions](/api-key).

## How do I install Espframe?

Connect the display to the bottom USB-C port with a data cable, open the installer in desktop Chrome or Edge, select the matching panel firmware, and follow the Wi-Fi and Immich setup steps. Follow [Install Espframe](/install) or [USB flashing help](/usb-flashing).

## Which browser can flash Espframe?

The browser installer requires Chrome or Edge on a desktop computer with Web Serial support. Safari and Firefox are not supported by the installer. See [browser and USB requirements](/usb-flashing#browser-requirements).

## What Immich API key permissions does Espframe need?

Create a read-only Immich API key and enable the permissions listed in the [Espframe API key guide](/api-key#recommended-permissions). Espframe reads photo data and thumbnails; it does not modify or upload photos.

## Can I choose which Immich photos appear?

Yes. Espframe supports photo sources and filters such as all photos, favorites, albums, people, tags, memories, date ranges, locations, ratings, exclusions, and orientation. See [photo sources and filters](/photo-sources).

## Are my photos uploaded to a cloud service?

No hosted photo service is required. The frame requests thumbnails and metadata from the Immich server URL that you configure; if that server is only available on your local network, the frame can stay local too. Read the [privacy model](/immich-photo-frame#privacy-model).

## Can Espframe connect to Immich over HTTPS?

Yes. Configure the Immich server URL with `http://` or `https://` and make sure the address is reachable from the frame. Local IP addresses and domain names are both supported. See [Immich connection troubleshooting](/troubleshooting#immich-connection-problems).

## How do I update Espframe firmware?

Open the device web interface, select Device, and expand Firmware in the System section to check for, install, or roll back Espframe firmware. Automatic update controls are also available. See the [firmware update guide](/firmware-update).

## What should I check if the frame shows no photos?

Start with All Photos, confirm the Immich server URL and read-only API key, and check that the selected albums, people, tags, or date filters match photos in Immich. Follow [troubleshooting missing photos](/troubleshooting#photos-do-not-appear) and review [photo source filters](/photo-sources).

## Can I build Espframe manually with ESPHome?

Yes. The [manual setup guide](/manual-setup) explains how to install from the ESPHome dashboard and configure substitutions for a local build.

## Can I use Espframe commercially?

Espframe project-owned code and documentation are licensed for non-commercial use. Commercial use needs separate permission from the project owner; third-party components retain their own licenses. Read the [license terms](/license).
