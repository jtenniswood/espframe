const assert = require("assert/strict");

const compat = require("../docs/webserver/src/compat.js");

assert.equal(compat.normalizeNtpServer("  pool.ntp.org  "), "pool.ntp.org");
assert.equal(compat.normalizeDateTakenFormat("January 1, 2000"), "January 1, 2026");
assert.equal(compat.normalizeDateTakenFormat("Month Day, Year"), "January 1, 2026");
assert.equal(compat.normalizeDateTakenFormat("Month Day Ordinal, Year"), "January 1, 2026");
assert.equal(compat.normalizeDateTakenFormat("unknown"), "1 January, 2026");

assert.equal(compat.normalizeImmichUrl(""), "");
assert.equal(compat.normalizeImmichUrl("photos.example.com"), "https://photos.example.com");
assert.equal(compat.normalizeImmichUrl("192.168.1.30:2283/"), "http://192.168.1.30:2283");
assert.equal(compat.normalizeImmichUrl("//photos.example.com/"), "https://photos.example.com");
assert.equal(compat.normalizeImmichUrl("HTTP://LOCALHOST:2283///"), "http://LOCALHOST:2283");

assert.equal(compat.normalizeFirmwareManifestUrl(" https://example.com/manifest.json/// "), "https://example.com/manifest.json");
assert.equal(compat.isValidHttpUrl("https://example.com/manifest.json"), true);
assert.equal(compat.isValidHttpUrl("http://192.168.1.30:80/manifest.json"), true);
assert.equal(compat.isValidHttpUrl("ftp://example.com/manifest.json"), false);
assert.equal(compat.isValidHttpUrl("javascript:alert(1)"), false);

assert.equal(compat.extractUrlHost("user:pass@photos.example.com:2283/path"), "photos.example.com");
assert.equal(compat.extractUrlPort("user:pass@photos.example.com:2283/path"), "2283");

assert.equal(compat.isValidUuidList(""), true);
assert.equal(compat.isValidUuidList("00000000-0000-0000-0000-000000000000"), true);
assert.equal(compat.isValidUuidList("00000000-0000-0000-0000-000000000000,11111111-1111-1111-1111-111111111111"), true);
assert.equal(compat.isValidUuidList("not-a-photo-id"), false);

assert.deepEqual(compat.splitPhotoIdList(""), [""]);
assert.deepEqual(compat.splitPhotoIdList(" a , , b "), ["a", "b"]);
assert.deepEqual(compat.parsePhotoLabelList('["Family",""]'), ["Family", ""]);
assert.deepEqual(compat.parsePhotoLabelList("Family, Friends"), ["Family", "Friends"]);

assert.equal(compat.photoIdFieldTooLong("x".repeat(255)), false);
assert.equal(compat.photoIdFieldTooLong("x".repeat(256)), true);
assert.equal(compat.photoLabelFieldTooLong("x".repeat(256)), true);

console.log("web compatibility helper tests passed");
