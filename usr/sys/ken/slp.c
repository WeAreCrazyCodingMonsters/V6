#
/*
 */

#include "../param.h"
#include "../user.h"
#include "../proc.h"
#include "../text.h"
#include "../systm.h"
#include "../file.h"
#include "../inode.h"
#include "../buf.h"

/*
 * Give up the processor till a wakeup occurs
 * on chan, at which time the process
 * enters the scheduling queue at priority pri.
 * The most important effect of pri is that when
 * pri<0 a signal cannot disturb the sleep;
 * if pri>=0 signals will be processed.
 * Callers of this routine must be prepared for
 * premature return, and check that the reason for
 * sleeping has gone away.
 */
sleep(chan, pri)
{
	register *rp, s;

	// 保存 PSW的当前值，为进程的下次执行做准备。​PS​指向 PSW所映射的地址
	s = PS->integ;
	// 将​rp​设置为​proc[]​中代表执行进程的元素。
	rp = u.u_procp;
	if(pri >= 0) {
		/*
		 *如果执行优先级大于等于 0，在入睡和被唤醒时对信号进行处理。首先使用issig()​判断是否收到信号，如果收到了信号则跳转到​psig​对其进行处理。
		 * 设定执行进程的​proc.p_wchan​、​proc.p_stat​和​proc.p_pri​。
		 * 由于在发生中断时被调用的​wakeup()​中也有可能修改这些数据，此处将处理器优先级提升到 6以防止中断的发生。
		 * 关于处理器优先级和中断请参见第 5章的说明。设定结束后再将处理器优先级重置为0。
		 * 如果标识变量​runin​为 1（表示不存在需要被换出至交换空间的对象），则启动进程调度器。
		 * 关于标识变量​runin​和进程调度器请参见第 4章的说明。最后调用​swtch()​切换执行进程。
		*/
		if(issig())
			goto psig;
		spl6();
		rp->p_wchan = chan;
		rp->p_stat = SWAIT;
		rp->p_pri = pri;
		spl0();
		if(runin != 0) {
			runin = 0;
			wakeup(&runin);
		}
		swtch();
		if(issig())
			goto psig;
	} else {
		/*
		 *此处为执行优先级小于 0时的处理。
		*/
		spl6();
		rp->p_wchan = chan;
		rp->p_stat = SSLEEP;
		rp->p_pri = pri;
		spl0();
		swtch();
	}
	// 为了再次执行被中断的进程，恢复被保存的 PSW。
	PS->integ = s;
	return;

	/*
	 * If priority was low (>=0) and
	 * there has been a signal,
	 * execute non-local goto to
	 * the qsav location.
	 * (see trap1/trap.c)
	 */
psig:
	aretu(u.u_qsav);
}

/*
 * Wake up all processes sleeping on chan.
 */
wakeup(chan)
{
	register struct proc *p;
	register c, i;

	c = chan;
	p = &proc[0];
	i = NPROC;
	do {
		if(p->p_wchan == c) {
			setrun(p);
		}
		p++;
	} while(--i);
}

/*
 * Set the process running;
 * arrange for it to be swapped in if necessary.
 */
setrun(p)
{
	register struct proc *rp;

	rp = p;
	rp->p_wchan = 0;
	rp->p_stat = SRUN;
	if(rp->p_pri < curpri)
		runrun++;
	if(runout != 0 && (rp->p_flag&SLOAD) == 0) {
		runout = 0;
		wakeup(&runout);
	}
}

/*
 * Set user priority.
 * The rescheduling flag (runrun)
 * is set if the priority is higher
 * than the currently running process.
 */
setpri(up)
{
	register *pp, p;

	/*
	*计算执行优先级
	*/
	pp = up;
	p = (pp->p_cpu & 0377)/16;
	p =+ PUSER + pp->p_nice;
	if(p > 127)
		p = 127;
	/*
	* ​curpri​为当前执行中的进程的执行优先级。
	* 标志变量​runrun​表示存在执行优先级大于当前进程的其他进程。
	* 因为proc.p_pri的值越小执行优先级越高，所以此处的不等号实际上是错误的,这个错误在 UNIX的下一个版本中已被修正。
	*/
	if(p > curpri)
		runrun++;
	pp->p_pri = p;
}

/*
 * The main loop of the scheduling (swapping)
 * process.
 * The basic idea is:
 *  see if anyone wants to be swapped in;
 *  swap out processes until there is room;
 *  swap him in;
 *  repeat.
 * Although it is not remarkably evident, the basic
 * synchronization here is on the runin flag, which is
 * slept on and is set once per second by the clock routine.
 * Core shuffling therefore takes place once per second.
 *
 * panic: swap error -- IO error while swapping.
 *	this is the one panic that should be
 *	handled in a less drastic way. Its
 *	very hard.
 */
sched()
{
	struct proc *p1;
	register struct proc *rp;
	register a, n;

	/*
	 * find user to swap in
	 * of users ready, select one out longest
	 */

	goto loop;

sloop:
	runin++;
	sleep(&runin, PSWP);

loop:
	spl6();
	n = -1;
	for(rp = &proc[0]; rp < &proc[NPROC]; rp++)
	if(rp->p_stat==SRUN && (rp->p_flag&SLOAD)==0 &&
	    rp->p_time > n) {
		p1 = rp;
		n = rp->p_time;
	}
	if(n == -1) {
		runout++;
		sleep(&runout, PSWP);
		goto loop;
	}

	/*
	 * see if there is core for that process
	 */

	spl0();
	rp = p1;
	a = rp->p_size;
	if((rp=rp->p_textp) != NULL)
		if(rp->x_ccount == 0)
			a =+ rp->x_size;
	if((a=malloc(coremap, a)) != NULL)
		goto found2;

	/*
	 * none found,
	 * look around for easy core
	 */

	spl6();
	for(rp = &proc[0]; rp < &proc[NPROC]; rp++)
	if((rp->p_flag&(SSYS|SLOCK|SLOAD))==SLOAD &&
	    (rp->p_stat == SWAIT || rp->p_stat==SSTOP))
		goto found1;

	/*
	 * no easy core,
	 * if this process is deserving,
	 * look around for
	 * oldest process in core
	 */

	if(n < 3)
		goto sloop;
	n = -1;
	for(rp = &proc[0]; rp < &proc[NPROC]; rp++)
	if((rp->p_flag&(SSYS|SLOCK|SLOAD))==SLOAD &&
	   (rp->p_stat==SRUN || rp->p_stat==SSLEEP) &&
	    rp->p_time > n) {
		p1 = rp;
		n = rp->p_time;
	}
	if(n < 2)
		goto sloop;
	rp = p1;

	/*
	 * swap user out
	 */

found1:
	spl0();
	rp->p_flag =& ~SLOAD;
	xswap(rp, 1, 0);
	goto loop;

	/*
	 * swap user in
	 */

found2:
	if((rp=p1->p_textp) != NULL) {
		if(rp->x_ccount == 0) {
			if(swap(rp->x_daddr, a, rp->x_size, B_READ))
				goto swaper;
			rp->x_caddr = a;
			a =+ rp->x_size;
		}
		rp->x_ccount++;
	}
	rp = p1;
	if(swap(rp->p_addr, a, rp->p_size, B_READ))
		goto swaper;
	mfree(swapmap, (rp->p_size+7)/8, rp->p_addr);
	rp->p_addr = a;
	rp->p_flag =| SLOAD;
	rp->p_time = 0;
	goto loop;

swaper:
	panic("swap error");
}

/*
 * This routine is called to reschedule the CPU.
 * if the calling process is not in RUN state,
 * arrangements for it to restart must have
 * been made elsewhere, usually by calling via sleep.
 */
swtch()
{
	static struct proc *p;
	register i, n;
	register struct proc *rp;

	/*
	* ​p​是遍历 ​proc[]​时所使用的​static​变量。
	* 它保存着上次执行​swtch()​时选择的进程（执行进程）所指向的​proc[]​的元素。
	* 初次执行​swtch()​时​p​的值为​NULL​，指向proc[]​的起始位置。
	*/
	if(p == NULL)
		p = &proc[0];
	/*
	 * Remember stack of caller
	 */
	/*
	* 执行​savu()​将 r5、r6的当前值保存于待中断进程的​user.u_rsav​之中。
	* 等到该进程再次执行时再予以恢复。 
	*/
	savu(u.u_rsav);
	/*
	 * Switch to scheduler's stack
	 */
	/*
	* 执行​retu()​切换至调度器进程。
	​* proc[0]​是供调度器使用的系统进程，在系统启动时被创建。
	*/
	retu(proc[0].p_addr);

loop:
	/*
	* 将标志变量​runrun​设定为 0，该标志变量表示与执行进程相比，存在执行优先级更高的进程。
	* 因为即将切换到拥有最高执行优先级的进程，在此将​runrun​重置为 0。
	*/
	runrun = 0;
	rp = p;
	p = NULL;
	n = 128;
	/*
	 * Search for highest-priority runnable process
	 */
	i = NPROC;
	// 选出优先级最高的进程
	do {
		rp++;
		if(rp >= &proc[NPROC])
			rp = &proc[0];
		if(rp->p_stat==SRUN && (rp->p_flag&SLOAD)!=0) {
			if(rp->p_pri < n) {
				p = rp;
				n = rp->p_pri;
			}
		}
	} while(--i);
	/*
	 * If no process is runnable, idle.
	 */
	if(p == NULL) {
		/*
		* 如果不存在可执行的进程，将​p​设置为与此前的执行进程相对应的​proc[]​元素。
		* 然后调用​idle()​，等待由于发生中断而出现的可执行进程。
		* 举例来说，读取块设备的处理结束时将引发中断，使得等待该处理结束的进程被唤醒，因此出现了处于可执行状态的进程。
		* 再比如，由时钟引发的中断导致设定在某个时刻启动的进程被唤醒的情况也是如此。
		* 中断处理结束后，从​wait​的下一条指令开始继续执行，并从idle()​返回​swtch()​。
		* 随后返回到标签​loop​的位置，再次尝试选择执行进程。 
		*/
		p = rp;
		idle();
		goto loop;
	}
	rp = p;
	curpri = n;
	/*
	 * Switch to stack of the new process and set up
	 * his segmentation registers.
	 */
	retu(rp->p_addr);
	sureg();
	/*
	 * If the new process paused because it was
	 * swapped out, set the stack level to the last call
	 * to savu(u_ssav).  This means that the return
	 * which is executed immediately after the call to aretu
	 * actually returns from the last routine which did
	 * the savu.
	 *
	 * You are not expected to understand this.
	 */
	if(rp->p_flag&SSWAP) {
		rp->p_flag =& ~SSWAP;
		aretu(u.u_ssav);
	}
	/*
	 * The value returned here has many subtle implications.
	 * See the newproc comments.
	 */
	/*　返回 1。
	* 返回位置为执行savu()的函数（该函数调用savu()并在user结构体中保存r5、r6的值）自身被调用的位置。
	*/
	return(1);
}

/*
 * Create a new process-- the internal version of
 * sys fork.
 * It returns 1 in the new process.
 * How this happens is rather hard to understand.
 * The essential fact is that the new process is created
 * in such a way that appears to have started executing
 * in the same call to newproc as the parent;
 * but in fact the code that runs is that of swtch.
 * The subtle implication of the returned value of swtch
 * (see above) is that this is the value that newproc's
 * caller in the new process sees.
 */
newproc()
{
	int a1, a2;
	struct proc *p, *up;
	register struct proc *rpp;
	register *rip, n;

	p = NULL;
	/*
	 * First, just locate a slot for a process
	 * and copy the useful info from this process into it.
	 * The panic "cannot happen" because fork has already
	 * checked for the existence of a slot.
	 */
retry:
	// 新建进程id
	mpid++;
	if(mpid < 0) {
		mpid = 0;
		goto retry;
	}
	// 遍历proc结构体数组,查看是否有可用的结构体。
	// 若有,则用p记录首个可用的结构体元素地址。
	// 若没有,则执行panic("no procs")
	for(rpp = &proc[0]; rpp < &proc[NPROC]; rpp++) {
		if(rpp->p_stat == NULL && p==NULL)
			p = rpp;
		if (rpp->p_pid==mpid)
			goto retry;
	}
	if ((rpp = p)==NULL)
		panic("no procs");

	/*
	 * make proc entry for new proc
	 */

	rip = u.u_procp;
	up = rip;
	rpp->p_stat = SRUN;
	rpp->p_flag = SLOAD;
	rpp->p_uid = rip->p_uid;
	rpp->p_ttyp = rip->p_ttyp;
	rpp->p_nice = rip->p_nice;
	rpp->p_textp = rip->p_textp;
	rpp->p_pid = mpid;
	rpp->p_ppid = rip->p_pid;
	rpp->p_time = 0;

	/*
	 * make duplicate entries
	 * where needed
	 */

	for(rip = &u.u_ofile[0]; rip < &u.u_ofile[NOFILE];)
		if((rpp = *rip++) != NULL)
			rpp->f_count++;
	if((rpp=up->p_textp) != NULL) {
		rpp->x_count++;
		rpp->x_ccount++;
	}
	u.u_cdir->i_count++;
	/*
	 * Partially simulate the environment
	 * of the new process so that when it is actually
	 * created (by copying) it will look right.
	 */
	savu(u.u_rsav);
	rpp = p;
	u.u_procp = rpp;
	rip = up;
	n = rip->p_size;
	a1 = rip->p_addr;
	rpp->p_size = n;
	a2 = malloc(coremap, n);
	/*
	 * If there is not enough core for the
	 * new process, swap out the current process to generate the
	 * copy.
	 */
	if(a2 == NULL) {
		rip->p_stat = SIDL;
		rpp->p_addr = a1;
		savu(u.u_ssav);
		xswap(rpp, 0, 0);
		rpp->p_flag =| SSWAP;
		rip->p_stat = SRUN;
	} else {
	/*
	 * There is core, so just copy.
	 */
		rpp->p_addr = a2;
		while(n--)
			copyseg(a1++, a2++);
	}
	u.u_procp = rip;
	return(0);
}

/*
 * Change the size of the data+stack regions of the process.
 * If the size is shrinking, it's easy-- just release the extra core.
 * If it's growing, and there is core, just allocate it
 * and copy the image, taking care to reset registers to account
 * for the fact that the system's stack has moved.
 * If there is no core, arrange for the process to be swapped
 * out after adjusting the size requirement-- when it comes
 * in, enough core will be allocated.
 * Because of the ssave and SSWAP flags, control will
 * resume after the swap in swtch, which executes the return
 * from this stack level.
 *
 * After the expansion, the caller will take care of copying
 * the user's stack towards or away from the data area.
 */
expand(newsize)
{
	int i, n;
	register *p, a1, a2;

	p = u.u_procp;
	n = p->p_size;
	p->p_size = newsize;
	a1 = p->p_addr;
	if(n >= newsize) {
		mfree(coremap, n-newsize, a1+newsize);
		return;
	}
	savu(u.u_rsav);
	a2 = malloc(coremap, newsize);
	if(a2 == NULL) {
		savu(u.u_ssav);
		xswap(p, 1, n);
		p->p_flag =| SSWAP;
		swtch();
		/* no return */
	}
	p->p_addr = a2;
	for(i=0; i<n; i++)
		copyseg(a1+i, a2++);
	mfree(coremap, n, a1);
	retu(p->p_addr);
	sureg();
}
