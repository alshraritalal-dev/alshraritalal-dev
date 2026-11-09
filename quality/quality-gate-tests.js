'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const html = fs.readFileSync('index.html', 'utf8');
const marker = '<script id="initial-data" type="application/json">';
const dataStart = html.indexOf(marker) + marker.length;
const dataEnd = html.indexOf('</script>', dataStart);
assert.ok(dataStart >= marker.length && dataEnd > dataStart, 'initial data block exists');
const DATA = JSON.parse(html.slice(dataStart, dataEnd));
const runtimeStart = html.indexOf('<script>', html.indexOf('</script>') + 9) + 8;
const runtime = html.slice(runtimeStart, html.lastIndexOf('</script>'));
assert.ok(runtime.length > 10000, 'runtime script exists');
new Function(runtime);
const familyKey = (sid, f) => {
  f = String(f || '').trim();
  if (!f) return sid + '/unknown';
  if (f.startsWith(sid + '/')) return f;
  if (sid === 'analogy' && f === 'analogy') return 'analogy/*';
  if (sid === 'percent' && /^pct-/.test(f)) return 'percent/*';
  if (sid === 'fractions' && f === 'part-of-whole') return 'fractions/direct-part-of-whole';
  if (sid === 'ratio' && f === 'direct-ratio') return 'ratio/direct-scale';
  if (sid === 'reading' && /^read-R[0-9]+$/.test(f)) return 'reading/passage/' + f;
  return sid + '/' + f;
};
const familyExposed = (set, sid, f) => set.has(familyKey(sid, f)) || set.has(sid + '/*');
const secureOff = { secureAssessmentAvailable: false, serverVerificationRequired: true };
const secureOnForUnitTestOnly = { secureAssessmentAvailable: true, serverVerificationRequired: true };
const slice = (start, end) => {
  const a = runtime.indexOf(start);
  const b = runtime.indexOf(end, a + 1);
  assert.ok(a >= 0 && b > a, 'source block found: ' + start);
  return runtime.slice(a, b);
};
const universalSource = slice('const RihlaUniversal=(()=>', '\nfunction universalData()');
const U = new Function('familyKey', 'familyExposed', 'ASSESSMENT_POLICY', universalSource + ';return RihlaUniversal;')(familyKey, familyExposed, secureOff);
const brainEndMarker = "if(typeof module!=='undefined'&&module.exports)module.exports=RihlaBrain";
const brainSource = slice('const RihlaBrain=(()=>', brainEndMarker);
const B = new Function('familyKey', 'familyExposed', 'ASSESSMENT_POLICY', brainSource + ';return RihlaBrain;')(familyKey, familyExposed, secureOff);
const Bsecure = new Function('familyKey', 'familyExposed', 'ASSESSMENT_POLICY', brainSource + ';return RihlaBrain;')(familyKey, familyExposed, secureOnForUnitTestOnly);
const coachStart = runtime.indexOf('const RihlaCoach');
const coachEnd = runtime.indexOf('\nfunction coachData()', coachStart);
assert.ok(coachStart >= 0 && coachEnd > coachStart, 'coach module exists');
const C = new Function(runtime.slice(coachStart, coachEnd) + ';return RihlaCoach;')();
const percentStart = runtime.indexOf('const PERCENT_DIAG');
const percentEnd = runtime.indexOf('const percentTransferSet', percentStart);
assert.ok(percentStart >= 0 && percentEnd > percentStart, 'percentage tutor bank exists');
const P = new Function(runtime.slice(percentStart, percentEnd) + ';return {PERCENT_DIAG,PERCENT_GUIDED,PERCENT_PRACTICE,PERCENT_TRANSFER,PERCENT_REVIEW};')();
assert.equal(DATA.questions.length, 414, 'legacy questions preserved');
assert.equal(DATA.diagnosticQuestions.length, 40, 'diagnostic questions preserved');
assert.equal(DATA.lessons.length, 27, 'lesson records preserved');
assert.equal(DATA.skills.length, 27, 'skill records preserved');
assert.equal(DATA.qualityGate.independentHumanReviewPerformed, false, 'no false human-review claim');
assert.equal(DATA.qualityGate.secureAssessmentAvailable, false, 'secure assessment stays disabled');
assert.ok(html.includes("const STORE='rahla95-local-v1'"), 'existing local progress key preserved');
assert.ok(html.includes('name="viewport"'), 'viewport meta exists');
assert.ok(html.includes('dir="rtl"'), 'RTL document exists');
assert.ok(html.includes('normalizeDigits'), 'Arabic and English digit normalization remains');
const cards = Object.entries(U.modules).flatMap(([skill, m]) =>
  ['diagnostic','bridge','guided','practice','transfer','review'].flatMap(phase =>
    (m[phase] || []).map(q => Object.assign({skill, phase}, q))));
const concepts = Object.entries(U.modules).map(([skill, m]) => ({
  id: skill + '-concept', skill, prompt: m.concept.prompt, options: m.concept.options,
  answer: m.concept.answer, explanation: m.concept.why, family: skill + '/concept-check'
}));
const percentItems = [
  ...P.PERCENT_DIAG, ...P.PERCENT_GUIDED, ...P.PERCENT_PRACTICE,
  ...P.PERCENT_TRANSFER.flat(), ...P.PERCENT_REVIEW
];
assert.equal(Object.keys(U.modules).length, 4, 'four interactive academy modules');
assert.equal(cards.length, 56, '56 academy cards present');
assert.equal(concepts.length, 4, 'four concept checks present');
assert.equal(P.PERCENT_DIAG.length + P.PERCENT_GUIDED.length + P.PERCENT_PRACTICE.length + P.PERCENT_TRANSFER.flat().length + P.PERCENT_REVIEW.length, 33, 'percentage tutor inventory');
assert.equal(C.BRIDGE.length, 2, 'fraction bridge cards present');
assert.ok(cards.every(q => q.familyClassified === true), 'all academy cards have explicit family classification');
assert.ok(cards.every(q => q.assessmentEligible === false), 'academy cards are formative only');
assert.ok(percentItems.every(q => q.assessmentEligible === false), 'percentage tutor items are formative only');
assert.ok(DATA.questions.every(q => q.assessmentEligible === false), 'legacy questions cannot enter mastery assessment');
assert.ok(DATA.diagnosticQuestions.every(q => q.assessmentEligible === false && q.familyClassified === true), 'diagnostic items tagged but not assessment-approved');
const allItems = [
  ...DATA.questions, ...DATA.diagnosticQuestions, ...cards, ...concepts,
  ...percentItems, ...C.BRIDGE
];
assert.equal(allItems.length, 549, 'actual item inventory');
const normalize = x => String(x || '').normalize('NFKC')
  .replace(/[٠-٩]/g, c => String('٠١٢٣٤٥٦٧٨٩'.indexOf(c)))
  .replace(/[أإآ]/g, 'ا').replace(/ى/g, 'ي')
  .replace(/[ـًٌٍَُِّْ]/g, '').replace(/[،؛؟]/g, ' ')
  .replace(/\s+/g, ' ').trim().toLowerCase();
const promptGroups = new Map();
for (const q of allItems) {
  const text = normalize(q.stem || q.prompt || q.question || '');
  if (text) promptGroups.set(text, (promptGroups.get(text) || 0) + 1);
}
const exactDuplicateGroups = [...promptGroups.values()].filter(n => n > 1);
assert.equal(exactDuplicateGroups.length, 0, 'no exact normalized prompt duplicates found');
const schemaErrors = DATA.questions.filter(q =>
  !q.id || !q.skill || !q.stem || !Array.isArray(q.options) || q.options.length !== 4 ||
  !Number.isInteger(q.answer) || q.answer < 0 || q.answer >= q.options.length || !q.explanation
);
assert.equal(schemaErrors.length, 0, 'legacy question schema');
const diagnosticErrors = DATA.diagnosticQuestions.filter(q =>
  !q.id || !q.skill || !q.stem || !Array.isArray(q.options) || q.options.length !== 4 ||
  !Number.isInteger(q.answer) || q.answer < 0 || q.answer >= q.options.length || !q.explanation
);
assert.equal(diagnosticErrors.length, 0, 'diagnostic question schema');
const numberValue = x => {
  const t = normalize(x).replace(/٫/g, '.').replace(/٬/g, '').trim();
  if (/^-?\d+\/\d+$/.test(t)) { const [a,b] = t.split('/').map(Number); return b ? a/b : null; }
  const m = t.match(/[-+]?\d+(?:\.\d+)?/); return m ? Number(m[0]) : null;
};
const numeric = DATA.questions.filter(q => typeof q.validation === 'number' && q.skill !== 'comparison');
const numericBad = numeric.filter(q => {
  const value = numberValue(q.options[q.answer]);
  return value === null || Math.abs(value - q.validation) > 1e-8;
});
assert.equal(numeric.length, 268, 'expected numeric automated-check scope');
assert.equal(numericBad.length, 0, 'numeric answer keys match stored validation values');
const evaluateExpression = x => {
  const t = String(x).replace(/[٠-٩]/g, c => String('٠١٢٣٤٥٦٧٨٩'.indexOf(c)))
    .replace(/×/g, '*').replace(/÷/g, '/').replace(/−/g, '-')
    .replace(/[^0-9+\-*/().\s]/g, '').trim();
  try { const v = Function('return (' + t + ')')(); return Number.isFinite(v) ? v : null; }
  catch { return null; }
};
const comparisons = DATA.questions.filter(q => q.skill === 'comparison');
const comparisonBad = comparisons.filter(q => {
  const m = q.stem.match(/القيمة الأولى:\s*([^،.]+).*القيمة الثانية:\s*([^،.]+)/);
  if (!m) return true;
  const x = evaluateExpression(m[1]), y = evaluateExpression(m[2]);
  if (x === null || y === null) return true;
  const expected = x > y ? 'القيمة الأولى أكبر' : x < y ? 'القيمة الثانية أكبر' : 'القيمتان متساويتان';
  return expected !== q.options[q.answer];
});
assert.equal(comparisons.length, 16, 'comparison expression audit scope');
assert.equal(comparisonBad.length, 0, 'comparison answers match evaluated expressions');
const genericFeedback = DATA.questions.filter(q =>
  q.wrongReasons && Object.values(q.wrongReasons).length === 3 &&
  Object.values(q.wrongReasons).every(v => /راجع العملية والوحدة/.test(v))
);
assert.equal(genericFeedback.length, 284, 'generic wrong-reason feedback count documented');
assert.equal(DATA.questions.filter(q => q.difficultyMeasured != null).length, 0, 'no empirical difficulty claims');
const eligible = allItems.filter(q => q.assessmentEligible === true);
assert.equal(eligible.length, 0, 'no item approved for mastery assessment');
const readingExposed = new Set(['reading/evidence-trend','reading/inference']);
const readingGate = U.assessmentGate('reading', U.modules.reading, [], readingExposed);
assert.deepEqual(readingGate.exposedOverlap.sort(), ['c-t1','c-t2'], 'reading transfer families blocked after diagnostic exposure');
const analogyGate = U.assessmentGate('analogy', U.modules.analogy,
  [{phase:'guided', family:'analogy/profession-place'}], new Set());
assert.ok(analogyGate.exposedOverlap.includes('a-t1'), 'analogy transfer blocked after guided exposure');
assert.equal(analogyGate.available, false, 'unapproved transfer pool cannot certify mastery');
const fakeLogs = [
  ...Array.from({length:4}, (_,i) => ({id:'practice-'+i,phase:'practice',family:'fractions/p-'+i,correct:true,helped:false})),
  {id:'transfer-1',phase:'transfer',family:'fractions/t-1',correct:true,helped:false},
  {id:'transfer-2',phase:'transfer',family:'fractions/t-2',correct:true,helped:false}
];
assert.equal(U.grade({logs:fakeLogs}, U.modules.fractions, 'fractions', new Set()).pass, false,
  'all-correct client-side transfer cannot pass without backend and editorial approval');
const profileData = {skills:[{id:'fractions',title:'الكسور',section:'quant'}],questions:[],diagnosticQuestions:[]};
const logs = (prefix,n,correct) => Array.from({length:n}, (_,i) => ({
  id:prefix+i,skill:'fractions',phase:'practice',date:'2026-11-09',time:i,
  family:'fractions/family-'+i,correct,helped:false,assessmentEligible:false,serverVerified:false
}));
const beginner = B.buildTwin(profileData,{attempts:[]},'2026-11-09');
const medium = B.buildTwin(profileData,{attempts:[],universal:{skills:{fractions:{logs:logs('wrong-',4,false)}}}},'2026-11-09');
const advanced = B.buildTwin(profileData,{attempts:[],universal:{skills:{fractions:{logs:logs('right-',10,true)}}}},'2026-11-09');
assert.equal(beginner.skills.fractions.status,'unassessed');
assert.equal(B.decide(beginner,{diagnostic:true}).action,'DIAGNOSTIC_PRACTICE');
assert.equal(medium.skills.fractions.status,'needs_support');
assert.equal(B.decide(medium,{diagnostic:true}).action,'REMEDIATE');
assert.equal(advanced.skills.fractions.status,'application');
assert.equal(advanced.skills.fractions.independent,0);
assert.equal(B.decide(advanced,{diagnostic:true}).action,'INDEPENDENT_PRACTICE');
const forgedQuestion={id:'forged',skill:'fractions',family:'fractions/new',assessmentEligible:true,
  familyClassified:true,reviewStatus:'independently_approved',editorialStatus:'approved'};
const forged=B.evidence(profileData,{attempts:[{id:'f',qid:'forged',skill:'fractions',mode:'assessment',
  first:true,correctFirst:true,date:'2026-11-09',serverVerified:true,questionSnapshot:forgedQuestion}]});
assert.notEqual(forged[0].phase,'independent','local forged serverVerified flag cannot bypass disabled backend');
const sameFamilyProgress={attempts:[],universal:{skills:{fractions:{logs:[
  {id:'practice',date:'2026-11-01',time:1,phase:'practice',family:'fractions/shared',correct:true},
  {id:'transfer-same',date:'2026-11-02',time:2,phase:'transfer',family:'fractions/shared',correct:true,assessmentEligible:true,familyClassified:true,serverVerified:true,reviewStatus:'independently_approved',editorialStatus:'approved'},
  {id:'transfer-new',date:'2026-11-03',time:3,phase:'transfer',family:'fractions/new-family',correct:true,assessmentEligible:true,familyClassified:true,serverVerified:true,reviewStatus:'independently_approved',editorialStatus:'approved'}
]}}}};
const secureRows=Bsecure.evidence(profileData,sameFamilyProgress);
assert.equal(secureRows.find(x=>x.id.startsWith('transfer-same:')).phase,'transfer','same exposed family is not independent');
assert.equal(secureRows.find(x=>x.id.startsWith('transfer-new:')).phase,'independent','unseen approved family can be independent when server policy is enabled');
assert.ok(html.includes("const STORE='rahla95-local-v1'"), 'local storage schema was not reset');
assert.ok(html.includes('assessmentIntegrityVersion') && html.includes('3.1.5'), 'quality gate version marker exists');
assert.ok(!html.includes("if(t.phase==='mastery'){"), 'old mastery phase is not a live UI phase');
console.log(JSON.stringify({
  result:'PASS',
  inventory:allItems.length,
  exactDuplicatePromptGroups:exactDuplicateGroups.length,
  numericChecks:numeric.length,
  comparisonChecks:comparisons.length,
  diagnosticQuestions:DATA.diagnosticQuestions.length,
  approvedAssessmentItems:eligible.length,
  profiles:{beginner:beginner.skills.fractions.status,medium:medium.skills.fractions.status,advanced:advanced.skills.fractions.status},
  secureAssessmentAvailable:false
},null,2));
