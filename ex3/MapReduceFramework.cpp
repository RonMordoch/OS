//
// Created by Ron & Yarden on 13-May-20.
//
#include "MapReduceFramework.h"
#include <pthread.h>
#include <vector>
#include <atomic>
#include <iostream>
#include "Barrier.h"

#define MUTEX_LOCK_ERR "system error: failed to lock mutex\n"
#define MUTEX_UNLOCK_ERR "system error: failed to unlock mutex\n"
#define MUTEX_INIT_ERR "system error: failed to init mutex\n"
#define MUTEX_DESTROY_ERR "system error: failed to destroy mutex\n"
#define PTHREAD_CREATE_ERR "system error: failed to create pthread\n"
#define PTHREAD_JOIN_ERR "system error: failed to join pthread\n"

// ------------------------------------- Mutex Wrapper Functions ---------------------------------
/**
 * Locks a mutex.
 * @param mutex mutex to lock
 */
void lockMutex(pthread_mutex_t &mutex)
{
    if (pthread_mutex_lock(&mutex) != 0)
    {
        std::cerr << MUTEX_LOCK_ERR;
        exit(1);
    }
}

/**
 * Unlocks a mutex.
 * @param mutex mutex to unlock
 */
void unlockMutex(pthread_mutex_t &mutex)
{
    if (pthread_mutex_unlock(&mutex) != 0)
    {
        std::cerr << MUTEX_UNLOCK_ERR;
        exit(1);
    }
}

/**
 * Inits a mutex.
 * @param mutex mutex to init
 */
void initMutex(pthread_mutex_t &mutex)
{
    if (pthread_mutex_init(&mutex, NULL) != 0)
    {
        std::cerr << MUTEX_INIT_ERR;
        exit(1);
    }
}

/**
 * Destroys a mutex.
 * @param mutex mutex to destroy
 */
void destroyMutex(pthread_mutex_t &mutex)
{
    if (pthread_mutex_destroy(&mutex) != 0)
    {
        std::cerr << MUTEX_DESTROY_ERR;
        exit(1);
    }
}

// ---------------------------------- Job and ThreadContexts Classes ------------------------------

/**
 * Forward declaration.
 */
class JobContext;


/**
 * Contexts for each running thread.
 */
class ThreadContext
{
public:
    // threads ID
    int threadID;
    // Intermediate pairs vectors
    std::vector<IntermediatePair> intermediate_vec;
    // Pointer to job context
    JobContext *job;

    /**
     * Constructs a thread context.
     * @param threadID threads id
     * @param job pointer to job
     */
    ThreadContext(int threadID, JobContext *job)
    {
        this->threadID = threadID;
        this->job = job;
    }
};

/**
 * Contains all the relevant information and context of job.
 */
class JobContext
{
public:
    // Job input
    const MapReduceClient *client;
    const InputVec *inputVec;
    OutputVec *outputVec;
    int threadsNum;

    // State of jobs, its threads and contexts
    JobState jobState;
    pthread_t *threads;
    ThreadContext **contexts;

    // Indicates if map stage is done
    bool mapDone;

    // Atomic counter for map and reduce stages
    std::atomic<int> counter;

    // Shuffle stage fields
    int totalToShuffle;
    IntermediateMap intermediateMap;
    std::vector<K2 *> uniqueKeys;

    // Mutexes
    pthread_mutex_t outputVecMutex;
    pthread_mutex_t stageMutex;
    std::vector<pthread_mutex_t> mutexesVec;

    // Barriers
    Barrier *reduceBarrier;
    Barrier *jobDoneBarrier;
    Barrier *shuffleBarrier;

    // Boolean flag for limiting calls to waitForJob
    bool waitForJobFlag;


    /**
     * Constructs a job context.
     * @param client job client
     * @param inputVec input vector
     * @param outputVec output vector
     * @param multiThreadLevel number of threads to run
     */
    JobContext(const MapReduceClient *client, const InputVec &inputVec, OutputVec &outputVec,
               int multiThreadLevel)
            : counter(0), jobState{MAP_STAGE, 0}, totalToShuffle(0),
              mutexesVec(multiThreadLevel - 1), mapDone(false), waitForJobFlag(false)
    {
        this->client = client;
        this->inputVec = &inputVec;
        this->outputVec = &outputVec;
        this->threadsNum = multiThreadLevel;

        // Init threads and thread contexts arrays
        this->threads = new pthread_t[multiThreadLevel];
        this->contexts = new ThreadContext *[multiThreadLevel];

        // init barriers
        this->reduceBarrier = new Barrier(multiThreadLevel);
        this->jobDoneBarrier = new Barrier(multiThreadLevel);
        this->shuffleBarrier = new Barrier(multiThreadLevel - 1);

        // init mutexes
        initMutex(this->outputVecMutex);
        for (int i = 0; i < (multiThreadLevel - 1); ++i)
        {
            initMutex(this->mutexesVec.at(i));
        }
        initMutex(this->stageMutex);
    }

    /**
     * Destructor.
     */
    ~JobContext()
    {
        if (this->threads != nullptr)
        {
            delete[] this->threads;
            this->threads = nullptr;
        }
        if (this->contexts != nullptr)
        {
            for (int i = 0; i < this->threadsNum; ++i)
            {
                if (this->contexts[i] != nullptr)
                {
                    delete this->contexts[i];
                    this->contexts[i] = nullptr;
                }
            }
            delete[] this->contexts;
            this->contexts = nullptr;
        }

        for (int j = 0; j < this->threadsNum - 1; ++j)
        {
            destroyMutex(this->mutexesVec.at(j));
        }
        destroyMutex(this->outputVecMutex);
        destroyMutex(this->stageMutex);
        delete this->reduceBarrier;
        delete this->jobDoneBarrier;
        delete this->shuffleBarrier;
    }
};

// ------------------------------------- Thread's Functions ---------------------------------

/**
 * The mapping function for each of the (threadsNum - 1) threads.
 * @param context thread's context
 */
void threadMap(void *context)
{
    ThreadContext *tc = (ThreadContext *) context;
    while (tc->job->counter < tc->job->inputVec->size())
    {
        lockMutex(tc->job->mutexesVec.at(tc->threadID));

        int old_value = (tc->job->counter)++;
        if (old_value < tc->job->inputVec->size())
        {
            InputPair p = tc->job->inputVec->at(old_value);
            tc->job->client->map(p.first, p.second, tc);
        }
        unlockMutex(tc->job->mutexesVec.at(tc->threadID));
    }
    tc->job->shuffleBarrier->barrier(); // make sure all thread finish map before shuffle
    tc->job->mapDone = true;
}

/**
 * The shuffle function for (threadsNum -1)-th thread.
 * @param context thread's context
 */
void threadShuffle(void *context)
{
    ThreadContext *tc = (ThreadContext *) context;
    while (!tc->job->mapDone)
    {
        for (int i = 0; i < (tc->job->threadsNum - 1); ++i)
        {
            while (!(tc->job->contexts[i]->intermediate_vec.empty()))
            {
                lockMutex(tc->job->mutexesVec.at(i));
                IntermediatePair p = tc->job->contexts[i]->intermediate_vec.back();
                tc->job->contexts[i]->intermediate_vec.pop_back();
                tc->job->intermediateMap[p.first].push_back(p.second);
                unlockMutex(tc->job->mutexesVec.at(i));

            }
        }
    }
    lockMutex(tc->job->stageMutex);
    for (int j = 0; j < tc->job->threadsNum - 1; ++j)
    {
        tc->job->totalToShuffle += tc->job->contexts[j]->intermediate_vec.size();
    }

    tc->job->jobState.stage = SHUFFLE_STAGE;
    tc->job->counter.store(0); // reset counter for shuffle
    unlockMutex(tc->job->stageMutex);

    // all map threads finished, repeat the process until we finish shuffling
    for (int i = 0; i < (tc->job->threadsNum - 1); ++i)
    {
        while (!(tc->job->contexts[i]->intermediate_vec.empty()))
        {
            IntermediatePair p = tc->job->contexts[i]->intermediate_vec.back();
            tc->job->contexts[i]->intermediate_vec.pop_back();
            tc->job->intermediateMap[p.first].push_back(p.second);
            tc->job->counter++;
        }
    }
    // shuffle stage is done
    //first, create a vector of unique keys from the map
    for (IntermediateMap::iterator it = tc->job->intermediateMap.begin();
         it != tc->job->intermediateMap.end(); ++it)
    {
        tc->job->uniqueKeys.push_back(it->first);
    }
    lockMutex(tc->job->stageMutex);
    tc->job->jobState.stage = REDUCE_STAGE;
    tc->job->counter.store(0);
    unlockMutex(tc->job->stageMutex);
}

/**
 * The reduce function for each thread.
 * @param context thread's context
 */
void threadReduce(void *context)
{
    ThreadContext *tc = (ThreadContext *) context;
    while (tc->job->counter < tc->job->uniqueKeys.size())
    {
        lockMutex(tc->job->outputVecMutex);
        if (tc->job->counter < tc->job->uniqueKeys.size())
        {
            int old_value = (tc->job->counter)++;
            K2 *key = tc->job->uniqueKeys.at(old_value);
            tc->job->client->reduce(key, tc->job->intermediateMap[key], tc->job);
        }
        unlockMutex(tc->job->outputVecMutex);
    }
}

/**
 * The function each thread runs.
 * @param arg threads context
 * @return null pointer
 */
void *runThread(void *arg)
{
    ThreadContext *tc = (ThreadContext *) arg;
    // run shuffle thread
    if (tc->threadID == tc->job->threadsNum - 1)
    {
        threadShuffle(tc);
    }
    else // else, all n-1 threads run map
    {
        threadMap(tc);
    }
    // all (n-1) threads finished map and the n-th thread finished the shuffle stage, start reduce
    tc->job->reduceBarrier->barrier();
    threadReduce(tc);
    return nullptr;
}


// -------------------------------------- Library Functions ---------------------------------

/**
 * Produces Intermediate pair (K2*, V2*).
 * @param key of type K2
 * @param value of type V2
 * @param context thread context
 */
void emit2(K2 *key, V2 *value, void *context)
{
    ThreadContext *tc = (ThreadContext *) context;
    IntermediatePair p = IntermediatePair(key, value);
    tc->intermediate_vec.push_back(p);
}

/**
 * Produces Output pair (K3*, V3*).
 * @param key of type K2
 * @param value of type V2
 * @param context job context
 */
void emit3(K3 *key, V3 *value, void *context)
{
    JobContext *job = (JobContext *) context;
    OutputPair p = OutputPair(key, value);
    job->outputVec->push_back(p);
}

/**
 * This function starts running the MapReduce algorithm.
 * @param client the task that the framework should run
 * @param inputVec a vector of type std::vector<std::pair<K1*, V1*>>, the input elements
 * @param outputVec a vector of type std::vector<std::pair<K3*, V3*>>, to which the output
 * elements will be added
 * @param multiThreadLevel the number of worker threads to be used for running the algorithm
 * @return a JobHandle
 */
JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel)
{
    JobContext *job = new JobContext(&client, inputVec, outputVec, multiThreadLevel);
    for (int i = 0; i < multiThreadLevel; ++i)
    {
        job->contexts[i] = new ThreadContext(i, job);

    }
    for (int j = 0; j < multiThreadLevel; ++j)
    {
        if (pthread_create(job->threads + j, NULL, runThread, job->contexts[j]) != 0)
        {
            std::cerr << PTHREAD_CREATE_ERR;
            exit(1);
        }
    }
    return (JobHandle) job;
}

/**
 * Gets the job handle returned by startMapReduceFramework and waits until it is finished.
 * @param job job handle returned by startMapReduce
 */
void waitForJob(JobHandle job)
{
    JobContext *j = (JobContext *) job;
    if (j->waitForJobFlag)
    {
        return;
    }
    j->waitForJobFlag = true;
    for (int i = 0; i < j->threadsNum; ++i)
    {
        if (pthread_join(j->threads[i], NULL) != 0)
        {
            std::cerr << PTHREAD_JOIN_ERR;
            exit(1);
        }
    }
}

/**
 * Gets a job handle and updates the state of the job into the given JobState struct.
 * @param job  job handle object
 * @param state the state to load into the stage and percentage
 */
void getJobState(JobHandle job, JobState *state)
{
    JobContext *j = (JobContext *) job;
    lockMutex(j->stageMutex);
    if (j->jobState.stage == MAP_STAGE)
    {
        state->stage = MAP_STAGE;
        state->percentage = (j->counter / (j->inputVec->size() * 1.00)) * 100.0;
        if (state->percentage > 100)
        { state->percentage = 100; }
    }
    else if (j->jobState.stage == SHUFFLE_STAGE)
    {
        state->stage = SHUFFLE_STAGE;
        if (j->totalToShuffle == 0)
        {
            state->percentage = 100;
        }
        else
        {
            state->percentage = (j->counter / (j->totalToShuffle * 1.00)) * 100.0;
        }
    }
    else if (j->jobState.stage == REDUCE_STAGE)
    {
        state->stage = REDUCE_STAGE;
        state->percentage = (j->counter / (j->uniqueKeys.size() * 1.00)) * 100.0;
    }
    unlockMutex(j->stageMutex);
}

/**
 * Releases all memory of given job.
 * @param job job to release memory of.
 */
void closeJobHandle(JobHandle job)
{
    JobContext *j = (JobContext *) job;
    waitForJob(j);
    if (j != nullptr)
    {
        delete j;
        j = nullptr;
    }
}