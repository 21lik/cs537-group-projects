Names: Kevin Li, Xuyang Liu

CS Logins: kjl, xuyang

WiscIDs: 9083145475, 9082556052

Emails: kjli@wisc.edu, xliu958@wisc.edu

Status: To our knowledge, the code is complete and consistently works for all tests except for test 2, 10, and 11. Test 2 fails on the VSCode terminal, but it succeeds in our built-in/MobaX terminals. Tests 10 and 11 usually pass, but occasionally they fail. These bugs are rooted in either the machines, our code, or the test files themselves.
Update: we found out that it was due to an oversight while revising the scheduler; we had forgotten that the scheduler continues where it left off in the code during a context switch. In a non-faulty machine/terminal, all tests usually succeed, but test 10 still sometimes fails.

Files changed:
<ul>
    <li>defs.h: Added walkpgdir, findminnice function headers</li>
    <li>Makefile: Added mutex.o in OBJS and ULIB, mutex.c in EXTRAS</li>
    <li>mutex.c: Created file, minit function</li>
    <li>mutex.h: Created file, mutex lock structure</li>
    <li>proc.c: Revised scheduler to use nice value, priority inheritance, added findminnice function</li>
    <li>proc.h: Added nice and elevated nice values, array of locks held in proc structure</li>
    <li>syscall.c: Added macquire, mrelease, and mnice system calls</li>
    <li>syscall.h: Added macquire, mrelease, and mnice system call numbers</li>
    <li>sysproc.c: Added macquire, mrelease, and mnice system call functions</li>
    <li>types.h: Added pte_t type</li>
    <li>user.h: Added minit function header and macquire, mrelease, and mnice system call function headers</li>
    <li>usys.S: Added macquire, mrelease, and mnice system calls</li>
    <li>vm.c: Removed static keyword in walkpgdir</li>
</ul>