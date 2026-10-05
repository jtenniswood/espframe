---
title: Immich Photo Frame Overview
description: The EspFrame Immich photo frame overview has moved.
---

<script>
(() => {
  const legacyAnchors = {
    '#how-setup-works': 'get-started',
    '#privacy-model': 'privacy',
    '#related-guides': 'get-started',
  };
  const target = new URL('/espframe/', window.location.origin);
  const legacyHash = window.location.hash;
  const section = legacyAnchors[legacyHash] || legacyHash.slice(1);
  if (section) target.hash = section;
  window.location.replace(target.href);
})();
</script>

# Overview moved

The EspFrame overview now combines the project introduction, requirements, setup steps, and privacy information on one page.

[Go to the EspFrame overview](/)
