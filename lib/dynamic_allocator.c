/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"
//#include"../inc/environment_definitions.h"
//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
inline unsigned int Log2(unsigned int x) {
	if (x <= 1) return 1;
	//int power = 2;
	int bits_cnt = 2 ;
	x--;
	while (x >>= 1) {
		//power <<= 1;
		bits_cnt++ ;
	}
	return bits_cnt;
}
//==================================

inline unsigned int nearestPow2(unsigned int x) {
	if (x <= 1) return 1;
	int power = 2;
	x--;
	while (x >>= 1) {
		power <<= 1;
	}
	return power;
}
//==================================.

uint32 our_log_2(uint32  base){
	 uint32 num=base;
	 uint32 pow=0;
	    while(num>1){
	    	num>>=1;
	    	pow++;

	    }return pow;
}

uint32 our_nearestpow2(uint32 n){
	if(n<8)return 8;
	uint32 ret=8;
	while(n>ret){
		ret*=2;
	}
	return ret;

}

// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	if (ptrPageInfo < &pageBlockInfoArr[0] || ptrPageInfo >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE/PAGE_SIZE])
			panic("to_page_va called with invalid pageInfoPtr");
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================
// [2] GET PAGE INFO OF PAGE VA:
//==================================
__inline__ struct PageInfoElement * to_page_info(uint32 va)
{
	int idxInPageInfoArr = (va - dynAllocStart) >> PGSHIFT;
	if (idxInPageInfoArr < 0 || idxInPageInfoArr >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("to_page_info called with invalid pa");
	return &pageBlockInfoArr[idxInPageInfoArr];
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here
	dynAllocStart=daStart;
		dynAllocEnd=daEnd;
		 for (int i=0;i<(LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1);i++){
			    	 LIST_INIT(&freeBlockLists[i]);
			     }
		   LIST_INIT(&freePagesList);
		uint32 idx = (daEnd-daStart)/PAGE_SIZE;
	     for(int i=0;i<idx;i++){
	    	 pageBlockInfoArr[i].block_size = 0;
	    	 pageBlockInfoArr[i].num_of_free_blocks = 0;
	    	 LIST_INSERT_TAIL(&freePagesList, &pageBlockInfoArr[i]);
	     }
	 	//==================================================================================

	  	//==================================================================================

	   	//==================================================================================
	//Comment the following line
	//panic("initialize_dynamic_allocator() Not implemented yet");

}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here
    int index=(uint32)(va - dynAllocStart)>>12;
    return pageBlockInfoArr[index].block_size;

	//Comment the following line
	//panic("get_block_size() Not implemented yet");
}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here
	//cprintf("start\n");

	if(size==0)return NULL;
    uint32 base=8;
  //  cprintf("size%d\n",size);

	if(base < size){
		base=nearestPow2(size);
	}//cprintf("nearstpow2%d\n",base);

	int idx=our_log_2(base)-LOG2_MIN_SIZE;//-3
	for(int i = idx ; i < (LOG2_MAX_SIZE - LOG2_MIN_SIZE)+1; i++){
	//	cprintf("outer loop\n");

	   if(!(LIST_EMPTY(&freeBlockLists[i]))){//if found a block dec freePagesList
//		   cprintf("found a block\n");

		  struct BlockElement * block=LIST_FIRST(&freeBlockLists[i]);
		  LIST_REMOVE(&freeBlockLists[i],block);
//		   cprintf("took a block\n");

		  struct PageInfoElement *page=to_page_info((uint32)block);//uint32

		   page->num_of_free_blocks--;
	//	   cprintf("Block VA = 0x%08x\n", (uint32)block);

	//	  cprintf("before block return\n");
		   return (void *)block;
	   }
	   else if(!(LIST_EMPTY(&freePagesList))){//hashab page w a2smha blocks(else, if a free page exists)
//		   cprintf("take a page\n");

		   struct PageInfoElement *page=LIST_FIRST(&freePagesList);
		   LIST_REMOVE(&freePagesList,page);

		   uint32 page_va = to_page_va(page);
		   get_page((void *)page_va);
		   page=to_page_info((uint32)page_va);

		   uint32 powof2=our_log_2(base);
		   int nofblocks =(PAGE_SIZE>>powof2);

		   page->block_size=base;
		   page->num_of_free_blocks=nofblocks-1;//tm ta2seem page and then i'll add it to the freeblocklist

		   uint32 add=(uint32)page_va;
		   for (int j = 1; j < nofblocks; j++) {
			//   cprintf("bblks\n");

			   add+=(uint32)base;
			   struct BlockElement *block = (struct BlockElement *)add;
			   //cprintf("page_va = %x\n", add);
		       LIST_INSERT_TAIL(&freeBlockLists[idx], block);

		   }

           return (void * )page_va;
	   }
	   else if(LIST_EMPTY(&freeBlockLists[i])&&LIST_EMPTY(&freePagesList)){
	//	   cprintf("case 3\n");

		   continue;
	   }else{
		   panic("Dynamic allocator is Full, Please free some memory");

	   }
  }

   return NULL;



	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================
	//cprintf("block Free\n");
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here
	 uint32 r_va=ROUNDDOWN((uint32)va,PAGE_SIZE);
	uint32 size=get_block_size(va);//get the size of the va
	struct PageInfoElement *page=to_page_info(r_va);//get the page
    uint32 page_va = to_page_va(page);//va to send it to the return page

	unsigned int powof2=our_log_2(size);
	unsigned int idx=powof2-LOG2_MIN_SIZE;//first block is 2^3 at idx=0;-3

	struct BlockElement *block = (struct BlockElement *)va;
	LIST_INSERT_TAIL(&freeBlockLists[idx], block);//return the block to the list

	page->num_of_free_blocks++;

	uint16 nofblocks =(PAGE_SIZE>>powof2);
	if(page->num_of_free_blocks==nofblocks){
		for(int i=0;i<nofblocks;i++){
			 struct BlockElement* block =(struct BlockElement*)(r_va + i * size);//gbna el blocks w shelnaha
			LIST_REMOVE(&freeBlockLists[idx],block);
		}
		page->block_size=0;
		page->num_of_free_blocks=0;

		return_page((void *)r_va);

		LIST_INSERT_TAIL(&freePagesList,page);//rg3naha lel list el free pages

	}

	//Comment the following line
	//panic("free_block() Not implemented yet");
}
//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================

void *realloc_block(void* va, uint32 new_size)
{
    //TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
    //Your code is here
    void * ret_va ;
    uint32 mve_size = get_block_size(va);

    if((uint32)va > dynAllocEnd){//in page asln fa alloc dirct
        ret_va = alloc_block(new_size);
        mve_size = new_size;
    }
    else{
        if(mve_size >= new_size){
            ret_va =va;
        	return ret_va;
        }
        else{
            ret_va = alloc_block(new_size);
        }
    }
    if (ret_va != NULL) {
		if (mve_size > new_size)
			mve_size = new_size;
		memcpy(ret_va, va, mve_size);
    }

    return ret_va;

    //Comment the following line
    //panic("realloc_block() Not implemented yet");
}


