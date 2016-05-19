/* Writes work with simple FR-FCFS. 
   Reads employ FR-RR, which is a combination of "first-ready", and "round-robin". */


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "utlist.h"
#include "utils.h"

#include "memory_controller.h"
#include "scheduler.h"

extern long long int CYCLE_VAL;
extern int NUMCORES;
extern int NUM_BANKS;//number of bank

int exp_2(int x)
{
	int y=1;
	if(x==0)
		return y;
	else{
		for(int i=1;;i++)
			if(i!=x)
				y *= 2;
			else
				return y;
	}
}


void bargainingchip_refresh(int channel)
{
	//printf("begin refresh\n");
	request_t * rd_ptr = NULL;
	int	max_instruction_id = -1;
	
	int BankNum[8] = { 0 };
	dram_address_t Temp;
	LL_FOREACH(read_queue_head[channel],rd_ptr){
		Temp = rd_ptr->dram_addr;
		BankNum[Temp.bank]++;
		if(rd_ptr->instruction_id>max_instruction_id)
		max_instruction_id = rd_ptr->instruction_id;
	}
	for (int i = 0; i < 8 ; i++)
	{
		//printf("BankNum[%d] = %d\n", i, BankNum[i]);
	}
	//printf("\n");
	
	//user_ptr: { select or not : bargainingchip } 
	dram_address_t TempBankNumChip;
	LL_FOREACH(read_queue_head[channel],rd_ptr){
		if(rd_ptr->user_ptr==NULL){
			rd_ptr->user_ptr = (void*)malloc(2*sizeof(int));	
			//printf("Allocate memory\n");
		}
			TempBankNumChip = rd_ptr->dram_addr;
		/*=================================================================*/
		/*=================================================================*/
		*(int*)((rd_ptr->user_ptr)+1) = exp_2(idletime[rd_ptr->thread_id]) + exp_2(BankNum[TempBankNumChip.bank]) + ((max_instruction_id-rd_ptr->instruction_id)/10);
		
		//printf("%d , %d ,%d ,%d \n",*(int*)((rd_ptr->user_ptr)+1) ,idletime[rd_ptr->thread_id],exp_2(BankNum[TempBankNumChip.bank]),max_instruction_id-rd_ptr->instruction_id/10);
		/*=================================================================*/
		/*=================================================================*/
		//printf("CHIPcount is:%d, thread is:%d, Bank is:%d, BankNum is:%d, idletime is:%d\n",*(int*)((rd_ptr->user_ptr)+1), rd_ptr->thread_id, TempBankNumChip.bank, BankNum[TempBankNumChip.bank], idletime[rd_ptr->thread_id]);
	}
	return;
}

void gambling(int channel)
{
	//printf("begin gambling\n");
	int random_num;
	request_t * rd_ptr = NULL;
	dram_address_t temp;
	int accum_bargainingchip = 0;
	int total_bargainingchip = 0;
	int gamblingisdone = 0;
	LL_FOREACH(read_queue_head[channel],rd_ptr){
		temp = rd_ptr->dram_addr;
		if(rd_ptr->command_issuable&&(!bank_used[temp.bank]))
		total_bargainingchip = total_bargainingchip + (*(int*)((rd_ptr->user_ptr)+1));
	}
	srand(time(NULL));
	if(total_bargainingchip==0)
		random_num = 0;
	else
		random_num = rand() % total_bargainingchip;
	
	LL_FOREACH(read_queue_head[channel],rd_ptr){
		if(!(rd_ptr->command_issuable&&(!bank_used[temp.bank])))
			continue;
		else{
			accum_bargainingchip = accum_bargainingchip + (*(int*)((rd_ptr->user_ptr)+1));
			(*(int*)(rd_ptr->user_ptr)) = 0;
			if((!gamblingisdone)&&accum_bargainingchip>random_num){
				(*(int*)(rd_ptr->user_ptr)) = 1;
				temp = rd_ptr->dram_addr;
				bank_used[temp.bank] = 1;
				gamblingisdone = 1;
			}
		}
	}
	return;
}


void init_scheduler_vars()
{
    // initialize all scheduler variables here
	idletime = (int*)malloc(NUMCORES*sizeof(int));
	bank_used = (int*)malloc(NUM_BANKS*sizeof(int));
	
	for(int i=0;i<NUMCORES;i++)
		idletime[i] = 0;
	for(int i=0;i<NUM_BANKS;i++)
		bank_used[i] = 0;
	
    return;
}

// write queue high water mark; begin draining writes if write queue exceeds this value
#define HI_WM 40

// end write queue drain once write queue has this many writes in it
#define LO_WM 20

// when switching to write drain mode, write at least this many times before switching back to read mode
#define MIN_WRITES_ONCE_WRITING_HAS_BEGUN 1

// 1 means we are in write-drain mode for that channel
int drain_writes[MAX_NUM_CHANNELS];

// how many writes have been performed since beginning current write drain
int writes_done_this_drain[MAX_NUM_CHANNELS];

// flag saying that we're only draining the write queue because there are no reads to schedule
int draining_writes_due_to_rq_empty[MAX_NUM_CHANNELS];

/* Each cycle it is possible to issue a valid command from the read or write queues
   OR
   a valid precharge command to any bank (issue_precharge_command())
   OR
   a valid precharge_all bank command to a rank (issue_all_bank_precharge_command())
   OR
   a power_down command (issue_powerdown_command()), programmed either for fast or slow exit mode
   OR
   a refresh command (issue_refresh_command())
   OR
   a power_up command (issue_powerup_command())
   OR
   an activate to a specific row (issue_activate_command()).

   If a COL-RD or COL-WR is picked for issue, the scheduler also has the
   option to issue an auto-precharge in this cycle (issue_autoprecharge()).

   Before issuing a command it is important to check if it is issuable. For the RD/WR queue resident commands, checking the "command_issuable" flag is necessary. To check if the other commands (mentioned above) can be issued, it is important to check one of the following functions: is_precharge_allowed, is_all_bank_precharge_allowed, is_powerdown_fast_allowed, is_powerdown_slow_allowed, is_powerup_allowed, is_refresh_allowed, is_autoprecharge_allowed, is_activate_allowed.
   */


void schedule(int channel)
{
	//printf("cycle start.\n");

    request_t * rd_ptr = NULL;
    request_t * wr_ptr = NULL;
	dram_address_t temp;
	bargainingchip_refresh(channel);
	gambling(channel);
    // begin write drain if we're above the high water mark
    if((write_queue_length[channel] > HI_WM) && (!drain_writes[channel]))
    {
        drain_writes[channel] = 1;
        writes_done_this_drain[channel] = 0;
    }

    // also begin write drain if read queue is empty
    if((read_queue_length[channel] < 1) && (write_queue_length[channel] > 0) && (!drain_writes[channel]))
    {
        drain_writes[channel] = 1;
        writes_done_this_drain[channel] = 0;
        draining_writes_due_to_rq_empty[channel] = 1;
    }

    // end write drain if we're below the low water mark
    if((drain_writes[channel]) && (write_queue_length[channel] <= LO_WM) && (!draining_writes_due_to_rq_empty[channel]))
    {
        drain_writes[channel] = 0;
    }

    // end write drain that was due to read_queue emptiness only if at least one write has completed
    if((drain_writes[channel]) && (read_queue_length[channel] > 0) && (draining_writes_due_to_rq_empty[channel]) && (writes_done_this_drain[channel] > MIN_WRITES_ONCE_WRITING_HAS_BEGUN))
    {
        drain_writes[channel] = 0;
        draining_writes_due_to_rq_empty[channel] = 0;
    }

    // make sure we don't try to drain writes if there aren't any
    if(write_queue_length[channel] == 0)
    {
        drain_writes[channel] = 0;
    }

    // drain from write queue now
    if(drain_writes[channel])
    {
        // prioritize open row hits
        LL_FOREACH(write_queue_head[channel], wr_ptr)
        {
            // if COL_WRITE_CMD is the next command, then that means the appropriate row must already be open
            if(wr_ptr->command_issuable && (wr_ptr->next_command == COL_WRITE_CMD))
            {
                writes_done_this_drain[channel]++;
                issue_request_command(wr_ptr);
				for(int k=0;k<NUMCORES;k++)
				idletime[k]++;
				//printf("cycle end.\n");
                return;
            }
        }

        // if no open rows, just issue any other available commands
        LL_FOREACH(write_queue_head[channel], wr_ptr)
        {
            if(wr_ptr->command_issuable)
            {
                issue_request_command(wr_ptr);
				for(int k=0;k<NUMCORES;k++)
				idletime[k]++;
				//printf("cycle end.\n");
                return;
            }
        }
        return;
    }

    LL_FOREACH(read_queue_head[channel],rd_ptr){

        if((rd_ptr->command_issuable) && 
                (*(int*)(rd_ptr->user_ptr)) && 
                (rd_ptr->next_command == COL_READ_CMD)){
			temp=rd_ptr->dram_addr;
            issue_request_command( rd_ptr);
			for(int k=0;k<NUMCORES;k++)
			idletime[k]++;
			idletime[rd_ptr->thread_id] = 0;
			if(rd_ptr->request_served)
				bank_used[temp.bank] = 0;
			//printf("cycle end.\n");	
            return;
        }
    }
	
    LL_FOREACH(read_queue_head[channel],rd_ptr){

        if((rd_ptr->command_issuable) && 
                (*(int*)(rd_ptr->user_ptr))){
			temp=rd_ptr->dram_addr;
            issue_request_command( rd_ptr);
			for(int k=0;k<NUMCORES;k++)
			idletime[k]++;
			idletime[rd_ptr->thread_id] = 0;
			if(rd_ptr->request_served)
				bank_used[temp.bank] = 0;
			//printf("cycle end.\n");	
            return;
        }
    }

    LL_FOREACH(read_queue_head[channel],rd_ptr){

        if(rd_ptr->command_issuable){ 
			temp=rd_ptr->dram_addr;
            issue_request_command( rd_ptr);
			for(int k=0;k<NUMCORES;k++)
			idletime[k]++;
			idletime[rd_ptr->thread_id] = 0;
			if(rd_ptr->request_served)
				bank_used[temp.bank] = 0;
			//printf("cycle end.\n");
            return;
        }
    }
	for(int k=0;k<NUMCORES;k++)
	idletime[k]++;
	//printf("cycle end.\n");
}

void scheduler_stats()
{
    /* Nothing to print for now. */
}

