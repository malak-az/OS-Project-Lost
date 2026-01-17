// Kernel-level Semaphore

#include "inc/types.h"
#include "inc/x86.h"
#include "inc/memlayout.h"
#include "inc/mmu.h"
#include "inc/environment_definitions.h"
#include "inc/assert.h"
#include "inc/string.h"
#include "ksemaphore.h"
#include "channel.h"
#include "../cpu/cpu.h"
#include "../proc/user_environment.h"

void init_ksemaphore(struct ksemaphore *ksem, int value, char *name)
{
	init_channel(&(ksem->chan), "ksemaphore channel");
	init_kspinlock(&(ksem->lk), "lock of ksemaphore");
	strcpy(ksem->name, name);
	ksem->count = value;
}

void wait_ksemaphore(struct ksemaphore *ksem)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #6 SEMAPHORE - wait_ksemaphore
    //Your code is here
#if USE_KHEAP
	//cprintf("wait ksema\n");
    struct kspinlock* guard = &(ksem->lk);
    struct Channel* chan = &(ksem->chan);

    acquire_kspinlock(guard);
    (ksem->count)--;
    if(ksem->count < 0)
        sleep(chan, guard);

    release_kspinlock(guard);

#endif

    //Comment the following line
    //panic("wait_ksemaphore() is not implemented yet...!!");

}
void signal_ksemaphore(struct ksemaphore *ksem)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #7 SEMAPHORE - signal_ksemaphore
    //Your code is here
#if USE_KHEAP
//	cprintf("signal ksema\n");
    struct kspinlock* guard = &(ksem->lk);
    struct Channel* chan = &(ksem->chan);

    acquire_kspinlock(guard);

    (ksem->count)++;

    if(ksem->count <= 0)
            wakeup_one(chan);


    release_kspinlock(guard);

#endif

    //Comment the following line
    //panic("signal_ksemaphore() is not implemented yet...!!");

}

