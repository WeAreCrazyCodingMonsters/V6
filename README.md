### README
The directory structure in this project is the same as [https://minnie.tuhs.org/cgi-bin/utree.pl?file=V6](https://minnie.tuhs.org/cgi-bin/utree.pl?file=V6).
The purpose of this is to facilitate learning.

已注释文件:

* [usr/source](usr/source)
  * [usr/source/s4](usr/source/s4)
    * [usr/source/s4/fork.s](usr/source/s4/fork.s)
* [usr/sys](usr/sys)
  * [usr/sys/ken](usr/sys/ken)
    * [usr/sys/ken/prf.c](usr/sys/ken/prf.c)
    * [usr/sys/ken/sig.c](usr/sys/ken/sig.c)
    * [usr/sys/ken/slp.c](usr/sys/ken/slp.c)
    * [usr/sys/ken/sys1.c](usr/sys/ken/sys1.c)

重点工作纪要:

在slp.c文件中发现了xv6的一个错误。

文件路径:[usr/sys/ken/slp.c](usr/sys/ken/slp.c)

```c++
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
	* curpri为当前执行中的进程的执行优先级。
	* 标志变量runrun表示存在执行优先级大于当前进程的其他进程。
	* 因为proc.p_pri的值越小执行优先级越高，所以此处的不等号实际上是错误的,这个错误在 UNIX的下一个版本中已被修正。
	*/
	if(p > curpri)
		runrun++;
	pp->p_pri = p;
}
```



