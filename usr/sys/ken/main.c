#
#include "../param.h"
#include "../user.h"
#include "../systm.h"
#include "../proc.h"
#include "../text.h"
#include "../inode.h"
#include "../seg.h"

#define	CLOCK1	0177546
#define	CLOCK2	0172540
/*
 * Icode is the octal bootstrap
 * program executed in user mode
 * to bring up the system.
 */
int	icode[]
{
	0104413,	/* sys exec; init; initp */
	0000014,
	0000010,
	0000777,	/* br . */
	0000014,	/* initp: init; 0 */
	0000000,
	0062457,	/* init: </etc/init\0> */
	0061564,
	0064457,
	0064556,
	0000164,
};

/*
 * Initialization code.
 * Called from m40.s or m45.s as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *	clear and free user core
 *	find which clock is configured
 *	hand craft 0th process
 *	call all initialization routines
 *	fork - process 0 to schedule
 *	     - process 1 execute bootstrap
 *
 * panic: no clock -- neither clock responds
 * loop at loc 6 in user mode -- /etc/init
 *	cannot be executed.
 */
main()
{
	extern schar;
	register i, *p;

	/*
	 * zero and free all of core
	 */

	updlock = 0;
	i = *ka6 + USIZE;
	UISD->r[0] = 077406;
	for(;;) {
		UISA->r[0] = i;
		if(fuibyte(0) < 0)
			break;
		clearseg(i);
		maxmem++;
		mfree(coremap, 1, i);
		i++;
	}
	if(cputype == 70)
	for(i=0; i<62; i=+2) {
		UBMAP->r[i] = i<<12;
		UBMAP->r[i+1] = 0;
	}
	printf("mem = %l\n", maxmem*5/16);
	maxmem = min(maxmem, MAXMEM);
	mfree(swapmap, nswap, swplo);

	/*
	 * determine clock
	 */

	UISA->r[7] = ka6[1]; /* io segment */
	UISD->r[7] = 077406;
	lks = CLOCK1;
	if(fuiword(lks) == -1) {
		lks = CLOCK2;
		if(fuiword(lks) == -1)
			panic("no clock");
	}

	/*
	 * set up system process
	 */

	proc[0].p_addr = *ka6;
	proc[0].p_size = USIZE;
	proc[0].p_stat = SRUN;
	proc[0].p_flag =| SLOAD|SSYS;
	u.u_procp = &proc[0];

	/*
	 * set up 'known' i-nodes
	 */

	*lks = 0115;
	cinit();
	binit();
	iinit();
	rootdir = iget(rootdev, ROOTINO);
	rootdir->i_flag =& ~ILOCK;
	u.u_cdir = iget(rootdev, ROOTINO);
	u.u_cdir->i_flag =& ~ILOCK;

	/*
	 * make init process
	 * enter scheduling loop
	 * with system process
	 */

	if(newproc()) {
		expand(USIZE+1);
		estabur(0, 1, 0, 0);
		copyout(icode, 0, sizeof icode);
		/*
		 * Return goes to loc. 0 of user init
		 * code just copied out.
		 */
		return;
	}
	sched();
}

/*
 * Load the user hardware segmentation
 * registers from the software prototype.
 * The software registers must have
 * been setup prior by estabur.
 */
sureg()
{
	register *up, *rp, a;

	a = u.u_procp->p_addr;
	up = &u.u_uisa[16];
	rp = &UISA->r[16];
	if(cputype == 40) {
		up =- 8;
		rp =- 8;
	}
	while(rp > &UISA->r[0])
		*--rp = *--up + a;/*根据数据段的物理地址对​user​结构体的 APR数据进行补正，并用补正后的值更新
							用户 PAR。​UISA​为用户 PAR0的地址。*/
	if((up=u.u_procp->p_textp) != NULL)
		a =- up->x_caddr;//如果执行进程使用代码段，则根据代码段的物理地址确定补正值，并将其赋予​a​
	up = &u.u_uisd[16];
	rp = &UISD->r[16];
	if(cputype == 40) {
		up =- 8;
		rp =- 8;
	}
	while(rp > &UISD->r[0]) {
		*--rp = *--up;
		if((*rp & WO) == 0)
			rp[(UISA-UISD)/2] =- a;
	}/*使用​user​结构体的 APR数据更新 PDR。在设定读取专用代码段的 PDR时，将
		对应的 PAR寄存器的值与第 17行设定的补正值相加。​UDSA​为用户 PDR0的地址。*/
}

/*
 * Set up software prototype segmentation
 * registers to implement the 3 pseudo
 * text,data,stack segment sizes passed
 * as arguments.
 * The argument sep specifies if the
 * text and data+stack segments are to
 * be separated.
 */
estabur(nt, nd, ns, sep)
{
	register a, *ap, *dp;
	
	/*​正当性检查​*/
	if(sep) {
		if(cputype == 40)
			goto err;
		if(nseg(nt) > 8 || nseg(nd)+nseg(ns) > 8)
			goto err;
	} else
		if(nseg(nt)+nseg(nd)+nseg(ns) > 8)
			goto err;//nseg()​是将以64字节为单位的块数转换为页数的函数。如果总页数大于8则出错。
	if(nt+nd+ns+USIZE > maxmem)
		goto err;/*如果需要的块数（以 64字节为单位）大于能够使用的内存容量上限则出错。
					maxmem​表示能够使用的物理内存容量上限，在系统启动时设定。*/
	
	/*​分配代码段 */
	a = 0;
	ap = &u.u_uisa[0];
	dp = &u.u_uisd[0];
	while(nt >= 128) {
		*dp++ = (127<<8) | RO;
		*ap++ = a;
		a =+ 128;
		nt =- 128;
	}
	if(nt) {
		*dp++ = ((nt-1)<<8) | RO;
		*ap++ = a;//对代码段使用的​user​结构体中的 APR进行设定。
	}
	if(sep)
	while(ap < &u.u_uisa[8]) {
		*ap++ = 0;
		*dp++ = 0;
	}
	
	​/*​分配数据区域​*/
	a = USIZE;
	while(nd >= 128) {
		*dp++ = (127<<8) | RW;
		*ap++ = a;
		a =+ 128;
		nd =- 128;
	}
	if(nd) {
		*dp++ = ((nd-1)<<8) | RW;
		*ap++ = a;
		a =+ nd;
	}
	while(ap < &u.u_uisa[8]) {
		*dp++ = 0;
		*ap++ = 0;/*对数据区域使用的​user​结构体中的 APR进行设定。因为数据段起始位置的
1					6×64字节被分配给 PPDA，因此需要将 PAR与​USIZE​（16）相加。当数据区域分配完毕时，
					到 APR6为止的区域被清 0。*/
	}
	if(sep)
	while(ap < &u.u_uisa[16]) {
		*dp++ = 0;
		*ap++ = 0;
	}
	
	​/*​分配栈区域​*/
	a =+ ns;
	while(ns >= 128) {
		a =- 128;
		ns =- 128;
		*--dp = (127<<8) | RW;
		*--ap = a;
	}
	if(ns) {
		*--dp = ((128-ns)<<8) | RW | ED;
		*--ap = a-128;//分配栈区域使用的页。
	}
	if(!sep) {
		ap = &u.u_uisa[0];
		dp = &u.u_uisa[8];
		while(ap < &u.u_uisa[8])
			*dp++ = *ap++;
		ap = &u.u_uisd[0];
		dp = &u.u_uisd[8];
		while(ap < &u.u_uisd[8])
			*dp++ = *ap++;
	}
	sureg();　//执行​sureg()​，将​user​结构体中的 APR反映到硬件的用户 APR，以更新用户空间。
	return(0);​//estabur()​执行成功时返回 0。

err:
	u.u_error = ENOMEM;
	return(-1);
}

/*
 * Return the arg/128 rounded up.
 */
nseg(n)
{

	return((n+127)>>7);
}
