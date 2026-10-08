'use strict';

// All calls must be made from SkyrimPlatform's update callback. This adapter
// retains primitive IDs/tokens only, never Actor wrappers across frames.
class StudioClient {
  constructor(callNative) {
    this.call = callNative;
    this.token = 0;
    this.revision = 0;
    this.yaw = 0;
    this.zoom = 1;
  }
  invoke(name, ...args) { return this.call('HdnTailorStudio', name, undefined, ...args); }
  available() {
    try { return this.invoke('ApiVersion') === 1; } catch (_) { return false; }
  }
  open() {
    this.close();
    if (!this.available()) return false;
    const token = this.invoke('BeginSession');
    if (!Number.isInteger(token) || token <= 0) return false;
    this.token = token;
    this.revision = 0;
    this.yaw = 0;
    this.zoom = 1;
    return true;
  }
  snapshot(actorId) {
    if (!this.token || !Number.isInteger(actorId) || actorId <= 0 ||
        actorId > 0xffffffff || (actorId >>> 24) !== 255) return false;
    // Papyrus int is signed, unlike the JS uint32 FormID.
    return this.invoke('Snapshot', this.token, ++this.revision, actorId | 0) === true;
  }
  rotate(delta) {
    if (!Number.isFinite(delta)) return false;
    this.yaw = ((this.yaw + delta) % 360 + 360) % 360;
    return true;
  }
  frame() {
    if (!this.token) return false;
    return this.invoke('Frame', this.token, this.yaw, this.zoom) === true;
  }
  viewport(bounds) {
    if (!this.token || !bounds) return false;
    const values = [bounds.x, bounds.y, bounds.width, bounds.height];
    if (!values.every(Number.isFinite) || bounds.width < 0.02 || bounds.height < 0.02 ||
        bounds.x - bounds.width / 2 < 0 || bounds.x + bounds.width / 2 > 1 ||
        bounds.y - bounds.height / 2 < 0 || bounds.y + bounds.height / 2 > 1) return false;
    return this.invoke('Viewport', this.token, ...values) === true;
  }
  status() { return this.token ? this.invoke('GetStatus', this.token) : 0; }
  close() {
    const token = this.token;
    this.token = 0;
    if (token) { try { this.invoke('EndSession', token); } catch (_) {} }
  }
}
module.exports = {StudioClient};
