#include <tatarus/kidney.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "Assertion failed: " << msg << std::endl;
        throw std::runtime_error(msg);
    }
}

void testKidneyBasicsAndStarlingGfr() {
    tatarus::organism::Kidney kidney;
    tatarus::organism::SoluteProfile plasma;
    tatarus::organism::EndocrineProfile hormones;

    double waterMl = 0.0, naMmol = 0.0, kMmol = 0.0, caMmol = 0.0;
    double clMmol = 0.0, hco3Mmol = 0.0, ureaMmol = 0.0, glucoseMmol = 0.0;

    kidney.step(60.0, 93.3, plasma, hormones, waterMl, naMmol, kMmol, caMmol, clMmol, hco3Mmol, ureaMmol, glucoseMmol);

    const auto& t = kidney.telemetry();
    require(t.available, "Kidney telemetry must be available");
    std::cout << "DEBUG GFR=" << t.glomerularFiltrationRateMlPerMin << " mL/min, Urine=" << t.urineOutputRateMlPerMin << " mL/min" << std::endl;
    require(t.glomerularFiltrationRateMlPerMin >= 90.0 && t.glomerularFiltrationRateMlPerMin <= 140.0,
            "GFR should be physiological (~120-125 mL/min)");
    require(t.urineOutputRateMlPerMin >= 0.5 && t.urineOutputRateMlPerMin <= 2.5,
            "Urine output rate should be ~1-2 mL/min");
    require(glucoseMmol == 0.0, "Normoglycemic urine must not contain glucose (complete reabsorption)");
    require(waterMl > 0.0 && naMmol > 0.0 && kMmol > 0.0 && ureaMmol > 0.0,
            "Kidney must excrete water, sodium, potassium, and urea");
    std::cout << "testKidneyBasicsAndStarlingGfr: PASS" << std::endl;
}

void testKidneyGlucoseGlucosuriaThreshold() {
    tatarus::organism::Kidney kidney;
    tatarus::organism::SoluteProfile diabeticPlasma;
    diabeticPlasma.glucoseMm = 20.0; // Severe hyperglycemia (~360 mg/dL)
    tatarus::organism::EndocrineProfile hormones;

    double waterMl = 0.0, naMmol = 0.0, kMmol = 0.0, caMmol = 0.0;
    double clMmol = 0.0, hco3Mmol = 0.0, ureaMmol = 0.0, glucoseMmol = 0.0;

    kidney.step(60.0, 93.3, diabeticPlasma, hormones, waterMl, naMmol, kMmol, caMmol, clMmol, hco3Mmol, ureaMmol, glucoseMmol);

    require(glucoseMmol > 0.0, "Hyperglycemia exceeding Tm must result in glucosuria");
    std::cout << "testKidneyGlucoseGlucosuriaThreshold: PASS" << std::endl;
}

void testKidneyRaasActivationUnderHypotension() {
    tatarus::organism::Kidney kidneyNorm;
    tatarus::organism::Kidney kidneyHypo;
    tatarus::organism::SoluteProfile plasma;
    tatarus::organism::EndocrineProfile hormones;

    double w1 = 0.0, na1 = 0.0, k1 = 0.0, ca1 = 0.0, cl1 = 0.0, h1 = 0.0, u1 = 0.0, g1 = 0.0;
    double w2 = 0.0, na2 = 0.0, k2 = 0.0, ca2 = 0.0, cl2 = 0.0, h2 = 0.0, u2 = 0.0, g2 = 0.0;

    // Normal pressure (MAP 93.3) vs Hypotension (MAP 60.0)
    kidneyNorm.step(60.0, 93.3, plasma, hormones, w1, na1, k1, ca1, cl1, h1, u1, g1);
    kidneyHypo.step(60.0, 60.0, plasma, hormones, w2, na2, k2, ca2, cl2, h2, u2, g2);

    require(kidneyHypo.reninSecretionRate() > kidneyNorm.reninSecretionRate(),
            "Hypotension must stimulate renin secretion by JGA");
    require(kidneyHypo.urineOutputRateMlPerMin() < kidneyNorm.urineOutputRateMlPerMin(),
            "Hypoperfusion must reduce urine output (oliguria to conserve fluid)");
    std::cout << "testKidneyRaasActivationUnderHypotension: PASS" << std::endl;
}

void testBilateralKidneysAreIndependentAndConserveTotals() {
    tatarus::organism::BilateralKidneys kidneys;
    tatarus::organism::SoluteProfile plasma;
    tatarus::organism::EndocrineProfile hormones;
    double water = 0.0, na = 0.0, k = 0.0, ca = 0.0;
    double cl = 0.0, hco3 = 0.0, urea = 0.0, glucose = 0.0;
    kidneys.step(60.0, 93.3, plasma, hormones, water, na, k, ca, cl, hco3, urea, glucose);

    const double leftGfr = kidneys.left().gfrMlPerMin();
    const double rightGfr = kidneys.right().gfrMlPerMin();
    require(leftGfr > 0.0 && rightGfr > 0.0, "Both anatomical kidneys must be active");
    require(std::abs(kidneys.gfrMlPerMin() - (leftGfr + rightGfr)) < 1e-9,
            "Aggregate GFR must equal left plus right GFR");

    kidneys.setRelativeFunction(0.0, 1.0);
    kidneys.step(60.0, 93.3, plasma, hormones, water, na, k, ca, cl, hco3, urea, glucose);
    require(kidneys.left().gfrMlPerMin() == 0.0, "Left nephrectomy must remove left GFR");
    require(kidneys.right().gfrMlPerMin() > 0.0, "Right kidney must remain functional");
    require(kidneys.gfrMlPerMin() == kidneys.right().gfrMlPerMin(),
            "Unilateral aggregate must equal the remaining kidney");
    std::cout << "testBilateralKidneysAreIndependentAndConserveTotals: PASS" << std::endl;
}

} // namespace

int main() {
    try {
        testKidneyBasicsAndStarlingGfr();
        testKidneyGlucoseGlucosuriaThreshold();
        testKidneyRaasActivationUnderHypotension();
        testBilateralKidneysAreIndependentAndConserveTotals();
        std::cout << "TATARUS Kidney tests: ALL PASS" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Kidney test failed: " << e.what() << std::endl;
        return 1;
    }
}
