// Reproduce reference pixels from real upstream web components, never CSS copies.
import { build } from 'esbuild';
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve, extname, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = dirname(fileURLToPath(import.meta.url));
const out = resolve(process.env.MD3_REFERENCE_OUT || resolve(root, '../../docs/images/md3'));
await mkdir(out, {recursive:true});
await mkdir(resolve(out, 'web-components'), {recursive:true});
await build({entryPoints:[resolve(root,'entry.js')], bundle:true, format:'esm', outfile:resolve(root,'build/bundle.js')});
const mime={'.html':'text/html','.js':'text/javascript','.css':'text/css','.woff2':'font/woff2','.woff':'font/woff'};
const server=createServer(async (req,res)=>{
 try {
  const path=resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://localhost').pathname));
  if(!path.startsWith(root+'/')) { res.writeHead(403);res.end();return; }
  res.setHeader('Content-Type',mime[extname(path)]||'application/octet-stream');res.end(await readFile(path));
 }catch{res.writeHead(404);res.end();}
});
await new Promise(ok=>server.listen(0,'127.0.0.1',ok));
const launchOptions={headless:true};
if(process.env.CHROMIUM_PATH) launchOptions.executablePath=process.env.CHROMIUM_PATH;
const browser=await chromium.launch(launchOptions);
const context=await browser.newContext({viewport:{width:1000,height:1000},deviceScaleFactor:1,reducedMotion:'reduce'});
const page=await context.newPage();
const errors=[];
page.on('pageerror',e=>errors.push(String(e)));
const manifest={materialWeb:'2.5.0',sourceCommit:'37aa14b843dcaeb3a46f898679fa6fcd109b1905',fontsourceRoboto:'5.2.8',browser:browser.version(),deviceScaleFactor:1,screenshots:[]};
try{
 for(const theme of ['light','dark']) for(const group of ['actions','inputs','layout','feedback','dialog']){
  await page.goto(`http://127.0.0.1:${server.address().port}/reference.html?theme=${theme}&group=${group}`);
  await page.waitForFunction(()=>window.fixtureReady);
  await page.evaluate(async()=>{
   const tags=[...new Set([...document.querySelectorAll('*')].map(e=>e.localName).filter(n=>n.startsWith('md-')))];
   await Promise.all(tags.map(t=>customElements.whenDefined(t)));
   // Lit child updates can themselves schedule updates; settle the complete tree.
   for(let n=0;n<3;n++){
    const visit=root=>[...root.querySelectorAll('*')].flatMap(el=>[el,...(el.shadowRoot?visit(el.shadowRoot):[])]);
    await Promise.all(visit(document).map(el=>el.updateComplete));
   }
   await document.fonts.ready;
  });
  await page.waitForTimeout(350);
  await page.mouse.move(990,990);
  const file=`web-${group}-${theme}.png`;
  await page.screenshot({path:resolve(out,file),fullPage:true,animations:'disabled'});
  const probes=await page.locator('[data-probe]').all();
  const bounds=[];
  for(const probe of probes){
   const name=await probe.getAttribute('data-probe');
   const box=await probe.boundingBox();
   const crop=`web-components/${name}-${theme}.png`;
   await probe.screenshot({path:resolve(out,crop),animations:'disabled'});
   bounds.push({name,box,crop});
  }
  if(group==='feedback'){
   const menu=page.locator('#reference-menu');
   await menu.screenshot({path:resolve(out,`web-components/menu-${theme}.png`),animations:'disabled'});
  }
  if(group==='dialog'){
   await page.locator('#reference-dialog').screenshot({path:resolve(out,`web-components/dialog-${theme}.png`),animations:'disabled'});
  }
  manifest.screenshots.push({theme,group,file,viewport:{width:1000,height:1000},probes:bounds});
 }
 if(errors.length) throw new Error(errors.join('\n'));
 await writeFile(resolve(out,'web-reference-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
 console.log(`Captured ${manifest.screenshots.length} full reference pages and component crops in ${out}`);
}finally{await browser.close();await new Promise(ok=>server.close(ok));}
