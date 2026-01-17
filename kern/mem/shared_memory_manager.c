#include <inc/memlayout.h>
#include "shared_memory_manager.h"

#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/queue.h>
#include <inc/environment_definitions.h>

#include <kern/proc/user_environment.h>
#include <kern/trap/syscall.h>
#include "kheap.h"
#include "memory_manager.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] INITIALIZE SHARES:
//===========================
//Initialize the list and the corresponding lock
void sharing_init()
{
#if USE_KHEAP
	LIST_INIT(&AllShares.shares_list) ;
	init_kspinlock(&AllShares.shareslock, "shares lock");
	//init_sleeplock(&AllShares.sharessleeplock, "shares sleep lock");
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//=========================
// [2] Find Share Object:
//=========================
//Search for the given shared object in the "shares_list"
//Return:
//	a) if found: ptr to Share object
//	b) else: NULL
struct Share* find_share(int32 ownerID, char* name)
{
#if USE_KHEAP
	struct Share * ret = NULL;
	bool wasHeld = holding_kspinlock(&(AllShares.shareslock));
	if (!wasHeld)
	{
		acquire_kspinlock(&(AllShares.shareslock));
	}
	{
		struct Share * shr ;
		LIST_FOREACH(shr, &(AllShares.shares_list))
		{
			//cprintf("shared var name = %s compared with %s\n", name, shr->name);
			if(shr->ownerID == ownerID && strcmp(name, shr->name)==0)
			{
				//cprintf("%s found\n", name);
				ret = shr;
				break;
			}
		}
	}
	if (!wasHeld)
	{
		release_kspinlock(&(AllShares.shareslock));
	}
	return ret;
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//==============================
// [3] Get Size of Share Object:
//==============================
int size_of_shared_object(int32 ownerID, char* shareName)
{
	// This function should return the size of the given shared object
	// RETURN:
	//	a) If found, return size of shared object
	//	b) Else, return E_SHARED_MEM_NOT_EXISTS
	//
	struct Share* ptr_share = find_share(ownerID, shareName);
	if (ptr_share == NULL)
		return E_SHARED_MEM_NOT_EXISTS;
	else
		return ptr_share->size;

	return 0;
}
//===========================================================


//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=====================================
// [1] Alloc & Initialize Share Object:
//=====================================
//Allocates a new shared object and initialize its member
//It dynamically creates the "framesStorage"
//Return: allocatedObject (pointer to struct Share) passed by reference
struct Share* alloc_share(int32 ownerID, char* shareName, uint32 size, uint8 isWritable)
{
	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #1 alloc_share
	//Your code is here
#if 	USE_KHEAP
	//cprintf("alloc_share\n");

	struct Share* obj=(struct Share *)kmalloc(sizeof(struct Share));
	if(!obj)return NULL;

	obj->references=1;
	int va=(int)obj;//int32
	va&=0x7FFFFFFF;
	obj->ID=va;
	obj->isWritable=isWritable;
	strcpy(obj->name, shareName);
	obj->ownerID=ownerID;
	obj->size=size;

	int  noframes= ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;

    obj->framesStorage=(struct FrameInfo**) kmalloc(noframes * sizeof(struct FrameInfo *));
    if(!obj->framesStorage){
    	kfree((void *)obj);
        return NULL;
    }

    for (int i = 0; i < noframes; i++)
            obj->framesStorage[i] = 0;

   return obj;
#else
   return NULL;
#endif
	//Comment the following line
	//panic("alloc_share() is not implemented yet...!!");
}


//=========================
// [4] Create Share Object:
//=========================
int create_shared_object(int32 ownerID, char* shareName, uint32 size, uint8 isWritable, void* virtual_address)
{
	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #3 create_shared_object
	//Your code is here
#if USE_KHEAP
//	cprintf("create_shared_object\n");
    struct Share* sh=find_share(ownerID,shareName);
	if(sh!=NULL){
	//	cprintf("EXISTS\n");
		return E_SHARED_MEM_EXISTS ;}

	struct Env* myenv = get_cpu_proc(); //The calling environment

	struct Share *obj=alloc_share(ownerID,shareName,size,isWritable);
	if(!obj)
		return E_NO_SHARE;


	bool holdd = holding_kspinlock(&(AllShares.shareslock));
	if (!holdd)
	{
		acquire_kspinlock(&(AllShares.shareslock));
	}

	LIST_INSERT_TAIL(&AllShares.shares_list,obj);

	if (!holdd)
	{
		release_kspinlock(&(AllShares.shareslock));
	}

	int  noframes= ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;

	struct FrameInfo** frms=obj->framesStorage;
	uint32 va=(uint32)virtual_address;

	for(int i=0;i<noframes;i++){
		int alloc_f=allocate_frame(&frms[i]);
		if(alloc_f!=0)return E_NO_SHARE;
		int mpf=map_frame(myenv->env_page_directory,frms[i],va+(i*PAGE_SIZE),PERM_USER|PERM_WRITEABLE);
		if(mpf!=0)return E_NO_SHARE;

	}

	return (int)obj->ID ;//revise the return


	//Comment the following line
	//panic("create_shared_object() is not implemented yet...!!");

#else
   return 0;
#endif
	// This function should create the shared object at the given virtual address with the given size
	// and return the ShareObjectID
	// RETURN:
	//	a) ID of the shared object (its VA after masking out its msb) if success
	//	b) E_SHARED_MEM_EXISTS if the shared object already exists
	//	c) E_NO_SHARE if failed to create a shared object
}


//======================
// [5] Get Share Object:
//======================
int get_shared_object(int32 ownerID, char* shareName, void* virtual_address)
{
	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #5 get_shared_object
	//Your code is here
	//Comment the following line
	//panic("get_shared_object() is not implemented yet...!!");
#if USE_KHEAP
//	cprintf("get_shared_object\n");
	struct Env* myenv = get_cpu_proc(); //The calling environment
//   cprintf("firstget\n");

	struct Share* obj=find_share(ownerID,shareName);
	if(obj==NULL)
		return E_SHARED_MEM_NOT_EXISTS ;

	int  noframes= ROUNDUP(obj->size, PAGE_SIZE) / PAGE_SIZE;
    uint32 va=(uint32)virtual_address;

    int perm = PERM_USER;
    if(obj->isWritable){perm |= PERM_WRITEABLE;}
	  for(int i=0;i<noframes;i++){
		//cprintf("framing\n");
		struct FrameInfo* frm=obj->framesStorage[i];
		map_frame(myenv->env_page_directory,frm,va+(i*PAGE_SIZE),perm);

      }

	//cprintf("out of framing\n");
	bool holdd = holding_kspinlock(&(AllShares.shareslock));
	if (!holdd)
	    acquire_kspinlock(&(AllShares.shareslock));

	obj->references++;

	if (!holdd)
	    release_kspinlock(&(AllShares.shareslock));

	return (int)obj->ID ;


#else
	return 0;
#endif
	// 	This function should share the required object in the heap of the current environment
	//	starting from the given virtual_address with the specified permissions of the object: read_only/writable
	// 	and return the ShareObjectID
	// RETURN:
	//	a) ID of the shared object (its VA after masking out its msb) if success
	//	b) E_SHARED_MEM_NOT_EXISTS if the shared object is not exists

}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//
//=========================
// [1] Delete Share Object:
//=========================
//delete the given shared object from the "shares_list"
//it should free its framesStorage and the share object itself
void free_share(struct Share* ptrShare)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - free_share
	//Your code is here
#if USE_KHEAP

	bool holdd = holding_kspinlock(&(AllShares.shareslock));
	if (!holdd)
	{
		acquire_kspinlock(&(AllShares.shareslock));
	}

	LIST_REMOVE(&AllShares.shares_list,ptrShare);

	if (!holdd)
	{
		release_kspinlock(&(AllShares.shareslock));
	}
	int sz=ptrShare->size;
	int  noframes= ROUNDUP(sz, PAGE_SIZE) / PAGE_SIZE;
	for(int i=0;i< noframes;i++){
      if(ptrShare->framesStorage[i]!=NULL){
		free_frame(ptrShare->framesStorage[i]);
	  }
	}

	kfree(ptrShare->framesStorage);
	kfree(ptrShare);


#endif	//Comment the following line
//	panic("free_share() is not implemented yet...!!");
}


//=========================
// [2] Free Share Object:
//=========================
int delete_shared_object(int32 sharedObjectID, void *startVA)
{

  //TODO: [PROJECT'25.BONUS#5] EXIT #2 - delete_shared_object
	//Your code is here
	//Comment the following line
//	panic("delete_shared_object() is not implemented yet...!!");
#if USE_KHEAP
bool holdd = holding_kspinlock(&(AllShares.shareslock));
	if (!holdd)
	{
		acquire_kspinlock(&(AllShares.shareslock));
	}

 struct Env* myenv = get_cpu_proc(); //The calling environment

struct Share *sh;
	bool fnd=0;
	LIST_FOREACH(sh, &AllShares.shares_list){
		if(sh->ID==sharedObjectID){
			fnd=1;
			break;
		}
	}
	if(!fnd){

		if (!holdd)
		{
			release_kspinlock(&(AllShares.shareslock));
		}
		return E_SHARED_MEM_NOT_EXISTS;}

	uint32 va=(uint32)ROUNDDOWN(startVA,PAGE_SIZE);
	uint32 * pt_ptble;

	int sz=sh->size;
	int  noframes= ROUNDUP(sz, PAGE_SIZE) / PAGE_SIZE;
	for(int i=0;i< noframes;i++){
		unmap_frame(myenv->env_page_directory,va+(i*PAGE_SIZE));
		get_page_table(myenv->env_page_directory,va+(i*PAGE_SIZE),&pt_ptble);
	}
	bool empty=1;
	for(int i=0;i<1024;i++){
	  if ((pt_ptble[i] & PERM_PRESENT)){
		  empty=0;
		  break;
	  }
	}
	  if(empty)del_page_table(myenv->env_page_directory,va);


	sh->references--;

	if(sh->references==0){
		free_share(sh);
	}
	tlbflush();

if (!holdd)
{
	release_kspinlock(&(AllShares.shareslock));
}
return 0;
#else
  return 0;
#endif
// This function should free (delete) the shared object from the User Heapof the current environment
	// If this is the last shared env, then the "frames_store" should be cleared and the shared object should be deleted
	// RETURN:
	//	a) 0 if success
	//	b) E_SHARED_MEM_NOT_EXISTS if the shared object is not exists

	// Steps:
	//	1) Get the shared object from the "shares" array (use get_share_object_ID())
	//	2) Unmap it from the current environment "myenv"
	//	3) If one or more table becomes empty, remove it
	//	4) Update references
	//	5) If this is the last share, delete the share object (use free_share())
	//	6) Flush the cache "tlbflush()"

}
