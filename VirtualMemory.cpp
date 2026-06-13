#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <algorithm>
#include <cstdint>


// Extract offset from virtual address
uint64_t getOffset(uint64_t virtualAddress)
{
    return virtualAddress & ((1ULL << OFFSET_WIDTH) - 1);
}

// Extract page number from virtual address
uint64_t getPageNumber(uint64_t virtualAddress)
{
    return virtualAddress >> OFFSET_WIDTH;
}

// Get index for specific level in page table hierarchy
uint64_t getIndexAtLevel(uint64_t virtualAddress, int level)
{
    int shiftAmount = OFFSET_WIDTH + (TABLES_DEPTH - 1 - level) * OFFSET_WIDTH;
    return (virtualAddress >> shiftAmount) & ((1ULL << OFFSET_WIDTH) - 1);
}

// Convert frame number and offset to physical address
uint64_t frameToPhysicalAddress(uint64_t frame, uint64_t offset)
{
    return (frame << OFFSET_WIDTH) + offset;
}

// Check if a frame contains an empty table (all zeros)
bool isEmptyTable(uint64_t frame)
{
    word_t value;
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        PMread(frameToPhysicalAddress(frame, i), &value);
        if (value != 0)
        {
            return false;
        }
    }
    return true;
}

// Clear all entries in a frame
void clearFrame(uint64_t frame)
{
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        PMwrite(frameToPhysicalAddress(frame, i), 0);
    }
}

// Calculate cyclic distance between two pages
uint64_t cyclicDistance(uint64_t page1, uint64_t page2)
{
    uint64_t directDistance = (page1 > page2) ? (page1 - page2) : (page2 - page1);
    uint64_t wrapDistance = NUM_PAGES - directDistance;
    return (directDistance < wrapDistance) ? directDistance : wrapDistance;
}


struct DFSResult
{
    uint64_t emptyTableFrame;
    uint64_t emptyTableParent;
    uint64_t emptyTableIndex;
    uint64_t maxFrameIndex;
    uint64_t evictFrame;
    uint64_t evictPage;
    uint64_t evictParent;
    uint64_t evictIndex;
    uint64_t maxDistance;

    DFSResult() : emptyTableFrame(0), emptyTableParent(0), emptyTableIndex(0),
                  maxFrameIndex(0), evictFrame(0), evictPage(0),
                  evictParent(0), evictIndex(0), maxDistance(0)
    {
    }
};

void dfsTraversal(uint64_t currentFrame, uint64_t parentFrame, uint64_t indexInParent,
                  int currentLevel, uint64_t pagePrefix, uint64_t targetPage,
                  bool* protectedFrames, DFSResult& result)
{
    // Update max frame index
    if (currentFrame > result.maxFrameIndex)
    {
        result.maxFrameIndex = currentFrame;
    }

    // If this is a leaf (page level)
    if (currentLevel == TABLES_DEPTH)
    {
        // Check if this frame can be evicted
        if (!protectedFrames[currentFrame])
        {
            uint64_t distance = cyclicDistance(pagePrefix, targetPage);
            if (distance > result.maxDistance)
            {
                result.maxDistance = distance;
                result.evictFrame = currentFrame;
                result.evictPage = pagePrefix;
                result.evictParent = parentFrame;
                result.evictIndex = indexInParent;
            }
        }
        return;
    }

    // Check if this is an empty table (not root, not protected)
    if (currentFrame != 0 && !protectedFrames[currentFrame] &&
        result.emptyTableFrame == 0 && isEmptyTable(currentFrame))
    {
        result.emptyTableFrame = currentFrame;
        result.emptyTableParent = parentFrame;
        result.emptyTableIndex = indexInParent;
    }

    // Traverse children
    word_t childFrame;
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        PMread(frameToPhysicalAddress(currentFrame, i), &childFrame);
        if (childFrame != 0)
        {
            uint64_t newPagePrefix = (pagePrefix << OFFSET_WIDTH) | i;
            dfsTraversal(childFrame, currentFrame, i, currentLevel + 1,
                         newPagePrefix, targetPage, protectedFrames, result);
        }
    }
}

uint64_t findFrame(bool* protectedFrames, uint64_t targetPage)
{
    DFSResult result;
    dfsTraversal(0, 0, 0, 0, 0, targetPage, protectedFrames, result);

    // Rule 1: Empty table
    if (result.emptyTableFrame != 0)
    {
        // Unlink the empty table from its parent
        PMwrite(frameToPhysicalAddress(result.emptyTableParent,
                                       result.emptyTableIndex), 0);
        return result.emptyTableFrame;
    }

    // Rule 2: Unused frame
    if (result.maxFrameIndex + 1 < NUM_FRAMES)
    {
        return result.maxFrameIndex + 1;
    }

    // Rule 3: Evict page with maximum cyclic distance
    // Evict the page
    PMevict(result.evictFrame, result.evictPage);
    // Unlink from parent
    PMwrite(frameToPhysicalAddress(result.evictParent, result.evictIndex), 0);
    return result.evictFrame;
}


uint64_t pageTableWalk(uint64_t virtualAddress)
{
    bool protectedFrames[NUM_FRAMES] = {false};
    protectedFrames[0] = true; // Root is always protected

    uint64_t currentFrame = 0; // Start from root
    uint64_t targetPage = getPageNumber(virtualAddress);

    // Walk through each level of the page table
    for (int level = 0; level < TABLES_DEPTH; level++)
    {
        uint64_t index = getIndexAtLevel(virtualAddress, level);
        word_t nextFrame;
        PMread(frameToPhysicalAddress(currentFrame, index), &nextFrame);

        // Page fault - need to allocate new frame
        if (nextFrame == 0)
        {
            uint64_t newFrame = findFrame(protectedFrames, targetPage);

            if (level < TABLES_DEPTH - 1)
            {
                // This is a table - clear it
                clearFrame(newFrame);
            }
            else
            {
                // This is a page - restore it
                PMrestore(newFrame, targetPage);
            }

            // Update the parent table
            PMwrite(frameToPhysicalAddress(currentFrame, index), newFrame);
            nextFrame = newFrame;
        }

        currentFrame = nextFrame;
        protectedFrames[currentFrame] = true;
    }

    return currentFrame;
}


void VMinitialize()
{
    clearFrame(0);
}

int VMread(uint64_t virtualAddress, word_t* value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }

    uint64_t pageFrame = pageTableWalk(virtualAddress);
    uint64_t offset = getOffset(virtualAddress);
    PMread(frameToPhysicalAddress(pageFrame, offset), value);
    return 1;
}

int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }

    uint64_t pageFrame = pageTableWalk(virtualAddress);
    uint64_t offset = getOffset(virtualAddress);
    PMwrite(frameToPhysicalAddress(pageFrame, offset), value);
    return 1;
}
