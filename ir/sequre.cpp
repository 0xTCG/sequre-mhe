#include "sequre.h"
#include "expr.h"
#include "obsolete/mpc.h"
#include "mhe.h"
// #include "perf/profiler.h"

namespace sequre {

void Sequre::addIRPasses( codon::ir::transform::PassManager *pm, bool debug ) {
  pm->registerPass(std::make_unique<ExpressivenessTransformations>(), debug ? "" : "core-folding-pass-group:2");
  if ( !debug ) {
    pm->registerPass(std::make_unique<MPCOptimizations>(), "sequre-expressiveness-transformation");
    pm->registerPass(std::make_unique<MHEOptimizations>(), "sequre-mpc-opt");
  }
  // pm->registerPass(std::make_unique<Profiler>());
}

} // namespace sequre

extern "C" std::unique_ptr<codon::DSL> load() { return std::make_unique<sequre::Sequre>(); }
