# مواصفات بنك التقييم الخاص — غير منفذ بعد

## البنية المقترحة
- خدمة API خاصة (مثل Cloudflare Workers) وقاعدة D1 خاصة، أو خدمة خادمية مكافئة.
- جداول: users, sessions, skills, question_items, question_families, assessment_pools, attempts, mastery_events, editorial_reviews.
- تخزين الإجابات الصحيحة والشرح الحساس في قاعدة البيانات فقط؛ لا تُرسل للمتصفح قبل التسليم.
- لكل سؤال: skill_id, family_id, version, editorial_status, difficulty_estimate, difficulty_measured, source, reviewed_by, reviewed_at.

## دورة التقييم
1. ينشئ الخادم جلسة اختبار بمعرّف عشوائي وانتهاء زمني.
2. يختار الخادم سؤالًا من بنك معتمد لم يسبق كشف عائلته للطالب.
3. يرسل نص السؤال والخيارات فقط، دون answer key أو explanation.
4. يستقبل الإجابة ويصححها خادميًا.
5. يسجل الخادم question_id وfamily_id والوقت والمساعدة ونتيجة التصحيح.
6. يرسل نتيجة مختصرة وتفسيرًا بعد انتهاء الاختبار، وفق سياسة المحتوى.
7. عقل رحلة يقرأ ملخصًا موقعًا/متحققًا منه من الخادم؛ لا يثق بعلم serverVerified محفوظ محليًا.

## ضوابط لازمة
- لا تستخدم localStorage كمصدر حقيقة للإتقان.
- لا تضع مفاتيح الإجابة في JavaScript أو ملفات عامة أو source maps.
- لا تخزن بنك التقييم في مستودع GitHub عام.
- لا تعِد استخدام عائلة مكشوفة في تقييم مستقل.
- سجل مراجعتين تحريريّتين مستقلتين قبل اعتماد السؤال.
- أضف rate limits، تحقق جلسات، صلاحيات، audit logs، وتشفيرًا للبيانات.
- اختبر تسرب answer key من HTML/JS/network responses قبل فتح التقييم.

## بوابة التفعيل
لا تغير ASSESSMENT_POLICY.secureAssessmentAvailable إلى true لمجرد وجود API. يجب أن توجد خدمة خاصة عاملة واختبارات تسرب ناجحة واعتماد تحريري فعلي.