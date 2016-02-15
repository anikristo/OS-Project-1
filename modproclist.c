#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>

static int __init start(void) {
    	struct task_struct *task;
	for_each_process(task){
		if(task != NULL && task->parent != NULL && (task->state == TASK_INTERRUPTIBLE || task->state == TASK_UNINTERRUPTIBLE))
	    			printk("%s\tPID: %d\tParent PID: %d\tState: %ld\n",task->comm, task->pid, task->parent->pid, task->state);
		}
    	return 0;
}
   
static void __exit end(void){
    	printk(KERN_INFO "MODPROCLIST exiting...\n");
}

module_init(start);
module_exit(end);
