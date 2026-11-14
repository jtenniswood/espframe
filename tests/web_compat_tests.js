const assert = require("assert/strict");

const compat = require("../docs/webserver/src/compat.ts");

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

const wireguardKey = "YBk6X3pP8KjUnS4VvXG0Zk1d3b0Qn0q9yYQe2jqk0Gs=";
assert.equal(compat.isValidWireguardKey(wireguardKey), true);
assert.equal(compat.isValidWireguardKey(" " + wireguardKey + " "), true);
assert.equal(compat.isValidWireguardKey(wireguardKey.slice(1)), false);
assert.equal(compat.isValidWireguardKey(wireguardKey.slice(0, 43) + "A"), false);
assert.equal(compat.isValidWireguardKey("YBk6X3pP8KjUnS4VvXG0Zk1d3b0Qn0q9yYQe2jqk0Gt="), false);
assert.equal(compat.isValidWireguardKey(""), false);

assert.equal(compat.isValidWireguardAddress("192.168.88.200"), true);
assert.equal(compat.isValidWireguardAddress("0.0.0.0"), false);
assert.equal(compat.isValidWireguardAddress("255.255.255.255"), false);
assert.equal(compat.isValidWireguardAddress("192.168.88.256"), false);
assert.equal(compat.isValidWireguardAddress("192.168.88"), false);

assert.equal(compat.isValidIpv4Netmask("255.255.255.255"), true);
assert.equal(compat.isValidIpv4Netmask("255.255.255.0"), true);
assert.equal(compat.isValidIpv4Netmask("255.255.254.0"), true);
assert.equal(compat.isValidIpv4Netmask("0.0.0.0"), true);
assert.equal(compat.isValidIpv4Netmask("255.0.255.0"), false);
assert.equal(compat.isValidIpv4Netmask("0.255.255.255"), false);
assert.equal(compat.isValidIpv4Netmask("255.255.255"), false);

assert.equal(compat.isValidWireguardPort("51820"), true);
assert.equal(compat.isValidWireguardPort("0"), false);
assert.equal(compat.isValidWireguardPort("65536"), false);
assert.equal(compat.isValidWireguardPort("51820abc"), false);
assert.equal(compat.isValidWireguardPort(""), false);

assert.equal(compat.isValidWireguardEndpoint("vpn.example.com"), true);
assert.equal(compat.isValidWireguardEndpoint("203.0.113.7"), true);
assert.equal(compat.isValidWireguardEndpoint("vpn.example.com:51820"), false);
assert.equal(compat.isValidWireguardEndpoint("https://vpn.example.com"), false);
assert.equal(compat.isValidWireguardEndpoint(".example.com"), false);
assert.equal(compat.isValidWireguardEndpoint(""), false);

assert.equal(compat.normalizeWireguardAllowedIps("192.168.88.0/24"), "192.168.88.0/24");
assert.equal(compat.normalizeWireguardAllowedIps("192.168.88.77/24;10.0.0.5"), "192.168.88.0/24, 10.0.0.5/32");
assert.equal(compat.normalizeWireguardAllowedIps("0.0.0.0/0"), "0.0.0.0/0");
assert.equal(compat.normalizeWireguardAllowedIps("192.168.88.0/33"), "");
assert.equal(compat.normalizeWireguardAllowedIps("192.168.88.0/"), "");
assert.equal(compat.normalizeWireguardAllowedIps("nope"), "");
assert.equal(compat.normalizeWireguardAllowedIps(""), "");
assert.equal(compat.normalizeWireguardAllowedIps("10.0.0.0/8 10.1.0.0/16 10.2.0.0/16 10.3.0.0/16"),
  "10.0.0.0/8, 10.1.0.0/16, 10.2.0.0/16, 10.3.0.0/16");
assert.equal(compat.normalizeWireguardAllowedIps("10.0.0.0/8 10.1.0.0/16 10.2.0.0/16 10.3.0.0/16 10.4.0.0/16"), "");

assert.equal(compat.formatWireguardHandshakeAge(-1), "");
assert.equal(compat.formatWireguardHandshakeAge(undefined), "");
assert.equal(compat.formatWireguardHandshakeAge(null), "");
assert.equal(compat.formatWireguardHandshakeAge(NaN), "");
assert.equal(compat.formatWireguardHandshakeAge(0), "0s ago");
assert.equal(compat.formatWireguardHandshakeAge(45), "45s ago");
assert.equal(compat.formatWireguardHandshakeAge(119.6), "2 min ago");
assert.equal(compat.formatWireguardHandshakeAge(600), "10 min ago");
assert.equal(compat.formatWireguardHandshakeAge(7200), "2 h ago");

console.log("web compatibility helper tests passed");
