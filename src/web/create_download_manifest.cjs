const fs = require('node:fs'), path = require('node:path');
const root = path.resolve(process.argv[2]);
const facePackage = fs.existsSync(path.join(root,'additionalface.zip'))
  ? 'additionalface.zip' : 'Data/Face.zip';
const manifest = {format:1, basic:[
  {kind:'zip', file:'System.zip', target:'System', roots:['System']},
  {kind:'zip', file:'Data.zip', target:'Data', roots:['Data']},
  {kind:'zip', file:'Scenario.zip', target:'Scenario', roots:['Scenario']}
], scenarios:[], faces:[{kind:'zip', file:facePackage, target:'Data/Face', roots:['Data/Face','Face','additionalface']}]};
for (const name of fs.readdirSync(path.join(root,'Yado')).sort()) {
  if (!/\.zip$/i.test(name)) continue;
  const inn = name.slice(0,-4);
  // 宿アーカイブに最上位の宿ディレクトリーが重複する場合がある
  // （Py_C宿/Py_C宿/Environment.xml
  // など）。通常の単一ルートの処理より先にその重複を除き、UserData/Yado/<
  // 宿名>/ の直下へ導入する。
  manifest.basic.push({kind:'zip', file:`Yado/${name}`, target:`Yado/${inn}`, roots:[`Yado/${inn}`,`${inn}/${inn}`,inn]});
}
function walk(relative) {
  for (const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
    const file = `${relative}/${entry.name}`;
    if (entry.isDirectory()) walk(file);
    else if (entry.isFile()) manifest.scenarios.push({kind:'file',file,target:file});
  }
}
walk('Scenario');
for (const item of [...manifest.basic,...manifest.faces])
  if (!fs.statSync(path.join(root,item.file)).isFile()) throw new Error(`Missing ${item.file}`);
fs.writeFileSync(path.join(root,'manifest.json'),JSON.stringify(manifest,null,2));
console.log(`Download manifest: basic=${manifest.basic.length}, scenarios=${manifest.scenarios.length}, faces=${manifest.faces.length}`);
