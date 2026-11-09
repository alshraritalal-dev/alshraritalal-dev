'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const { chromium } = require('playwright');
const server = http.createServer((req,res) => {
  res.writeHead(200, {'content-type':'text/html; charset=utf-8'});
  fs.createReadStream('index.html').pipe(res);
});
(async () => {
  await new Promise(resolve => server.listen(4173,'127.0.0.1',resolve));
  const browser = await chromium.launch({headless:true});
  const results = [];
  try {
    for (const viewport of [
      {width:1365,height:900,name:'desktop'},
      {width:390,height:844,name:'mobile-390'},
      {width:360,height:800,name:'mobile-360'}
    ]) {
      const page = await browser.newPage({viewport:{width:viewport.width,height:viewport.height}});
      const pageErrors = [];
      page.on('pageerror', e => pageErrors.push(e.message));
      await page.goto('http://127.0.0.1:4173', {waitUntil:'domcontentloaded'});
      await page.waitForTimeout(250);
      const initialText = await page.locator('body').innerText();
      assert.ok(initialText.trim().length > 20, viewport.name + ': app renders');
      const pages = ['home','brain','coach','academy','practice','mock','settings'];
      for (const name of pages) {
        const item = page.locator('[data-page="' + name + '"]').first();
        if (await item.count()) {
          await item.click({force:true});
          await page.waitForTimeout(80);
        }
      }
      const layout = await page.evaluate(() => ({
        width: document.documentElement.scrollWidth,
        viewport: window.innerWidth,
        height: document.documentElement.scrollHeight
      }));
      assert.ok(layout.width <= layout.viewport + 3,
        viewport.name + ': horizontal overflow ' + layout.width + ' > ' + layout.viewport);
      assert.deepEqual(pageErrors, [], viewport.name + ': uncaught browser errors');
      results.push({viewport:viewport.name,layout,errors:pageErrors.length});
      await page.close();
    }
    console.log(JSON.stringify({result:'PASS',runs:results},null,2));
  } finally {
    await browser.close();
    server.close();
  }
})().catch(e => { console.error(e); process.exitCode=1; });
