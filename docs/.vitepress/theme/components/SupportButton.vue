<template>
  <a
    class="sp-support-btn"
    href="https://www.buymeacoffee.com/jtenniswood"
    target="_blank"
    rel="noopener"
  >
    <span>Buy Me A Coffee</span>
    <img
      src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png"
      alt="Buy Me A Coffee"
      height="60"
      style="border-radius:999px;"
    >
  </a>
</template>

<style scoped>
.sp-support-btn {
  position: fixed;
  right: 28px;
  bottom: 28px;
  z-index: 150;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 217px;
  height: 60px;
  overflow: hidden;
  border-radius: 999px;
  background: #ffdd00;
  color: #000;
  font-family: Arial, sans-serif;
  font-size: 18px;
  font-weight: 700;
  line-height: 1;
  text-decoration: none;
  box-shadow: 0 2px 5px rgba(0, 0, 0, .15);
}

.sp-support-btn span {
  position: absolute;
}

.sp-support-btn img {
  position: absolute;
  inset: 0;
  width: 217px;
  height: 60px;
  display: block;
  border-radius: 999px;
}
</style>
