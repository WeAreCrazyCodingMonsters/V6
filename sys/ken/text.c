#
/*
 */

#include "../param.h"
#include "../systm.h"
#include "../user.h"
#include "../proc.h"
#include "../text.h"
#include "../inode.h"

/*
 * Swap out process p.
 * The ff flag causes its core to be freed--
 * it may be off when called to create an image for a
 * child process in newproc.
 * Os is the old size of the data area of the process,
 * and is supplied during core expansion swaps.
 *
 * panic: out of swap space
 * panic: swap error -- IO error
 */
xswap(p, ff, os)
int *p;
{
	register *rp, a;

	rp = p;
//如果第 3个参数​os​的值为 0，将其设置为换出对象进程的数据段的长度。
	if(os == 0)
		os = rp->p_size;
//　分配交换空间。​malloc()​返回交换磁盘的块编号。
	a = malloc(swapmap, (rp->p_size+7)/8);
	if(a == NULL)
		panic("out of swap space");
/*执行​xccdec()​，递减换出对象进程所参照的代码段的参照计数器，该计数器反映内
*存中的进程对此代码段的参照次数。
*/
	xccdec(rp->p_textp);
//　设置换出对象进程的​SLOCK​标志位（表示处于交换处理中）。
	rp->p_flag =| SLOCK;
	if(swap(a, rp->p_addr, os, 0))
		panic("swap error");
//　如果第 2个参数​ff​不为 0，则从内存中释放换出对象进程的数据段。
	if(ff)
		mfree(coremap, os, rp->p_addr);
/*
*将换出的对象进程的数据段的地址换成为其分配的交换空间的地址（块编号）。
*清除​SLOAD​和​SLOCK​标志位，并将在交换空间内表示滞留时间的​proc.p_time​清 0。
*/
	rp->p_addr = a;
	rp->p_flag =& ~(SLOAD|SLOCK);
	rp->p_time = 0;
//如果设置了​runout​标志变量（表示不存在可换入的进程），则会启动调度器。
	if(runout) {
		runout = 0;
		wakeup(&runout);
	}
}

/*
 * relinquish use of the shared text segment
 * of a process.
 */
xfree()
{
	register *xp, *ip;
//　如果执行进程没有使用代码段，则不做任何处理。
	if((xp=u.u_procp->p_textp) != NULL) {
		u.u_procp->p_textp = NULL;
//执行​xccdec()​，递减以内存中的进程为对象的的代码段参照计数器。
		xccdec(xp);
		if(--xp->x_count == 0) {
			ip = xp->x_iptr;
			if((ip->i_mode&ISVTX) == 0) {
				xp->x_iptr = NULL;
				mfree(swapmap, (xp->x_size+7)/8, xp->x_daddr);
				ip->i_flag =& ~ITEXT;
				iput(ip);
			}
		}
	}
}

/*
 * Attach to a shared text segment.
 * If there is no shared text, just return.
 * If there is, hook up to it:
 * if it is not currently being used, it has to be read
 * in from the inode (ip) and established in the swap space.
 * If it is being used, but is not currently in core,
 * a swap has to be done to get it back.
 * The full coroutine glory has to be invoked--
 * see slp.c-- because if the calling process
 * is misplaced in core the text image might not fit.
 * Quite possibly the code after "out:" could check to
 * see if the text does fit and simply swap it in.
 *
 * panic: out of swap space
 */
xalloc(ip)
int *ip;
{
	register struct text *xp;
	register *rp, ts;
/*
*代码段的长度为 0时不做任何处理。​u.u_arg[1]​由​exec()​设置为程序执行文件
*的代码长度（以字节为单位）
*/
	if(u.u_arg[1] == 0)
		return;
/*
*在​text[]​中寻找未使用元素。如果所需的代码段已存在于​text[]​中，将该代码
*段的参照计数器加 1，并将执行进程指向该​text[]​元素。
*/
	rp = NULL;
	for(xp = &text[0]; xp < &text[NTEXT]; xp++)
		if(xp->x_iptr == NULL) {
			if(rp == NULL)
				rp = xp;
		} else
			if(xp->x_iptr == ip) {
				xp->x_count++;
				u.u_procp->p_textp = xp;
				goto out;
			}
	if((xp=rp) == NULL)
		panic("out of text");
	xp->x_count = 1;
	xp->x_ccount = 0;
	xp->x_iptr = ip;
	ts = ((u.u_arg[1]+63)>>6) & 01777;
	xp->x_size = ts;
	if((xp->x_daddr = malloc(swapmap, (ts+7)/8)) == NULL)
		panic("out of swap space");
/*
*为了将程序的指令读取至数据区域，执行​expand()​、​estabur()​更新用户空
*间。以用户空间的地址 0为起点，分配与程序文件代码数据相同长度的内存作为数据区域。
*/
	expand(USIZE+ts);
	estabur(0, ts, 0, 0);
	u.u_count = u.u_arg[1];
	u.u_offset[1] = 020;
	u.u_base = 0;
	readi(ip);
	rp = u.u_procp;
/*执行​swap()​，将（容纳着代码数据的）数据段换出内存。在进行交换处理的过程
*中保持已设置​SLOCK​标志位的状态。
*/
	rp->p_flag =| SLOCK;
	swap(xp->x_daddr, rp->p_addr+USIZE, ts, 0);
	rp->p_flag =& ~SLOCK;
	rp->p_textp = xp;
	rp = ip;
	rp->i_flag =| ITEXT;
	rp->i_count++;
	expand(USIZE);
/*
*如果没有任何内存中的进程参照程序文件的代码数据，则将进程（数据段）暂时
*换出内存。此时，数据段中只包含最低限度的数据（PPDA）。然后执行​swtch()​切换执行
*进程。当进程再次执行时，从退出​xalloc()​的位置开始继续处理。
*/
out:
	if(xp->x_ccount == 0) {
		savu(u.u_rsav);
		savu(u.u_ssav);
		xswap(u.u_procp, 1, 0);
		u.u_procp->p_flag =| SSWAP;
		swtch();
		/* no return */
	}
	xp->x_ccount++;
}

/*
 * Decrement the in-core usage count of a shared text segment.
 * When it drops to zero, free the core space.
 */
xccdec(xp)
int *xp;
{
	register *rp;

	if((rp=xp)!=NULL && rp->x_ccount!=0)
		if(--rp->x_ccount == 0)
			mfree(coremap, rp->x_size, rp->x_caddr);
}
