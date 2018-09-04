#include "sched.h"
#include <linux/slab.h>

/* for group path */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


static inline struct task_struct *wrr_task_of(struct sched_wrr_entity *wrr_se)
{
	return container_of(wrr_se, struct task_struct, wrr);
}

static inline int wrr_se_prio(struct sched_wrr_entity *wrr_se)
{
	return wrr_task_of(wrr_se)->wrr_priority;
}

#ifdef CONFIG_SMP
static struct task_struct *pick_pullable_task_wrr(struct rq *src_rq,
						  int this_cpu)
{
	struct task_struct *p;
	struct list_head *queue;
	struct wrr_prio_array *array;
	struct sched_wrr_entity *pos;
	int idx;
	int flag; /* done or not */

	array = &src_rq->active;
	idx = sched_find_first_bit(array->bitmap);

	flag = 0;
	p = NULL;

	while (idx < MAX_RT_PRIO) {
		queue = array->queue + idx;
		list_for_each_entry(pos, queue, run_list) {
			p = wrr_task_of(pos);
			if (p == src_rq->curr)
				continue;
			if (cpumask_test_cpu(this_cpu, &p->cpus_allowed))
				flag = 1;
		}

		if (!flag)
			idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx+1);
		else
			break;
	}
	
	return p;
}

void idle_balance_wrr(struct rq *this_rq)
{
	int this_cpu = this_rq->cpu, cpu;
	struct task_struct *p;
	struct rq *src_rq;

	for_each_possible_cpu(cpu) {
		if (this_cpu == cpu)
			continue;

		src_rq = cpu_rq(cpu);
		double_lock_balance(this_rq, src_rq);

		if (src_rq->wrr.wrr_nr_running <= 1)
			goto skip;

		/* pick a task from src_rq */
		p = pick_pullable_task_wrr(src_rq, this_cpu);

		if (p) {
			if (p == src_rq->curr)
				goto skip;
			WARN_ON(!p->on_rq);

			deactivate_task(src_rq, p, 0);
			set_task_cpu(p, this_cpu);
			activate_task(this_rq, p, 0);
			double_unlock_balance(this_rq, src_rq);
			return;
		}
skip:
		double_unlock_balance(this_rq, src_rq);
	}
}

static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
	int cpu;
	int min_cpu = task_cpu(p);
	int min_weight = INT_MAX;

	if (p->nr_cpus_allowed == 1)
		goto out;

	/* For anything but wake ups, just return the task_cpu */
	if (sd_flag != SD_BALANCE_WAKE && sd_flag != SD_BALANCE_FORK)
		goto out;

	/* search for the cpu with minimized load weight */
	for_each_possible_cpu(cpu) {
		if (my_wrr.total_weight[cpu] < min_weight) {
			min_weight = my_wrr.total_weight[cpu];
			min_cpu = cpu;
		}
	}
	return min_cpu;
out:
	return min_cpu;
}

#else /* !CONFIG_SMP */

void idle_balance_wrr(struct rq *this_rq)
{
}

static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
}

#endif /* CONFIG_SMP */

#ifdef CONFIG_WRR_PRIO
static void print_wrr_rq(struct wrr_rq *wrr_rq)
{
	struct task_struct *p;
	struct list_head *queue;
	struct wrr_prio_array *array;
	struct sched_wrr_entity *pos;
	int idx;

	array = &wrr_rq->active;
	idx = sched_find_first_bit(array->bitmap);

	while (idx < MAX_RT_PRIO) {
		queue = array->queue + idx;
		printk("on runqueue %d:\t", idx);
		list_for_each_entry(pos, queue, run_list) {
			p = wrr_task_of(pos);
			printk("[%d]\t", p->pid);
		}
		printk("\n");

		idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx+1);
	}
}
#endif

void init_wrr_rq(struct wrr_rq *wrr_rq, struct rq *rq)
{
#ifdef CONFIG_WRR_PRIO
	printk("init_wrr_rq called!\n");

	struct wrr_prio_array *array;
	int i;

	array = &wrr_rq->active;
	for (i = 0; i < MAX_RT_PRIO; i++) {
		INIT_LIST_HEAD(array->queue + i);
		__clear_bit(i, array->bitmap);
	}
	/* delimiter for bitsearch: */
	__set_bit(MAX_RT_PRIO, array->bitmap);
	wrr_rq->wrr_nr_running = 0;

#else /* !CONFIG_WRR_PRIO */

	INIT_LIST_HEAD(&wrr_rq->queue);
	wrr_rq->wrr_nr_running = 0;

#endif /* CONFIG_WRR_PRIO */
}

static void set_curr_task_wrr(struct rq *rq)
{
	struct task_struct *p = rq->curr;
	p->se.exec_start = rq->clock_task;
}

static void put_prev_task_wrr(struct rq *rq, struct task_struct *p)
{
}

static void check_preempt_curr_wrr(struct rq *rq,
				   struct task_struct *p, int flags)
{
	if (p->prio < rq->curr->prio) {
		resched_task(rq->curr);
		return;
	}
}

static struct task_struct *pick_next_task_wrr(struct rq *rq)
{
	struct task_struct *p;
	struct wrr_rq *wrr_rq;
	struct sched_wrr_entity *next = NULL;

	wrr_rq = &rq->wrr;

#ifdef CONFIG_WRR_PRIO

	struct wrr_prio_array *array = &wrr_rq->active;
	struct list_head *queue;
	int idx;

	if (!wrr_rq->wrr_nr_running)
		return NULL;

	idx = sched_find_first_bit(array->bitmap);
	
	BUG_ON(idx >= MAX_RT_PRIO);

	queue = array->queue + idx;
	next = list_entry(queue->next, struct sched_wrr_entity, run_list);

	BUG_ON(!next);

#ifdef CONFIG_WRR_DEBUG
	while (next != queue) {
		printk("%d ", wrr_task_of(list_entry(queue->next, struct sched_wrr_entity, run_list)).pid);
		printk("\n");
	}
#endif
#else /* !CONFIG_WRR_PRIO */
	if (list_empty(&wrr_rq->queue))
		return NULL;
	next = list_entry(wrr_rq->queue.next, struct sched_wrr_entity, run_list);
#endif /* CONFIG_WRR_PRIO */

	p = wrr_task_of(next);
	print_wrr_rq(wrr_rq);
	printk("pick [%d] as the next\n", p->pid);
	return p;
}

static void dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
#ifdef CONFIG_WRR_DEBUG
	printk("[%d]\tdequeue_task_wrr called!\n", p->pid);
#endif
#ifdef CONFIG_SMP
	int cpu;
#endif
	
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *wrr_rq = &rq->wrr;
	
#ifdef CONFIG_WRR_PRIO
	int prio = wrr_se_prio(wrr_se);
	struct wrr_prio_array *array = &wrr_rq->active;
#endif

	if (wrr_se == NULL)
		return;

	list_del_init(&wrr_se->run_list); /* delete and initialize node */
	
#ifdef CONFIG_WRR_PRIO
	if (list_empty(array->queue + prio))
		__clear_bit(prio, array->bitmap);
	WARN_ON(!rt_prio(prio));
#endif

	WARN_ON(!wrr_rq->wrr_nr_running);
	wrr_rq->wrr_nr_running--;
	dec_nr_running(rq);

#ifdef CONFIG_SMP
	cpu = cpu_of(rq);
	raw_spin_lock(&wrr_info_locks[cpu]);
	my_wrr.nr_running[cpu]--;
	my_wrr.total_weight[cpu] -= wrr_se->weight;
	raw_spin_unlock(&wrr_info_locks[cpu]);
#endif
}

static void enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{	
#ifdef CONFIG_WRR_DEBUG
	printk("[%d]\tenqueue_task_wrr called!\n", p->pid);
#endif
#ifdef CONFIG_SMP
	int cpu;
#endif

	struct sched_wrr_entity *wrr_se;
	struct wrr_rq *wrr_rq;

	wrr_se = &p->wrr;
	wrr_rq = &rq->wrr;

#ifdef CONFIG_WRR_PRIO
	int prio = wrr_se_prio(wrr_se);
	struct wrr_prio_array *array = &wrr_rq->active;
	struct list_head *queue = array->queue + prio;

	if (wrr_se == NULL)
		return;

	if (flags & ENQUEUE_HEAD) {
		list_add(&wrr_se->run_list, queue);
	}
	else {
#ifdef CONFIG_WRR_DEBUG
		if (&wrr_se->run_list == NULL)
			printk("run_list is null\n");
		if (queue->prev == NULL) {
			printk("queue initialization has problem\n");
			queue->prev = queue;
		}
		printk("list_add_tail called\n");
#endif
		list_add_tail(&wrr_se->run_list, queue);
	}

#ifdef CONFIG_WRR_DEBUG
	printk("list adding finished\n");
#endif

	__set_bit(prio, array->bitmap);

#ifdef CONFIG_WRR_DEBUG
	printk("set bit finished\n");
#endif

	WARN_ON(!rt_prio(prio));

#else /* CONFIG_WRR_PRIO */

	if (wrr_se == NULL)
		return;
	if (flags & ENQUEUE_HEAD)
		list_add(&wrr_se->run_list, &wrr_rq->queue);
	else
		list_add_tail(&wrr_se->run_list, &wrr_rq->queue);

#endif /* CONFIG_WRR_PRIO */

	wrr_rq->wrr_nr_running++;
	inc_nr_running(rq);

#ifdef CONFIG_SMP
	cpu = cpu_of(rq);
	raw_spin_lock(&wrr_info_locks[cpu]);
	my_wrr.nr_running[cpu]++;
	my_wrr.total_weight[cpu] += wrr_se->weight;
	raw_spin_unlock(&wrr_info_locks[cpu]);
#endif
}

static void requeue_task_wrr(struct rq *rq, struct task_struct *p, int head)
{
#ifdef CONFIG_WRR_DEBUG
	printk("[%d]\trequeue_task_wrr called!\n", p->pid);
#endif

	struct sched_wrr_entity *wrr_se;
	struct wrr_rq *wrr_rq;

	wrr_se = &p->wrr;
	wrr_rq = &rq->wrr;

#ifdef CONFIG_WRR_PRIO
	int prio = wrr_se_prio(wrr_se);
	struct wrr_prio_array *array = &wrr_rq->active;
	struct list_head *queue = array->queue + prio;

	if (wrr_se == NULL) 
		return;
	if (head)
		list_move(&wrr_se->run_list, queue);
	else
		list_move_tail(&wrr_se->run_list, queue);
#else
	if (wrr_se == NULL)
		return;
	list_move_tail(&wrr_se->run_list, &wrr_rq->queue);
#endif
}

static void yield_task_wrr(struct rq *rq)
{
	requeue_task_wrr(rq, rq->curr, 0);
}

static char group_path[PATH_MAX];
static void task_tick_wrr(struct rq *rq, struct task_struct *p, int queued)
{
	struct sched_wrr_entity *wrr_se;
	wrr_se = &p->wrr;

	if (wrr_se == NULL) {
		return;
	}
	
	printk("[%d]\tprio is %d\n", p->pid, p->wrr_priority);
	printk("[%d]\t%d time slice left\n", p->pid, p->wrr.time_slice);

	if (--wrr_se->time_slice) {
		return;
	}

	cgroup_path(task_group(p)->css.cgroup, group_path, PATH_MAX);
	
	if (strcmp(group_path, "/") == 0) {
		wrr_se->time_slice = WRR_TIMESLICE_FORE;
#ifdef CONFIG_WRR_DEBUG
		printk("[%d]\ttime slice is set as FOREground\n", p->pid);
#endif
	} else {
		wrr_se->time_slice = WRR_TIMESLICE_BACK;
#ifdef CONFIG_WRR_DEBUG
		printk("[%d]\ttime slice is set as BACKground\n", p->pid);
#endif
	}

#ifdef CONFIG_WRR_PRIO
	/* decrease the priority */
	if (p->wrr_priority < MAX_RT_PRIO-1) {
		dequeue_task_wrr(rq, p, 1);
		p->wrr_priority++;
		enqueue_task_wrr(rq, p, 1);
	}
#endif

	if (wrr_se->run_list.prev != wrr_se->run_list.next) {
		requeue_task_wrr(rq, p, 0);
		set_tsk_need_resched(p);
		return;
	}
}

static void switched_to_wrr(struct rq *rq, struct task_struct *p)
{
	int foreground;
	cgroup_path(task_group(p)->css.cgroup, group_path, PATH_MAX);
	foreground = (strcmp(group_path, "/") == 0);

	printk("group=%s\n", group_path);
	printk("Switched to a %s WRR entity, pid=%d, proc=%s\n",
		foreground ? "foreground" : "background", p->pid, p->comm);

	p->wrr.time_slice = foreground ? WRR_TIMESLICE_FORE : WRR_TIMESLICE_BACK;
	
	if (p->prio < rq->curr->prio)
		resched_task(rq->curr);
}

static bool
yield_to_task_wrr(struct rq *rq, struct task_struct *p, bool preempt)
{
	return true;
}

static void
prio_changed_wrr(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static void task_fork_wrr(struct task_struct *p)
{
	printk("task_fork_wrr called\n");
}

static void set_cpus_allowed_wrr(struct task_struct *p,
				const struct cpumask *new_mask)
{
}

static void rq_online_wrr(struct rq *rq)
{
}

static void rq_offline_wrr(struct rq *rq)
{
}

static void pre_schedule_wrr(struct rq *rq, struct task_struct *prev)
{
}

static void post_schedule_wrr(struct rq *rq)
{
}

static void task_woken_wrr(struct rq *rq, struct task_struct *p)
{
}

static void switched_from_wrr(struct rq *rq, struct task_struct *p)
{
}

static unsigned int get_rr_interval_wrr(struct rq *rq, struct task_struct *task)
{
	return task->wrr.time_slice;
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
const struct sched_class wrr_sched_class = {
	.next 				= 	&fair_sched_class, 		/* never */
	
	/* [required] adding/removing a task to/from a priority array */
	.enqueue_task 		= 	enqueue_task_wrr,		/* required */
	.dequeue_task 		= 	dequeue_task_wrr,		/* required */
	
	.yield_task 		= 	yield_task_wrr, 		/* required */
	.check_preempt_curr = 	check_preempt_curr_wrr,	/* required */

	.pick_next_task 	=	pick_next_task_wrr, 	/* required */
	.put_prev_task		=	put_prev_task_wrr,		/* required */

	.task_fork			=	task_fork_wrr,			/* required */

#ifdef CONFIG_SMP
	.select_task_rq 	= 	select_task_rq_wrr,		/* never */
	.set_cpus_allowed	= 	set_cpus_allowed_wrr,	/* never */
	.rq_online			=	rq_online_wrr,			/* never */
	.rq_offline			=	rq_offline_wrr,			/* never */
	.pre_schedule		=	pre_schedule_wrr,		/* never */
	.post_schedule		=	post_schedule_wrr,		/* never */
	.task_woken			=	task_woken_wrr,			/* never */
#endif

	.switched_from		=	switched_from_wrr,		/* never */

	.set_curr_task		=	set_curr_task_wrr,		/* required */
	.task_tick			=	task_tick_wrr,			/* required */

	.get_rr_interval	=	get_rr_interval_wrr,	/* required */

	.prio_changed		=	prio_changed_wrr,		/* never */
	.switched_to		=	switched_to_wrr,		/* required */
};
