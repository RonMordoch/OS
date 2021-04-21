//
// Created by Ron & Yarden on 12-Apr-20.
//

#include "Thread.h"
#include <signal.h>

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
        "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif

/**
 * A default constructor, constructs the main thread (tid = 0).
 */
Thread::Thread()
{
    this->id = 0;
    this->priority = 0;
    this->state = RUNNING;
    this->quantum_count = 1;
    this->stack = nullptr;

    sigsetjmp(this->env, 1);
    sigemptyset(&env->__saved_mask);
}

/**
 * Constructs a new thread with the given id, priority and function.
 * @param id thread's id
 * @param priority threads priority
 * @param f a pointer to a function
 */
Thread::Thread(int id, int priority, void (*f)(void))
{
    this->id = id;
    this->priority = priority;
    this->state = READY;
    this->quantum_count = 0;
    this->stack = new char[STACK_SIZE];
    // No need to catch exception in case of bad_alloc - its thrown to the library and there
    // its caught

    address_t sp, pc;
    sp = (address_t) this->stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t) f;
    sigsetjmp(this->env, 1);
    (env->__jmpbuf)[JB_SP] = translate_address(sp);
    (env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env->__saved_mask);

}

/**
 * Destructor.
 */
Thread::~Thread()
{
    if (this->stack != nullptr)
    {
        delete[] this->stack;
        this->stack = nullptr;
    }
}

/**
 * @return thread's id.
 */
int Thread::get_id()
{
    return this->id;
}

/**
 * @return thread's priority.
 */
int Thread::get_priority()
{
    return this->priority;
}

/**
 * @return thread's state.
 */
int Thread::get_state()
{
    return this->state;
}

/**
 * @return thread's quantum count.
 */
int Thread::get_quantum_count()
{
    return this->quantum_count;
}

/**
 * Sets the given priority to be the thread's new priority.
 * @param new_priority new priority
 */
void Thread::set_priority(int new_priority)
{
    this->priority = new_priority;
}

/**
 * Sets the given state to be the thread's new state.
 * If the new state is RUNNING, this increase the thread's quantum count
 * by one.
 * @param new_state new state
 */
void Thread::set_state(int new_state)
{
    this->state = new_state;
    if (new_state == RUNNING)
    {
        this->quantum_count++;
    }
}

