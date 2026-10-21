// Free-flight camera: click to capture the mouse (or drag to look where pointer lock is not allowed),
// WASD to move, Space/E up, Ctrl/Q down, Shift to boost, mouse wheel to change cruise speed.
// Touch: drag with one finger to look, hold two fingers to fly forward.
import * as THREE from 'three';

export class FlyCam {
  constructor(camera, dom, groundHeight) {
    this.camera = camera;
    this.dom = dom;
    this.groundHeight = groundHeight;
    this.yaw = 0;
    this.pitch = 0;
    this.speed = 8;
    this.velocity = new THREE.Vector3();
    this.keys = new Set();
    this.pointers = new Map();
    this.onSpeedChange = () => {};

    const look = (dx, dy) => {
      this.yaw -= dx * 0.0022;
      this.pitch = Math.max(-1.55, Math.min(1.55, this.pitch - dy * 0.0022));
    };
    dom.addEventListener('pointerdown', (e) => {
      dom.focus();
      if (e.pointerType === 'mouse' && document.pointerLockElement !== dom) {
        // Pointer lock is optional: hosted frames and some browsers refuse it, then dragging looks around.
        try {
          dom.requestPointerLock()?.catch?.(() => {});
        } catch { /* drag to look instead */ }
      }
      this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
      dom.setPointerCapture?.(e.pointerId);
    });
    dom.addEventListener('pointermove', (e) => {
      if (document.pointerLockElement === dom) {
        look(e.movementX, e.movementY);
        return;
      }
      const p = this.pointers.get(e.pointerId);
      if (!p) return;
      const scale = e.pointerType === 'touch' ? 1.6 : 1;
      look((e.clientX - p.x) * scale, (e.clientY - p.y) * scale);
      p.x = e.clientX;
      p.y = e.clientY;
    });
    const release = (e) => this.pointers.delete(e.pointerId);
    dom.addEventListener('pointerup', release);
    dom.addEventListener('pointercancel', release);
    window.addEventListener('keydown', (e) => {
      if (e.target instanceof HTMLInputElement) return;
      this.keys.add(e.code);
      if (e.code === 'Space' || e.code.startsWith('Arrow')) e.preventDefault();
    });
    window.addEventListener('keyup', (e) => this.keys.delete(e.code));
    window.addEventListener('blur', () => this.keys.clear());
    dom.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.speed = Math.min(400, Math.max(1, this.speed * (e.deltaY < 0 ? 1.2 : 1 / 1.2)));
      this.onSpeedChange(this.speed);
    }, { passive: false });
  }

  lookAt(position, target) {
    this.camera.position.copy(position);
    const d = new THREE.Vector3().subVectors(target, position).normalize();
    this.yaw = Math.atan2(-d.x, -d.z);
    this.pitch = Math.asin(Math.max(-1, Math.min(1, d.y)));
    this.velocity.set(0, 0, 0);
    this.apply();
  }

  apply() {
    this.camera.quaternion.setFromEuler(new THREE.Euler(this.pitch, this.yaw, 0, 'YXZ'));
  }

  update(dt) {
    this.apply();
    const k = this.keys;
    const forward = new THREE.Vector3(0, 0, -1).applyQuaternion(this.camera.quaternion);
    const right = new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion);
    const wish = new THREE.Vector3();
    if (k.has('KeyW') || k.has('ArrowUp') || this.pointers.size >= 2) wish.add(forward);
    if (k.has('KeyS') || k.has('ArrowDown')) wish.sub(forward);
    if (k.has('KeyD') || k.has('ArrowRight')) wish.add(right);
    if (k.has('KeyA') || k.has('ArrowLeft')) wish.sub(right);
    if (k.has('Space') || k.has('KeyE')) wish.y += 1;
    if (k.has('ControlLeft') || k.has('KeyQ') || k.has('KeyC')) wish.y -= 1;
    if (wish.lengthSq() > 0) wish.normalize();
    const boost = k.has('ShiftLeft') || k.has('ShiftRight') ? 5 : 1;
    wish.multiplyScalar(this.speed * boost);
    this.velocity.lerp(wish, 1 - Math.exp(-dt * 7));
    this.camera.position.addScaledVector(this.velocity, dt);
    const floor = this.groundHeight(this.camera.position.x, this.camera.position.z) + 0.35;
    if (this.camera.position.y < floor) this.camera.position.y = floor;
  }
}
