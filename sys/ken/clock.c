#
#include "../param.h"
#include "../systm.h"
#include "../user.h"
#include "../proc.h"

#define	UMODE	0170000
#define	SCHMAG	10

/*
 * clock is called straight from
 * the real time clock interrupt.
 *
 * Functions:
 *	reprime clock
 *	copy *switches to display
 *	implement callouts
 *	maintain user/system times
 *	maintain date
 *	profile
 *	tout wakeup (sys sleep)
 *	lightning bolt wakeup (every 4 sec)
 *	alarm clock signals
 *	jab the scheduler
 */
clock(dev, sp, r1, nps, r0, pc, ps)
{
	register struct callo *p1, *p2;
	register struct proc *pp;

	/*
	 * restart clock
	 */
//　这种写法可以正确设置 KW11-L和 KW11-P两种时钟设备的寄存器。
	*lks = 0115;

	/*
	 * display register
	 */
//display()​在 PDP-11/40的环境下不做任何处理
	display();

	/*
	 * callouts
	 * if none, just return
	 * else update first non-zero time
	 */
//如果​callout[]​中不存在需要执行的元素，则跳转到​out​。
	if(callout[0].c_func == 0)
		goto out;
/*
*对​callout[]​中未到执行时间的元素，递减其中首个元素​callo.c_time​。因
*为​callo.c_time​为距前一个元素的相对时间，所以如果改变了第一个元素的值，对排在
*后面的所有元素都会产生影响。
*/
	p2 = &callout[0];
	while(p2->c_time<=0 && p2->c_func!=0)
		p2++;
	p2->c_time--;

	/*
	 * if ps is high, just return
	 */
//被中断的进程的处理器优先级如果不为 0，就不处理​callout[]​，跳转到​out​
	if((ps&0340) != 0)
		goto out;

	/*
	 * callout
	 */

	spl5();
	if(callout[0].c_time <= 0) {
		p1 = &callout[0];
		while(p1->c_func != 0 && p1->c_time <= 0) {
			(*p1->c_func)(p1->c_arg);
			p1++;
		}
		p2 = &callout[0];
		while(p2->c_func = p1->c_func) {
			p2->c_time = p1->c_time;
			p2->c_arg = p1->c_arg;
			p1++;
			p2++;
		}
	}

	/*
	 * lightning bolt time-out
	 * and time of day
	 */

out:
//UMODE​用来检查 PSW是否被设置了用户模式
	if((ps&UMODE) == UMODE) {
		u.u_utime++;
		if(u.u_prof[3])
			incupc(pc, u.u_prof);
	} else
		u.u_stime++;
	pp = u.u_procp;
	if(++pp->p_cpu == 0)
		pp->p_cpu--;
//递增​lbolt​。如果已经经过了 1秒以上的时间，则启动以 1秒为周期的处理
	if(++lbolt >= HZ) {
//如果被中断的进程的处理器优先级不为 0则返回
		if((ps&0340) != 0)
			return;
//从​lbolt​递减相当于 1秒的值
		lbolt =- HZ;
		if(++time[1] == 0)
			++time[0];
/*
*将处理器优先级设定为 1。此后的操作需要花费一些时间，因此可以适当降低处理器
*优先级。
*/
		spl1();
		if(time[1]==tout[1] && time[0]==tout[0])
			wakeup(tout);
/*
*进行 lightning bolt处理。以 4秒为周期唤醒通过​lbolt​进入睡眠状态的进程。设置
*标志变量​runrun​，表示存在执行优先级较高的进程（即通过​lbolt​进入睡眠状态的进程）
*/
		if((time[1]&03) == 0) {
			runrun++;
			wakeup(&lbolt);
		}
/*
*对所有存在的进程进行下述处理。递增​proc.p_time​，最大值为 127。调整
*proc.p_cpu​使其最大值小于​SCHMAG​（代码清单 5-12）。如果执行优先级小于或等于基准值
*PUSER​（代码清单 5-13），将再次计算执行优先级。
*/
		for(pp = &proc[0]; pp < &proc[NPROC]; pp++)
		if (pp->p_stat) {
			if(pp->p_time != 127)
				pp->p_time++;
			if((pp->p_cpu & 0377) > SCHMAG)
				pp->p_cpu =- SCHMAG; else
				pp->p_cpu = 0;
			if(pp->p_pri > PUSER)
				setpri(pp);
		}
		if(runin!=0) {
			runin = 0;
			wakeup(&runin);
		}
		if((ps&UMODE) == UMODE) {
			u.u_ar0 = &r0;
			if(issig())
				psig();
			setpri(u.u_procp);
		}
	}
}

/*
 * timeout is called to arrange that
 * fun(arg) is called in tim/HZ seconds.
 * An entry is sorted into the callout
 * structure. The time in each structure
 * entry is the number of HZ's more
 * than the previous entry.
 * In this way, decrementing the
 * first entry has the effect of
 * updating all entries.
 */
timeout(fun, arg, tim)
{
	register struct callo *p1, *p2;
	register t;
	int s;

	t = tim;
	s = PS->integ;
	p1 = &callout[0];
/*
*将处理器优先级提升至 7防止发生时钟中断。因为时钟中断处理函数的内部也会操作
*callout[]​，为了避免与​timeout​下面的处理发生冲突，进行了上述处理。
*/
	spl7();
/*
*从​callout[]​的起始位置遍历数组，跳过那些执行时间早于参数指定时间的元
*素，同时用参数指定的时间减去​callo.c_time​，得到距前一元素的相对时间。
*/
	while(p1->c_func != 0 && p1->c_time <= t) {
		t =- p1->c_time;
		p1++;
	}
//调整排在追加元素之后的元素的相对时间。
	p1->c_time =- t;
//将指针移动至最终有效元素之后的位置。
	p2 = p1;
	while(p2->c_func != 0)
		p2++;
//将排在追加元素之后的元素向后依次移动一个位置。
	while(p2 >= p1) {
		(p2+1)->c_time = p2->c_time;
		(p2+1)->c_func = p2->c_func;
		(p2+1)->c_arg = p2->c_arg;
		p2--;
	}
//　追加一个元素。
	p1->c_time = t;
	p1->c_func = fun;
	p1->c_arg = arg;
//恢复处理器优先级。
	PS->integ = s;
}
