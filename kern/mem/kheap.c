#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"
#include <inc/queue.h>

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
//Remember to initialize locks (if any)

	 bool found;
	 bool fullspace;
	 struct kspinlock kheapLock;



void kheap_init()
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		kheapPageAllocBreak = kheapPageAllocStart;
	    LIST_INIT(&block_list); //in bef
        fullspace=0;
	    init_kspinlock(&kheapLock, "kheapLock");
	}
	//==================================================================================
	//==================================================================================
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	//mai: for kheap_virtual_address fun
	//uint32* page_table_va = NULL;
	uint32 pa=kheap_physical_address((unsigned int) va);
	struct FrameInfo* frame = to_frame_info(pa);
	frame->page_va = (uint32)va;
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
	//uint32 pa=kheap_physical_address((unsigned int) va);
	//struct FrameInfo* frame = to_frame_info(pa);
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
//BSTs for our beloved kmalloc and kfreee
struct pages_block_info {
    uint32 block_start_address;
    uint32 num_of_pages_occupied;
    uint8 notfree;
    struct addr_node *addr_ptr;
    struct size_node *size_ptr;
};

struct addr_node {
    uint32 st_ad;
    struct pages_block_info *b_info;
    struct addr_node *left, *right;
    int height;
};
struct size_node {
    uint32 pages;
    uint32 start;
    struct pages_block_info *b_info;
    struct size_node *left, *right;
    int height;
    uint32 subtree_max_pages;
};
struct addr_node *addr_root = NULL;
struct size_node *size_root = NULL;
int max_int(int a, int b) {
	return (a > b) ? a : b;
}
int height_addrnode(struct addr_node *n) { return n ? n->height : 0; }
void update_height_addrnode(struct addr_node *n) {
    if (!n) return;
    n->height = 1 + max_int(height_addrnode(n->left), height_addrnode(n->right));
}
int balance_addrnode(struct addr_node *n) {
    if (!n) return 0;
    return height_addrnode(n->left) - height_addrnode(n->right);
}
struct addr_node* rotate_right_addrnode(struct addr_node* y) {
    struct addr_node *x = y->left;
    struct addr_node *T2 = x->right;
    x->right = y;
    y->left = T2;
    update_height_addrnode(y);
    update_height_addrnode(x);
    return x;
}
struct addr_node* rotate_left_addrnode(struct addr_node* x) {
    struct addr_node *y = x->right;
    struct addr_node *T2 = y->left;
    y->left = x;
    x->right = T2;
    update_height_addrnode(x);
    update_height_addrnode(y);
    return y;
}
struct addr_node* addr_insert_node(struct addr_node* node, struct addr_node* to_insert) {
    if (!node) {
        to_insert->left = to_insert->right = NULL;
        to_insert->height = 1;
        return to_insert;
    }
    if (to_insert->st_ad< node->st_ad)
        node->left = addr_insert_node(node->left, to_insert);
    else if (to_insert->st_ad > node->st_ad)
        node->right = addr_insert_node(node->right, to_insert);
    else
        return node;

    update_height_addrnode(node);
    int bal = balance_addrnode(node);

    if (bal > 1 && to_insert->st_ad< node->left->st_ad)
        return rotate_right_addrnode(node);
    if (bal < -1 && to_insert->st_ad > node->right->st_ad)
        return rotate_left_addrnode(node);
    if (bal > 1 && to_insert->st_ad > node->left->st_ad) {
        node->left = rotate_left_addrnode(node->left);
        return rotate_right_addrnode(node);
    }
    if (bal < -1 && to_insert->st_ad < node->right->st_ad) {
        node->right = rotate_right_addrnode(node->right);
        return rotate_left_addrnode(node);
    }
    return node;
}

struct addr_node* addr_min_node(struct addr_node* n) {
    struct addr_node* cur = n;
    while (cur && cur->left) cur = cur->left;
    return cur;
}
struct addr_node* addr_delete_node(struct addr_node* root, uint32 key) {
    if (!root) return NULL;
    if (key < root->st_ad) {
        root->left = addr_delete_node(root->left, key);
    } else if (key > root->st_ad) {
        root->right = addr_delete_node(root->right, key);
    } else {
        // found
        if (!root->left || !root->right) {
            struct addr_node *temp = root->left ? root->left : root->right;

            free_block((void*)root);
            return temp;
        } else {
            struct addr_node *temp = addr_min_node(root->right);

            uint32 tmp_key = temp->st_ad;
            struct pages_block_info *tmp_payload = temp->b_info;
            root->right = addr_delete_node(root->right, temp->st_ad);
            root->st_ad = tmp_key;
            root->b_info = tmp_payload;
            if (root->b_info) root->b_info->addr_ptr = root;
        }
    }

    update_height_addrnode(root);
    int bal = balance_addrnode(root);

    if (bal > 1 && balance_addrnode(root->left) >= 0)
        return rotate_right_addrnode(root);
    if (bal > 1 && balance_addrnode(root->left) < 0) {
        root->left = rotate_left_addrnode(root->left);
        return rotate_right_addrnode(root);
    }
    if (bal < -1 && balance_addrnode(root->right) <= 0)
        return rotate_left_addrnode(root);
    if (bal < -1 && balance_addrnode(root->right) > 0) {
        root->right = rotate_right_addrnode(root->right);
        return rotate_left_addrnode(root);
    }
    return root;
}
struct addr_node* addr_search(struct addr_node* root, uint32 key) {
    struct addr_node* cur = root;
    while (cur) {
        if (key == cur->st_ad) return cur;
        if (key < cur->st_ad) cur = cur->left;
        else cur = cur->right;
    }
    return NULL;
}

int height_sizenode(struct size_node *n) {
	return n ? n->height : 0;
}
void update_height_sizenode(struct size_node *n) {
    if (!n) return;
    n->height = 1 + max_int(height_sizenode(n->left), height_sizenode(n->right));
}
uint32 subtree_max_pages_of(struct size_node *n) { return n ? n->subtree_max_pages : 0; }
void update_subtree_max_pages(struct size_node *n) {
    if (!n) return;
    uint32 m = n->pages;
    uint32 l = subtree_max_pages_of(n->left);
    uint32 r = subtree_max_pages_of(n->right);
    if (l > m) m = l;
    if (r > m) m = r;
    n->subtree_max_pages = m;
}
int balance_sizenode(struct size_node *n) {
    if (!n) return 0;
    return height_sizenode(n->left) - height_sizenode(n->right);
}
struct size_node* rotate_right_sizenode(struct size_node* y) {
    struct size_node *x = y->left;
    struct size_node *T2 = x->right;
    x->right = y;
    y->left = T2;
    update_height_sizenode(y);
    update_subtree_max_pages(y);
    update_height_sizenode(x);
    update_subtree_max_pages(x);
    return x;
}
struct size_node* rotate_left_sizenode(struct size_node* x) {
    struct size_node *y = x->right;
    struct size_node *T2 = y->left;
    y->left = x;
    x->right = T2;
    update_height_sizenode(x);
    update_subtree_max_pages(x);
    update_height_sizenode(y);
    update_subtree_max_pages(y);
    return y;
}
int sizenode_cmp(uint32 a_pages, uint32 a_start, uint32 b_pages, uint32 b_start) {
    if (a_pages < b_pages) return -1;
    if (a_pages > b_pages) return 1;
    if (a_start < b_start) return -1;
    if (a_start > b_start) return 1;
    return 0;
}
struct size_node* size_insert_node(struct size_node* node, struct size_node* to_insert) {
    if (!node) {
        to_insert->left = to_insert->right = NULL;
        to_insert->height = 1;
        to_insert->subtree_max_pages = to_insert->pages;
        return to_insert;
    }
    int cmp = sizenode_cmp(to_insert->pages, to_insert->start, node->pages, node->start);
    if (cmp < 0)
        node->left = size_insert_node(node->left, to_insert);
    else if (cmp > 0)
        node->right = size_insert_node(node->right, to_insert);
    else
        return node;

    update_height_sizenode(node);
    update_subtree_max_pages(node);
    int bal = balance_sizenode(node);

    if (bal > 1 && sizenode_cmp(to_insert->pages, to_insert->start, node->left->pages, node->left->start) < 0)
        return rotate_right_sizenode(node);
    if (bal < -1 && sizenode_cmp(to_insert->pages, to_insert->start, node->right->pages, node->right->start) > 0)
        return rotate_left_sizenode(node);
    if (bal > 1 && sizenode_cmp(to_insert->pages, to_insert->start, node->left->pages, node->left->start) > 0) {
        node->left = rotate_left_sizenode(node->left);
        return rotate_right_sizenode(node);
    }
    if (bal < -1 && sizenode_cmp(to_insert->pages, to_insert->start, node->right->pages, node->right->start) < 0) {
        node->right = rotate_right_sizenode(node->right);
        return rotate_left_sizenode(node);
    }

    return node;
}

static struct size_node* size_min_node(struct size_node* n) {
    struct size_node* cur = n;
    while (cur && cur->left) cur = cur->left;
    return cur;
}
struct size_node* size_delete_node(struct size_node* root, uint32 pages, uint32 start) {
    if (!root) return NULL;
    int cmp = sizenode_cmp(pages, start, root->pages, root->start);
    if (cmp < 0) root->left = size_delete_node(root->left, pages, start);
    else if (cmp > 0) root->right = size_delete_node(root->right, pages, start);
    else {
        if (!root->left || !root->right) {
            struct size_node *temp = root->left ? root->left : root->right;
            free_block((void*)root);
            return temp;
        } else {
            struct size_node *temp = size_min_node(root->right);
            uint32 t_pages = temp->pages;
            uint32 t_start = temp->start;
            struct pages_block_info *t_payload = temp->b_info;
            root->right = size_delete_node(root->right, temp->pages, temp->start);
            root->pages = t_pages;
            root->start = t_start;
            root->b_info= t_payload;
            if (t_payload) t_payload->size_ptr = root;
        }
    }

    update_height_sizenode(root);
    update_subtree_max_pages(root);

    int bal = balance_sizenode(root);

    if (bal > 1 && balance_sizenode(root->left) >= 0)
        return rotate_right_sizenode(root);
    if (bal > 1 && balance_sizenode(root->left) < 0) {
        root->left = rotate_left_sizenode(root->left);
        return rotate_right_sizenode(root);
    }
    if (bal < -1 && balance_sizenode(root->right) <= 0)
        return rotate_left_sizenode(root);
    if (bal < -1 && balance_sizenode(root->right) > 0) {
        root->right = rotate_right_sizenode(root->right);
        return rotate_left_sizenode(root);
    }
    return root;
}
//here i start
struct size_node* size_find_exact(struct size_node* root,uint32 pages) {
    struct size_node* curr=root;
    while(curr){
        if (pages==curr->pages)return curr;
        if (pages < curr->pages)curr=curr->left;
        else curr=curr->right;
    }
    return NULL;
}
struct size_node* size_find_worst(struct size_node* root,uint32 pages) {
    if (!root) return NULL;
    if (subtree_max_pages_of(root)<pages) return NULL;
    struct size_node* ret=root;
    while(ret->right) ret=ret->right;
    return ret;
}
static struct pages_block_info* cr_blq(uint32 start,uint32 pages,uint8 notfree) {
    struct pages_block_info *p=(struct pages_block_info*)alloc_block(sizeof(struct pages_block_info));
    if (!p) return NULL;
    p->block_start_address=start;
    p->num_of_pages_occupied=pages;
    p->notfree=notfree;
    p->addr_ptr=NULL;
    p->size_ptr=NULL;
    struct addr_node *an=(struct addr_node*)alloc_block(sizeof(struct addr_node));
    an->st_ad=start;
    an->b_info=p;
    an->left=an->right=NULL;
    //an->right=NULL;
    an->height=1;
    addr_root=addr_insert_node(addr_root, an);
    p->addr_ptr=an;
    if (notfree==0) {
        struct size_node *sn=(struct size_node*)alloc_block(sizeof(struct size_node));
        sn->pages=pages;
        sn->start=start;
        sn->b_info=p;
        sn->left=sn->right=NULL;
        //sn->righr=NULL;
        sn->height=1;
        sn->subtree_max_pages=pages;
        size_root=size_insert_node(size_root, sn);
        p->size_ptr=sn;
    }
    return p;
}

static void rem_block(struct pages_block_info* p) {
    if (!p) return;
    if (p->size_ptr) {
        size_root=size_delete_node(size_root, p->size_ptr->pages, p->size_ptr->start);
        p->size_ptr = NULL;
    }
    if (p->addr_ptr) {
        addr_root = addr_delete_node(addr_root, p->addr_ptr->st_ad);
        p->addr_ptr = NULL;
    }
    free_block((void*)p);
}
static void nw_szz(struct pages_block_info* p) {
    if (!p) return;
    if (p->size_ptr) {
        size_root=size_delete_node(size_root, p->size_ptr->pages, p->size_ptr->start);
        p->size_ptr=NULL;
    }
    if (p->notfree==0) {
        struct size_node *sn=(struct size_node*)alloc_block(sizeof(struct size_node));
        sn->pages=p->num_of_pages_occupied;
        sn->start=p->block_start_address;
        sn->b_info=p;
        sn->left=sn->right=NULL;
        sn->height=1;
        sn->subtree_max_pages=sn->pages;
        size_root=size_insert_node(size_root,sn);
        p->size_ptr=sn;
    }
    if (p->addr_ptr) {
        addr_root=addr_delete_node(addr_root, p->addr_ptr->st_ad);
        struct addr_node *an=(struct addr_node*)alloc_block(sizeof(struct addr_node));
        an->st_ad=p->block_start_address;
        an->b_info=p;
        an->left=an->right=NULL;
        an->height=1;
        addr_root=addr_insert_node(addr_root, an);
        p->addr_ptr=an;
    }
}
static void alloc_pgb(uint32 start,uint32 pg){ //rep?
	for(uint32 i=0;i<pg;i++){
		get_page((void*) start+i*PAGE_SIZE);
	}
}
static void free_pgb(uint32 start,uint32 pg){
	//cprintf("in kheap free_pgb");
	uint32 *va=NULL;
	for(uint32 i=0;i<pg;i++){
		struct FrameInfo *fr=get_frame_info(ptr_page_directory,start+i*PAGE_SIZE,&va);
		if(fr){
			free_frame(fr);
			return_page((void*) ((start+i*PAGE_SIZE)));
		}

	}
}
struct pages_block_info *split(struct pages_block_info *curr, uint32 needed_pages){
	//cprintf("in kheap split");
	if(curr->size_ptr){
		size_root=size_delete_node(size_root,curr->size_ptr->pages,curr->size_ptr->start);
		curr->size_ptr=NULL;
	}
	uint32 c_st=curr->block_start_address,c_pg=curr->num_of_pages_occupied;
	curr->num_of_pages_occupied=needed_pages;
	curr->notfree=1;
	//curr->size_ptr=NULL;
	uint32 rem_st=c_st+needed_pages*PAGE_SIZE,r_pg=c_pg-needed_pages;
	struct pages_block_info *remm=cr_blq(rem_st,r_pg,0);
	//cprintf("in kheap split end");
	return curr;
}
//for BST
struct addr_node* addr_l(struct addr_node* root, uint32 key) {
    struct addr_node* curr=root;
    struct addr_node* pred=NULL;
    while (curr) {
        if (curr->b_info->block_start_address<key) {
            pred=curr;
            curr=curr->right;
        } else {
            curr=curr->left;
        }
    }
    return pred;
}
struct addr_node* addr_r(struct addr_node* root, uint32 key) {
    struct addr_node* curr= root;
    struct addr_node* succ = NULL;
    while (curr) {
        if (curr->b_info->block_start_address > key) {
            succ = curr;
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }
    return succ;
}
//here i start 2
void merge(struct pages_block_info *l,struct pages_block_info *r){
	//cprintf("in kheap merge");
    if (!l || !r) return;
    if ((l->notfree!=0) || (r->notfree!=0)) return;
    if (l->size_ptr) {
        size_root=size_delete_node(size_root, l->size_ptr->pages, l->size_ptr->start);
        l->size_ptr=NULL;
    }
    if (r->size_ptr) {
        size_root=size_delete_node(size_root, r->size_ptr->pages, r->size_ptr->start);
        r->size_ptr=NULL;
    }
    addr_root=addr_delete_node(addr_root, r->addr_ptr->st_ad);
    l->num_of_pages_occupied+=r->num_of_pages_occupied;
    free_block((void*)r);
    uint32 l_start = l->block_start_address;
    uint32 l_end =l_start+l->num_of_pages_occupied*PAGE_SIZE;
    if (l_end==kheapPageAllocBreak) {
        rem_block(l);
        kheapPageAllocBreak=l_start;
        return;
    }
    nw_szz(l);
    //cprintf("in kheap merge dne");
}

void* kmalloc(unsigned int size)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	//Your code is here
#if USE_KHEAP
	bool wasHeld = holding_kspinlock(&kheapLock);
	if (!wasHeld)
	{
		acquire_kspinlock(&kheapLock);
	}

	//cprintf("kmalloc\n");
	//check if in block alloc area
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE) {
		release_kspinlock(&kheapLock);
		return alloc_block(size);
	}
	//page alloc area
	else {
		int needed_pages = (ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE);
		//int total_pages = (kheapPageAllocBreak - kheapPageAllocStart) / PAGE_SIZE;

		int max_free_block = 0;
		uint32 va;
		bool found = 0;
		if(kheapPageAllocBreak==kheapPageAllocStart){ //empty
			struct pages_block_info *curr=cr_blq(kheapPageAllocStart,needed_pages,1);
			if(!curr){
				release_kspinlock(&kheapLock);
				return NULL;
			}
			alloc_pgb(curr->block_start_address,needed_pages);
			uint32 rnd=ROUNDUP(size,PAGE_SIZE);
			kheapPageAllocBreak=curr->block_start_address+rnd;
		if (!wasHeld)
			{
			release_kspinlock(&kheapLock);
			}

			return (void *) curr->block_start_address;
		}
		struct size_node *exact=size_find_exact(size_root,needed_pages);
		//struct pages_block_info * fnd_b_info=NULL;
		if(exact && exact->b_info) {//exact
			struct pages_block_info * fnd_b_info=exact->b_info;
			if(fnd_b_info->size_ptr) {
				size_root=size_delete_node(size_root,fnd_b_info->size_ptr->pages,fnd_b_info->size_ptr->start);
				fnd_b_info->size_ptr=NULL;
			}
			fnd_b_info->notfree=1;
			alloc_pgb(fnd_b_info->block_start_address,needed_pages);
			if (!wasHeld)
			{
			  release_kspinlock(&kheapLock);
			}
			return (void*) fnd_b_info->block_start_address;
		}
		//else{ //worst
			struct size_node *worst=size_find_worst(size_root,needed_pages);
			if(worst && worst->b_info){
				struct pages_block_info * fnd_b_info=worst->b_info;
				if(fnd_b_info->num_of_pages_occupied==needed_pages){
					if(fnd_b_info->size_ptr){
						size_root=size_delete_node(size_root,fnd_b_info->size_ptr->pages,fnd_b_info->size_ptr->start);
						fnd_b_info->size_ptr=NULL;
					}
					fnd_b_info->notfree=1;
					alloc_pgb(fnd_b_info->block_start_address,needed_pages);
					if (!wasHeld)
					{
					release_kspinlock(&kheapLock);
					}
					return(void *)fnd_b_info->block_start_address;
				}
				else if(fnd_b_info->num_of_pages_occupied>needed_pages){
					struct pages_block_info *maskon=split(fnd_b_info,needed_pages);
					alloc_pgb(fnd_b_info->block_start_address,needed_pages);
					if (!wasHeld)
					{
					release_kspinlock(&kheapLock);
					}
					return(void *)maskon->block_start_address;
				}

			}
			uint32 rnd=ROUNDUP(size,PAGE_SIZE);
			if (KERNEL_HEAP_MAX - kheapPageAllocBreak >= rnd){
				uint32 start=kheapPageAllocBreak;
				struct pages_block_info *curr=cr_blq(start,needed_pages,1);
				if(!curr){
					if (!wasHeld)
					{
					release_kspinlock(&kheapLock);
					}					return NULL;
				}
				alloc_pgb(start,needed_pages);
				kheapPageAllocBreak+=rnd;
				if (!wasHeld)
				{
				release_kspinlock(&kheapLock);
				}
				return (void*) start;
			}

		}
	if (!wasHeld)
	{
	release_kspinlock(&kheapLock);
	}	//cprintf("kmalloc return\n");
	return NULL;
#else
	return NULL;
#endif

}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	//Your code is here
#if USE_KHEAP
	bool wasHeld = holding_kspinlock(&kheapLock);
	if (!wasHeld)
	{
		acquire_kspinlock(&kheapLock);
	}
		//cprintf("kfree st\n");
	if(!virtual_address){  //nada check locks
		if (!wasHeld)
		{
		release_kspinlock(&kheapLock);
		}
		return;
	}
	//check if va in block alloc area
	if(virtual_address >=(void*)dynAllocStart && virtual_address < (void*)dynAllocEnd){
		if (!wasHeld)
		{
		release_kspinlock(&kheapLock);
		}
		free_block(virtual_address);
		return;
	}

	//check in used area bounds
	else if(virtual_address >= (void*)kheapPageAllocStart && virtual_address < (void*)kheapPageAllocBreak){
		//virtual_address=ROUNDDOWN(virtual_address,PAGE_SIZE);
	//	cprintf("va>= and < \n");
		uint32 va=(uint32) virtual_address;
		struct addr_node *sad=addr_search(addr_root,va);
		if(!sad){
			if (!wasHeld)
			{
			release_kspinlock(&kheapLock);
			}		//	cprintf("not fnd");
			return;
		}
		struct pages_block_info *curr=sad->b_info;
		if(!curr){
			if (!wasHeld)
			{
			release_kspinlock(&kheapLock);
			}		//	cprintf("b info in kfree");
			return;
		}
		curr->notfree=0;
		nw_szz(curr);
		free_pgb(curr->block_start_address,curr->num_of_pages_occupied);
		struct pages_block_info* freed ;
		struct addr_node *adj_l=addr_l(addr_root,curr->block_start_address);
		uint32 lf=0;
		//adj&=(par->b_info->notfree==0);
		if(adj_l && adj_l->b_info &&adj_l->b_info->notfree==0){
			lf=adj_l->b_info->block_start_address;
			uint32 end=adj_l->b_info->block_start_address+adj_l->b_info->num_of_pages_occupied*PAGE_SIZE;
			if(end ==curr->block_start_address){
				merge(adj_l->b_info,curr);
				//curr=adj_l->b_info;
				struct addr_node *adjj=addr_search(addr_root,lf);
				if(!adjj){
					if (!wasHeld)
					{
					release_kspinlock(&kheapLock);
					}				    return;
				}
				curr=adjj->b_info;
			}
		}
		uint32 st2=curr->block_start_address;
		struct addr_node *adj_r=addr_r(addr_root,st2);
		if(adj_r && adj_r->b_info &&adj_r->b_info->notfree==0){
			uint32 end=st2+curr->num_of_pages_occupied*PAGE_SIZE;
			uint32 rg_st=adj_r->b_info->block_start_address;
			if(end ==rg_st){
			merge(curr,adj_r->b_info);
			//curr=adj_l->b_info;
			struct addr_node *adjj=addr_search(addr_root,rg_st);
			if(!adjj){
				if (!wasHeld)
				{
				release_kspinlock(&kheapLock);
				}				return;
			}
			curr=adjj->b_info;
			}
	  }

		if (!wasHeld)
		{
		release_kspinlock(&kheapLock);
		}	   return;
	}
	if (!wasHeld)
	{
	release_kspinlock(&kheapLock);
	}	//cprintf("kfree return\n");
    return;
#endif
}
/*
//before optimization
void* kmalloc(unsigned int size)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	//Your code is here
	cprintf("before lock kmallocc\n");
	acquire_kspinlock(&kheapLock);
cprintf("kmallocc\n");
	if(size>KERNEL_HEAP_MAX-KERNEL_HEAP_START){
		cprintf("big size\n");
		release_kspinlock(&kheapLock);
			return NULL;
		}
	//check if in block alloc area
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE) {
		release_kspinlock(&kheapLock);
		cprintf("alloc clock\n");
		return alloc_block(size);
	}
	//page alloc area
	else {
		cprintf("page alk\n");
		int needed_pages = (ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE);
		//int total_pages = (kheapPageAllocBreak - kheapPageAllocStart) / PAGE_SIZE;

		int max_free_block = 0;
		uint32 va;
		bool found = 0;

		//heap is empty >> first alloc
		if (kheapPageAllocBreak == kheapPageAllocStart) {
			cprintf("first alk\n");
			//cprintf("empty  ");
			struct pages_block_info* new = (struct pages_block_info*)alloc_block(sizeof(struct pages_block_info));
			LIST_INSERT_HEAD(&block_list, new);
			new->block_start_address = kheapPageAllocStart;
			new->num_of_pages_occupied = needed_pages;
			new->notfree = 1;

			va = new->block_start_address;
			kheapPageAllocBreak = new->block_start_address + ROUNDUP(size, PAGE_SIZE);
			found = 1;

			//actual alloc pages
			for (int i = 0; i < needed_pages; i++) {
				get_page((void*)new->block_start_address + i * PAGE_SIZE);
			}

		}

		else {
			cprintf("wfit\n");
			//find the max free block for worst fit
			struct pages_block_info* new2 ;

			LIST_FOREACH(new2, &block_list) {

					//find the exact/best fit
					if (new2->notfree==0&&new2->num_of_pages_occupied == needed_pages) {
						cprintf("best fit \n ");
						for (int j = 0; j < needed_pages; j++) {
							get_page((void*)new2->block_start_address + j * PAGE_SIZE);
						}
						new2->notfree = 1;
						va = new2->block_start_address;
						found = 1;
						break;
					}}
					//find the worst fit
				if(!found){
					cprintf("find the worst fit\n");
					LIST_FOREACH(new2, &block_list) {
						cprintf("freach\n");
						if ( new2->notfree == 0&&new2->num_of_pages_occupied > needed_pages ) {
							cprintf("max  ");
							if (new2->num_of_pages_occupied > max_free_block)
								max_free_block = new2->num_of_pages_occupied;
						}
					}
					if(max_free_block>needed_pages){
						cprintf("mx>p\n");
					   LIST_FOREACH(new2, &block_list) {
						   if (new2->notfree==0 && new2->num_of_pages_occupied == max_free_block) {
						   //sec: is the second part of the block after split
						   struct pages_block_info* sec = (struct pages_block_info*)alloc_block(sizeof(struct pages_block_info));
						   LIST_INSERT_AFTER(&block_list, new2, sec);
						   sec->block_start_address = new2->block_start_address + ROUNDUP(size, PAGE_SIZE);
						   sec->num_of_pages_occupied = new2->num_of_pages_occupied - needed_pages;
						   //cprintf("worst fit  ");
						   new2->num_of_pages_occupied = needed_pages;
						   new2->notfree = 1;
						   va = new2->block_start_address;
						   found = 1;

						   for (int j = 0; j < needed_pages; j++)
							   get_page((void*)new2->block_start_address + j * PAGE_SIZE);

						   break;
						   }
					   }
					}
				}


			//no proper free blocks in unused area
			if (!found) {
				cprintf("no proper free blocks in unused area\n");
				struct pages_block_info* new3 = (struct pages_block_info*)alloc_block(sizeof(struct pages_block_info));
				if (KERNEL_HEAP_MAX - kheapPageAllocBreak >= ROUNDUP(size, PAGE_SIZE)) {
					cprintf("kmax>=\n");
					new3->num_of_pages_occupied = needed_pages;
					new3->notfree = 1;
					new3->block_start_address = kheapPageAllocBreak;

					LIST_INSERT_TAIL(&block_list, new3);

					va = new3->block_start_address;

					for (int j = 0; j < needed_pages; j++) {
						get_page((void*)new3->block_start_address + j * PAGE_SIZE);
					}

					kheapPageAllocBreak = kheapPageAllocBreak + ROUNDUP(size, PAGE_SIZE);
					found = 1;

				} else {
					release_kspinlock(&kheapLock);
					return NULL;
				}
			}

		}
		cprintf("kmalloc va0x%08x\n",va);
		cprintf("bef rel\n");
		release_kspinlock(&kheapLock);
		cprintf("released\n");
		return (void*)va;
	}
}


//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	//Your code is here
	cprintf("bef kfree\n");
	acquire_kspinlock(&kheapLock);
	cprintf("kfree\n");
	//check if va in block alloc area
	if(virtual_address >=(void*)dynAllocStart && virtual_address < (void*)dynAllocEnd){
		release_kspinlock(&kheapLock);
		free_block(virtual_address);
	}

	//check in used area bounds
	else if(virtual_address >= (void*)kheapPageAllocStart && virtual_address < (void*)kheapPageAllocBreak){
		struct pages_block_info* freed ;

		LIST_FOREACH_SAFE(freed, &block_list, pages_block_info){
			if(virtual_address == (void*)freed -> block_start_address ){
				//try to access freed area X
				if(freed->notfree == 0)
				{
					release_kspinlock(&kheapLock);
					break;
				}


				freed->notfree = 0;
				//actual free the pages
				uint32* page_table_va =NULL;
				for (int i = 0; i < freed->num_of_pages_occupied; i++) {
				    struct FrameInfo* frame = get_frame_info(ptr_page_directory, (uint32)freed->block_start_address+(i*PAGE_SIZE), &page_table_va);
				    if(frame){
					free_frame(frame);
					return_page((void*)freed->block_start_address + (i * PAGE_SIZE));}
				}

				//if next block is free >> merge
				struct pages_block_info* next =LIST_NEXT(freed);
				if(next!=NULL&&next->notfree == 0){
					freed->num_of_pages_occupied += next->num_of_pages_occupied;
					LIST_REMOVE(&block_list, next);
				}
				//if the prev block is free >> merge
				struct pages_block_info* prev =LIST_PREV(freed);
				if(prev!=NULL&&prev->notfree == 0 ){
					freed->num_of_pages_occupied += prev->num_of_pages_occupied;
					freed->block_start_address = prev->block_start_address;
					LIST_REMOVE(&block_list, prev);
				}

				//if last >> move break
				if(freed==LIST_LAST(&block_list)){
					kheapPageAllocBreak = freed->block_start_address;
					LIST_REMOVE(&block_list,freed);
				}
				release_kspinlock(&kheapLock);
				break;
			}

	}
}
	else{
		release_kspinlock(&kheapLock);
		panic("Virtual Address Out Of Bounds !!!");
	}


	//Comment the following line
	//panic("kfree() is not implemented yet...!!");
}*/
//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address
	//Your code is here
//	cprintf("kh virt add\n");
#if USE_KHEAP
	bool wasHeld = holding_kspinlock(&kheapLock);
	if (!wasHeld)
	{
		acquire_kspinlock(&kheapLock);
	}

	uint16 offset = physical_address & 0xFFF;

	uint32 frame_pa = physical_address & 0xFFFFF000;

	//get the added virtual address in the frame
	struct FrameInfo *frame = to_frame_info(frame_pa);
	 if (frame == NULL) {

		 if (!wasHeld)
		 			{
		 			release_kspinlock(&kheapLock);
		 			}
	        // not mapped aw invalid
	        return 0;
	    }
	uint32 page_va = frame->page_va;
	   if (page_va == 0) {
	        //not mapped
		   if (!wasHeld)
		   			{
		   			release_kspinlock(&kheapLock);
		   			}
	        return 0;
	    }

//	cprintf("page va = %x", page_va);

	//form the va
	uint32 virtual_address = page_va + offset;
	//cprintf("kh virtual 0x%08x",virtual_address);
	if (!wasHeld)
				{
				release_kspinlock(&kheapLock);
				}
	return virtual_address;

	//Comment the following line
	//panic("kheap_virtual_address() is not implemented yet...!!");
#else
	return 0;
#endif
	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
}
//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address
	//Your code is here
	//cprintf("k phys add0x%08x\n",virtual_address);
#if USE_KHEAP
	bool wasHeld = holding_kspinlock(&kheapLock);
	if (!wasHeld)
	{
		acquire_kspinlock(&kheapLock);
	}

	uint16 offset = virtual_address & 0xFFF; //to get the left 12 bits

	// get the page table
	uint32 *page_table_va = NULL;
	int ret = get_page_table(ptr_page_directory, virtual_address, &page_table_va);
	if (ret == TABLE_NOT_EXIST){ //the not mapped
		if (!wasHeld)
					{
					release_kspinlock(&kheapLock);
					}
	   return 0;}

	// get the pa !!
	struct FrameInfo *frame = get_frame_info(ptr_page_directory, virtual_address, &page_table_va);
	if(frame==NULL){
		if (!wasHeld)
		{
		release_kspinlock(&kheapLock);
		}
		return 0;}

	uint32 pa = to_physical_address(frame);
	if (!wasHeld)
	{
	release_kspinlock(&kheapLock);
	}
	return pa | offset;

	//Comment the following line
	//panic("kheap_physical_address() is not implemented yet...!!");
#else
	return 0;

#endif
	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);
void* krealloc(void *virtual_address, uint32 new_size)
{
	//plan:
	//1 size s + blk_allc >> realloc_block()
	//2 size s + pg_allc >> realloc_block() + check for va
	//3 size L + blk_allc >> kmalloc()
	//4 size L + pg_allc >> the else
	//movew content !!!!!!!!

#if USE_KHEAP
	void * ret_va;

	bool lckd = holding_kspinlock(&kheapLock);
	if (!lckd)
		acquire_kspinlock(&kheapLock);

	if (virtual_address == NULL)
		ret_va = kmalloc(new_size);


	if (new_size == 0) {
		kfree(virtual_address);
		ret_va = NULL;
	}


	//1 / 2
	if (new_size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		ret_va = realloc_block(virtual_address, new_size);
		if(ret_va != NULL)
			kfree(virtual_address);
	}

	//3
	if (new_size > DYN_ALLOC_MAX_BLOCK_SIZE
			&& virtual_address >= (void*)dynAllocStart && virtual_address < (void*)dynAllocEnd)
	{
		ret_va = kmalloc(new_size);
		if(ret_va != NULL){
			uint32 mve_size = get_block_size(virtual_address);
			memcpy(ret_va, virtual_address, mve_size);

			kfree(virtual_address);
		}
	}
	//4
	else
	{
		uint32 o_va = (uint32)virtual_address;
		struct addr_node *node = addr_search(addr_root, o_va);
		if (!node || !node->b_info) {

			ret_va = NULL;
		}

		else{
			struct pages_block_info *blk = node->b_info;
			uint32 o_pgs = blk->num_of_pages_occupied;

			uint32 n_pgs = ROUNDUP(new_size, PAGE_SIZE) / PAGE_SIZE;

			//downsize
			if (n_pgs < o_pgs) {
				uint32 excess = o_pgs - n_pgs;
				uint32 tail_start = blk->block_start_address + n_pgs * PAGE_SIZE;
				free_pgb(tail_start, excess);
				blk->num_of_pages_occupied = n_pgs;
				nw_szz(blk);
				struct addr_node *adj_r = addr_r(addr_root, blk->block_start_address);
				if (adj_r && adj_r->b_info && adj_r->b_info->notfree == 0) {
					uint32 rg_st = adj_r->b_info->block_start_address;
					if (tail_start + excess * PAGE_SIZE == rg_st)
						merge(blk, adj_r->b_info);
				}

				ret_va = virtual_address;
			}

					//internal frag
			if (n_pgs > o_pgs) {
				struct addr_node *adj_r = addr_r(addr_root, blk->block_start_address);
				if (adj_r && adj_r->b_info && adj_r->b_info->notfree == 0) {
					uint32 free_pages = adj_r->b_info->num_of_pages_occupied;
					if (o_pgs + free_pages >= n_pgs) {
						merge(blk, adj_r->b_info);
						alloc_pgb(blk->block_start_address + o_pgs*PAGE_SIZE,n_pgs - o_pgs);
						blk->num_of_pages_occupied = n_pgs;
						nw_szz(blk);

						ret_va =  virtual_address;
					}
				}
			}

			//upsize elsewhere
			ret_va = kmalloc(new_size);
			if (ret_va != NULL) {
				if (!lckd){
					release_kspinlock(&kheapLock);
					kfree(virtual_address);
				}
			}

			// move content !!!!!!!
			uint32 mve_size = o_pgs * PAGE_SIZE;
			if (mve_size > new_size)
				mve_size = new_size;
			memcpy(ret_va, virtual_address, mve_size);

			kfree(virtual_address);

    }
 }

	if (!lckd)
		release_kspinlock(&kheapLock);
	return ret_va;

#else
	return NULL;
#endif
}

