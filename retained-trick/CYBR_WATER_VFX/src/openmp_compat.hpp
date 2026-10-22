#pragma once
#ifdef _OPENMP
#include <omp.h>
#else
// Existing OpenMP loops execute serially when pragmas are ignored.
inline void omp_set_num_threads(int) {}
inline int omp_get_thread_num() { return 0; }
#endif
