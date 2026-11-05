// No npm dependencies. Start a local web/ server and an isolated Chromium/Edge
// with --remote-debugging-port=9223, then run: node scripts/check-ui.mjs
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const target = await fetch('http://127.0.0.1:9223/json/new?about:blank', { method: 'PUT' }).then(r => r.json());
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
let sequence = 0;
const pending = new Map();
const errors = [];
socket.onmessage = event => {
  const message = JSON.parse(event.data);
  if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails.text);
  if (!message.id) return;
  const request = pending.get(message.id);
  pending.delete(message.id);
  if (message.error) request.reject(new Error(message.error.message));
  else request.resolve(message.result);
};
function command(method, params = {}) {
  const id = ++sequence;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function waitFor(expression) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    if (await evaluate(expression)) return;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error('Timed out waiting for: ' + expression);
}
async function screenshot(name) {
  const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
  const path = join(tmpdir(), name);
  await writeFile(path, Buffer.from(result.data, 'base64'));
  console.log('Screenshot:', path);
}

try {
  await command('Runtime.enable');
  await command('Page.enable');
  await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1150, deviceScaleFactor: 1, mobile: false });
  await command('Page.navigate', { url: 'http://127.0.0.1:8765/?preview' });
  await waitFor("document.getElementById('interval')?.value === '5'");
  await evaluate('localStorage.clear(); location.reload();');
  await waitFor("document.getElementById('interval')?.value === '5'");
  assert.match(await evaluate("document.getElementById('modeBadge').textContent"), /preview/i);
  assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
  await evaluate("document.getElementById('addTime').click(); let time = document.querySelector('#times input'); time.value='07:00'; time.dispatchEvent(new Event('input',{bubbles:true})); document.getElementById('addTime').click(); time=document.querySelectorAll('#times input')[1]; time.value='07:00'; time.dispatchEvent(new Event('input',{bubbles:true})); document.getElementById('settingsForm').requestSubmit();");
  assert.match(await evaluate("document.getElementById('message').textContent"), /different times/);
  assert.equal(await evaluate("document.getElementById('addCat')"), null, 'Single-cat feeder must not offer extra profiles');
  await evaluate("document.querySelectorAll('#times .remove')[1].click(); document.getElementById('portion').value='12.5'; document.getElementById('portion').dispatchEvent(new Event('input',{bubbles:true})); const fields=document.querySelectorAll('#catList input'); fields[0].value='<b>Test cat</b>'; fields[0].dispatchEvent(new Event('input',{bubbles:true})); fields[1].value='test-tag'; fields[1].dispatchEvent(new Event('input',{bubbles:true}));");
  assert.equal(await evaluate("document.querySelectorAll('#catList b').length"), 0, 'Cat names must render as text');
  await evaluate("document.getElementById('settingsForm').requestSubmit()");
  await waitFor("document.getElementById('message').textContent.includes('Preview saved')");
  assert.equal(await evaluate("JSON.parse(localStorage.getItem('catfeeder-ui-preview-v1')).portionGrams"), 12.5);
  await command('Page.reload');
  await waitFor("document.getElementById('portion')?.value === '12.5'");
  assert.equal(await evaluate("document.querySelectorAll('#catList .catrow').length"), 1);
  assert.equal(await evaluate("document.querySelector('#times input').value"), '07:00');
  await evaluate("document.getElementById('portion').value='20'; document.getElementById('portion').dispatchEvent(new Event('input',{bubbles:true})); document.getElementById('discard').click();");
  assert.equal(await evaluate("document.getElementById('portion').value"), '12.5');
  await screenshot('CatFeeder-ui-desktop.png');
  await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'Mobile must not overflow');
  await screenshot('CatFeeder-ui-mobile.png');
  await command('Page.navigate', { url: 'http://127.0.0.1:8765/' });
  await waitFor("document.getElementById('connectionLabel')?.textContent === 'Feeder offline'");
  assert.equal(await evaluate("document.getElementById('save').disabled"), true, 'Offline save must be disabled');
  assert.deepEqual(errors, [], 'No browser runtime exceptions');
  console.log('PASS: preview labeling, desktop/mobile layout, duplicate-time validation, safe text rendering, save/reload, discard, and offline handling.');
} finally {
  await command('Page.close').catch(() => {});
  socket.close();
}
