//
// Created by Ron & Yarden on 12-Apr-20.
//

#ifndef EX2_THREAD_H
#define EX2_THREAD_H

#include <sys/time.h>
#include <setjmp.h>

#define RUNNING 0
#define BLOCKED 1
#define READY 2
#define STACK_SIZE 4096

/**
 * A class representing a single Thread.
 */
class Thread
{
public:
    /**
     * Constructs a new thread with the given id, priority and function.
     * @param id thread's id
     * @param priority threads priority
     * @param f a pointer to a function
     */
    Thread(int id, int priority, void (*f)(void));

    /**
     * A default constructor, constructs the main thread (tid = 0).
     */
    Thread();

    /**
     * @return thread's id
     */
    int get_id();

    /**
     * @return thread's priority.
     */
    int get_priority();

    /**
    * @return thread's state.
    */
    int get_state();

    /**
    * @return thread's quantum count.
    */
    int get_quantum_count();

    /**
     * Sets the given priority to be the thread's new priority.
     * @param new_priority new priority
     */
    void set_priority(int new_priority);

    /**
     * Sets the given state to be the thread's new state.
     * If the new state is RUNNING, this increase the thread's quantum count
     * by one.
     * @param new_state new state
     */
    void set_state(int new_state);

    /**
     * Destructor.
     */
    ~Thread();

    /**
     * The thread's environment - stack pointer and program counter.
     */
    sigjmp_buf env;


private:
    int id;
    int priority;
    int state;
    int quantum_count;
    char *stack;

};

#endif //EX2_THREAD_H
