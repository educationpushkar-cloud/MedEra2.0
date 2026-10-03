/* Small local QR encoder for compact appointment links (byte mode, error correction L). */
(function (global) {
  'use strict';

  // Versions 1–3 each use a single error-correction block. Keep the payload
  // within those versions so every generated image remains a standards-valid QR.
  const DATA_CODEWORDS = [19, 34, 55];
  const ECC_CODEWORDS = [7, 10, 15];
  const ALIGNMENT_CENTER = [null, [], [18], [22]];

  function multiply(x, y) {
    let result = 0;
    while (y !== 0) {
      if (y & 1) result ^= x;
      y >>>= 1;
      x <<= 1;
      if (x & 0x100) x ^= 0x11d;
    }
    return result;
  }

  function reedSolomonDivisor(degree) {
    let result = [1];
    let root = 1;
    for (let i = 0; i < degree; i++) {
      const next = new Array(result.length + 1).fill(0);
      for (let j = 0; j < result.length; j++) {
        next[j] ^= result[j];
        next[j + 1] ^= multiply(result[j], root);
      }
      result = next;
      root = multiply(root, 2);
    }
    return result;
  }

  function reedSolomonRemainder(data, divisor) {
    const degree = divisor.length - 1;
    const result = new Array(degree).fill(0);
    for (const value of data) {
      const factor = value ^ result[0];
      result.shift();
      result.push(0);
      for (let i = 0; i < degree; i++) result[i] ^= multiply(divisor[i + 1], factor);
    }
    return result;
  }

  function makeCodewords(bytes, version) {
    const dataCapacity = DATA_CODEWORDS[version - 1];
    const bits = [];
    const appendBits = (value, length) => {
      for (let bit = length - 1; bit >= 0; bit--) bits.push(((value >>> bit) & 1) !== 0);
    };
    appendBits(0x4, 4); // Byte mode
    appendBits(bytes.length, 8);
    for (const byte of bytes) appendBits(byte, 8);
    const capacityBits = dataCapacity * 8;
    for (let i = 0; i < Math.min(4, capacityBits - bits.length); i++) bits.push(false);
    while (bits.length % 8 !== 0) bits.push(false);
    const data = [];
    for (let i = 0; i < bits.length; i += 8) {
      let value = 0;
      for (let j = 0; j < 8; j++) value = (value << 1) | (bits[i + j] ? 1 : 0);
      data.push(value);
    }
    for (let pad = 0xec; data.length < dataCapacity; pad ^= 0xec ^ 0x11) data.push(pad);
    const divisor = reedSolomonDivisor(ECC_CODEWORDS[version - 1]);
    return data.concat(reedSolomonRemainder(data, divisor));
  }

  function makeMatrix(text) {
    const bytes = [...new TextEncoder().encode(text)];
    const version = DATA_CODEWORDS.findIndex(capacity => 4 + 8 + bytes.length * 8 <= capacity * 8) + 1;
    if (!version || bytes.length > 53) throw new Error('The link is too long for this QR code.');
    const size = version * 4 + 17;
    const modules = Array.from({ length: size }, () => new Array(size).fill(false));
    const functions = Array.from({ length: size }, () => new Array(size).fill(false));
    const setFunction = (x, y, dark) => { modules[y][x] = dark; functions[y][x] = true; };

    function finder(centerX, centerY) {
      for (let dy = -4; dy <= 4; dy++) for (let dx = -4; dx <= 4; dx++) {
        const x = centerX + dx, y = centerY + dy;
        if (x < 0 || y < 0 || x >= size || y >= size) continue;
        const radius = Math.max(Math.abs(dx), Math.abs(dy));
        setFunction(x, y, radius !== 2 && radius !== 4);
      }
    }
    finder(3, 3);
    finder(size - 4, 3);
    finder(3, size - 4);

    for (const center of ALIGNMENT_CENTER[version]) {
      for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) {
        const x = center + dx, y = center + dy;
        setFunction(x, y, Math.max(Math.abs(dx), Math.abs(dy)) !== 1);
      }
    }
    for (let i = 8; i < size - 8; i++) {
      if (!functions[6][i]) setFunction(i, 6, i % 2 === 0);
      if (!functions[i][6]) setFunction(6, i, i % 2 === 0);
    }

    function drawFormatBits(mask) {
      const data = (1 << 3) | mask; // Error correction L
      let remainder = data << 10;
      for (let bit = 14; bit >= 10; bit--) if (((remainder >>> bit) & 1) !== 0) remainder ^= 0x537 << (bit - 10);
      const format = ((data << 10) | remainder) ^ 0x5412;
      const bitAt = bit => ((format >>> bit) & 1) !== 0;
      for (let i = 0; i <= 5; i++) setFunction(8, i, bitAt(i));
      setFunction(8, 7, bitAt(6));
      setFunction(8, 8, bitAt(7));
      setFunction(7, 8, bitAt(8));
      for (let i = 9; i < 15; i++) setFunction(14 - i, 8, bitAt(i));
      for (let i = 0; i < 8; i++) setFunction(size - 1 - i, 8, bitAt(i));
      for (let i = 8; i < 15; i++) setFunction(8, size - 15 + i, bitAt(i));
      setFunction(8, size - 8, true);
    }
    drawFormatBits(0); // Mask pattern 0

    const codewords = makeCodewords(bytes, version);
    let bitIndex = 0;
    let upward = true;
    for (let right = size - 1; right >= 1; right -= 2) {
      if (right === 6) right = 5;
      for (let vertical = 0; vertical < size; vertical++) {
        const y = upward ? size - 1 - vertical : vertical;
        for (let offset = 0; offset < 2; offset++) {
          const x = right - offset;
          if (functions[y][x]) continue;
          const source = bitIndex < codewords.length * 8 ? (codewords[bitIndex >>> 3] >>> (7 - (bitIndex & 7))) & 1 : 0;
          bitIndex++;
          modules[y][x] = (source ^ (((x + y) % 2 === 0) ? 1 : 0)) !== 0;
        }
      }
      upward = !upward;
    }
    return modules;
  }

  function toCanvas(text, modulePixels = 6) {
    const modules = makeMatrix(text);
    const quietZone = 4;
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = (modules.length + quietZone * 2) * modulePixels;
    const context = canvas.getContext('2d');
    context.fillStyle = '#fff';
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = '#17231c';
    for (let y = 0; y < modules.length; y++) for (let x = 0; x < modules.length; x++) {
      if (modules[y][x]) context.fillRect((x + quietZone) * modulePixels, (y + quietZone) * modulePixels, modulePixels, modulePixels);
    }
    return canvas;
  }

  global.MedEraQR = { toCanvas };
})(window);
