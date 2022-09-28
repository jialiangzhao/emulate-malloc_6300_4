#include <Windows.h>

#include <assert.h>
#include <algorithm>
#include <vector>

#include <iostream>

#define SUPPORTS_ALIGNMENT
#define SUPPORTS_SHOWFREEBLOCKS
#define SUPPORTS_SHOWOUTSTANDINGALLOCATIONS
using namespace std;


struct HeapManager
{
	void* pBaseAddress;
	size_t BlockSize;
	struct HeapManager* pNextBlock;
};

struct HeapManager TheHeap;
struct HeapManager* FreeList = &TheHeap;
struct HeapManager* OutstandingAllocations = nullptr;

struct HeapManager* pFreeList = nullptr;

//void* malloc(size_t i_size) {
//	void* pUserMem = TheHeap.pBaseAddress;
//	TheHeap.pBaseAddress += i_size;
//	TheHeap.BlockSize -= i_size;
//
//
//	return pUserMem;
//};
//
void* alloc(size_t i_size) {

	HeapManager* pBlock = GetHeapManager();

	HeapManager* pFreeBlock = FreeList;

	while (pFreeBlock) {
		if (pFreeBlock->BlockSize > i_size)
			break;
		pFreeBlock = pFreeBlock->pNextBlock;
	}

	assert(pFreeBlock);

	pBlock->pBaseAddress = TheHeap.pBaseAddress;
	pBlock->BlockSize = i_size;

	ReturnHeapManager(pBlock);

	//TheHeap.pBaseAddress += i_size;
	TheHeap.BlockSize -= i_size;

	return pBlock->pBaseAddress;
}
//
//void* free(void* i_ptr) {
//	HeapManager* pBlock = RemoveOutstandingAllocation(i_ptr);
//	assert(pBlock);
//
//	pBlock->pNextBlock = FreeList;
//	FreeList = pBlock;
//}
//void* malloc(size_t i_size) 
//{
//	HeapManager* pBlock = FindFirstFittingFreeBlock(i_size);
//	if (pBlock == nullptr) {
//		CoalesceFreeList();
//		pBlock = FindFirstFittingFreeBlock(i_size);
//
//	}
//	assert(pBlock);
//}



HeapManager* CreateHeapManager(void* i_pBlocksMemory, size_t i_BlocksMemoryBytes, size_t numDescriptors)
{
	assert((i_pBlocksMemory != nullptr) && (i_BlocksMemoryBytes > numDescriptors));
	pFreeList = reinterpret_cast<HeapManager*>(i_pBlocksMemory);
	const size_t NumberofBlocks = i_BlocksMemoryBytes / numDescriptors;
	HeapManager* pCurrentBlock = pFreeList;

	for (size_t i = 0; i < (NumberofBlocks - 1); i++, pCurrentBlock++) {
		pCurrentBlock->pBaseAddress = nullptr;
		pCurrentBlock->BlockSize = 0;
		pCurrentBlock->pNextBlock = pCurrentBlock + 1;

	}
	pCurrentBlock->pBaseAddress = nullptr;
	pCurrentBlock->BlockSize = 0;
	pCurrentBlock->pNextBlock = nullptr;

	return pCurrentBlock;
}

HeapManager* GetHeapManager()
{
	assert(pFreeList != nullptr);

	HeapManager* pReturnBlock = pFreeList;
	pFreeList = pFreeList->pNextBlock;

	return pReturnBlock;

}

void ReturnHeapManager(HeapManager* i_pFreeBlock)
{
	assert(i_pFreeBlock != nullptr);

	i_pFreeBlock->pBaseAddress = nullptr;
	i_pFreeBlock->BlockSize = 0;
	i_pFreeBlock->pNextBlock = pFreeList;
	
	pFreeList = i_pFreeBlock;
}

int main()
{
	const size_t 		sizeHeap = 1024 * 1024;
	const unsigned int 	numDescriptors = 2048;

#ifdef USE_HEAP_ALLOC
	void* pHeapMemory = HeapAlloc(GetProcessHeap(), 0, sizeHeap);
#else
	// Get SYSTEM_INFO, which includes the memory page size
	SYSTEM_INFO SysInfo;
	GetSystemInfo(&SysInfo);
	// round our size to a multiple of memory page size
	assert(SysInfo.dwPageSize > 0);
	size_t sizeHeapInPageMultiples = SysInfo.dwPageSize * ((sizeHeap + SysInfo.dwPageSize) / SysInfo.dwPageSize);
	void* pHeapMemory = VirtualAlloc(NULL, sizeHeapInPageMultiples, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#endif
	assert(pHeapMemory);

	// Create a heap manager for my test heap.
	HeapManager* pHeapManager = CreateHeapManager(pHeapMemory, sizeHeap, numDescriptors);
	assert(pHeapManager);
	if (pHeapManager == nullptr)
		return false;
	


#ifdef TEST_SINGLE_LARGE_ALLOCATION
	// This is a test I wrote to check to see if using the whole block if it was almost consumed by 
	// an allocation worked. Also helped test my ShowFreeBlocks() and ShowOutstandingAllocations().
	{
#ifdef SUPPORTS_SHOWFREEBLOCKS
		ShowFreeBlocks(pHeapManager);
#endif // SUPPORTS_SHOWFREEBLOCKS

		size_t largestBeforeAlloc = GetLargestFreeBlock(pHeapManager);
		void* pPtr = alloc(pHeapManager, largestBeforeAlloc - HeapManager::s_MinumumToLeave);

		if (pPtr)
		{
#if defined(SUPPORTS_SHOWFREEBLOCKS) || defined(SUPPORTS_SHOWOUTSTANDINGALLOCATIONS)
			printf("After large allocation:\n");
#ifdef SUPPORTS_SHOWFREEBLOCKS
			ShowFreeBlocks(pHeapManager);
#endif // SUPPORTS_SHOWFREEBLOCKS
#ifdef SUPPORTS_SHOWOUTSTANDINGALLOCATIONS
			ShowOutstandingAllocations(pHeapManager);
#endif // SUPPORTS_SHOWOUTSTANDINGALLOCATIONS
			printf("\n");
#endif

			size_t largestAfterAlloc = GetLargestFreeBlock(pHeapManager);
			bool success = Contains(pHeapManager, pPtr) && IsAllocated(pHeapManager, pPtr);
			assert(success);

			success = free(pHeapManager, pPtr);
			assert(success);

			Collect(pHeapManager);

#if defined(SUPPORTS_SHOWFREEBLOCKS) || defined(SUPPORTS_SHOWOUTSTANDINGALLOCATIONS)
			printf("After freeing allocation and garbage collection:\n");
#ifdef SUPPORTS_SHOWFREEBLOCKS
			ShowFreeBlocks(pHeapManager);
#endif // SUPPORTS_SHOWFREEBLOCKS
#ifdef SUPPORTS_SHOWOUTSTANDINGALLOCATIONS
			ShowOutstandingAllocations(pHeapManager);
#endif // SUPPORTS_SHOWOUTSTANDINGALLOCATIONS
			printf("\n");
#endif

			size_t largestAfterCollect = GetLargestFreeBlock(pHeapManager);
		}
	}
#endif

	std::vector<void*> AllocatedAddresses;

	long	numAllocs = 0;
	long	numFrees = 0;
	long	numCollects = 0;

	// allocate memory of random sizes up to 1024 bytes from the heap manager
	// until it runs out of memory
	do
	{
		const size_t		maxTestAllocationSize = 1024;

		size_t			sizeAlloc = 1 + (rand() & (maxTestAllocationSize - 1));

#ifdef SUPPORTS_ALIGNMENT
		// pick an alignment
		const unsigned int	alignments[] = { 4, 8, 16, 32, 64 };

		const unsigned int	index = rand() % (sizeof(alignments) / sizeof(alignments[0]));

		const unsigned int	alignment = alignments[index];

		void* pPtr = alloc(pHeapManager, sizeAlloc, alignment);

		// check that the returned address has the requested alignment
		assert((reinterpret_cast<uintptr_t>(pPtr) & (alignment - 1)) == 0);
#else
		void* pPtr = alloc(pHeapManager, sizeAlloc);
#endif // SUPPORT_ALIGNMENT

}