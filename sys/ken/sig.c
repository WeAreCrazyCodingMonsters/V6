#
/*
 */

#include "../param.h"
#include "../systm.h"
#include "../user.h"
#include "../proc.h"
#include "../inode.h"
#include "../reg.h"

/*
 * Priority for tracing
 */
#define	IPCPRI	(-1)

/*
 * Structure to access an array of integers.
 */
struct
{
	int	inta[];
};

/*
 * Tracing variables.
 * Used to pass trace command from
 * parent to child being traced.
 * This data base cannot be
 * shared and is locked
 * per user.
 */
struct
{
	int	ip_lock;
	int	ip_req;
	int	ip_addr;
	int	ip_data;
} ipc;
/*
 *  ipc​（inter process communication）结构体是供跟踪功能使用的全局结构体。
 * 父进程通过执行系统调用​ptrace​，可以将对子进程的请求保存在​ipc​结构体中。
 */

/*
 * Send the specified signal to
 * all processes with 'tp' as its
 * controlling teletype.
 * Called by tty.c for quits and
 * interrupts.
 */
signal(tp, sig)//signal()​通过执行​psignal()​，向与执行进程位于同一终端的所有进程发送信号。​signal()​由终端的中断处理函数运行。
{
	register struct proc *p;

	for(p = &proc[0]; p < &proc[NPROC]; p++)
		if(p->p_ttyp == tp)
			psignal(p, sig);
}

/*
 * Send the specified signal to
 * the specified process.
 */
psignal(p, sig)// psignal()​向指定的进程发送信号。但是，​SIGKIL​信号不会被覆盖。
int *p;
{
	register *rp;

	if(sig >= NSIG)
		return;
	rp = p;
	if(rp->p_sig != SIGKIL)
		rp->p_sig = sig;
	if(rp->p_stat > PUSER)
		rp->p_stat = PUSER;
	/*
	 * 此处疑为Bug，处理对象的变量不应为​proc.p_stat​，而应是​proc.p_pri​。
	 * 开发者的意图应该是使执行优先级保持一个定值，从而使信号更容易处理。
	 */
	if(rp->p_stat == SWAIT)
		setrun(rp);
	/* 
	 * 如果对象进程的状态为​SWAIT​并处于睡眠之中，将其唤醒并促使其进行信号处理。
	 * ​SWAIT​在进程调用​sleep()​时进入睡眠状态，且​proc.p_pri​大于等于 0时被设定。
	 * 进程被唤醒并通过​swtch()​成为执行进程后，在​sleep()​内将对是否收到信号进行确认。
	 */
}

/*
 * Returns true if the current
 * process has a signal to process.
 * This is asked at least once
 * each time a process enters the
 * system.
 * A signal does not do anything
 * directly to a process; it sets
 * a flag that asks the process to
 * do something to itself.
 */
issig()//issig()​用来确认执行进程是否收到了信号。当​user.u_signal[n]​（​n​表示信号的种类）被设为奇数时，忽略该信号。
{
	register n;
	register struct proc *p;

	p = u.u_procp;
	if(n = p->p_sig) {//收到信号时的处理。
		if (p->p_flag&STRC) {//与跟踪功能相关的处理。在后文中将对其进行说明。
			stop();
			if ((n = p->p_sig) == 0)
				return(0);
		}
		if((u.u_signal[n]&1) == 0)//如果​u.u_signal[n]​（n=proc.p_sig）的值为偶数，则返回​n​
			return(n);
	}
	return(0);//　如果没有收到信号，或是需要忽略信号时返回 0。
}

/*
 * Enter the tracing STOP state.
 * In this state, the parent is
 * informed and the process is able to
 * receive commands from the parent.
 */
stop()
{
	register struct proc *pp, *cp;

loop:
	cp = u.u_procp;
	if(cp->p_ppid != 1)//如果父进程不是​init​进程，唤醒父进程并将执行进程设置为​SSTOP​状态。
	for (pp = &proc[0]; pp < &proc[NPROC]; pp++)
		if (pp->p_pid == cp->p_ppid) {
			wakeup(pp);
			cp->p_stat = SSTOP;
			swtch();//执行​swtch()​切换执行进程并期待能够切换至父进程。当前进程若希望再次成为执行进程，必须由父进程将其设定为​SRUN​状态。
			if ((cp->p_flag&STRC)==0 || procxmt())
				return;
			goto loop;
			/*
			 * 当前进程再次成为执行进程时的处理。
			 * 如果跟踪处理已结束，或是​procxmt()​的处理结果为真时，执行​return​并返回一般处理。
			 * 否则将继续循环，以促使父进程再次介入子进程的处理。
			 */
		}
	exit();//如果没有发现父进程，或父进程为 init进程时则认为发生异常，调用​exit()​终止进程。
}
/*
 * stop()​将进程设置为​SSTOP​状态。
 * 设置后，进程在收到信号，并开始对其进行相应处理时将通知父进程，父进程也可以对子进程发送指令。
 * 当进程的跟踪标志位（STRC​）为1时，stop()​由​issig()​调用。
 * issig()​在执行​stop()​后，会检查是否设置了​proc.p_sig​的值，如果未设置则返回0。
 * 这应该是考虑到stop()​处理中信号有可能被清除而采取的措施。
 */

/*
 * Perform the action specified by
 * the current signal.
 * The usual sequence is:
 *	if(issig())
 *		psig();
 */
/*
 * psig()​进行与执行进程的​proc.p_sig​相对应的信号处理。
 * 当内核进程通过​issig()​进行的检查结果为真时被调用。
 * 如果​user.u_signal[n]​的值为偶数，执行由该值指向的信号处理函数。
 * 首先将用户进程的PSW、pc的当前值压入用户进程的栈的顶部，并将栈指针指向存放pc的位置。
 * 然后清除用户进程PSW的陷入位（trap bit），并将pc设定为信号处理函数的地址。
 * 信号处理函数在控制权返回用户进程后被执行。信号处理函数由rtt或rti指令终止 。
 * ​rtt​和​rti​指令从栈顶部恢复pc和PSW的值，随后被暂停的进程将继续原有的处理。
 * 如果​user.u_signal[n]​的值为0，则终止进程。
 * 根据信号的种类可以输出调试用的core文件。
 * 最后将用户进程的​r0​的值、信号的种类、是否生成了core文件等信息作为结束状态通知父进程。
 */
psig()
{
	register n, p;
	register *rp;

	rp = u.u_procp;
	n = rp->p_sig;
	rp->p_sig = 0;
	if((p=u.u_signal[n]) != 0) {//​user.u_signal[n]​被设定为独自定义的信号处理函数地址时的处理。
		u.u_error = 0;
		if(n != SIGINS && n != SIGTRC)
			u.u_signal[n] = 0;
		n = u.u_ar0[R6] - 4;
		grow(n);//为了安全起见，执行​grow()​扩展用户进程的栈区域。
		suword(n+2, u.u_ar0[RPS]);
		suword(n, u.u_ar0[R7]);
		u.u_ar0[R6] = n;
		u.u_ar0[RPS] =& ~TBIT;
		u.u_ar0[R7] = p;
		return;
	}
	switch(n) {//​u.u_signal[n]​被设定为 0时的处理。

	case SIGQIT:
	case SIGINS:
	case SIGTRC:
	case SIGIOT:
	case SIGEMT:
	case SIGFPT:
	case SIGBUS:
	case SIGSEG:
	case SIGSYS:
		u.u_arg[0] = n;
		if(core())
			n =+ 0200;//如果生成了 core文件，则将​n​的第 7比特位设置为 1。
	}
	u.u_arg[0] = (u.u_ar0[R0]<<8) | n;
	/*
	 * 将​u.u_arg[0]​的高位 8比特设定为用户进程的​r0​的值，低位 8比特设定为表示信号种类以及是否生成了 core文件的信息。
	 * ​u.u_arg[0]​作为结束状态通知父进程。
	 */
	exit();//执行​exit()​，使进程结束自身的处理。
}

/*
 * Create a core image on the file "core"
 * If you are looking for protection glitches,
 * there are probably a wealth of them here
 * when this occurs to a suid command.
 *
 * It writes USIZE block of the
 * user.h area followed by the entire
 * data+stack segments.
 */
/*
 * core()​在当前目录下生成名为core的文件。
 * 文件包括数据段的全部内容（PPDA、数据区域、栈区域）。
 * 用户可以通过检查该文件调试程序。
 * 此处的​core()​进行的处理通常被称作内核转储（Core Dump）。
 */
core()
{
	register s, *ip;
	extern schar;

	u.u_error = 0;
	u.u_dirp = "core";
	ip = namei(&schar, 1);
	if(ip == NULL) {
		if(u.u_error)
			return(0);
		ip = maknode(0666);
		if(ip == NULL)
			return(0);
	}
	if(!access(ip, IWRITE) &&
	   (ip->i_mode&IFMT) == 0 &&
	   u.u_uid == u.u_ruid) {
		itrunc(ip);
		u.u_offset[0] = 0;
		u.u_offset[1] = 0;
		u.u_base = &u;
		u.u_count = USIZE*64;
		u.u_segflg = 1;
		writei(ip);
		s = u.u_procp->p_size - USIZE;
		estabur(0, s, 0, 0);
		u.u_base = 0;
		u.u_count = s*64;
		u.u_segflg = 0;
		writei(ip);
	}
	iput(ip);
	return(u.u_error==0);
}

/*
 * grow the stack to include the SP
 * true return if successful.
 */

grow(sp)
char *sp;
{
	register a, si, i;

	if(sp >= -u.u_ssize*64)//如果栈区域已经足够大，则不做任何处理并返回。由于栈区域的地址从0xffff开始，因此为负值。
		return(0);
	si = ldiv(-sp, 64) - u.u_ssize + SINCR;//计算栈需要扩展的长度，并检查是否为正值，如果不是则返回，且不做扩展处理。SINCR​被定义为 20。
	if(si <= 0)
		return(0);
	if(estabur(u.u_tsize, u.u_dsize, u.u_ssize+si, u.u_sep))//执行​estabur()​更新用户​APR​。
		return(0);
	expand(u.u_procp->p_size+si);//执行​expand()​扩展数据段。
	//移动栈区域，移动的距离为对数据段进行扩展的长度。
	a = u.u_procp->p_addr + u.u_procp->p_size;
	for(i=u.u_ssize; i; i--) {
		a--;
		copyseg(a-si, a);
	}
	for(i=si; i; i--)
		clearseg(--a);
	u.u_ssize =+ si;//更新栈区域的长度，返回 1表示扩展成功。
	return(1);
}

/*
 * sys-trace system call.
 */
/*
 * ptrace()​是系统调用​ptrace​的处理函数，向作为跟踪对象的子进程发出指令进行处理。
 * 父进程和子进程通过​ipc​结构体通信。
 * 子进程也可以利用系统调用​ptrace​将自身的​STRC​标志位设置为1。
*/
ptrace()
{
	register struct proc *p;

	if (u.u_arg[2] <= 0) {//如果表示跟踪指令种类的参数小于或等于 0，则将执行进程的跟踪标志位​STRC​置 1并返回。
		u.u_procp->p_flag =| STRC;
		return;
	}
	for (p=proc; p < &proc[NPROC]; p++)
	//寻找满足下述条件的进程，找到后跳转到​found​。1.处于​SSTOP​状态2.​proc.p_pid​与系统调用​ptrace​的参数值相同3.执行进程的子进程 
		if (p->p_stat==SSTOP
		 && p->p_pid==u.u_arg[0]
		 && p->p_ppid==u.u_procp->p_pid)
			goto found;
	u.u_error = ESRCH;
	return;

    found:
	while (ipc.ip_lock)//找到跟踪对象进程时的处理。首先尝试取得​ipc​结构体的锁，如果无法取得则进入睡眠状态。
		sleep(&ipc, IPCPRI);
	ipc.ip_lock = p->p_pid;//如果成功取得锁，设置​ipc​结构体的参数，并解除跟踪对象进程的​SWTED​标志位。
	ipc.ip_data = u.u_ar0[R0];
	ipc.ip_addr = u.u_arg[1] & ~01;
	ipc.ip_req = u.u_arg[2];
	p->p_flag =& ~SWTED;
	setrun(p);//执行​setrun()​，设定子进程的状态为​SRUN​。此时子进程的​SSTOP​状态被解除，成为可执行状态。
	while (ipc.ip_req > 0)//进入睡眠状态直至​ipc.ip_req​小于或等于 0。由子进程执行的​procxmt()​会把​ipc.ip_req​设为 0。
		sleep(&ipc, IPCPRI);
	u.u_ar0[R0] = ipc.ip_data;
	//当​procxmt()​由子进程执行完毕，且当前进程通过​swtch()​被选为执行进程后，将r0​设为​ipc.ip_data​，并将其作为系统调用​ptrace​的返回值。
	if (ipc.ip_req < 0)//​ipc.ip_req​的值小于 0时按出错处理。如果​procxmt()​在处理中出错，会将ipc.ip_req​设置为小于 0的值。
		u.u_error = EIO;
	ipc.ip_lock = 0;//最后解除​ipc​结构体的锁，并唤醒正在等待同一把锁的其他进程。
	wakeup(&ipc);
}

/*
 * Code that the child process
 * executes to implement the command
 * of the parent process in tracing.
 */
procxmt()
//procxmt()​由被跟踪的子进程执行。根据由父进程执行的系统调用​ptrace​设定的​ipc​结构体进行读写数据等处理。
{
	register int i;清单6
	register int *p;

	if (ipc.ip_lock != u.u_procp->p_pid)//如果锁没有被正确取得则退出处理。此处期待锁由​ptrace()​取得。
		return(0);
	i = ipc.ip_req;
	ipc.ip_req = 0;//重置​ipc.ip_req​，并唤醒因执行​ptrace()​并等待​procxmt()​执行结束而处于睡眠状态的进程。
	wakeup(&ipc);
	/*
	 * 根据由父进程设定的​ipc.ip_req​进行各种处理。
	 * 如果​ipc.ip_req​的值为 7，则向子进程发送信号并返回 1，然后继续子进程的一般处理。
	 * 如果值为 8，则执行​exit()​强制终止进程的运行。
	 * 如果为其他值则返回 0，再次促使父进程介入子进程的处理。
	 */
	switch (i) {

	/* read user I */
	case 1:
		if (fuibyte(ipc.ip_addr) == -1)
			goto error;
		ipc.ip_data = fuiword(ipc.ip_addr);
		break;

	/* read user D */
	case 2:
		if (fubyte(ipc.ip_addr) == -1)
			goto error;
		ipc.ip_data = fuword(ipc.ip_addr);
		break;

	/* read u */
	case 3:
		i = ipc.ip_addr;
		if (i<0 || i >= (USIZE<<6))
			goto error;
		ipc.ip_data = u.inta[i>>1];
		break;

	/* write user I (for now, always an error) */
	case 4:
		if (suiword(ipc.ip_addr, 0) < 0)
			goto error;
		suiword(ipc.ip_addr, ipc.ip_data);
		break;

	/* write user D */
	case 5:
		if (suword(ipc.ip_addr, 0) < 0)
			goto error;
		suword(ipc.ip_addr, ipc.ip_data);
		break;

	/* write u */
	case 6:
		p = &u.inta[ipc.ip_addr>>1];
		if (p >= u.u_fsav && p < &u.u_fsav[25])
			goto ok;
		for (i=0; i<9; i++)
			if (p == &u.u_ar0[regloc[i]])
				goto ok;
		goto error;
	ok:
		if (p == &u.u_ar0[RPS]) {
			ipc.ip_data =| 0170000;	/* assure user space */
			ipc.ip_data =& ~0340;	/* priority 0 */
		}
		*p = ipc.ip_data;
		break;

	/* set signal and continue */
	case 7:
		u.u_procp->p_sig = ipc.ip_data;
		return(1);

	/* force exit */
	case 8:
		exit();

	default:
	error:
		ipc.ip_req = -1;
	}
	return(0);
}
