#
/*
 */

#include "../param.h"
#include "../systm.h"
#include "../user.h"
#include "../proc.h"
#include "../buf.h"
#include "../reg.h"
#include "../inode.h"

/*
 * exec system call.
 * Because of the fact that an I/O buffer is used
 * to store the caller's arguments during exec,
 * and more buffers are needed to read in the text file,
 * deadly embraces waiting for free buffers are possible.
 * Therefore the number of processes simultaneously
 * running in exec has to be limited to NEXEC.
 */
#define EXPRI	-1

exec()
{
	int ap, na, nc, *bp;
	int ts, ds, sep;
	register c, *ip;
	register char *cp;
	extern uchar;

​	/*​正当性检查​*/
	ip = namei(&uchar, 0);
	if(ip == NULL)
		return;/*33~35 　通过用户作为参数指定的程序执行文件的路径，取得​inode[]​中对应的该文件
				的元素。如果用户指定了不存在的文件路径，或是不具备访问该文件的权限时，​namei()​将
				返回​NULL​*/
				
	while(execnt >= NEXEC)
		sleep(&execnt, EXPRI);
	execnt++;/*39~41 　确认正在执行的​exec()​的进程数小于​NEXEC​。如果大于或等于​NEXEC​则调用
				sleep()​进入睡眠状态，直到正在执行的​exec()​的进程数小于或等于​NEXEC​。通过此处的
				检查后递增​execnt​的值，表示正在执行​exec()​的进程数又增加了 1个。*/

	bp = getblk(NODEV);/*45 　执行​getblk()​取得尚未被分配给其他块设备的块设备缓冲区，并将其作为处理参数时
							的工作内存。大概开发人员认为与通过​malloc()​和​mfree()​分配内存相比，这种做法更简单。*/
							
	if(access(ip, IEXEC) || (ip->i_mode&IFMT)!=0)
		goto bad;//48~49 　执行​access()​检查程序文件的执行权限，同时确认该文件不是特殊文件。

	/*​将参数存入缓冲区​*/
	cp = bp->b_addr;
	na = 0;
	nc = 0;/*52~54 　将传递给程序的参数存入刚才取得的缓冲区。首先将​cp​设定为缓冲区数据区域
				的地址，然后将​na​（参数的数量）和​nc​（参数的总字节数）清 0。*/
				
	while(ap = fuword(u.u_arg[1])) {/* 57　依次处理用户传递给程序的参数。​fuword()​是从上一模式所示的虚拟地址空间向当
								前模式所示的虚拟地址空间复制的 1个字长的数据。​u.u_arg[1]​此时为系统调用​exec​的第 2个参数的地址。*/
		na++;
		if(ap == -1)//59~60 ​fuword()​处理失败时返回 -1，此时跳转到​bad​进行错误处理。
			goto bad;//61 　将​u.u_arg[1]​增加 1个字以便访问下一个参数。
		u.u_arg[1] =+ 2;
		for(;;) {
			c = fubyte(ap++);
			if(c == -1)
				goto bad;
			*cp++ = c;
			nc++;
			if(nc > 510) {
				u.u_error = E2BIG;
				goto bad;
			}
			if(c == 0)
				break;/*62~74 　 将 参 数 以 字 节 为 单 位 依 次 存 入 缓 冲 区， 将​ap​设 定 为 指 向 该 参 数 的 指 针。
						fubyte()​与​fuword()​相似，但是处理单位为 1个字节。参数以字节为单位存入缓冲区。
						fubyte()​发生错误时返回 -1，此时跳转到​bad​进行错误处理。
						每处理 1个字节，​nc​都会加 1。当这个​while​处理结束时，​nc​的值等于参数的总字节数。因
						为缓冲区的长度只有 512字节，当​nc​大于 510时将认为参数的长度过长，此时跳转到​bad​进
						行错误处理。
						由于各参数都以 0（​NULL​）结尾，因此当​fubyte()​取得的值为 0时，转而处理下一个参数。
						当存入缓冲区的处理结束后，缓冲区中保存的数据形式为“参数 0参数 1...参数 n”。*/
		}
	}
	if((nc&1) != 0) {
		*cp++ = 0;
		nc++;//84~86 　当​nc​的值为奇数时，向缓冲区中追加 1个字节使数据长度成为偶数。因为系统希望以字为单位进行处理。
	}


​	/*​读取程序执行文件的文件头​*/
	u.u_base = &u.u_arg[0];
	u.u_count = 8;
	u.u_offset[1] = 0;
	u.u_offset[0] = 0;//91~94 　读取程序文件的文件头。​readi()​是用于读取文件内容的函数，其参数由​user​结构体指定。
	u.u_segflg = 1;
	readi(ip);
	u.u_segflg = 0;
	if(u.u_error)
		goto bad;//98~99 　如果​readi()​在执行中发生错误，将错误代码保存至​u.u_error​，并跳转到bad​进行错误处理。
	sep = 0;
	if(u.u_arg[0] == 0407) {
		u.u_arg[2] =+ u.u_arg[1];
		u.u_arg[1] = 0;
	} else
	if(u.u_arg[0] == 0411)
		sep++; else
	if(u.u_arg[0] != 0410) {
		u.u_error = ENOEXEC;
		goto bad;/*101~109 　根据魔术数字进行分支处理。魔术数字为 0407时不使用代码段，而是将代码
					和数据一并读取至数据区域。将​u.u_arg[2]​设置为代码长度和数据长度之和，将​u.u_arg[1]​清 0。
					如果魔术数字为 0407、0410、0411以外的值则认为不是程序的执行文件，并跳转到​bad​进行错误处理。*/
	}
	if(u.u_arg[1]!=0 && (ip->i_flag&ITEXT)==0 && ip->i_count!=1) {
		u.u_error = ETXTBSY;
		goto bad;//113~115 　如果代码的长度（​u.u_arg[1]​）不为 0，该程序文件又被作为数据文件打开，则进行错误处理。
	}

	ts = ((u.u_arg[1]+63)>>6) & 01777;
	ds = ((u.u_arg[2]+u.u_arg[3]+63)>>6) & 01777;//118~119 　计算代码段和数据区域的长度。段的长度以 64字节为单位进行管理。
	if(estabur(ts, ds, SSIZE, sep))
		goto bad;//120~121 　执行​estabur()​更新用户 APR和用户空间A。​SSIZE​表示栈区域的初始长度，被定义为 20（×64字节）。


	/*​读取程序执行文件的代码部分​*/
	u.u_prof[3] = 0;//125 　从此处开始进行代码段和数据段的初始化处理。首先将用于统计的变量清 0。
	xfree();/*126 　执行​xfree()​释放当前使用的代码段。如果在此之前依次执行了​fork​和​exec​，
				那么到此为止就终于告别了父进程所使用的代码段。*/
	expand(USIZE);//128 　执行​expand()​将数据段缩小为与​user​结构体相同的长度。
	xalloc(ip);//129 　执行​xalloc()​分配代码段使用的内存。如果未使用代码段，则在​xalloc()​中不做任何处理。
	c = USIZE+ds+SSIZE;
	expand(c);
	while(--c >= USIZE)
		clearseg(u.u_procp->p_addr+c);/*130~133 　执行​expand()​确保数据区域。由于位于​user​结构体之后的区域留有过去的数
										据，因此需要将其清 0。*/

	/*​读取程序执行文件的数据部分​*/
	estabur(0, ds, 0, 0);//137 　执行​estabur()​，将进程的数据区域的起始位置变更至虚拟地址为 0的位置。
	u.u_base = 0;
	u.u_offset[1] = 020+u.u_arg[1];
	u.u_count = u.u_arg[2];
	readi(ip);/*138~141 　执行​readi()​，将程序读取至进程的数据区域。魔术数字为 0407时读取代码和
					数据，魔术数字为 0410时只读取数据。偏移量 020表示程序文件头的长度。*/
	

	u.u_tsize = ts;
	u.u_dsize = ds;
	u.u_ssize = SSIZE;
	u.u_sep = sep;
	estabur(u.u_tsize, u.u_dsize, u.u_ssize, u.u_sep);
	/*145~149 　执行​estabur()​最终确定用户空间。魔术数字为 0410时，通过​estabur()​设
		定用户 APR，对虚拟地址空间进行如下变更：将代码段起始地址调整至虚拟地址为 0处，数
		据区域起始地址以 8KB为边界对齐。
	
	​/*​将参数压入栈​*/
	cp = bp->b_addr;//155 　从这里开始将保存于缓冲区的参数转存至栈区域。首先将 cp设定为缓冲区的数据区域的地址。
	ap = -nc - na*2 - 4;
	u.u_ar0[R6] = ap;/*156~157 　计算栈指针的地址，并设定用户进程的 sp。请注意，
	因为栈区域的起始地址为​0xffff​，换算成​int​型将为负数。*/
	
	suword(ap, na);/*160 　将​na​（参数的数量）的值设定至栈指针指向的用户空间地址。
						​suword()​是从当前模式的虚拟地址空间向前一模式的虚拟地址空间复制的数据。*/
	c = -nc;//162 　将​c​设定为保存参数的用户空间地址。
	while(na--) {
		suword(ap=+2, c);
		do
			subyte(c++, *cp);
		while(*cp++);//将参数及地址存入栈区域。
	}
	suword(ap+2, -1);//将参数地址的下一地址设置为 -1。


​	/*​设置SUID、SGID​*/
	if ((u.u_procp->p_flag&STRC)==0) {//如果没有处于跟踪状态，则从此处开始设置SUID和SGID。
		if(ip->i_mode&ISUID)
			if(u.u_uid != 0) {
				u.u_uid = ip->i_uid;
				u.u_procp->p_uid = ip->i_uid;/*如果​inode[]​中代表该程序文件的元素的标志位​ISUID​已被设置，且当前用
												户并非超级用户（​u.u_uid​不为 0），则将该元素的成员​inode.i_uid​设定至​u.u_uid​*/
			}
		if(ip->i_mode&ISGID)
			u.u_gid = ip->i_gid;
	}//如果​inode[]​中代表该程序文件的元素的标志位​ISGID​已被设置，则将该元素的成员​inode.i_gid​设定至​u.u_gid​


	/*​清空信号和寄存器​*/
	c = ip;//从此处开始对信号和寄存器进行初始化。
	for(ip = &u.u_signal[0]; ip < &u.u_signal[NSIG]; ip++)
		if((*ip & 1) == 0)
			*ip = 0;/*如果​u.u_signal[n]​的值为偶数则清 0，如果为奇数则保留原有数据。如果
						在此之前依次执行了​fork​和​exec​，​u.u_signal[n]​的值则为从父进程继承的数据。*/
	for(cp = &regloc[0]; cp < &regloc[6];)
		u.u_ar0[*cp++] = 0;
	u.u_ar0[R7] = 0;//将用户进程的 r0~r5、r7的值清 0。
	for(ip = &u.u_fsav[0]; ip < &u.u_fsav[25];)
		*ip++ = 0;//将进行浮动小数点运算的寄存器清 0。此处理与 PDP-11/40无关。
	ip = c;

bad:
	iput(ip);//将​inode[]​中代表该程序执行文件的元素的参照计数器减 1。
	brelse(bp);//释放用于参数处理的缓冲区。
	if(execnt >= NEXEC)
		wakeup(&execnt);
	execnt--;/*如果正在执行的​exec()​的进程数小于或等于​NEXEC​，则唤醒其他等待进入
				exec()​处理的进程。同时递减​execnt​，表示正在执行的​exec()​的进程数减少了 1个。*/
}

/*
 * exit system call:
 * pass back caller's r0
 */
rexit()
{

	u.u_arg[0] = u.u_ar0[R0] << 8;
	exit();
}

/*
 * Release resources.
 * Save u. area for parent to look at.
 * Enter zombie state.
 * Wake up parent and init processes,
 * and dispose of children.
 */
exit()
{
	register int *q, a;
	register struct proc *p;

	u.u_procp->p_flag =& ~STRC;//如果当前处于跟踪处理中，则将跟踪标志位设置为无效。
	for(q = &u.u_signal[0]; q < &u.u_signal[NSIG];)
		*q++ = 1;//为了忽略所有信号，将所有​u.u_signal[n]​设置为 1。
	for(q = &u.u_ofile[0]; q < &u.u_ofile[NOFILE]; q++)
		if(a = *q) {
			*q = NULL;
			closef(a);
		}//关闭所有由当前进程打开的文件。
	iput(u.u_cdir);//递减当前目录的参照计数器。
	xfree();//释放代码段。
	a = malloc(swapmap, 1);
	if(a == NULL)
		panic("out of swap");//分配交换空间。
	p = getblk(swapdev, a);
	bcopy(&u, p->b_addr, 256);
	bwrite(p);/*执行​getblk()​，取得交换磁盘（用作交换空间的块设备）的块设备缓冲区。然后
					执行​bcopy()​将包含​user​结构体在内的位于数据段头部的 512字节复制到上述缓冲区，再
					执行​bwrite()​将缓冲区内的数据写入交换空间。*/
	q = u.u_procp;
	mfree(coremap, q->p_size, q->p_addr);
	q->p_addr = a;
	q->p_stat = SZOMB;//　释放内存中的数据段。然后把​proc.p_addr​设置为交换磁盘中的块编号，再将proc.p_stat​设置为​SZOMB​。

loop:
	for(p = &proc[0]; p < &proc[NPROC]; p++)
	if(q->p_ppid == p->p_pid) {
		wakeup(&proc[1]);
		wakeup(p);
		for(p = &proc[0]; p < &proc[NPROC]; p++)
		if(q->p_pid == p->p_ppid) {
			p->p_ppid  = 1;
			if (p->p_stat == SSTOP)
				setrun(p);
		}
		swtch();/*唤醒父进程和​init​进程。如果存在尚未清理的子进程，则将其父进程设置为
					init​进程。另外，如果该子进程处于​SSTOP​状态，则解除该状态并将其设置为可执行状态。
					最后执行​swtch()​切换执行进程。因为​proc.p_stat​为​SZOMB​，所以当前进程不会再次执行。*/
		/* no return */
	}
	q->p_ppid = 1;
	goto loop;//如果由于某种原因当前进程中才不存在父进程，则将其父进程设置为​init​进程，并返回第 28行再次执行。
}

/*
 * Wait system call.
 * Search for a terminated (zombie) child,
 * finally lay it to rest, and collect its status.
 * Look also for stopped (traced) children,
 * and pass back status from them.
 */
wait()
{
	register f, *bp;
	register struct proc *p;

	f = 0;//将表示子进程数量的​f​清 0。

loop://遍历​proc[]​寻找子进程。
	for(p = &proc[0]; p < &proc[NPROC]; p++)//递增子进程的数量。
	if(p->p_ppid == u.u_procp->p_pid) {
		f++;
		if(p->p_stat == SZOMB) {
			u.u_ar0[R0] = p->p_pid;
			bp = bread(swapdev, f=p->p_addr);
			mfree(swapmap, 1, f);
			p->p_stat = NULL;
			p->p_pid = 0;
			p->p_ppid = 0;
			p->p_sig = 0;
			p->p_ttyp = 0;
			p->p_flag = 0;
			p = bp->b_addr;
			u.u_cstime[0] =+ p->u_cstime[0];
			dpadd(u.u_cstime, p->u_cstime[1]);
			dpadd(u.u_cstime, p->u_stime);
			u.u_cutime[0] =+ p->u_cutime[0];
			dpadd(u.u_cutime, p->u_cutime[1]);
			dpadd(u.u_cutime, p->u_utime);
			u.u_ar0[R1] = p->u_arg[0];
			brelse(bp);
			return;/*如果子进程处于僵尸状态则进行下列处理。
						● 将用户进程的​r0​设置为子进程的​proc.p_pid​，并将其作为系统调用​wait​的返回值。
							用户程序可通过该值确认执行完毕的子进程。
						● 从换出至交换空间的​user​结构体中获得子进程占用CPU的时间。
						● 清除​proc​结构体的各个成员。
						● 将用户进程的​r1​设置为子进程的​user.u_arg[0]​。用户程序通过该值可了解子进程运行结束时的状态。
						最后执行​return​。依次清理处于僵尸状态的子进程。如果需要清理多个子进程，需要父进程
						再次执行系统调用​wait​*/
		}
		if(p->p_stat == SSTOP) {
			if((p->p_flag&SWTED) == 0) {
				p->p_flag =| SWTED;
				u.u_ar0[R0] = p->p_pid;
				u.u_ar0[R1] = (p->p_sig<<8) | 0177;
				return;
			}
			p->p_flag =& ~(STRC|SWTED);
			setrun(p);
		}
	}
	if(f) {
		sleep(u.u_procp, PWAIT);
		goto loop;
	}/*如果发现既不处于僵尸状态，也不处于​SSTOP​状态的子进程，则进入睡眠状态，
		等待该子进程处理完毕。被唤醒后返回​loop​再次进行检查。*/
	u.u_error = ECHILD;//如果没有发现子进程则认为出错。
}

/*
 * fork system call.
 * p1:pointer to executing process's proc structure
 */
fork()
{
	register struct proc *p1, *p2;
	p1 = u.u_procp;
	for(p2 = &proc[0]; p2 < &proc[NPROC]; p2++)
		if(p2->p_stat == NULL)
			// find an not using proc
			goto found;
	u.u_error = EAGAIN;
	goto out;

found:
	if(newproc()) {
		/*
		* 将用户进程的r0设定为父进程的proc.p_pid
		* 以此作为系统调用fork对子进程的返回值。
		*/
		u.u_ar0[R0] = p1->p_pid;
		u.u_cstime[0] = 0;
		u.u_cstime[1] = 0;
		u.u_stime = 0;
		u.u_cutime[0] = 0;
		u.u_cutime[1] = 0;
		u.u_utime = 0;
		return;
	}
	/* 
	* 将父进程的R0设置为子进程的id
	* R0 = 0
	* u_ar0 : address of users saved R0
	*/
	u.u_ar0[R0] = p2->p_pid;

out:

	/* 
	* 没有空余的proc结构体了，没法执行子进程
	* 程序计数器指向下一条程序
	*/
	u.u_ar0[R7] =+ 2;
}

/*
 * break system call.
 *  -- bad planning: "break" is a dirty word in C.
 */
sbreak()
{
	register a, n, d;
	int i;

	/*
	 * set n to new data size
	 * set d to new-old
	 * set n to new total size
	 */

	n = (((u.u_arg[0]+63)>>6) & 01777);
	if(!u.u_sep)
		n =- nseg(u.u_tsize) * 128;
	if(n < 0)
		n = 0;
	d = n - u.u_dsize;
	n =+ USIZE+u.u_ssize;/*将通过参数获得的以字节为单位的地址变成以 64字节为单位的形式，然后减去按
							页（以 8KB为单位）划分的代码段的长度，得到新的数据区域的长度。​d​用于保存当前数据
							区域和新的数据区域的长度差。​n​用于保存新的数据段全体的长度。*/
	if(estabur(u.u_tsize, u.u_dsize+d, u.u_ssize, u.u_sep))
		return;//更新用户 APR，以更新用户空间。
	u.u_dsize =+ d;//更新通过​user​结构体管理的数据区域长度。
	if(d > 0)
		goto bigger;//如果是扩展数据区域，则跳转到​bigger​
	a = u.u_procp->p_addr + n - u.u_ssize;
	i = n;
	n = u.u_ssize;
	while(n--) {
		copyseg(a-d, a);
		a++;
	}
	expand(i);
	return;//如果是缩小数据区域，则将栈区域向物理地址的低位地址方向移动，并执行expand()​删除剩余的部分。

bigger:
	expand(n);
	a = u.u_procp->p_addr + n;
	n = u.u_ssize;
	while(n--) {
		a--;
		copyseg(a-d, a);
	}
	while(d--)
		clearseg(--a);//如果是扩展数据区域，则将栈区域向物理地址的高位地址方向移动，然后执行expand()​扩展数据段。
}