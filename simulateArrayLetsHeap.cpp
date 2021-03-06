#ifndef SIMULATE_ARRAYLETS_HEAP
#define SIMULATE_ARRAYLETS_HEAP

#include <iostream>
#include <fstream>
#include <string>

#include "util.hpp"

// TODO try HUGE_TLB passed to mmap - MACOSX does not suport MAP_HUGETLB
// TODO Code cleanup
// TODO run approach 1 on linux. Use Ubuntu 16.04 on docker

// To run:
// g++ -g3 -Wno-write-strings -std=c++11 simulateArrayLetsHeap.cpp -o simulateArrayLetsHeap
// Note: Insert -lrt flag for linux systems
// ./simulateArrayLetsHeap 12 1000

char * mmapContiguous(size_t totalArraySize, size_t arrayletSize, long arrayLetOffsets[], int fh)
   {
    char * contiguousMap = (char *)mmap(
                   NULL,
                   totalArraySize, // File size
                   PROT_READ|PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, // Must be shared
                   -1,
                   0);

    if (contiguousMap == MAP_FAILED) {
      std::cerr << "Failed to mmap contiguousMap\n";
      return NULL;
    }

    for (size_t i = 0; i < ARRAYLET_COUNT; i++) {
       void *nextAddress = (void *)(contiguousMap+i*arrayletSize);
       void *address = mmap(
                   nextAddress,
                   arrayletSize, // File size
                   PROT_READ|PROT_WRITE,
                   MAP_SHARED | MAP_FIXED,
                   fh,
                   arrayLetOffsets[i]);

        if (address == MAP_FAILED) {
            std::cerr << "Failed to mmap address[" << i << "] at mmapContiguous()\n";
            munmap(contiguousMap, totalArraySize);
            contiguousMap = NULL;
        } else if (nextAddress != address) {
            std::cerr << "Map failed to provide the correct address. nextAddress " << nextAddress << " != " << address << std::endl;
            munmap(contiguousMap, totalArraySize);
            contiguousMap = NULL;
        }
    }

     return contiguousMap;
   }

/**
 * Uses shm_open to create dummy empty file and
 * ftruncate to allocate desired memory size.
 */

int main(int argc, char** argv) {

    if (argc != 4) {
        std::cout<<"USAGE: " << argv[0] << " seed# iterations# debug<0,1>" << std::endl;
        std::cout << "Example: " << argv[0] << " 6363 50000 0" << std::endl;
        return 1;
    }
    
    PaddedRandom rnd;
    int seed = atoi(argv[1]);
    int iterations = atoi(argv[2]);
    int debug = atoi(argv[3]);
    rnd.setSeed(seed);

    size_t pagesize = getpagesize(); // 4096 bytes
    std::cout << "System page size: " << pagesize << " bytes.\n";
    size_t arrayletSize = getArrayletSize(pagesize);
    std::cout << "Arraylet size: " << arrayletSize << " bytes" << std::endl;

    char * filename = "temp.txt";
    int fh = shm_open(filename, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fh == -1) {
      std::cerr << "Error while reading file" << filename << "\n";
      return 1;
    }
    shm_unlink(filename);

    // Sets the desired size to be allocated
    // Failing to allocate memory will result in a bus error on access.
    ftruncate(fh, FOUR_GB);

    char * heapMmap = (char *)mmap(
                NULL,
                FOUR_GB, // File size
                PROT_READ|PROT_WRITE,
                MAP_SHARED, // Must be shared
                fh, // File handle
                0);

    if (heapMmap == MAP_FAILED) {
       std::cerr << "Failed to mmap " << strerror(errno) << "\n";
       return 1;
    } else {
       std::cout << "Successfully mmaped heap at address: " << (void *)heapMmap << "\n";
    }

    // Get page alligned offsets
    long arrayLetOffsets[ARRAYLET_COUNT];
    for(size_t i = 0; i < ARRAYLET_COUNT; i++) {
        arrayLetOffsets[i] = getPageAlignedOffset(arrayletSize, rnd.nextNatural() % FOUR_GB);
    }

    for (size_t i = 0; i < ARRAYLET_COUNT; i++) {
       std::cout << "Arralylet at " << i << " has offset: " << arrayLetOffsets[i] << '\n';
    }

    char vals[SIXTEEN] = {'3', '5', '6', '8', '9', '0', '1', '2', '3', '7', 'A', 'E', 'C', 'B', 'D', 'F'};
    
    size_t totalArraySize = 0;

    for (size_t i = 0; i < ARRAYLET_COUNT; i++) {
        memset(heapMmap+arrayLetOffsets[i], vals[i%SIXTEEN], arrayletSize);
        totalArraySize += arrayletSize;
    }

    if (1 == debug) {
        fprintf(stdout, "First 32 chars of data before mapping and modification of the double mapped addresses\n");
        for (size_t i = 0; i < ARRAYLET_COUNT; i++) {
            fprintf(stdout, "\tvals[%02lu] %.64s\n", i, heapMmap+arrayLetOffsets[i]);
        }
    }

    std::cout << "Arraylets created successfully.\n";
    std::cout << "ArrayLets combined have size: " << totalArraySize << " bytes." << '\n';

    double totalMapTime = 0;
    double totalModifyTime = 0;
    double totalFreeTime = 0;

    ElapsedTimer timer;
    timer.startTimer();

    for(size_t i = 0; i < iterations / 10; i++) {
        char *maps[10];
        for (int j = 0; j < 10; j++) {
            double start = timer.getElapsedMicros();
            // 3. Make Arraylets look contiguous with mmap
            maps[j] = mmapContiguous(totalArraySize, arrayletSize, arrayLetOffsets, fh);

            double mapEnd = timer.getElapsedMicros();

            // 4. Modify contiguous memory view and observe change in the heap
            modifyContiguousMem(pagesize, arrayletSize, maps[j]);

            double modifyEnd = timer.getElapsedMicros();

            totalMapTime += (mapEnd - start);
            totalModifyTime += (modifyEnd - mapEnd);

        }
        for (int j = 0; j < 10; j++) {
            double freeStart = timer.getElapsedMicros();

            // Free addresses
            munmap(maps[j], totalArraySize);

            double freeEnd = timer.getElapsedMicros();

            totalFreeTime += (freeEnd - freeStart);
        }
    }

    int64_t elapsedTime = timer.getElapsedMicros();
    
    if (1 == debug) {
        fprintf(stdout, "First 32 chars of data after mapping and modification of the double mapped addresses\n");
        for (size_t i = 0; i < ARRAYLET_COUNT; i++) {
            char *arraylet = heapMmap+arrayLetOffsets[i];
            fprintf(stdout, "\tvals[%02lu] %.64s\n", i, arraylet);
        }
    }

    std::cout << "Test completed " << iterations << " iterations" << std::endl;
    std::cout << "Total elapsed time " << elapsedTime << "us" << std::endl;
    std::cout << "Total map time " << totalMapTime << "us AVG map time " << (totalMapTime / iterations) << "us" << std::endl;
    std::cout << "Total modify time " << totalModifyTime << "us AVG modify time " << (totalModifyTime / iterations) << "us" << std::endl;
    std::cout << "Total free time " << totalFreeTime << "us AVG free time " << (totalFreeTime / iterations) << "us" << std::endl;

    munmap(heapMmap, FOUR_GB);

    return 0;
}

#endif /* SIMULATE_ARRAYLETS_HEAP */
