text = """
//------------------------------------------------------------------------------
// Explicit instantiation.
//------------------------------------------------------------------------------
#include "Geometry/Dimension.hh"
#include "FSISPH/FSISpecificThermalEnergyPolicy.cc"

namespace Spheral {
  template class FSISpecificThermalEnergyPolicy<Dim< %(ndim)s > >;
}
"""