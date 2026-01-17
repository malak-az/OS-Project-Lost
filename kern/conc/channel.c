/*
 * channel.c
 *
 *  Created on: Sep 22, 2024
 *      Author: HP
 */
#include "channel.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <inc/string.h>
#include <inc/disk.h>

//===============================
// 1) INITIALIZE THE CHANNEL:
//===============================
// initialize its lock & queue
void init_channel(struct Channel *chan, char *name)
{
	strcpy(chan->name, name);
	init_queue(&(chan->queue));
}

//===============================
// 2) SLEEP ON A GIVEN CHANNEL:
//===============================
// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// Ref: xv6-x86 OS code
void sleep(struct Channel *chan, struct kspinlock* lk)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #1 CHANNEL - sleep
    //Your code is here

#if USE_KHEAP

	//cprintf("sleep\n");
    //release lk and reacquire
    //get curr_runn proc >> make it blocked >> add to blocked qu in chan >> schedule a next ready one
    struct Env* cur_run = get_cpu_proc();
    cur_run->channel = chan;
    cur_run->env_status = ENV_BLOCKED;
    struct Env_Queue* qu = &chan->queue;
    //protect el qu
    acquire_kspinlock(&ProcessQueues.qlock);
    //sched_insert_ready(cur_run);
    enqueue(qu, cur_run);
    release_kspinlock(lk);
    sched();
    release_kspinlock(&ProcessQueues.qlock);

    //reacquire on  wakeup
    acquire_kspinlock(lk);

#endif

    //Comment the following line
    //panic("sleep() is not implemented yet...!!");
}

//==================================================
// 3) WAKEUP ONE BLOCKED PROCESS ON A GIVEN CHANNEL:
//==================================================
// Wake up ONE process sleeping on chan.
// The qlock must be held.
// Ref: xv6-x86 OS code
// chan MUST be of type "struct Env_Queue" to hold the blocked processes
void wakeup_one(struct Channel *chan)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #2 CHANNEL - wakeup_one
    //Your code is here
#if USE_KHEAP
	//cprintf("wakeup_one\n");
    //remove one proc from qu >> make it ready
    struct Env_Queue* qu = &chan->queue;
    //protect el qu
    acquire_kspinlock(&ProcessQueues.qlock);
    struct Env *ret_proc = dequeue(qu);
    if(ret_proc != NULL){
       ret_proc->env_status = 1;
       sched_insert_ready(ret_proc);
    }
    release_kspinlock(&ProcessQueues.qlock);

#endif

    //Comment the following line
    //panic("wakeup_one() is not implemented yet...!!");
}

//====================================================
// 4) WAKEUP ALL BLOCKED PROCESSES ON A GIVEN CHANNEL:
//====================================================
// Wake up all processes sleeping on chan.
// The queues lock must be held.
// Ref: xv6-x86 OS code
// chan MUST be of type "struct Env_Queue" to hold the blocked processes

void wakeup_all(struct Channel *chan)
{
    //TODO: [PROJECT'25.IM#5] KERNEL PROTECTION: #3 CHANNEL - wakeup_all
    //Your code is here
#if USE_KHEAP
	//cprintf("wakeup all\n");
    struct Env_Queue* qu = &chan->queue;
    //protect el qu
    acquire_kspinlock(&ProcessQueues.qlock);
    int size = qu->size ;
    for(int i = 0; i < size; i++){
        struct Env *ret_proc = dequeue(qu);
        if(ret_proc != NULL){
        ret_proc->env_status = ENV_READY;
        sched_insert_ready(ret_proc);
        }
    }
    release_kspinlock(&ProcessQueues.qlock);


#endif

    //Comment the following line
    //panic("wakeup_all() is not implemented yet...!!");
}
