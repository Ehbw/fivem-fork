/******************************************************************************
MIT License Copyright (c) 2023 Jeslas Pravin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************
*/

#pragma once
#include <atomic>
#include <chrono>
#include <vector>
#include <winternl.h>

template <typename HazardType>
struct HazardPointerDeleter
{
    void operator()(HazardType* ptr) { delete ptr; }
};

/**
 *  HazardRecord is supposed to be held by a single thread and must not be shared to another thread
 */
struct alignas(128) HazardRecord
{
public:
    constexpr static const uintptr_t RESET_VALUE = 0;
    constexpr static const uintptr_t FREE_VALUE = ~(uintptr_t)(0);

    /**
     * Just storing hazardous pointer alone is not thread safe.
     * As after load the thread has to store which is not safe
     * We can avoid that by using pointer to hazard ptr store
     */
    std::atomic<uintptr_t> hazardPtr{ FREE_VALUE };
    /**
     * If this value is set and hazardPtr is invalid, It means either hazardPtr is not loaded yet or is loaded but not stored
     * gcCollect() tries to load by calling getHazardPtr() which sets up the hazardPtr using CAS
     */
    std::atomic<uintptr_t> hazardPtrStorePtr{ RESET_VALUE };

public:
    template <typename T>
    T* setHazardPtr(const std::atomic<T*>& ptr)
    {
        hazardPtrStorePtr.store((uintptr_t)&ptr, std::memory_order_relaxed);
        // If we are setting new pointer then pointer value also must be reset
        hazardPtr.store(RESET_VALUE, std::memory_order_release);

        // We need fence here to ensure that instructions never gets reordered at compiler
        std::atomic_signal_fence(std::memory_order_seq_cst);
        // We can read relaxed as the HazardRecord will be acquired by a thread and not shared
        // so setting will technically happen in a thread only
        return getHazardPtr<T>(std::memory_order_relaxed);
    }

    /**
     * Must be called only if setHazardPtr with valid ptr store is called on this record
     */
    template <typename T>
    T* getHazardPtr(std::memory_order hazardPtrLoadOrder)
    {
        uintptr_t currHazardPtr = hazardPtr.load(hazardPtrLoadOrder);
        if (!isValid(currHazardPtr))
        {
            /**
             * store ptr can be loaded relaxed as both in setHazardPtr and gcCollect we are thread, cache coherency safe
             * However in gcCollect there is a possibility that hazardPtrStorePtr will be reset(0) but never visible due to relaxed store
             * In reset(0) invisible or before reset case we will either return null or set hazardPtr thus not freeing ptr until next gc
             */
            const std::atomic<T*>* ptrStore = (const std::atomic<T*> *)hazardPtrStorePtr.load(std::memory_order_relaxed);
            // RMW must be acquire and release as gcCollect might have written first, On failure we can just load relaxed
            if (hazardPtr.compare_exchange_strong(
                currHazardPtr, ptrStore ? (uintptr_t)ptrStore->load(std::memory_order_acquire) : RESET_VALUE, std::memory_order_acq_rel,
                std::memory_order_relaxed
            ))
            {
                // Relaxed is fine as we are the one modified it just now
                currHazardPtr = hazardPtr.load(std::memory_order_relaxed);
            }
        }
        return (T*)currHazardPtr;
    }

    void reset()
    {
        hazardPtrStorePtr.store(RESET_VALUE, std::memory_order_relaxed);
        hazardPtr.store(RESET_VALUE, std::memory_order_release);
    }
    void free()
    {
        hazardPtrStorePtr.store(RESET_VALUE, std::memory_order_relaxed);
        hazardPtr.store(FREE_VALUE, std::memory_order_release);
    }

    constexpr static bool isUseable(uintptr_t ptr) 
    { 
        return ptr == RESET_VALUE; 
    }
    constexpr static bool isFree(uintptr_t ptr) 
    { 
        return ptr == FREE_VALUE; 
    }
    constexpr static bool isValid(uintptr_t ptr) 
    {
        return ptr != FREE_VALUE && ptr != RESET_VALUE;
    }
};

struct alignas(128) HazardPointersChunk
{
public:
    constexpr static const size_t RECORDS_PER_CHUNK = 32;

    // pNext will be in different cache line from allocated chunk even if they are next to each other
    std::atomic<HazardPointersChunk*> pNext{ nullptr };
    // records will be in different cache line from pNext and each record will be in different line as well
    HazardRecord records[RECORDS_PER_CHUNK];
};

class SpinLock
{
private:
    std::atomic_flag flag;
public:
    void lock() noexcept
    {
        while (flag.test_and_set(std::memory_order_acq_rel));
    }

    void unlock() noexcept
    {
        return flag.clear(std::memory_order_release);
    }
};

template <typename Type, size_t MinPerThreadDeleteQSize = 4>
class HazardPointersManager
{
public:
    using HazardType = Type;
    using HazardPointerType = HazardType*;
private:
    // 2 seconds once at very minimum
    constexpr static const std::chrono::steady_clock::duration::rep COLLECT_INTERVAL
        = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(2)).count();
    constexpr static const size_t MIN_PER_THREAD_DELETE_QSIZE = MinPerThreadDeleteQSize;

    struct HazardPtrPerThreadData
    {
        std::vector<HazardPointerType> deletingPtrs;
        std::chrono::time_point<std::chrono::steady_clock> lastCollect;
    };

    // Necessary for proper clean up
    std::vector<HazardPtrPerThreadData*> allPerThreadData;
    SpinLock perThreadDataLock;
    uint32_t perThreadSlot;

    HazardPointersChunk head;

public:
    ~HazardPointersManager()
    {
        for (HazardPtrPerThreadData* threadData : allPerThreadData)
        {
            for (HazardPointerType hazPtr : threadData->deletingPtrs)
            {
                HazardPointerDeleter<HazardType>{}(hazPtr);
            }
            delete threadData;
        }
        allPerThreadData.clear();

        HazardPointersChunk* topChunk = &head;
        std::vector<HazardPointersChunk*> chunks;
        while (HazardPointersChunk* nextChunk = topChunk->pNext.load(std::memory_order_relaxed))
        {
            // Destruction must happen after external synchronization so relaxed if fine
            chunks.emplace_back(nextChunk);
            topChunk = nextChunk;
        }

        head.pNext = nullptr;
        for (HazardPointersChunk* chunk : chunks)
        {
            delete chunk;
        }
    }

    void enqueueDelete(HazardPointerType hazardPtr)
    {
        HazardPtrPerThreadData& threadData = getPerThreadData();
        threadData.deletingPtrs.emplace_back(hazardPtr);

        // If we waited long enough and deleting queue is more than minimum to collect? we start collect
        if ((std::chrono::steady_clock::now().time_since_epoch() - threadData.lastCollect.time_since_epoch()).count() >= COLLECT_INTERVAL
            && threadData.deletingPtrs.size() >= MIN_PER_THREAD_DELETE_QSIZE)
        {
            gcCollect();
        }
    }

    HazardPointerType dequeueDelete()
    {
        HazardPtrPerThreadData& threadData = getPerThreadData();
        if (threadData.deletingPtrs.empty())
        {
            return nullptr;
        }

        HazardPointerType hazPtr = threadData.deletingPtrs.back();
        threadData.deletingPtrs.pop_back();
        return hazPtr;
    }

private:
    HazardPtrPerThreadData* createPerThreadData()
    {
        HazardPtrPerThreadData* perThreadData = new HazardPtrPerThreadData();
        perThreadData->lastCollect = std::chrono::steady_clock::now();

        std::scoped_lock<SpinLock> allThreadDataLock{ perThreadDataLock };
        allPerThreadData.emplace_back(perThreadData);
        return perThreadData;
    }
    HazardPtrPerThreadData& getPerThreadData()
    {
        HazardPtrPerThreadData* perThreadDataPtr = (HazardPtrPerThreadData*)TlsGetValue(perThreadSlot);
        if (!perThreadDataPtr)
        {
            perThreadDataPtr = createPerThreadData();
        }
        return *perThreadDataPtr;
    }

    HazardPointersChunk* addChunk(HazardPointersChunk* addTo)
    {
        HazardPointersChunk* newChunk = new HazardPointersChunk();
        HazardPointersChunk* expectedChunk = nullptr;
        if (addTo->pNext.compare_exchange_weak(expectedChunk, newChunk, std::memory_order_acq_rel, std::memory_order_relaxed))
        {
            return newChunk;
        }
        delete newChunk;
        // nullptr cannot be returned so return addTo as it will do the work again and calls this from acquireRecord again
        return (expectedChunk != nullptr) ? expectedChunk : addTo;
    }
public:
    /**
     * This will be most contented path if we are not obtaining per thread record and acquiring record every time
     *
     * - CAS could fail even if valid we can ignore that and continue with next record
     * - pNext load might not see latest and see nullptr. It is okay and gets corrected in addChunk
     * - addChunk might fail CAS but it is okay as that will get corrected in next spin using same chunk here
     */
    HazardRecord* acquireRecord()
    {
        HazardPointersChunk* chunk = &head;
        while (true)
        {
            while (chunk)
            {
                for (uint32_t i = 0; i != HazardPointersChunk::RECORDS_PER_CHUNK; ++i)
                {
                    uintptr_t expectedPtr = HazardRecord::FREE_VALUE;
                    if (chunk->records[i].hazardPtr.compare_exchange_weak(
                        expectedPtr, HazardRecord::RESET_VALUE, std::memory_order_acq_rel, std::memory_order_relaxed
                    ))
                    {
                        return &chunk->records[i];
                    }
                }
                if (HazardPointersChunk* nextChunk = chunk->pNext.load(std::memory_order_relaxed))
                {
                    chunk = nextChunk;
                }
                else
                {
                    break;
                }
            }
            chunk = addChunk(chunk);
        }
    }

    /**
     * This will be read only on multi consumer data and RW on thread specific data
     */
    void gcCollect()
    {
        HazardPtrPerThreadData& threadData = getPerThreadData();

        std::vector<HazardPointerType> referencedPtrs;
        HazardPointersChunk* chunk = &head;
        while (chunk)
        {
            for (uint32_t i = 0; i != HazardPointersChunk::RECORDS_PER_CHUNK; ++i)
            {
                // Relaxed is fine here as even if this is not valid we can safely getHazardPtr in else case
                uintptr_t hazPtr = chunk->records[i].hazardPtr.load(std::memory_order_relaxed);
                if (HazardRecord::isValid(hazPtr))
                {
                    referencedPtrs.emplace_back((HazardPointerType)hazPtr);
                }
                else if (HazardRecord::isValid(chunk->records[i].hazardPtrStorePtr.load(std::memory_order_acquire)))
                {
                    referencedPtrs.emplace_back(chunk->records[i].getHazardPtr<HazardType>(std::memory_order_acquire));
                }
            }
            chunk = chunk->pNext.load(std::memory_order_acquire);
        }

        std::sort(referencedPtrs.begin(), referencedPtrs.end());
        for (auto itr = threadData.deletingPtrs.begin(); itr != threadData.deletingPtrs.end();)
        {
            if (std::binary_search(referencedPtrs.cbegin(), referencedPtrs.cend(), *itr))
            {
                ++itr;
            }
            else
            {
                HazardPointerDeleter<HazardType>{}(*itr);
                itr = threadData.deletingPtrs.erase(itr);
            }
        }
        threadData.lastCollect = std::chrono::steady_clock::now();
    }
};

template<typename Node>
struct HazardPointer
{
    HazardRecord* record{ nullptr };

    HazardPointer(HazardPointersManager<Node>& manager)
        : record(manager.acquireRecord())
    {}
    HazardPointer(HazardPointer&& other)
        : record(other.record)
    {
        other.record = nullptr;
    }
    HazardPointer& operator=(HazardPointer&& other)
    {
        record = other.record;
        other.record = nullptr;
        return *this;
    }

    ~HazardPointer()
    {
        if (record)
        {
            record->free();
        }
    }

    // No copying allowed
    HazardPointer() = delete;
    HazardPointer(const HazardPointer&) = delete;
    HazardPointer& operator=(const HazardPointer&) = delete;
};