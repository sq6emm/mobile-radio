// Render an HTML file to PDF with the pre-installed Chromium (Playwright).
// Prints A4 with page numbers; diagrams are inline SVG, so nothing is fetched.
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const [html, pdf, title] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + path.resolve(html), { waitUntil: 'networkidle', timeout: 60000 });
  await page.pdf({
    path: pdf, format: 'A4', printBackground: true,
    margin: { top: '20mm', bottom: '18mm', left: '17mm', right: '17mm' },
    displayHeaderFooter: true,
    headerTemplate: `<div style="font-size:8px;color:#666;width:100%;padding:0 17mm;display:flex;justify-content:space-between;">
       <span>${title}</span><span class="date"></span></div>`,
    footerTemplate: `<div style="font-size:8px;color:#666;width:100%;padding:0 17mm;display:flex;justify-content:space-between;">
       <span>Review draft</span><span>page <span class="pageNumber"></span> / <span class="totalPages"></span></span></div>`,
  });
  await browser.close();
})();
