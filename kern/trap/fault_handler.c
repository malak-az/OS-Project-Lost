/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>
#include <kern/proc/user_environment.h>

//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			/*============================================================================================*/
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
			//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
			//your code is here
			//uint32 pt_get_page_permissions(struct Env* ptr_env, uint32 virtual_address )
			uint32 all_perms=pt_get_page_permissions(faulted_env->env_page_directory,fault_va);
			uint32 pres=all_perms&PERM_PRESENT;
			uint32 marked=all_perms&PERM_AVAILABLE;
			uint32 user=all_perms&PERM_USER	;
			uint32 read=all_perms&PERM_WRITEABLE;
			//ask the doc about: the difference bet the range and the exact perm_user
			if(fault_va>=USER_HEAP_START && fault_va<USER_HEAP_MAX && pres==0 &&(marked==0)){
				//cprintf("for fault handler: unmarked user heap page");
				env_exit();
			}
			//fault_va>=USER_LIMIT
			if(pres && user==0){
				//cprintf("for fault handler: out of user bounds");
				env_exit();
			}
			if(pres && (read==0)){
				//cprintf("for fault handler: it is a read only page not allowed to write");
				env_exit();
			}

			/*============================================================================================*/

		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;

//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
//				cprintf("\nPage working set BEFORE fault handler...\n");
//				env_page_ws_print(faulted_env);
		//int ffb = sys_calculate_free_frames();

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//struct WS_List actvlist;

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
//basmala sign
int victim_isheree(uint32 *reff,uint32 *bld,int sz,int idx,int total){
#if USE_KHEAP
	int vic=-1,len=-1;
	for(int j=0;j<sz;j++){
		uint32 vaa=bld[j];
		int mx_nxt=(int)1e9;//valid?
		for(int k=idx+1;k<total;k++){
			if(reff[k]==vaa){
				mx_nxt=k;
				break;
			}
		}
		if(mx_nxt>len){
			len=mx_nxt;
			vic=j;
		}

    }
	return vic;
#else
	return 0;
#endif
}
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
#if USE_KHEAP

	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	//panic("get_optimal_num_faults() is not implemented yet...!!");
	//simulate
	//no frame ->fault all
	//ref cnt copying
	//built IWS va fake mem
	//full -> choose vic
	//hit miss check
	//minimze nxt
	int ret=LIST_SIZE(pageReferences);
	if(maxWSSize==0) return ret;
	if(ret==0) return 0;
	uint32 *reff=(uint32*)kmalloc(ret*sizeof(uint32));
	int idx=0;
	//personal comment:it is the easiest not the L3
	struct PageRefElement *ref;
	LIST_FOREACH(ref,pageReferences)reff[idx++]=ref->virtual_address;
	uint32 *bld=(uint32*)kmalloc(maxWSSize*sizeof(uint32));
	if(bld==NULL) {
		kfree(reff);
		return ret;
	}
	int szz=0;
	struct WorkingSetElement *curr;
	LIST_FOREACH_SAFE(curr,initWorkingSet,WorkingSetElement){
		if(szz<maxWSSize) bld[szz++]=curr->virtual_address;
		else break;
	}
	int f=0; //needs optimization?
	for(int i=0;i<ret;i++){
		uint32 va=reff[i];
		bool fnd=0;
		for(int j=0;j<szz;j++){
			if(bld[j]==va){
				fnd=1;
				break;
			}
		}
		if(fnd) continue;
		f++;
		if(szz<maxWSSize){
			bld[szz++]=va;
			continue;
		}
		//cprintf("%d",ret);
		int vic=victim_isheree(reff,bld,szz,i,ret);
		//cprintf("%d",vic);
		if(vic>=0) bld[vic]=va;
	}
	kfree(reff);
	kfree(bld);
	return f;
#else
	return 0;
#endif
}

void actvlist_init(struct Env *env){
#if USE_KHEAP

	//cprintf("just once");
	//LIST_INIT(&(env->activeList));
	if(LIST_SIZE(&(env->activeList))==0) {
	struct WorkingSetElement *curr;
	LIST_FOREACH_SAFE(curr,&(env->page_WS_list),WorkingSetElement){
		struct WorkingSetElement *new=env_page_ws_list_create_element(env,curr->virtual_address);
		new->empty=0;
		//pt_set_page_permissions(env->env_page_directory,curr->virtual_address,PERM_PRESENT,0);
		LIST_INSERT_TAIL(&(env->activeList),new);
	}
	}
#endif
}
bool isvalid_va(uint32 va){
	if((va>=USER_HEAP_START && va<USER_HEAP_MAX)) return 1;
	return 0;
}
void ref_insert(struct Env *env,uint32 va){
#if USE_KHEAP

	//if(!isvalid_va(va)) return;
	struct PageRefElement *new=(struct PageRefElement*) kmalloc(sizeof(struct PageRefElement));
	if(!new) {
		cprintf("kmalloc failed");
		return;
	}
	new->virtual_address=va;
	LIST_INSERT_TAIL(&(env->referenceStreamList),new);
#endif
}
bool inc_Actv(struct Env *env,uint32 va){
#if USE_KHEAP

	struct WorkingSetElement *curr;
	LIST_FOREACH_SAFE(curr,&(env->activeList),WorkingSetElement){
		if(curr->virtual_address==va) return 1;
	}
	return 0;
#else
	return 0;
#endif
}
int sz_Actv(struct Env *env){
#if USE_KHEAP

	int cnt=0;
	struct WorkingSetElement *curr;
	LIST_FOREACH(curr,&(env->activeList)) cnt++;
	return cnt;
#else
	return 0;
#endif
}
bool pagein_ws(struct Env *env,uint32 va){
#if USE_KHEAP

	if(!env) return 0;
	struct WorkingSetElement *curr;
	LIST_FOREACH_SAFE(curr,&(env->page_WS_list),WorkingSetElement){
		if(curr->virtual_address==va) return 1;
	}
	return 0;
#else
	return 0;
#endif
}


void replace_argoooook(struct Env* faulted_env, uint32 fault_va){
#if USE_KHEAP
	uint32 va_past=faulted_env->page_last_WS_element->virtual_address;
	 struct WorkingSetElement *victim = faulted_env->page_last_WS_element;
	uint32 *tb_ptr=NULL;
	struct FrameInfo *fr=get_frame_info(faulted_env->env_page_directory,va_past,&tb_ptr);
	uint32 perms=pt_get_page_permissions(faulted_env->env_page_directory,va_past);
	if((perms&PERM_MODIFIED)==PERM_MODIFIED) pf_update_env_page(faulted_env,va_past,fr);
	unmap_frame(faulted_env->env_page_directory,va_past);
	//if(fr) free_frame(fr);
	struct FrameInfo *frr=NULL;
	//int ret=allocate_frame(&frr);
	if(allocate_frame(&frr)!=0 || frr==NULL ){
		panic("error allocation");
	}
	uint32 permss=PERM_WRITEABLE|PERM_USER|PERM_PRESENT;
	map_frame(faulted_env->env_page_directory,frr,fault_va,permss);
	int ret=pf_read_env_page(faulted_env,(void*)fault_va);
	if(ret==E_PAGE_NOT_EXIST_IN_PF){
		uint32 vald=(fault_va>=USER_HEAP_START && fault_va<USER_HEAP_MAX);
	    vald|=(fault_va>=USTACKBOTTOM && fault_va<USTACKTOP);
	    if (!((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) ||
	    		(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP))){
	    	//unmap_frame(faulted_env->env_page_directory,fault_va); ->ask
	    	cprintf("here invalid");
	    	env_exit();
	    }
	}
	struct WorkingSetElement *curr,*nxt;
	struct WS_List temp;
	LIST_INIT(&temp);
	for(curr=victim->prev_next_info.le_next;curr!=NULL;curr=nxt){
		nxt=LIST_NEXT(curr);
		LIST_REMOVE(&(faulted_env->page_WS_list),curr);
		LIST_INSERT_TAIL(&temp,curr);
	}
	struct WorkingSetElement *nxtt;
	for(curr=LIST_FIRST(&(faulted_env->page_WS_list));curr!=victim;curr=nxtt){
		nxtt=LIST_NEXT(curr);
		LIST_REMOVE(&(faulted_env->page_WS_list),curr);
		LIST_INSERT_TAIL(&temp,curr);
	}
	struct WorkingSetElement *neww=env_page_ws_list_create_element(faulted_env,fault_va);
	LIST_INSERT_TAIL(&temp,neww);
	faulted_env->page_WS_list=temp;
	faulted_env->page_last_WS_element=LIST_FIRST(&(faulted_env->page_WS_list));
	//1 2 3 4 5
	//4 5 1 2 6
#endif
}

void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	if (isPageReplacmentAlgorithmOPTIMAL())
	{
		//cprintf("before is here0");
		//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
		//Your code is here
		//Comment the following line
		//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
		//logic
		//keep track of active WS
		//if fp in mem read from disk else set pres bit
		//if fp in WS do nothing else if active WS if full reset pres then del
		//add fp to active WS
		//add fp to list
	   // if(faulted_env->ActiveList.lh_first==NULL) actvlist_init(faulted_env);
		if(LIST_SIZE(&(faulted_env->activeList))==0) actvlist_init(faulted_env);
		//cprintf("Debug: fault @ 0x%x\n", fault_va);
		//print_reference_stream(faulted_env);
		uint32 va=ROUNDDOWN(fault_va,PAGE_SIZE);
		/*if(va>=USER_TOP){

			cprintf("basmala: invalid va",va);
			unmap_frame(faulted_env->env_page_directory,va);
			//env_exit();
			return;
		}*/
	//	cprintf("it passed");
		//ref_insert(faulted_env,va);
		uint32 perms=PERM_PRESENT|PERM_USER|PERM_WRITEABLE;
		uint32 *tb_ptr=NULL;
		struct FrameInfo* infoo=NULL;
		//get_frame_info(uint32 *ptr_page_directory, uint32 virtual_address, uint32 **ptr_page_table)
		infoo=get_frame_info(faulted_env->env_page_directory,va,&tb_ptr);
		if(infoo==0){
			//struct FrameInfo *fr=NULL;
			int vld=allocate_frame(&infoo);
			if(vld!=0){
				cprintf("Failed frame");
				env_exit();
				return;
			}
		    map_frame(faulted_env->env_page_directory,infoo,va,perms);
			//cprintf("loading page %x\n", va);
			int ret =pf_read_env_page(faulted_env, (void*)va); //<-rem prints
			 //cprintf("loading retttt 0x%x\n", ret);
			if(ret==E_PAGE_NOT_EXIST_IN_PF){
				//heap or stack acc
				uint32 vald=(fault_va>=USER_HEAP_START && fault_va<USER_HEAP_MAX);
			    vald|=(fault_va>=USTACKBOTTOM && fault_va<USTACKTOP);
			    if (!((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) ||
			    	(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP))){
					unmap_frame(faulted_env->env_page_directory,va);
					cprintf("invalid optimal\n");
					env_exit();
				//return;
				}
				//cprintf("not in disk",va,ret);
				//pt_set_page_permissions(faulted_env->env_page_directory,va,PERM_PRESENT,0);
				//tlb_invalidate(faulted_env->env_page_directory,(void*)va);
			}

			//pt_set_page_permissions(faulted_env->env_page_directory,va,1,PERM_PRESENT);
			tlb_invalidate(faulted_env->env_page_directory,(void*)va);
			//cprintf("set present here",va);
		}
		else {
			pt_set_page_permissions(faulted_env->env_page_directory,va,PERM_PRESENT,0);
			tlb_invalidate(faulted_env->env_page_directory,(void*)va);

		}
		//cprintf("here loaded");
		if(inc_Actv(faulted_env,va))return;
		int szzz=faulted_env->activeList.size;
		if(szzz>=faulted_env->page_WS_max_size){
			cprintf("yes checked");
			struct WorkingSetElement *curr=LIST_FIRST(&(faulted_env->activeList));
			while(curr!=NULL){
				struct WorkingSetElement *nxt=LIST_NEXT(curr);
				uint32 vaa=curr->virtual_address;
				LIST_REMOVE(&(faulted_env->activeList),curr);
				pt_set_page_permissions(faulted_env->env_page_directory,curr->virtual_address,0,PERM_PRESENT);
				tlb_invalidate(faulted_env->env_page_directory,(void*)vaa);
				curr=nxt;
			}

			LIST_INIT(&(faulted_env->activeList));
			//cprintf("it doesnot");
			//tlb_invalidate(faulted_env->env_page_directory,(void*)va);
		}
		//cprintf("lstpnt");
		struct WorkingSetElement *new=env_page_ws_list_create_element(faulted_env,va);
		if(!new){
			//cprintf("creation error at last");
			return;
		}
		new->empty=0;
		LIST_INSERT_TAIL(&(faulted_env->activeList),new);
		pt_set_page_permissions(faulted_env->env_page_directory,va,PERM_PRESENT,0);
		ref_insert(faulted_env,va);
		tlb_invalidate(faulted_env->env_page_directory,(void*)va);
		//cprintf("done");
		return;
	}
	else
	{
		struct WorkingSetElement *victimWSElement = NULL;
		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
		if(wsSize < (faulted_env->page_WS_max_size))
		{
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
			//Your code is here
			//Comment the following line
			//panic("page_fault_handler().PLACEMENT is not implemented yet...!!");

			//allocate page
			//read page to mem
			//if DNE-> acc stack,heap exit other
			//add to WS list
		    //cprintf("deb bef");
		    //print_ws(faulted_env,fault_va);
			//cprintf("fault %x\n",fault_va);
			uint32 *tb_ptr=NULL;
			struct FrameInfo* fr_inf=NULL;
			//get_frame_info(uint32 *ptr_page_directory, uint32 virtual_address, uint32 **ptr_page_table)
			fr_inf=get_frame_info(faulted_env->env_page_directory,fault_va,&tb_ptr);
			int vd=allocate_frame(&fr_inf); // wanna check it first but??
			if(vd!=E_NO_MEM){
			//map_frame(uint32 *ptr_page_directory, struct FrameInfo *ptr_frame_info, uint32 virtual_address, int perm)
			int perms=PERM_USER|PERM_WRITEABLE;
			vd=map_frame(faulted_env->env_page_directory,fr_inf,fault_va,perms);//wanna check
			if(vd!=E_NO_MEM){
			//pf_read_env_page(struct Env* ptr_env, void* virtual_address)
			void* curr_va=(void *) fault_va;
			int valid=pf_read_env_page(faulted_env,curr_va);
			if(valid==E_PAGE_NOT_EXIST_IN_PF){
			   //heap or stack acc
		       uint32 vald=(fault_va>=USER_HEAP_START && fault_va<USER_HEAP_MAX);
		       vald|=(fault_va>=USTACKBOTTOM && fault_va<USTACKTOP);
		       if (!((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) ||
		       	(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP))){
		    	   //cprintf("invalid placement\n");
                   env_exit();}
			       }
			 //env_page_ws_list_create_element(struct Env* e, uint32 virtual_address)
			 struct WorkingSetElement* add_ws=env_page_ws_list_create_element(faulted_env,fault_va);
			 //LIST_INSERT_AFTER(Linked_List * list, Type_inside_list* listElem,Type_inside_list* elemToInsert
			 if(add_ws!=NULL){
				LIST_INSERT_TAIL(&(faulted_env->page_WS_list),add_ws);
				if(faulted_env->page_WS_max_size==LIST_SIZE(&(faulted_env->page_WS_list)))
				    faulted_env->page_last_WS_element=LIST_FIRST(&(faulted_env->page_WS_list));
				 else faulted_env->page_last_WS_element=NULL;
			  }

		      }
			  }
					//cprintf("deb after");
					//print_ws(faulted_env,fault_va);
		}
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
						{
							//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
							//Your code is here
							//Comment the following line
							//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");

				//cprintf("before");
				//print_ws(faulted_env,fault_va);
				fault_va=ROUNDDOWN(fault_va,PAGE_SIZE);
							struct WorkingSetElement *curr_lst=NULL;
							if(faulted_env->page_last_WS_element==NULL)
								faulted_env->page_last_WS_element=LIST_FIRST(&(faulted_env->page_WS_list));
							int mx=faulted_env->page_WS_max_size;
							bool done=0;

							for(int i=0;i<2*mx &&(!done);i++){
								if(done) break;
								if(faulted_env->page_last_WS_element==NULL)
								    faulted_env->page_last_WS_element=LIST_FIRST(&(faulted_env->page_WS_list));
								uint32 va=faulted_env->page_last_WS_element->virtual_address;
								uint32 perms=pt_get_page_permissions(faulted_env->env_page_directory,va);
								if((perms&PERM_USED)==PERM_USED){
									pt_set_page_permissions(faulted_env->env_page_directory,va,0,PERM_USED);
									faulted_env->page_last_WS_element=faulted_env->page_last_WS_element->prev_next_info.le_next;
									if(faulted_env->page_last_WS_element==NULL)
										faulted_env->page_last_WS_element=LIST_FIRST(&(faulted_env->page_WS_list));
								}
								else{
									replace_argoooook(faulted_env,fault_va);
									curr_lst=faulted_env->page_last_WS_element->prev_next_info.le_next;
									if(curr_lst==NULL) curr_lst=LIST_FIRST(&(faulted_env->page_WS_list));
									done=1;
									break;
						  	    }


						    }
							//cprintf("after");
							//print_ws(faulted_env,fault_va);
						}

			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
		}
	}
#endif
}


void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}



