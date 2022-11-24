#
#include "../param.h"
#include "../systm.h"
#include "../user.h"
#include "../proc.h"
#include "../reg.h"
#include "../seg.h"

#define	EBIT	1		/* user error bit in PS: C-bit */
#define	UMODE	0170000		/* user-mode bits in PS word */
#define	SETD	0170011		/* SETD instruction */
#define	SYS	0104400		/* sys (trap) instruction */
#define	USER	020		/* user-mode flag added to dev */

/*
 * structure of the system entry table (sysent.c)
 */
// sysent​结构体保存与系统调用处理函数相关的数据。具体来说，就是保存着参数的数量，和系统调用处理函数的地址。
struct sysent	{
	int	count;		/* argument count */
	int	(*call)();	/* name of handler */
} sysent[64];

/*
 * Offsets of the user's registers relative to
 * the saved r0. See reg.h
 */
char	regloc[9]
{
	R0, R1, R2, R3, R4, R5, R6, R7, RPS
};

/*
 * Called from l40.s or l45.s when a processor trap occurs.
 * The arguments are the words saved on the system stack
 * by the hardware and software during the trap processing.
 * Their order is dictated by the hardware and the details
 * of C's calling sequence. They are peculiar in that
 * this call is not 'by value' and changed user registers
 * get copied back on return.
 * dev is the kind of trap that occurred.
 */
trap(dev, sp, r1, nps, r0, pc, ps)
{
	register i, a;
	register struct sysent *callp;

	savfp();//​savfp()​在 PDP-11/40的环境下不做任何处理。
	if ((ps&UMODE) == UMODE)
		dev =| USER;
	/*
	 * 如果触发陷入的进程为用户进程，则设置​dev​的​USER​标志位(=第 4比特位)。
	 * 此后的处理通过此标志位来判断触发陷入的进程为用户进程还是内存进程。
	 */
	u.u_ar0 = &r0;
	/*
	 * 将​u.u_ar0​设定为保存于栈中的​r0​的地址。
	 * 通过​u.u_ar0[Rn]​可以访问触发陷入的进程的 r0~r7，以及 PSW。
	 * 保存于栈中的值最终将会恢复至用户进程。​Rn​在reg.h​中被定义。
	 */
	switch(dev) {//dev​的值为陷入种类。根据​dev​进行相应的处理。

	/*
	 * Trap not expected.
	 * Usually a kernel mode bus error.
	 * The numbers printed are used to
	 * find the hardware PS/PC as follows.
	 * (all numbers in octal 18 bits)
	 *	address_of_saved_ps =
	 *		(ka6*0100) + aps - 0140000;
	 *	address_of_saved_pc =
	 *		address_of_saved_ps - 2;
	 */
	default:
		printf("ka6 = %o\n", *ka6);
		printf("aps = %o\n", &ps);
		printf("trap type %o\n", dev);
		panic("trap");
	/*
	 * 在内核进程内发生陷入时的处理。
	 * ​trap()​基本没有考虑过要如何处理这种情况，通常是将​nofault​指向陷入处理函数，由该函数进行后续处理。
	 * 因此，此处在输出内核PAR6、被陷入中断的进程的 PSW和​dev​（陷入种类）之后，调用​panic()​结束处理。
	 */

	case 0+USER: /* bus error */
		i = SIGBUS;
		break;
	/*
	 * ​case​语句中的​n+USER​表示用户模式的陷入。​USER​标志位在第 8行被设定。
	 * 如果是总线错误，则将​i​设置为信号种类并退出​switch​语句。
	 */

	/*
	 * If illegal instructions are not
	 * being caught and the offending instruction
	 * is a SETD, the trap is ignored.
	 * This is because C produces a SETD at
	 * the beginning of every program which
	 * will trap on CPUs without 11/45 FPU.
	 */
	case 1+USER: /* illegal instruction */
		if(fuiword(pc-2) == SETD && u.u_signal[SIGINS] == 0)
			goto out;
		i = SIGINS;
		break;
	/*
	 * 发生错误指令时的处理。与总线错误相同，将​i​设置为信号种类并退出​switch​文。
	 * 如果引发错误指令的指令（即​fuiword(pc-2)​。​pc​指向触发陷入的指令的下一个指令）为​SETD​，
	 * 但同时却并没有设定错误指令的信号（​u.u_signal[SIGINS]​）时，则忽略此陷入。
	 * ​SETD​为 C编译器插入到所有程序起始位置的指令。
	 */

	//发生错误指令、断点/跟踪、iot、emt时，执行与发生总线错误时相同的处理。
	case 2+USER: /* bpt or trace */
		i = SIGTRC;
		break;

	case 3+USER: /* iot */
		i = SIGIOT;
		break;

	case 5+USER: /* emt */
		i = SIGEMT;
		break;

	//发生系统调用时的处理。
	case 6+USER: /* sys call */
	//发生系统调用时的处理。
		u.u_error = 0;//重置​u.u_error​
		ps =& ~EBIT;//重置触发陷入的进程的 PSW[0]。​EBIT​的值为 1。
		callp = &sysent[fuiword(pc-2)&077];//​fuiword(pc-2)​表示触发陷入的指令（=​trap​指令）。​trap​指令低位 6比特的数字代表系统调用的种类。
		if (callp == sysent) { /* indirect */
		//间接系统调用时的处理。
		/*
		 * 紧跟着​trap​指令的下一个地址中存放的值也是一个地址，如果该地址指向的数据不是​trap​指令，则将​i​设定为 077（​nosys​）。
		 * ​SYS​表示​trap​指令（代码清单 5-28）。
		 */
			a = fuiword(pc);
			pc =+ 2;
			i = fuword(a);
			if ((i & ~077) != SYS)
				i = 077;	/* illegal */
			callp = &sysent[i&077];//将​callp​设置为实际执行的​sysent[]​的元素。
			for(i=0; i<callp->count; i++)//将​u.u_arg[i]​赋予位于​trap​指令后方传递给系统调用的参数。
				u.u_arg[i] = fuword(a =+ 2);
		} else {//直接系统调用时的处理。
			for(i=0; i<callp->count; i++) {//将​u.u_arg[]​赋予位于​trap​指令后方传递给系统调用的参数。
				u.u_arg[i] = fuiword(pc);
				pc =+ 2;
			}
		}
		u.u_dirp = u.u_arg[0];
		/*
		 * 将​u.u_dirp​赋予位于​trap​指令后方传递给系统调用的参数的起始位置的值。
		 * ​u.u_dirp​用于在系统调用处理函数内部取得从用户程序传递过来的文件路径名。
		 */
		trap1(callp->call);//trap1()​用来执行系统调用处理函数。
		if(u.u_intflg)
			u.u_error = EINTR;
		if(u.u_error < 100) {
		/*
		 * ​u.u_error​不为​EFAULT​时的处理。未发生错误时这里的if语句的值为真。
		 * 如果u.u_error​为​EFAULT​，与发生总线错误时相同，将​i​设置为陷入种类并退出​switch​语句。
		 */
			if(u.u_error) {
			/*
			 * 如果​u.u_error​被赋予了错误代码，则将触发陷入的进程的 PSW[0]设为 1，并将​r0​设定为​u.u_error​的值。
			 * 执行系统调用的程序可以通过 PSW[0]判断系统调用中是否发生了错误。
			 */
				ps =| EBIT;
				r0 = u.u_error;
			}
			goto out;//跳转至​out​，对信号进行处理并再次计算进程优先级，然后结束系统调用。
		}
		i = SIGSYS;
		break;

	/*
	 * Since the floating exception is an
	 * imprecise trap, a user generated
	 * trap may actually come from kernel
	 * mode. In this case, a signal is sent
	 * to the current process to be picked
	 * up later.
	 */
	case 8: /* floating exception */
		psignal(u.u_procp, SIGFPT);
		return;
	/*
	 * 内核进程触发浮动小数点异常时的处理。
	 * 发送信号后随即返回。内核进程本身不进行浮动小数点运算。
	 * 在内核进程中发生了浮动小数点异常，则表示由用户程序执行的浮动小数点运算在执行系统调用，并切换到内核进程后触发了陷入。
	 * 因为 PDP-11/40似乎无法在正确位置触发浮动小数点运算的异常，所以会导致这种现象的发生。
	 */

	case 8+USER://用户进程触发浮动小数点异常时的处理。与总线错误相同，将​i​设置为陷入种类并退出​switch​语句。
		i = SIGFPT;
		break;

	/*
	 * If the user SP is below the stack segment,
	 * grow the stack automatically.
	 * This relies on the ability of the hardware
	 * to restart a half executed instruction.
	 * On the 11/40 this is not the case and
	 * the routine backup/l40.s may fail.
	 * The classic example is on the instruction
	 *	cmp	-(sp),-(sp)
	 */
	case 9+USER: /* segmentation exception */
		a = sp;
		if(backup(u.u_ar0) == 0)
		if(grow(a))
			goto out;
		i = SIGSEG;
		break;
	/*
	 * 用户进程触发段异常时的处理。
	 * 由于异常的原因有可能是栈区域溢出，因此首先执行​backup()​恢复触发陷入前的状态，再执行​grow()​尝试扩展栈区域。
	 * 正是此处的处理实现了栈区域的自动扩展 。
	 */
	}
	psignal(u.u_procp, i);//退出​switch​语句后的处理。执行​psignal()​向执行进程发送信号。

out:
	if(issig())//如果收到信号，则进行信号处理。此前通过​psignal()​发送的信号在此处立即得到处理。
		psig();
	setpri(u.u_procp);//执行​setpri()​，再次计算进程的执行优先级。
}

/*
 * Call the system-entry routine f (out of the
 * sysent table). This is a subroutine for trap, and
 * not in-line, because if a signal occurs
 * during processing, an (abnormal) return is simulated from
 * the last caller to savu(qsav); if this took place
 * inside of trap, it wouldn't have a chance to clean up.
 *
 * If this occurs, the return takes place without
 * clearing u_intflg; if it's still set, trap
 * marks an error which means that a system
 * call (like read on a typewriter) got interrupted
 * by a signal.
 */
trap1(f)//实际执行系统调用处理函数。
/*
 * trap1()​首先设定​u.u_intflg​，在​u.u_qsav​中保存r5、r6的当前值，
 * 然后执行通过参数取得的系统调用处理函数。执行完毕后重置​u.u_intflg​并返回。
 */
int (*f)();
{

	u.u_intflg = 1;
	savu(u.u_qsav);
	(*f)();
	u.u_intflg = 0;
}

/*
 * nonexistent system call-- set fatal error code.
 */
nosys()//执行​nosys()​后将触发错误，会被赋予还未使用的sysent[]​的元素。
{
	u.u_error = 100;
}

/*
 * Ignored system call
 */
nullsys()//nullsys()​是不做任何处理的函数，被赋予用于间接执行的​sysent[0]​，实际上不会被任何人调用。
{
}
