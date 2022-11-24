/ C library -- fork

/ pid = fork();
/
/ pid == 0 in child process; pid == -1 means error return
/ in child, parents id is in par_uid if needed

.globl	_fork, cerror, _par_uid

_fork:
	mov	r5,-(sp)
	mov	sp,r5
	/ usr\sys\ken\sys1.c\fork()
	sys	fork
		/ 无条件跳转，直接跳转到1
		br 1f
	/ 检查进&借位标志（PSW[0]）是否为0
	bec	2f
	jmp	cerror
1:
	/ 将proc.p_pid复制到_par_uid中
	/ r0中保存父进程id这个操作在14行fork函数中“u.u_ar0[R0] = p1->p_pid;”进行的
	mov	r0,_par_uid
	/ r0清零
	clr	r0
2:
	/ 为0,通过rts指令返回调用C语言库函数fork()的位置。返回值为保存在r0中的子进程的ID。
	mov	(sp)+,r5
	/ 返回调用C语言库函数fork()的位置
	rts	pc
.bss
_par_uid: .=.+2
