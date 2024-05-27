# Analysis of a Linux Kernel Oops

## Description of a Linux Kernel Oops

A Linux Kernel Oops is a message that popped out when a type of a serious error that may not be fatal but can either cause a kernel panic or reliability issues in the after. In this article, I will analyze some methods to debug a typical oops created by a null pointer in a kernel module.

### First of all, we need to identify if a problem is considered as an Oops.

1. A oops is a message showed up by kernel as below picture. On the 13th line, there is a statement of this error:
```
Internel error: Oops:
```
This is an identification of an oops problem.

2. After an oops identification, we need to know the exactly problem of this problem which is on the first line of the message:
```
Unable to handle kernel NULL pointer deference at virtual address 0000000000000000.

This showed that a NULL was accessed.

3. In the next we need to find out the detail of code that created the problem, around the middle of the message:
```
PC : faulty_write+0x10/0x20 [faulty]
```
While this message shows the location of a PC (or Program Counter) register which shows that the code in the module faulty, there is a method called faulty_write which has this NULL pointer access.

4. By using below command, we can easily identify the file of the code which creates the problem:
```
grep -r "faulty_write" ./*
Binary file ./misc-modules/faulty.ko matches
Binary file ./misc-modules/faulty.o matches
./misc-modules/faulty.c:ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
./misc-modules/faulty.c:	.write = faulty_write,
Binary file ./misc-modules/.faulty.c.swp matches
```

5. When a file is identified, the code should be carefully checked and if the cause of the problem is still not known, more diagnostic log should be added.
```
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
        loff_t *pos)
{
    /* make a simple fault by dereferencing a NULL pointer */
    *(int *)0 = 0;
    return 0;
}
```

Here is the picture of the Oops message:

![Screenshot of the Oops message we analyzed in this article.](https://github.com/cu-ecen-aeld/assignment-7-wangmengj.git/assignments/assignment7/assets/images/oops.jpeg)
