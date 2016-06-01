# Task_2: Modify scheduler

I modify *kernel/fork.c to* change the default scheduler and priority of all descendants 
of zygote. 

The changed code segment is form line 1534 to line 1543

```c
// change process main's descendants' policy and priority`
if (strcmp("main", p->parent->comm) == 0) {
    struct sched_param param;
    param.sched_priority = (99 / 5) * (p->pid % 5) + 1;
    sched_setscheduler_nocheck(p, SCHED_RR, &param);
    printk("ForkPr: %s scheduler: %d pid: %d prio: %d rt_prio: %d\n", 
        p->comm, p->policy, p->pid, p->prio, p->rt_priority);
}
```

***

I modify *kernel/sched/rt.c* to change the policy of SCHED_RR to pick the next process randomly.

The changed code segment is form line 1329 to line 1361

```c
static struct sched_rt_entity *pick_next_rt_entity(struct rq *rq,struct rt_rq *rt_rq) 
{
    struct rt_prio_array *array = &rt_rq->active;
    struct sched_rt_entity *next = NULL;
    struct list_head *queue;
    
    int next_ps_set[MAX_RT_PRIO] = { 0 };   // next processed set 
    int next_ps_num = 0;    //number of next processes
    
    // get next processes set and store them in next_ps_set[]
    int i = 0;
    while (i < MAX_RT_PRIO) {
        if (!list_empty(array->queue + i)) {
            next_ps_set[next_ps_num++] = i;
            printk("Get and save idx: %d\n", i);
        }
        ++i;
    }

    // get random next process idx
    unsigned int idx_temp;
    get_random_bytes(&idx_temp, sizeof(int));
    idx_temp = idx_temp % next_ps_num;
    int idx = next_ps_set[idx_temp];
    BUG_ON(idx >= MAX_RT_PRIO);
    printk("Pick idx: %d\n", idx);

    // get next process and return it
    queue = array->queue + idx;
    next = list_entry(queue->next, struct sched_rt_entity, run_list);
    
    return next;
}
```
