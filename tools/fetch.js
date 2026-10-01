const fs=require('fs');
const url='https://raw.githubusercontent.com/archowdren/archowdren.github.io/main/cmdtool.zip';
(async()=>{
  const r=await fetch(url);
  console.log('status',r.status);
  const b=Buffer.from(await r.arrayBuffer());
  fs.writeFileSync(process.argv[2], b);
  console.log('wrote', process.argv[2], b.length, 'bytes');
})().catch(e=>{console.error('ERR',e.message);process.exit(1);});
