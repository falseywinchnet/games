// Profiles a browser build in headless Chrome (run it on the Mini, where the
// build is served): frame rate, GUI.Forms' per-frame costs and the hottest
// functions on the page's main thread and on every program thread.
//   node tools/web/profile.mjs <url> [--settle 8] [--seconds 6] [--out prefix] [--width 1280 --height 800 --scale 2]
//     [--actions "click:x,y|key:5|wait:500|shot:file.png"]   (run after settling, before profiling)
// Console errors and uncaught exceptions are printed at the end.
// Needs Google Chrome; uses Node's built-in WebSocket for the DevTools protocol.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const args = process.argv.slice(2);
const option = (name, fallback) => {
  const index = args.indexOf(`--${name}`);
  return index >= 0 ? args[index + 1] : fallback;
};
const url = args.find((value) => !value.startsWith('--') && !args[args.indexOf(value) - 1]?.startsWith('--'));
if (!url) throw new Error('usage: node tools/web/profile.mjs <url> [options]');
const settle = Number(option('settle', 8));
const seconds = Number(option('seconds', 6));
const out = option('out', '');
const width = Number(option('width', 1280));
const height = Number(option('height', 800));
const scale = Number(option('scale', 2));
const actions = option('actions', '');
const chromePath = option('chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
const port = 9300 + Math.floor(Math.random() * 500);
const profileDir = mkdtempSync(join(tmpdir(), 'playsuite-profile-'));
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const chrome = spawn(chromePath, [
  '--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profileDir}`,
  `--window-size=${width},${height}`, `--force-device-scale-factor=${scale}`, '--no-first-run', '--no-default-browser-check',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
  '--mute-audio', 'about:blank'], { stdio: 'ignore' });

let target;
for (let attempt = 0; attempt < 50 && !target; ++attempt) {
  await sleep(200);
  try {
    const pages = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    target = pages.find((page) => page.type === 'page');
  } catch {}
}
if (!target) throw new Error('Chrome did not start');

const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve) => socket.addEventListener('open', resolve, { once: true }));
let nextId = 0;
const pending = new Map();
const workers = new Map();
const problems = [];
socket.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) {
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    message.error ? reject(new Error(message.error.message)) : resolve(message.result);
  } else if (message.method === 'Runtime.exceptionThrown') {
    const details = message.params.exceptionDetails;
    problems.push(`exception: ${details.exception?.description || details.text}`);
  } else if (message.method === 'Runtime.consoleAPICalled' && ['error', 'warning', 'assert'].includes(message.params.type)) {
    problems.push(`${message.params.type}: ${message.params.args.map((arg) => arg.value ?? arg.description ?? '').join(' ')}`);
  } else if (message.method === 'Target.attachedToTarget') {
    workers.set(message.params.sessionId, message.params.targetInfo);
  }
});
const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
  const id = ++nextId;
  pending.set(id, { resolve, reject });
  socket.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
});
const evaluate = async (expression) =>
  (await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result.value;

await send('Runtime.enable');
await send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });
await send('Page.navigate', { url });
await sleep(settle * 1000);
for (const step of actions.split('|').filter(Boolean)) {
  const [verb, argument = ''] = step.split(/:(.*)/s);
  if (verb === 'wait') await sleep(Number(argument));
  else if (verb === 'click') {
    const [x, y] = argument.split(',').map(Number);
    for (const type of ['mousePressed', 'mouseReleased'])
      await send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1 });
    await sleep(150);
  } else if (verb === 'key') {
    const code = /^[0-9]$/.test(argument) ? `Digit${argument}` : /^[a-z]$/i.test(argument) ? `Key${argument.toUpperCase()}` : argument;
    const text = argument.length === 1 ? argument : undefined;
    await send('Input.dispatchKeyEvent', { type: 'keyDown', code, key: argument, text });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: argument });
    await sleep(150);
  } else if (verb === 'shot') {
    const shot = await send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(argument, Buffer.from(shot.data, 'base64'));
  }
}
const sessions = [...workers.keys()];
const profiled = [undefined, ...sessions];
for (const session of profiled) {
  await send('Profiler.enable', {}, session).catch(() => {});
  await send('Profiler.setSamplingInterval', { interval: 200 }, session).catch(() => {});
}
await evaluate('Module.guiFormsStats && Module.guiFormsStats(), 0');
for (const session of profiled) await send('Profiler.start', {}, session).catch(() => {});
const frames = await evaluate(`new Promise((resolve) => {
  let count = 0, worst = 0;
  const start = performance.now();
  let last = start;
  const step = (time) => {
    ++count; worst = Math.max(worst, time - last); last = time;
    if (time - start < ${seconds * 1000}) requestAnimationFrame(step);
    else resolve({ rafPerSecond: count / ${seconds}, worstGapMs: Math.round(worst) });
  };
  requestAnimationFrame(step);
})`);
const stats = await evaluate('Module.guiFormsStats ? Module.guiFormsStats() : null');
const profiles = [];
for (const session of profiled) {
  const result = await send('Profiler.stop', {}, session).catch(() => null);
  if (result) profiles.push({ thread: session ? (workers.get(session)?.title || session) : 'main', profile: result.profile });
}

const summary = (profile) => {
  const self = new Map();
  const nodes = new Map(profile.nodes.map((node) => [node.id, node]));
  const deltas = profile.timeDeltas;
  let total = 0;
  profile.samples.forEach((id, index) => {
    const node = nodes.get(id);
    const name = node.callFrame.functionName || `(${node.callFrame.url.split('/').pop() || 'anonymous'})`;
    const time = (deltas[index] || 0) / 1000;
    total += time;
    self.set(name, (self.get(name) || 0) + time);
  });
  const busy = [...self.entries()].filter(([name]) => !/^\((idle|program|garbage collector)\)$/.test(name) && !/futex_wait|emscripten_futex_wait|_emscripten_thread_mailbox_await|Atomics.wait/.test(name));
  const busyTotal = busy.reduce((sum, [, time]) => sum + time, 0);
  return { totalMs: Math.round(total), busyMs: Math.round(busyTotal),
           top: busy.sort((a, b) => b[1] - a[1]).slice(0, 15).map(([name, time]) => `${time.toFixed(1).padStart(8)} ms  ${name.slice(0, 110)}`) };
};

console.log(JSON.stringify({ url, frames, perFrame: stats?.perFrame, framesPresented: stats?.frames }, null, 1));
for (const { thread, profile } of profiles) {
  const result = summary(profile);
  if (result.busyMs < 20) continue;
  console.log(`\n# ${thread}: busy ${result.busyMs} of ${result.totalMs} ms`);
  console.log(result.top.join('\n'));
  if (out) writeFileSync(`${out}-${thread.replace(/[^A-Za-z0-9]+/g, '_')}.cpuprofile`, JSON.stringify(profile));
}
if (problems.length) console.log(`\n# console problems (${problems.length})\n` + [...new Set(problems)].slice(0, 30).join('\n'));
socket.close();
chrome.kill();
await sleep(300);
rmSync(profileDir, { recursive: true, force: true });
