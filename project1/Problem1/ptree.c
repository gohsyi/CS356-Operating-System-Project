#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pid.h>
#include <linux/init_task.h>
#include <linux/spinlock.h>
#include <linux/rwlock_types.h>
#include <linux/rwlock.h>

MODULE_LICENSE("Dual BSD/GPL");

#define __NR_ptreecall 287

static int (*oldcall)(void);

DEFINE_RWLOCK(buf_lock);

struct prinfo {
    pid_t parent_pid;
    pid_t pid;
    pid_t first_child_pid;
    pid_t next_sibling_pid;
    long state;
    long uid;
    char comm[16];  // name of program executed
};


static int get_prinfo(struct prinfo *dst, struct task_struct *src) 
{
    struct task_struct *first_child;
    struct task_struct *next_sibling;

    // parent_pid
    dst->parent_pid = src->parent->pid;

    // pid
    dst->pid = src->pid;

    // first_child_pid
    if (list_empty(&src->children)) {
        dst->first_child_pid = 0; // the process does not have a child
    } else {
        first_child = list_entry((src->children).next, struct task_struct, sibling);
        dst->first_child_pid = first_child->pid;
    }

    // next_sibling_pid
    if (list_empty(&(src->sibling))) {
        dst->next_sibling_pid = 0; // the process does not have a sibling
    } else {
        next_sibling = list_entry((src->sibling).next, struct task_struct, sibling);
        dst->next_sibling_pid = next_sibling->pid;
    }

    // state
    dst->state = src->state;

    // uid
    dst->uid = src->cred->uid;

    // comm
    get_task_comm(dst->comm, src);

    return 0;
}

// traverse through current process recursively
void dfs(struct task_struct *cur_task,
         struct prinfo *cur_buf, 
         int *cur_num, 
         const int tot) 
{
    struct task_struct *tmp; // tmp defined as an iterator
    
    if (*cur_num>=tot || cur_task == NULL)
        return;
        
    write_lock(&buf_lock);
    get_prinfo(cur_buf+(*cur_num), cur_task);
    ++*cur_num;
    write_unlock(&buf_lock);
    
    // traverse through children
    if (!list_empty(&cur_task->children)) {
        list_for_each_entry(tmp, &cur_task->children, sibling) {
            dfs(tmp, cur_buf, cur_num, tot);
        }
    }
}

// main function of ptree syscall
static long sys_ptree(struct prinfo __user *buf, int __user *nr)
{
    struct prinfo __kernel *kern_buf;
    int __kernel user_nr;
    int __kernel kern_nr;
    
    if (buf == NULL || nr == NULL) {
        printk(KERN_ERR "[ ERROR ptree ]\tinvalid buf or nr!\n");
        return -1;
    }

    if (copy_from_user(&user_nr, nr, sizeof(int))) {
        printk(KERN_ERR "[ ERROR ptree ]\tcopying nr failed!\n");
        return -1;
    } else {
    	printk(KERN_INFO "[ INFO ptree ]\tcopying nr succeed!\n");
    }

    kern_nr = 0;
    kern_buf = kmalloc(user_nr * sizeof(struct prinfo), GFP_KERNEL);

    if (kern_buf == NULL) {
        printk(KERN_ERR "[ ERROR ptree ]\tkmallocing failed!\n");
        return -1;
    }

    read_lock(&tasklist_lock);
    dfs(&init_task, kern_buf, &kern_nr, user_nr);
    read_unlock(&tasklist_lock);
    
    if (copy_to_user(buf, kern_buf, kern_nr*sizeof(struct prinfo))) {
        printk(KERN_ERR "[ ERROR ptree ]\tcopying to userspace buf failed!\n");
        return -1;
    }
    if (copy_to_user(nr, &kern_nr, sizeof(int))) {
        printk(KERN_ERR "[ ERROR ptree ]\tcopying to userspce nr failed!\n");
        return -1;
    }
    kfree(kern_buf);
    return 0;
}

static int addsyscall_init(void) {
    long *syscall = (long*) 0xc000d8c4;
    oldcall = (int(*)(void))(syscall[__NR_ptreecall]);
    syscall[__NR_ptreecall] = (unsigned long) sys_ptree;
    printk(KERN_INFO "[ INFO ptree ]\tmodule loaded!\n");
    return 0;
}

static void addsyscall_exit(void) {
    long *syscall = (long*) 0xc000d8c4;
    syscall[__NR_ptreecall] = (unsigned long) oldcall;
    printk(KERN_INFO "[ INFO ptree ]\tmodule exit!\n");
}

module_init(addsyscall_init);
module_exit(addsyscall_exit);
