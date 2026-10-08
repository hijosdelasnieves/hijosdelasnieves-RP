(function () {
  'use strict';
  // CEF only sends primitive bounds. SkyrimPlatform receives and queues them;
  // no game/native function is called from a browser message callback.
  if (window.__hdnPrivateStudioViewport) return;
  window.__hdnPrivateStudioViewport = true;
  var signature = '';
  function measure() {
    var root = document.getElementById('hdn-tailor-root');
    var stage = root && root.querySelector('.ts-stage');
    if (!stage || !window.hdnVendor || !window.hdnVendor.isVisible()) { signature = ''; return; }
    var box = stage.getBoundingClientRect();
    var title = stage.querySelector('.ts-stage-title');
    var bottom = stage.querySelector('.ts-stage-bottom');
    var topY = title ? title.getBoundingClientRect().bottom + 12 : box.top;
    var bottomY = bottom ? bottom.getBoundingClientRect().top - 12 : box.bottom;
    if (!innerWidth || !innerHeight || box.width <= 40 || bottomY - topY <= 40) return;
    var bounds = {
      x: (box.left + box.width / 2) / innerWidth,
      y: (topY + bottomY) / 2 / innerHeight,
      width: (box.width - 24) / innerWidth,
      height: (bottomY - topY) / innerHeight
    };
    var next = JSON.stringify(bounds);
    if (next === signature) return;
    signature = next;
    try {
      window.skyrimPlatform.sendMessage(JSON.stringify({kind:'hdnVendorPrivateViewport',bounds:bounds}));
    } catch (_) { signature = ''; }
  }
  window.addEventListener('resize', measure);
  window.setInterval(measure, 250);
}());
