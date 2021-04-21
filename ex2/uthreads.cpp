//
// Created by Ron & Yarden on 12-Apr-20.
//

#include "uthreads.h"
#include <list>
#include "Thread.h"
#include <cstdlib>
#include <iostream>
#include <signal.h>
# include <sys/time.h>

#define SYS_ERR "system error: "
#define LIB_ERR "thread library error: "

// Library fields
static int *quantums_by_priority; // Quantums array
static Thread *threads[MAX_THREAD_NUM] = {nullptr}; // All existing library threads
static std::list<Thread *> ready_queue; // List of all threads that are waiting in READY state
static int curr_thread; // ID of current thread, i.e. the running thread
static int quantums_counter; // Library quantums counter
static struct sigaction sa; // Time handler
static struct itimerval timer; // Timer
static sigset_t set; // Set of signals to block/unblock
static int max_priority; // Maximum priority possbile


/**
 * Blocks all signals.
 * @return -1 is there was an error blocking the signals; 0 otherwise.
 */
int block_signals()
{
    if (sigprocmask(SIG_BLOCK, &set, NULL) < 0)
    {
        std::cerr << SYS_ERR << "Sigprocmask Error: Blocking failed\n";
        return -1;
    }
    return 0;
}

/**
 * Unlocks all signals.
 * @return -1 is there was an error unblocking the signals; 0 otherwise.
 */
int unblock_signals()
{
    if (sigprocmask(SIG_UNBLOCK, &set, NULL) < 0)
    {
        std::cerr << SYS_ERR << "Sigprocmask Error: Unblocking failed\n";
        return -1;
    }
    return 0;
}


/**
 * @param tid, thread id
 * @return true if the tid is invalid; false if tid is valid.
 */
int tid_invalidity(int tid)
{
    return (threads[tid] == nullptr || tid < 0 || tid > MAX_THREAD_NUM - 1);
}

/**
 * Rests the timer and sets it to run per the current thread's priority.
 * @return -1 if there was an error resetting the timer; 0 otherwise.
 */
int reset_timer()
{
    // Configure the timer to expire per priority
    timer.it_value.tv_sec = 0;        // first time interval, seconds part
    timer.it_value.tv_usec = quantums_by_priority[threads[curr_thread]->get_priority()];
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        std::cerr << SYS_ERR << "Problem with setting timer.\n";
        return -1;
    }
    return 0;
}

/**
 * Switches threads.
 */
void switch_threads()
{
    int ret_val = 0;
    if (threads[curr_thread] != nullptr) // thread was not terminated
    {
        // thread was not blocked, move it to READY
        if (threads[curr_thread]->get_state() != BLOCKED)
        {
            threads[curr_thread]->set_state(READY);
            ready_queue.push_back(threads[curr_thread]);
        }
        // saves the current thread's state
        ret_val = sigsetjmp(threads[curr_thread]->env, 1);
        if (ret_val == 1)
        {
            unblock_signals();
            return;
        }
    }
    // switch to the next thread
    curr_thread = ready_queue.front()->get_id(); // sets the curr thread to be the next thread
    ready_queue.pop_front(); // remove from ready queue
    threads[curr_thread]->set_state(RUNNING);
    quantums_counter++;
    reset_timer();
    unblock_signals();
    siglongjmp(threads[curr_thread]->env, 1);
}

/**
 * Function that is called when the signal arrives to the sa.handler.
 * @param sig signal
 */
void timer_handler(int sig)
{
    if (sig == SIGVTALRM)
    {
        switch_threads();
    }
}

/**
 * Free all memory allocated by the library.
 */
void delete_library()
{
    delete[] quantums_by_priority;
    quantums_by_priority = nullptr;
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        if (threads[i] != nullptr)
        {
            delete threads[i];
            threads[i] = nullptr;
        }
    }
}

// --------------------- Library API Implementation ---------------------

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * an array of the length of a quantum in micro-seconds for each priority.
 * It is an error to call this function with an array containing non-positive integer.
 * size - is the size of the array.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int *quantum_usecs, int size)
{
    if (size <= 0)
    {
        std::cerr << LIB_ERR << "Invalid size input.\n";
        return -1;
    }

    max_priority = size - 1;
    try
    {
        quantums_by_priority = new int[size];

    }
    catch (std::bad_alloc &e)
    {
        std::cerr << SYS_ERR << "Error allocating library memory.\n";
        return -1;
    }

    for (int i = 0; i < size; ++i)
    {
        if (quantum_usecs[i] <= 0) // invalid priority input
        {
            std::cerr << LIB_ERR << "Invalid priority.\n";
            delete[] quantums_by_priority;
            quantums_by_priority = nullptr;
            return -1;
        }
        quantums_by_priority[i] = quantum_usecs[i];
    }

    if (sigemptyset(&set) < 0) // init signal block set
    {
        delete_library();
        std::cerr << SYS_ERR << "sigemptyset failed.\n";
        return -1;
    }
    if (sigaddset(&set, SIGVTALRM) < 0) // add virtual time signal to block set
    {
        delete_library();
        std::cerr << SYS_ERR << "sigaddset failed.\n";
        return -1;
    }
    // init main thread
    try
    {
        threads[0] = new Thread();
    }
    catch (std::bad_alloc &e)
    {
        std::cerr << SYS_ERR << "Error allocating library memory.\n";
        delete_library();
        return -1;
    }

    curr_thread = threads[0]->get_id();
    quantums_counter = 1;
    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        delete_library();
        std::cerr << SYS_ERR << "sigadction failed.\n";
        return -1;
    }
    if (reset_timer() == -1) // error resetting timer
    {
        delete_library();
        return -1;
    }
    return 0;

}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * priority - The priority of the new thread.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void), int priority)
{
    block_signals();
    if (priority < 0 || priority > max_priority || f == nullptr) // invalid input
    {
        unblock_signals();
        std::cerr << LIB_ERR << "Invalid input: priority.\n";
        return -1;
    }
    for (int i = 0; i < MAX_THREAD_NUM; ++i) // search for an available location for thread
    {
        if (threads[i] == nullptr)
        {
            try
            {
                threads[i] = new Thread(i, priority, f);
            }
            catch (std::bad_alloc &e)
            {
                std::cerr << SYS_ERR << "Error allocating library memory.\n";
                delete_library();
                return -1;
            }

            ready_queue.push_back(threads[i]);
            unblock_signals();
            return i;
        }
    }
    unblock_signals();
    // no available location was found, can't spawn thread
    std::cerr << LIB_ERR << "Max number of threads reached.\n";
    return -1;
}

/*
 * Description: This function changes the priority of the thread with ID tid.
 * If this is the current running thread, the effect should take place only the
 * next time the thread gets scheduled.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_change_priority(int tid, int priority)
{
    block_signals();
    if (priority < 0 || priority > max_priority || tid_invalidity(tid)) // invalid input
    {
        unblock_signals();
        std::cerr << LIB_ERR << "Invalid input: tid or priority.\n";
        return -1;
    }
    threads[tid]->set_priority(priority);
    unblock_signals();
    return 0;
}

/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    block_signals();
    if (tid_invalidity(tid))
    {
        unblock_signals();
        std::cerr << LIB_ERR << "Invalid input: tid.\n";
        return -1;
    }
    // now tid is either 0 or larger than 0
    if (tid == 0)
    {
        delete_library();
        exit(0);
    }
    else
    {  // else tid > 0, not main thread
        if (tid == curr_thread)
        { // curr thread, delete it and switch to next one
            delete threads[tid];
            threads[tid] = nullptr;
            switch_threads();
            return 0;
        }
        else // tid is either ready or blocked
        { // not running thread, remove from ready queue and delete
            if (threads[tid]->get_state() == READY)
            {
                ready_queue.remove(threads[tid]);
            }
            delete threads[tid];
            threads[tid] = nullptr;
        }
        unblock_signals();
        return 0;
    }
}


/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    block_signals();
    if (tid_invalidity(tid) || tid == 0) // invalid input
    {
        unblock_signals();
        std::cerr << LIB_ERR << "Invalid input: tid or unable to block main thread.\n";
        return -1;
    }
    if (curr_thread == tid) // block main thread and switch to the next one
    {
        threads[tid]->set_state(BLOCKED);
        switch_threads();
        return 0;
    }
    else // thread is either READY or BLOCKED
    {
        if (threads[tid]->get_state() == READY)
        {
            ready_queue.remove(threads[tid]);
        }
        threads[tid]->set_state(BLOCKED);
    }
    unblock_signals();
    return 0;
}


/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    block_signals();
    if (tid_invalidity(tid))
    {
        unblock_signals();
        std::cerr << LIB_ERR << "Invalid input: tid.\n";
        return -1;
    }
    if (threads[tid]->get_state() == BLOCKED) // resume blocked thread
    {
        threads[tid]->set_state(READY);
        ready_queue.push_back(threads[tid]);
    }
    unblock_signals();
    return 0;
}


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid()
{
    return curr_thread;
}


/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return quantums_counter;
}


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    if (tid_invalidity(tid))
    {
        std::cerr << LIB_ERR << "Invalid input: tid.\n";
        return -1;
    }
    return threads[tid]->get_quantum_count();
}
