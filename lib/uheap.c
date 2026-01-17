#include <inc/lib.h>

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
struct user_pages_block_info user_blockarr[NUM_OF_UHEAP_PAGES];
//==============================================
// [1] INITIALIZE USER HEAP:
//==============================================
int __firstTimeFlag = 1;
int block_cnt;
void uheap_init()
{
	if(__firstTimeFlag)
	{
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();
		uheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		uheapPageAllocBreak = uheapPageAllocStart;
		block_cnt=0;
		__firstTimeFlag = 0;
	}
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER|PERM_WRITEABLE|PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() in user: failed to allocate page from the kernel");

	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() in user: failed to return a page to the kernel");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
void * our_customfit(uint32 size){

#if USE_KHEAP
// if(block_cnt>NUM_OF_UHEAP_PAGES)//cprintf("big block\n");
//	cprintf("cf\n");
	if(size>USER_HEAP_MAX-USER_HEAP_START){
		return NULL;
	}
	int worst_arr_index=-1;
	bool found=0;
	int needed_pages=ROUNDUP(size,PAGE_SIZE)/PAGE_SIZE;
	int max_pages=0;
	uint32 va;
	if(uheapPageAllocBreak==uheapPageAllocStart){
		user_blockarr[0].block_start_address=uheapPageAllocStart;
		user_blockarr[0].num_of_pages_occupied=needed_pages;
		user_blockarr[0].notfree=1;
		va=user_blockarr[0].block_start_address;
		uheapPageAllocBreak=user_blockarr[0].block_start_address+ROUNDUP(size,PAGE_SIZE);

		block_cnt++;
		found = 1;

	}
	else{
	//	cprintf("exact fit\n");

		for(int i=0;i<block_cnt;i++){
			if(user_blockarr[i].notfree==0&&user_blockarr[i].num_of_pages_occupied==needed_pages){
				user_blockarr[i].notfree=1;
				va=user_blockarr[i].block_start_address;
				found=1;
				break;
			}
		}
		if(!found){

			for(int i=0;i<block_cnt;i++){
				if(user_blockarr[i].notfree==0&&user_blockarr[i].num_of_pages_occupied>needed_pages){
					if(user_blockarr[i].num_of_pages_occupied>max_pages){
						max_pages=user_blockarr[i].num_of_pages_occupied;
					worst_arr_index=i;}

				}
			}

		if(worst_arr_index>-1){

	    	for (int j=block_cnt;j>worst_arr_index+1;j--)
			user_blockarr[j]=user_blockarr[j-1];
			user_blockarr[worst_arr_index+1].num_of_pages_occupied=user_blockarr[worst_arr_index].num_of_pages_occupied-needed_pages;
			user_blockarr[worst_arr_index +1].block_start_address=user_blockarr[worst_arr_index].block_start_address+ROUNDUP(size,PAGE_SIZE);
			user_blockarr[worst_arr_index +1].notfree=0;
			user_blockarr[worst_arr_index].notfree=1;
			user_blockarr[worst_arr_index].num_of_pages_occupied=needed_pages;
			va=user_blockarr[worst_arr_index].block_start_address;

			found=1;
			block_cnt++;


			}

		}
		if(!found){
		//	cprintf("extend\n");
			if (USER_HEAP_MAX - uheapPageAllocBreak >= ROUNDUP(size, PAGE_SIZE)){
				user_blockarr[block_cnt].block_start_address=uheapPageAllocBreak;
				user_blockarr[block_cnt].notfree=1;
				user_blockarr[block_cnt].num_of_pages_occupied=needed_pages;
				va =  user_blockarr[block_cnt].block_start_address;
				uheapPageAllocBreak = uheapPageAllocBreak + ROUNDUP(size, PAGE_SIZE);

				found = 1;
			//	cprintf("before sys 2");
				block_cnt++;
			}
			else{
				return NULL;
			}

		}

	}

	return (void*)va;
#else
	return NULL;
#endif
}
//=================================
// [1] ALLOCATE SPACE IN USER HEAP:
//=================================
void* malloc(uint32 size)
{
	#if USE_KHEAP
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();

	if (size == 0) return NULL ;

	//==============================================================
	//TODO: [PROJECT'25.IM#2] USER HEAP - #1 malloc
	//Your code is here
	//Comment the following line
	//panic("malloc() is not implemented yet...!!");
	//cprintf("mallocc\n");
	if(size<=DYN_ALLOC_MAX_BLOCK_SIZE){
		return alloc_block(size);
	}
	else{
		void*va=our_customfit(size);
		if(!va)return NULL;
if(va<(void*)USER_HEAP_START||va>=(void*)USER_HEAP_MAX){
//	cprintf("error in malloc");
	return NULL;
}
      sys_allocate_user_mem((uint32)va,ROUNDUP(size,PAGE_SIZE));

	return va;
	}
#else
	return NULL;
#endif
	}

//=================================
// [2] FREE SPACE FROM USER HEAP:
//=================================
void free(void* virtual_address)
{
#if USE_KHEAP
//	cprintf("free\n");
	bool last=0;
 if(! virtual_address)return;
	if(virtual_address >=(void*)dynAllocStart && virtual_address < (void*)dynAllocEnd){
			free_block(virtual_address);
			return;
		}
	else if(virtual_address >= (void*)uheapPageAllocStart && virtual_address < (void*)uheapPageAllocBreak){
		virtual_address=(void*)ROUNDDOWN(virtual_address,PAGE_SIZE);
		int chosen_indx=-1;
	   for(int i=0;i<block_cnt;i++){
		   if((void*)user_blockarr[i].block_start_address==virtual_address){
			   chosen_indx=i;
			   break;}
 }

 if(chosen_indx==-1)return;
 if(user_blockarr[chosen_indx].notfree==0){
		panic("address is already free !");
			}
			if (chosen_indx==block_cnt-1)
					last=1;

 uint32 size2=user_blockarr[chosen_indx].num_of_pages_occupied*PAGE_SIZE;
			sys_free_user_mem(user_blockarr[chosen_indx].block_start_address,size2);
			user_blockarr[chosen_indx].notfree=0;


		while(chosen_indx<block_cnt-1&&user_blockarr[chosen_indx+1].notfree==0){
		user_blockarr[chosen_indx].num_of_pages_occupied+=user_blockarr[chosen_indx+1].num_of_pages_occupied;
		for(int j=chosen_indx+1;j<block_cnt-1;j++){
				user_blockarr[j]=user_blockarr[j+1];
				}
				block_cnt--;
							}

				if(chosen_indx>0&&user_blockarr[chosen_indx-1].notfree==0){
				user_blockarr[chosen_indx-1].num_of_pages_occupied+=user_blockarr[chosen_indx].num_of_pages_occupied;
				for(int j=chosen_indx;j<block_cnt-1;j++){
				user_blockarr[j]=user_blockarr[j+1];
					}

				block_cnt--;
				chosen_indx--;
				}



			if (block_cnt>0&&user_blockarr[block_cnt-1].notfree==0){
			uheapPageAllocBreak=user_blockarr[block_cnt-1].block_start_address;
             block_cnt--;
			}

	}else panic("address out of bounds !");

	//TODO: [PROJECT'25.IM#2] USER HEAP - #3 free
	//Your code is here
	//Comment the following line
	//panic("free() is not implemented yet...!!");
#endif
}

//=================================
// [3] ALLOCATE SHARED VARIABLE:
//=================================
void* smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
#if USE_KHEAP
//	cprintf("smallocc\n");
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//cprintf("hello\n");
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	//Your code is here
	//cprintf("before cf smalloc\n");
	void*va=our_customfit(size);
	//cprintf("after cf smalloc\n");
	if(!va)return NULL;

//	cprintf("before sys create\n");
    int created_add = sys_create_shared_object(sharedVarName, size, isWritable, va);
	//cprintf("after sys create\n");
    if(created_add==E_NO_SHARE||created_add==E_SHARED_MEM_EXISTS)return NULL;


    return va;


#else
   return NULL;
#endif
	//Comment the following line
	//panic("smalloc() is not implemented yet...!!");
    }


//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void* sget(int32 ownerEnvID, char *sharedVarName)
{
#if USE_KHEAP
//	cprintf("sgetBefinit\n");
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #4 sget
	//Your code is here
//	cprintf("sget\n");
	int size=sys_size_of_shared_object(ownerEnvID,sharedVarName);
	if(size==E_SHARED_MEM_NOT_EXISTS){//cprintf("sgetnonexist\n");
	return NULL;}


	void*va=our_customfit((uint32)size);

	if(!va){//cprintf("VAnonexist\n");
	return NULL;}

	int created_add = sys_get_shared_object(ownerEnvID, sharedVarName,va);
	if(created_add==E_SHARED_MEM_NOT_EXISTS)return NULL;
	return va;
#else
   return NULL;
#endif
	//Comment the following line
	//panic("sget() is not implemented yet...!!");
}


//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//


//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}


//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void* virtual_address)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	//Your code is here
	//Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}


//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
