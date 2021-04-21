#include "VirtualMemory.h"
#include "PhysicalMemory.h"

/**
 * clear a single frame
 * @param frameIndex
 */
void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

/**
 * Checks is the frame in not in the currnt path
 * @param frame - fram index
 * @param path - current path to page
 * @return bool
 */
bool validFramePath(uint64_t frame, uint64_t *path)
{
    for (uint64_t i = 0; i < TABLES_DEPTH; ++i)
    {
        if (path[i] == frame)
        {
            return false;
        }
    }
    return true; // frame is not in path
}

/**
 * get relevant address part from virtual address by the position
 * @param virtualAddress
 * @param position
 * @return relevant address part
 */
uint64_t splitAddress(uint64_t virtualAddress, uint64_t position)
{
    uint64_t shiftedAddress = virtualAddress >> (position * OFFSET_WIDTH);
    return shiftedAddress & ((1 << OFFSET_WIDTH) - 1);
}

/**
 * checks is the given fram index is empty
 * @param frameIndex
 * @return true if empty, false otherwise
 */

bool isFrameEmpty(uint64_t frameIndex)
{
    int value = 0;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMread(frameIndex * PAGE_SIZE + i, &value);
        if (value != 0)
        {
            return false;
        }
    }
    return true;
}

/**
 * gets the cyclic distance between pages
 */
uint64_t getCyclicDist(uint64_t pageToSwapIn, uint64_t pageToSwapOut)
{
    uint64_t dist;
    if (pageToSwapIn > pageToSwapOut)
    {
        dist = pageToSwapIn - pageToSwapOut;
    }
    else
    {
        dist = pageToSwapOut - pageToSwapIn;
    }
    if (dist < NUM_PAGES - dist)
    {
        return dist;
    }
    return NUM_PAGES - dist;
}

/**
 * Recursively traverses the entire page table.
 * @param currFrame current frame number
 * @param parentFrame parent frame number of the current frame
 * @param parentIndex index of the current frame inside the parent frame
 * @param maxFrame maximum frame encountered during the traversal
 * @param depth depth inside the page table
 * @param path the path we built so far
 * @param maxCyclicDist maximum cyclic distance
 * @param currPage destination page number
 * @param victimPage page number of potential victim
 * @param victimFrame frame number of potential victim
 * @param victimParent frame victim of the parent
 * @param victimParentIndex index of the victim frame inside the parent victim
 * @param foundEmptyFrame true if we found an empty frame, false otherwise
 * @param theFoundFrame the found frame
 * @param pathToCurrFrame current path the the frame (page number)
 */
void traversePageTable(uint64_t currFrame, uint64_t parentFrame, uint64_t parentIndex,
                       uint64_t &maxFrame, int depth,
                       uint64_t *path, uint64_t &maxCyclicDist,
                       uint64_t &currPage, uint64_t &victimPage,
                       uint64_t &victimFrame,
                       uint64_t &victimParent, uint64_t &victimParentIndex, bool &foundEmptyFrame,
                       uint64_t &theFoundFrame, uint64_t pathToCurrFrame)
{
    if (currFrame > maxFrame)
    {
        maxFrame = currFrame;
    }
    if (depth == TABLES_DEPTH)
    {
        uint64_t currDist = getCyclicDist(currPage, pathToCurrFrame);
        if (currDist > maxCyclicDist)
        {
            maxCyclicDist = currDist;
            victimPage = pathToCurrFrame;
            victimFrame = currFrame;
            victimParent = parentFrame;
            victimParentIndex = parentIndex;
        }
        return;
    }
    if (isFrameEmpty(currFrame) && validFramePath(currFrame, path))
    {
        theFoundFrame = currFrame;
        // unlink from parent..
        PMwrite(parentFrame * PAGE_SIZE + parentIndex, 0);
        foundEmptyFrame = true;
        return;
    }
    depth++;
    parentFrame = currFrame;
    pathToCurrFrame = pathToCurrFrame << OFFSET_WIDTH;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        parentIndex = i;
        word_t value;
        PMread(currFrame * PAGE_SIZE + i, &value);
        if (value != 0)
        {
            uint64_t currPath = pathToCurrFrame + i;
            traversePageTable((uint64_t) value, parentFrame, parentIndex,
                              maxFrame, depth,
                              path, maxCyclicDist,
                              currPage, victimPage,
                              victimFrame,
                              victimParent, victimParentIndex, foundEmptyFrame,
                              theFoundFrame, currPath);
        }
    }
}

/**
 *
 * @param path the path we built so far
 * @param pageNumber destination page number
 * @param isLeaf true if we are finding an actual page and not table; false otherwise
 * @return frame number
 */
uint64_t findFrame(uint64_t *path, uint64_t pageNumber, bool &isLeaf)
{
    uint64_t maxFrame = 0;
    uint64_t parentIndex = 0, parentFrame = 0;
    uint64_t frame = 0;
    uint64_t maxCyclicDist = 0;
    uint64_t potentialVictim = 0, victimFrame = 0;
    uint64_t victimParent = 0, victimParentIndex = 0;
    bool foundEmptyFrame = false;
    int depth = 0;
    uint64_t theFoundFrame = 0;
    // start by traversing the tree root children
    depth++;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        word_t value;
        PMread(frame * PAGE_SIZE + i, &value); // here frame is 0 beginning of recursion
        if (value != 0)
        {
            parentIndex = i;
            traversePageTable(value, parentFrame, parentIndex,
                              maxFrame, depth, path, maxCyclicDist,
                              pageNumber, potentialVictim, victimFrame,
                              victimParent, victimParentIndex, foundEmptyFrame,
                              theFoundFrame, i);
        }
    }
    if (foundEmptyFrame)
    {
        return theFoundFrame;

    }
    else if (maxFrame + 1 < NUM_FRAMES)
    {
        theFoundFrame = maxFrame + 1;
        if (!isLeaf)
            {
            clearTable(theFoundFrame);
            }
        return theFoundFrame;

    }
    else // evict
    {
        // unlink from parent
        PMwrite(victimParent * PAGE_SIZE + victimParentIndex, 0);
        // evict
        PMevict(victimFrame, potentialVictim);
        // clear table
        clearTable(victimFrame);
        // return it
        return victimFrame;


    }
}

/**
 * Processes the given virtual address into an actual physical address.
 * @param virtualAddress virtual address
 * @param currPhysicalAddress physical address to map virtual address into
 */
void processAddress(uint64_t virtualAddress, uint64_t &currPhysicalAddress)
{

    word_t nextAddress = 0;
    uint64_t pageNumber = (virtualAddress >> OFFSET_WIDTH);
    bool isLeaf = false;
    uint64_t path[TABLES_DEPTH];
    for (int i = 0; i < TABLES_DEPTH; ++i)
    {
        if (i == TABLES_DEPTH - 1)
        {
            isLeaf = true;
        }
        uint64_t addressPart = splitAddress(virtualAddress, TABLES_DEPTH - i);
        PMread(currPhysicalAddress * PAGE_SIZE + addressPart,
               &nextAddress);
        if (nextAddress == 0)
        {
            nextAddress = findFrame(path, pageNumber, isLeaf);
            PMwrite(currPhysicalAddress * PAGE_SIZE + addressPart, nextAddress);
        }
        currPhysicalAddress = nextAddress;
        path[i] = nextAddress;
        if (i == TABLES_DEPTH - 1)
        {
            PMrestore(nextAddress, pageNumber);
        }

    }
}

/*
 * Initialize the virtual memory
 */
void VMinitialize()
{
    clearTable(0);
}

/* reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t *value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    uint64_t currPhysicalAddress = 0;
    processAddress(virtualAddress, currPhysicalAddress);
    PMread(currPhysicalAddress * PAGE_SIZE + splitAddress(virtualAddress, 0), value);
    return 1;
}

/* writes a word to the given virtual address
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    uint64_t currPhysicalAddress = 0;
    processAddress(virtualAddress, currPhysicalAddress);
    PMwrite(currPhysicalAddress * PAGE_SIZE + splitAddress(virtualAddress, 0), value);
    return 1;
}

