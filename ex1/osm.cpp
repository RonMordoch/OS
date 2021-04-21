//
// Created by Ron & Yarden on 27-Mar-20.
//

#include <sys/time.h>
#include "osm.h"
#include <cstddef>


/**
 * Returns the average time it takes to perform operation
 * @param t1 start time
 * @param t2 finish time
 * @param iterations number of iterations
 * @return
 */
double average_time(const timeval &start, const timeval &end, const unsigned int iterations)
{

    return ((end.tv_sec - start.tv_sec) * 1000000000.0 + (end.tv_usec - start.tv_usec) * 1000.0) /
           (iterations * 3.0);
}

/**
 * Empty function.
 */
void empty_function()
{}


/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time(unsigned int iterations)
{
    timeval start, end;
    if (iterations == 0)
    {
        return -1;
    }
    unsigned int r = 0, o = 0, n = 0;
    if (gettimeofday(&start, NULL) == -1)
    {
        return -1;
    }
    for (unsigned int i = 0; i < iterations; ++i)
    {
        r++;
        o++;
        n++;
    }
    if (gettimeofday(&end, NULL) == -1)
    {
        return -1;
    }
    return average_time(start, end, iterations);
}


/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time(unsigned int iterations)
{
    timeval start,end;
    if (iterations == 0)
    {
        return -1;
    }
    if (gettimeofday(&start, NULL) == -1)
    {
        return -1;
    }
    for (unsigned int i = 0; i < iterations; ++i)
    {
        empty_function();
        empty_function();
        empty_function();

    }
    if (gettimeofday(&end, NULL) == -1)
    {
        return -1;
    }
    return average_time(start, end, iterations);
}

/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time(unsigned int iterations)
{
    timeval start, end;
    if (iterations == 0)
    {
        return -1;
    }
    if (gettimeofday(&start, NULL) == -1)
    {
        return -1;
    }
    for (unsigned int i = 0; i < iterations; ++i)
    {
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
    }
    if (gettimeofday(&end, NULL) == -1)
    {
        return -1;
    }
    return average_time(start, end, iterations);
}