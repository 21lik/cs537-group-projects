Names: Kevin Li, Xuyang Liu
CS Logins: kjl, xuyang
WiscIDs: 9083145475, 9082556052
Emails: kjli@wisc.edu, xliu958@wisc.edu
Status: To our knowledge, the code is complete and consistently works for all tests except for test 2, 10, and 11. Test 2 fails on the VSCode terminal, but it succeeds in our built-in/MobaX terminals. Tests 10 and 11 usually pass, but occasionally they fail. These bugs are rooted in either the machines, our code, or the test files themselves.
Files changed:
    defs.h: Added walkpgdir function header
    Makefile: Added mutex.o in OBJS and ULIB, mutex.c in EXTRAS
    mutex.c: Created file, minit function
    mutex.h: Created file, mutex lock structure
    proc.c: Revised scheduler to use nice value, priority inheritance
    proc.h: Added nice and elevated nice values, array of locks held in proc structure
    syscall.c: Added macquire, mrelease, and mnice system calls
    syscall.h: Added macquire, mrelease, and mnice system call numbers
    sysproc.c: Added macquire, mrelease, and mnice system call functions
    types.h: Added pte_t type
    user.h: Added minit function header and macquire, mrelease, and mnice system call function headers
    usys.S: Added macquire, mrelease, and mnice system calls
    vm.c: Removed static keyword in walkpgdir