Names: Kevin Li, Xuyang Liu

CS Logins: kjl, xuyang

WiscIDs: 9083145475, 9082556052

Emails: kjli@wisc.edu, xliu958@wisc.edu

Status: The program compiles and works consistently for tests 1-4. However, test 5 sometimes times out, while test 6 always times out. This may be due to slow machines, a race condition in our code, or another unforseen bug.

Files changed:
<ul>
    <li>kv_store.c: Implemented server main function, kv_store structure, methods.</li>
    <li>ring_buffer.c: Implemented ring buffer methods.</li>
    <li>ring_buffer.h: Added semaphores and mutex lock to ring structure.</li>
</ul>

# Workload Generator
You can use `gen_workload.py` to generate workloads and test your key-value store implementation.
This script will generate a text file named `workload.txt` with one request in each line. The line format is as follows:
```
<request type> <key> <value>
```
For example:
```
put 4 5
get 3
put 3 6
get 4
```
Run the script with `-h` to see the possible input options.
It also generates another file called `solution.txt` which has the result of all the get requests in the order that they appear in `workload.txt`. For example, the corresponding `solution.txt` file for the above example would be:
```
0
5
```
If you set the `-c` option when calling the client, it will validate the correctness of the results it got from the server. Note that this check would only be meaningful if you have a single request in flight (`-n 1 -w 1`).
