//---------------------------------Spheral++----------------------------------//
// Physics -- dampled linear spring contact model Spheral++
//----------------------------------------------------------------------------//
#include "DEM/HerzianDEM.hh"

#include "DataBase/State.hh"
#include "DataBase/StateDerivatives.hh"
#include "DataBase/DataBase.hh"
#include "DataBase/IncrementFieldList.hh"
#include "Field/FieldList.hh"
#include "Neighbor/ConnectivityMap.hh"
#include "Hydro/HydroFieldNames.hh"
#include "DEM/DEMFieldNames.hh"
#include "DEM/DEMDimension.hh"

#ifdef _OPENMP
#include "omp.h"
#endif

#include <cmath>
#include <limits>
namespace Spheral {

//------------------------------------------------------------------------------
// Default constructor
//------------------------------------------------------------------------------
template<typename Dimension>
HerzianDEM<Dimension>::
HerzianDEM(const DataBase<Dimension>& dataBase,
           const Scalar YoungsModulus,
           const Scalar restitutionCoefficient,
           const Scalar stepsPerCollision,
           const Vector& xmin,
           const Vector& xmax):
  DEMBase<Dimension>(dataBase,stepsPerCollision,xmin,xmax),
  mYoungsModulus(YoungsModulus),
  mRestitutionCoefficient(restitutionCoefficient) {
      mBeta = 3.14159/std::log(restitutionCoefficient);
}

//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
template<typename Dimension>
HerzianDEM<Dimension>::
~HerzianDEM() {}


//------------------------------------------------------------------------------
// time step 
//------------------------------------------------------------------------------
template<typename Dimension>
typename HerzianDEM<Dimension>::TimeStepType
HerzianDEM<Dimension>::
dt(const DataBase<Dimension>& dataBase,
   const State<Dimension>& state,
   const StateDerivatives<Dimension>& /*derivs*/,
   const Scalar /*currentTime*/) const {
  
  const auto& mass = state.fields(HydroFieldNames::mass, 0.0); 
  const auto& radius = state.fields(DEMFieldNames::particleRadius,0.0);

  const auto numNodeLists = dataBase.numNodeLists();
  const auto Y2eff = 16.0/9.0*mYoungsModulus*mYoungsModulus;        
  const auto pi = 3.14159265358979323846;
  auto minContactTime = std::numeric_limits<double>::max();

  for (auto nodeListi = 0u; nodeListi < numNodeLists; ++nodeListi) {
  const auto& nodeList = mass[nodeListi]->nodeList();
  const auto ni = nodeList.numInternalNodes();

  #pragma omp parallel
  {
    auto minContactTime_thread = std::numeric_limits<double>::max();
    #pragma omp parallel for
      for (auto i = 0u; i < ni; ++i) {
          const auto mi = mass(nodeListi,i);
          const auto Ri = radius(nodeListi,i);
          const auto k2 = Y2eff*Ri;
          minContactTime_thread = min(minContactTime_thread,mi*mi/k2);
      }

    #pragma omp critical
    if (minContactTime_thread < minContactTime) minContactTime = minContactTime_thread;

  }
  }

  minContactTime = pi*std::pow(minContactTime,0.25);
  return std::make_pair(minContactTime/this->stepsPerCollision(),"Herzian DEM vote for time-step");
}

//------------------------------------------------------------------------------
// get our acceleration and other things
//------------------------------------------------------------------------------
template<typename Dimension>
void
HerzianDEM<Dimension>::
evaluateDerivatives(const typename Dimension::Scalar /*time*/,
                    const typename Dimension::Scalar /*dt*/,
                    const DataBase<Dimension>& dataBase,
                    const State<Dimension>& state,
                          StateDerivatives<Dimension>& derivatives) const{

  // A few useful constants we'll use in the following loop.
  //const double tiny = std::numeric_limits<double>::epsilon();
  //auto minContactTime = std::numeric_limits<double>::max();
  const auto fourOverOnePlusBetaSquared = 4.0/(1.0+mBeta*mBeta);
  const auto Y = 4.0/3.0*mYoungsModulus;

  // The connectivity.
  const auto& connectivityMap = dataBase.connectivityMap();
  const auto& nodeLists = connectivityMap.nodeLists();
  const auto numNodeLists = nodeLists.size();
  const auto& pairs = connectivityMap.nodePairList();
  const auto  npairs = pairs.size();
  
  // Get the state and derivative FieldLists.
  // State FieldLists.
  const auto mass = state.fields(HydroFieldNames::mass, 0.0);
  const auto position = state.fields(HydroFieldNames::position, Vector::zero);
  const auto velocity = state.fields(HydroFieldNames::velocity, Vector::zero);
  const auto omega = state.fields(DEMFieldNames::angularVelocity, DEMDimension<Dimension>::zero);
  const auto radius = state.fields(DEMFieldNames::particleRadius, 0.0);

  CHECK(mass.size() == numNodeLists);
  CHECK(position.size() == numNodeLists);
  CHECK(velocity.size() == numNodeLists);
  CHECK(radius.size() == numNodeLists);
  CHECK(omega.size() == numNodeLists);

  auto  DxDt = derivatives.fields(IncrementFieldList<Dimension, Scalar>::prefix() + HydroFieldNames::position, Vector::zero);
  auto  DvDt = derivatives.fields(HydroFieldNames::hydroAcceleration, Vector::zero);
  auto  DomegaDt = derivatives.fields(IncrementFieldList<Dimension, Scalar>::prefix() + DEMFieldNames::angularVelocity, DEMDimension<Dimension>::zero);

  CHECK(DxDt.size() == numNodeLists);
  CHECK(DvDt.size() == numNodeLists);
  CHECK(DomegaDt.size() == numNodeLists);


#pragma omp parallel
  {
    // Thread private scratch variables
    int i, j, nodeListi, nodeListj;

    typename SpheralThreads<Dimension>::FieldListStack threadStack;
    auto DvDt_thread = DvDt.threadCopy(threadStack);

#pragma omp for
    for (auto kk = 0u; kk < npairs; ++kk) {

      i = pairs[kk].i_node;
      j = pairs[kk].j_node;
      nodeListi = pairs[kk].i_list;
      nodeListj = pairs[kk].j_list;
      
      // Get the state for node i.
      const auto& ri = position(nodeListi, i);
      const auto& mi = mass(nodeListi, i);
      const auto& vi = velocity(nodeListi, i);
      const auto& Ri = radius(nodeListi, i);
      
      auto& DvDti = DvDt_thread(nodeListi, i);

      // Get the state for node j
      const auto& rj = position(nodeListj, j);
      const auto& mj = mass(nodeListj, j);
      const auto& vj = velocity(nodeListj, j);
      const auto& Rj = radius(nodeListj, j);

      auto& DvDtj = DvDt_thread(nodeListj, j);
      
      CHECK(mi > 0.0);
      CHECK(mj > 0.0);
      CHECK(Ri > 0.0);
      CHECK(Rj > 0.0);

      // are we overlapping ? 
      const auto rij = ri-rj;
      const auto delta = (Ri+Rj) - std::sqrt(rij.dot(rij)); 
      
      // if so do the things
      if (delta > 0.0){

        const auto vij = vi-vj;
        const auto rhatij = rij.unitVector();

        // effective quantities
        const auto mij = (mi*mj)/(mi+mj);
        const auto Rij = (Ri*Rj)/(Ri+Rj);

        // normal force w/ Herzian spring constant
        const auto c1 = Y*std::sqrt(Rij);
        const auto c2 = std::sqrt(mij*c1*fourOverOnePlusBetaSquared);
        const auto vn = vij.dot(rhatij);

        // normal force
        const auto f = c1*std::sqrt(delta*delta*delta) - c2*vn;
        DvDti += f/mi*rhatij;
        DvDtj -= f/mj*rhatij;
      }
    } // loop over pairs
    // Reduce the thread values to the master.
    threadReduceFieldLists<Dimension>(threadStack);
  }   // OpenMP parallel region

  
  for (auto nodeListi = 0u; nodeListi < numNodeLists; ++nodeListi) {
    const auto& nodeList = mass[nodeListi]->nodeList();

    const auto ni = nodeList.numInternalNodes();
#pragma omp parallel for
    for (auto i = 0u; i < ni; ++i) {
        const auto veli = velocity(nodeListi,i);
        DxDt(nodeListi,i) = veli;
    }
  }

}


}