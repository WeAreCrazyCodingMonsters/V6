### README
The directory structure in this project is the same as [https://minnie.tuhs.org/cgi-bin/utree.pl?file=V6](https://minnie.tuhs.org/cgi-bin/utree.pl?file=V6).
The purpose of this is to facilitate learning.

**工作纪要:**

[Unix源码剖析：进程](https://geyuyao-hub.github.io/2022/09/07/Unix%E6%BA%90%E7%A0%81%E5%89%96%E6%9E%90%EF%BC%9A%E8%BF%9B%E7%A8%8B/)

[Unix源码剖析：进程管理[1]](https://geyuyao-hub.github.io/2022/09/11/Unix%E6%BA%90%E7%A0%81%E5%89%96%E6%9E%90%EF%BC%9A%E8%BF%9B%E7%A8%8B%E7%AE%A1%E7%90%86-1/)

**已注释文件:**

* [source](source)
  * [source/s4](source/s4)
    * [source/s4/fork.s](source/s4/fork.s)
* [sys](sys)
  * [sys/ken](sys/ken)
    * [sys/ken/prf.c](sys/ken/prf.c)
    * [sys/ken/sig.c](sys/ken/sig.c)
    * [sys/ken/slp.c](sys/ken/slp.c)
    * [sys/ken/sys1.c](sys/ken/sys1.c)
    * [sys/ken/main.c](sys/ken/main.c)
    * [sys/ken/text.c](sys/ken/text.c)
    * [sys/ken/clock.c](sys/ken/clock.c)
  * [sys/conf](sys/conf)
    * [sys/conf/m40.s](sys/conf/m40.s)

**工作日志:**

在slp.c文件中发现了xv6的一个错误。

文件路径:[sys/ken/slp.c](sys/ken/slp.c)

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



**相关资料以及参考:**

* The Unix Tree（Minnie’s Home Page）

  [https://minnie.tuhs.org/cgi-bin/utree.pl](https://minnie.tuhs.org/cgi-bin/utree.pl)

  可在此阅览UNIX V6的代码。

* Unix V6 Manuals

  [http://man.cat-v.org/unix-6th/](http://man.cat-v.org/unix-6th/)

  可在此阅览UNIX V6的手册

* The Unix Time-Sharing System / Dennis M. Ritchie，Ken Thompson 著

  介绍了处于萌芽时期UNIX的整体概要。

* UNIX Implementation / Ken Thompson 著

  从实现的角度对处于萌芽期的UNIX进行介绍。
  
* SETTING UP UNIX - Sixth Edition

  介绍UNIX V6的环境构筑和启动方法。对希望了解用户空间（userland）系统程序的读者会有帮助。 
  
* The UNIX I/O System / Dennis M. Ritchie 著

  介绍了处于萌芽期的UNIX的I/O处理。
