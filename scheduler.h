#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

int  *idletime;
int	 *bank_used;

int exp_2(int);
void init_scheduler_vars(); //called from main
void bargainingchip_refresh(int);
void gambling(int);

void scheduler_stats(); //called from main
void schedule(int); // scheduler function called every cycle


#endif //__SCHEDULER_H__

