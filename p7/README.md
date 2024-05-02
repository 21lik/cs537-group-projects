Names: Kevin Li, Xuyang Liu

CS Logins: kjl, xuyang

WiscIDs: 9083145475, 9082556052

Emails: kjli@wisc.edu, xliu958@wisc.edu

Status: We completed the main functions for both files and implemented the fuse operation functions for wfs.c. After fixing the read and implementing writing for indirectly pointed blocks, we were able to pass 18 of the 20 tests (0-16, 19). We will most likely revise and resubmit later, with a new slipday.txt file if necessary.

Files changed:
<ul>
    <li>mkfs.c: Added file, main function for initializing filesystem.</li>
    <li>wfs.c: Added file, main function for mounting disk image, fuse operations, helper functions.</li>
</ul>