# Virtual Memory Management

Implementation of a virtual memory system using hierarchical page tables.

## Overview
- Multi-level page table translation with configurable depth
- Page fault handling with automatic frame allocation
- Three-rule frame allocation policy (empty tables → unused frames → LRU-like eviction)
- Supports various memory configurations via compile-time constants

## Implementation Features
- **No dynamic allocation**: Uses only simulated physical memory
- **Cyclic distance algorithm**: For optimal page replacement
- **Bit manipulation**: Efficient address translation
- **Protected frame tracking**: Prevents eviction of active page table path

## Build
```bash
make              # Build library
make clean        # Clean build artifacts
```
## Files
- `VirtualMemory.cpp` - Core implementation
- `VirtualMemory.h` - Public API
- `Makefile` - Build configuration