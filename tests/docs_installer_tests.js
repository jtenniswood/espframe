// Exercise the flashing library actually resolved by the bundled installer.
// Register I/O is mocked; this test never opens a serial port or writes firmware.
const assert = require('node:assert/strict');
const { createRequire } = require('node:module');
const { resolve } = require('node:path');
const { buildSync } = require('esbuild');

const installerRequire = createRequire(require.resolve('esp-web-tools'));
const target = installerRequire.resolve('esptool-js/lib/targets/esp32p4.js');
const compiled = buildSync({
  entryPoints: [target], bundle: true, platform: 'node', format: 'cjs', write: false,
});
const loaded = { exports: {} };
new Function('module', 'exports', 'require', compiled.outputFiles[0].text)(loaded, loaded.exports, require);
const { ESP32P4ROM } = loaded.exports;

async function checkPreservingManifest() {
  const compiled = buildSync({
    stdin: {
      contents: "export { preservingManifestUrl } from './docs/.vitepress/theme/usb-installer';",
      resolveDir: resolve(__dirname, '..'),
    },
    bundle: true, platform: 'node', format: 'cjs', write: false,
  });
  const loaded = { exports: {} };
  new Function('module', 'exports', compiled.outputFiles[0].text)(loaded, loaded.exports);
  const { preservingManifestUrl } = loaded.exports;
  const olderManifest = {
    name: 'Espframe V2',
    version: 'v1.17.3',
    home_assistant_domain: 'esphome',
    builds: [{
      chipFamily: 'ESP32-P4',
      parts: [{ path: 'immich-frame-v2.factory.bin', offset: 0 }],
      ota: { path: 'immich-frame-v2.ota.bin', md5: 'example' },
    }],
  };
  const source = 'https://firmware.example/espframe/firmware/jc8012p4a1-v2/manifest.json';
  const url = preservingManifestUrl(olderManifest, source);
  const manifest = await (await fetch(url)).json();
  assert.deepEqual(manifest, {
    ...olderManifest,
    new_install_prompt_erase: true,
    builds: [{
      ...olderManifest.builds[0],
      parts: [{ path: 'https://firmware.example/espframe/firmware/jc8012p4a1-v2/immich-frame-v2.factory.bin', offset: 0 }],
    }],
  });
  assert.equal(olderManifest.new_install_prompt_erase, undefined);
  assert.equal(olderManifest.builds[0].parts[0].path, 'immich-frame-v2.factory.bin');
  console.log('The browser installer offers the erase choice for older published manifests.');
}

async function checkFlashInitialization(revision, romPowered = false, secureDownloadMode = false) {
  const chip = new ESP32P4ROM();
  chip.getChipRevision = async () => revision;
  chip.disableWatchdogs = async () => {};
  const registers = new Map([
    [chip.EFUSE_RD_REPEAT_DATA1_REG, romPowered ? chip.EFUSE_DOWNLOAD_MODE_XPD_ON_MASK : 0],
    [chip.PMU_DATE_REG, 0x103],
  ]);
  const writes = [];
  const loader = {
    usesUsbOtg: async () => false,
    IS_STUB: false,
    secureDownloadMode,
    readReg: async (address) => registers.get(address) || 0,
    writeReg: async (address, value) => {
      writes.push([address, value]);
      registers.set(address, value);
    },
  };

  // ESPLoader calls postConnect before attaching flash and reading its ID.
  await chip.postConnect(loader);
  if (secureDownloadMode || ![301, 302].includes(revision)) {
    assert.deepEqual(writes, [], `P4 revision ${revision}: no flash-power writes required`);
  } else if (romPowered) {
    assert.deepEqual(writes, [[chip.PMU_DATE_REG, 0x100]],
      'ECO7 must release the ROM force-on bits, preserve other bits, and avoid a second power-up');
  } else {
    assert.equal(registers.get(chip.LP_SYSTEM_REG_ANA_XPD_PAD_GROUP_REG), 1,
      `P4 revision ${revision}: flash must be powered before attachment/ID reads (esptool-js#268)`);
    assert.equal(registers.get(chip.PMU_EXT_LDO_P0_0P1A_ANA_REG) & chip.PMU_ANA_0P1A_EN_CUR_LIM_0, 0,
      'Release the temporary current limit after power-up');
    assert.equal(registers.get(chip.PMU_EXT_LDO_P0_0P1A_REG) & chip.PMU_0P1A_FORCE_TIEH_SEL_0, 0,
      'Release the temporary force selection after power-up');
  }
}

(async () => {
  for (const revision of [100, 300, 301, 302]) await checkFlashInitialization(revision);
  await checkFlashInitialization(302, true);
  await checkFlashInitialization(302, false, true);
  console.log('Installer P4 flash initialization checks passed (mocked registers, no USB writes).');
  await checkPreservingManifest();
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
