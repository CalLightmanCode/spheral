#include "DeviceTest.hh"

#include "RAJA/RAJA.hpp"
#include "chai/ManagedArray.hpp"

#include<stdio.h>

namespace Spheral
{

RAJA_HOST_DEVICE void add(int a, int b, int *c)
{
  *c = a + b;
}

int launchCaller(int a, int b)
{
  chai::ManagedArray<int> c(1);

#ifdef RAJA_ENABLE_CUDA
  using EXEC_POL=RAJA::cuda_exec<256>;
#else
  using EXEC_POL=RAJA::seq_exec;
#endif
  
  RAJA::forall<EXEC_POL>(RAJA::RangeSegment(0,1),
    [=] RAJA_HOST_DEVICE (int i) {
      add(a,b,&c[0]);
    }
  );

  return c[0];
}

} // namespace Spehral