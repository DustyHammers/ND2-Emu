// Renders every layer of the default skin at 2x on a transparent canvas, the digit segment cells, and the control list.
const { chromium } = require('playwright');
const fs = require('fs');
const LAYERS = ['jacks', 'body', 'panels', 'display', 'redprint', 'tagboxes', 'lines', 'text', 'wordmark', 'controls', 'all'];
const SEGS = ['unlit', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'dp'];
(async () => {
  const b = await chromium.launch({ executablePath: process.env.CHROMIUM || undefined, headless: false, args: ['--headless=new'] });
  const p = await b.newPage({ viewport: { width: 1725, height: 1075 }, deviceScaleFactor: 2 });
  for (const L of LAYERS) {
    await p.goto('file://' + __dirname + '/skin_source.html?layer=' + L);
    await p.waitForFunction(() => window.__ready); await p.waitForTimeout(500);
    await p.screenshot({ path: `out/${L}.png`, omitBackground: true, clip: { x: 0, y: 0, width: 1725, height: 1075 } });
    if (L === 'all') fs.writeFileSync('out/controls.json', JSON.stringify(await p.evaluate(() => window.__controls), null, 1));
  }
  for (const s of SEGS) {
    await p.goto('file://' + __dirname + '/skin_source.html?seg=' + s);
    await p.waitForFunction(() => window.__ready); await p.waitForTimeout(200);
    await p.screenshot({ path: `out/seg_${s}.png`, omitBackground: true, clip: { x: 0, y: 0, width: 88, height: 106 } });
  }
  await b.close();
})();
